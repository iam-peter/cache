#include "gitlabresolver.h"

#include "highlight.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QRegularExpression>
#include <QTextDocumentFragment>

#include <algorithm>
#include <memory>

namespace {

constexpr int kFileLines = 30;  // of a file linked without lines
constexpr int kMaxLines = 200; // however many a link asks for
constexpr qsizetype kExcerpt = 280;

QString enc(const QString &text)
{
    return QString::fromUtf8(QUrl::toPercentEncoding(text));
}

QJsonObject objectOf(const QByteArray &body)
{
    return QJsonDocument::fromJson(body).object();
}

QJsonArray arrayOf(const QByteArray &body)
{
    return QJsonDocument::fromJson(body).array();
}

QString when(const QString &text)
{
    const QDateTime t = QDateTime::fromString(text, Qt::ISODateWithMs);
    return t.isValid() ? QLocale().toString(t.toLocalTime(), QLocale::ShortFormat) : QString();
}

QString namesOf(const QJsonArray &people)
{
    QStringList names;
    for (const QJsonValue &p : people)
        names << p.toObject().value(QLatin1String("name")).toString();
    names.removeAll(QString());
    return names.join(QStringLiteral(", "));
}

QString stringsOf(const QJsonArray &values)
{
    QStringList list;
    for (const QJsonValue &v : values)
        list << v.toString();
    return list.join(QStringLiteral(", "));
}

// Markdown or HTML as a line of plain text, shortened
QString excerptOf(const QString &text)
{
    QString plain = QTextDocumentFragment::fromMarkdown(text).toPlainText().simplified();
    if (plain.size() > kExcerpt)
        plain = plain.left(kExcerpt).section(QLatin1Char(' '), 0, -2) + QStringLiteral("…");
    return plain;
}

// A pipeline's (or job's) status as people say it, and its tone
QString statusWord(const QString &status)
{
    static const QHash<QString, const char *> words{
        {QStringLiteral("success"), "Passed"},       {QStringLiteral("failed"), "Failed"},
        {QStringLiteral("running"), "Running"},      {QStringLiteral("pending"), "Pending"},
        {QStringLiteral("canceled"), "Canceled"},    {QStringLiteral("skipped"), "Skipped"},
        {QStringLiteral("created"), "Created"},      {QStringLiteral("manual"), "Manual"},
        {QStringLiteral("scheduled"), "Scheduled"},  {QStringLiteral("preparing"), "Preparing"},
        {QStringLiteral("waiting_for_resource"), "Waiting"}};
    const char *word = words.value(status);
    return word ? QObject::tr(word) : status;
}

QString toneOf(const QString &status)
{
    if (status == QLatin1String("success") || status == QLatin1String("merged"))
        return QStringLiteral("done");
    if (status == QLatin1String("failed"))
        return QStringLiteral("failed");
    if (status == QLatin1String("running") || status == QLatin1String("pending") || status == QLatin1String("opened")
        || status == QLatin1String("preparing") || status == QLatin1String("waiting_for_resource"))
        return QStringLiteral("progress");
    return QStringLiteral("todo");
}

// An issue's or merge request's state
QString stateWord(const QJsonObject &item)
{
    const QString state = item.value(QLatin1String("state")).toString();
    if (state == QLatin1String("opened"))
        return item.value(QLatin1String("draft")).toBool() || item.value(QLatin1String("work_in_progress")).toBool()
            ? QObject::tr("Draft") : QObject::tr("Open");
    if (state == QLatin1String("merged"))
        return QObject::tr("Merged");
    if (state == QLatin1String("closed"))
        return QObject::tr("Closed");
    if (state == QLatin1String("locked"))
        return QObject::tr("Locked");
    return state;
}

void add(LinkInfo &info, const QString &name, const QString &value)
{
    if (!value.isEmpty())
        info.fields.append({name, value});
}

} // namespace

struct GitLabResolver::Context
{
    Account account;
    QString site;
    QByteArray token;
    Ref ref;
    Done done;
    QString base; // /api/v4/projects/<project>
    // A request of the project's, its answer as it came
    std::function<void(const QString &path, const Links::BytesReply &reply)> get;
};

GitLabResolver::GitLabResolver(Links *links)
    : m_links(links)
{
}

GitLabResolver::Ref GitLabResolver::refOf(const QUrl &url)
{
    Ref ref;
    QString path = url.path(QUrl::FullyDecoded);
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    const qsizetype dash = path.indexOf(QLatin1String("/-/"));
    const QStringList parts = (dash >= 0 ? path.left(dash) : path).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    // Not a project: GitLab's own pages
    static const QStringList reserved{QStringLiteral("users"),   QStringLiteral("dashboard"), QStringLiteral("explore"),
                                      QStringLiteral("groups"),  QStringLiteral("admin"),     QStringLiteral("help"),
                                      QStringLiteral("search"),  QStringLiteral("api"),       QStringLiteral("oauth"),
                                      QStringLiteral("profile"), QStringLiteral("-")};
    if (parts.size() < 2 || reserved.contains(parts.first()))
        return {};
    ref.project = parts.join(QLatin1Char('/'));
    ref.kind = QStringLiteral("project");
    if (dash < 0)
        return ref;

    const QStringList rest = path.mid(dash + 3).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const QString what = rest.value(0);
    const QString id = rest.value(1);
    static const QRegularExpression digits(QStringLiteral(R"(^\d+$)"));
    static const QRegularExpression sha(QStringLiteral(R"(^[0-9a-f]{7,40}$)"));
    if ((what == QLatin1String("merge_requests") || what == QLatin1String("issues")
         || what == QLatin1String("work_items") || what == QLatin1String("pipelines") || what == QLatin1String("jobs")
         || what == QLatin1String("milestones") || what == QLatin1String("snippets"))
        && digits.match(id).hasMatch()) {
        ref.kind = what == QLatin1String("merge_requests") ? QStringLiteral("mr")
                 : what == QLatin1String("issues") || what == QLatin1String("work_items") ? QStringLiteral("issue")
                 : what == QLatin1String("pipelines") ? QStringLiteral("pipeline")
                 : what == QLatin1String("jobs") ? QStringLiteral("job")
                 : what == QLatin1String("milestones") ? QStringLiteral("milestone")
                                                       : QStringLiteral("snippet");
        ref.id = id;
    } else if (what == QLatin1String("commit") && sha.match(id).hasMatch()) {
        ref.kind = QStringLiteral("commit");
        ref.id = id;
    } else if (what == QLatin1String("blob") && rest.size() >= 3) {
        // The first part is the branch or commit; one with slashes in its
        // name can't be told apart from the path
        ref.kind = QStringLiteral("file");
        ref.ref = id;
        ref.path = rest.mid(2).join(QLatin1Char('/'));
        static const QRegularExpression lines(QStringLiteral(R"(^L(\d+)(?:-L?(\d+))?$)"));
        const QRegularExpressionMatch m = lines.match(url.fragment());
        if (m.hasMatch()) {
            ref.from = m.captured(1).toInt();
            ref.to = m.captured(2).isEmpty() ? ref.from : m.captured(2).toInt();
        }
    } else if ((what == QLatin1String("releases") || what == QLatin1String("tags")) && rest.size() >= 2) {
        ref.kind = what == QLatin1String("releases") ? QStringLiteral("release") : QStringLiteral("tag");
        ref.id = rest.mid(1).join(QLatin1Char('/'));
    } else if (what == QLatin1String("compare") && id.contains(QLatin1String("..."))) {
        ref.kind = QStringLiteral("compare");
        ref.id = rest.mid(1).join(QLatin1Char('/'));
    } else if (what == QLatin1String("wikis") && rest.size() >= 2) {
        ref.kind = QStringLiteral("wiki");
        ref.id = rest.mid(1).join(QLatin1Char('/'));
    }
    // Anything else of the project (its tree, its settings): the project
    return ref;
}

bool GitLabResolver::handles(const QUrl &url) const
{
    return Links::secure(url) && refOf(url).valid() && m_links->accountFor(id(), url).has_value();
}

void GitLabResolver::signIn(const Account &account, const QByteArray &token, const SignedIn &done)
{
    m_links->request(account.host, account, token, QStringLiteral("/api/v4/user"),
                     [account, done](int status, const QJsonObject &json, const QString &error) {
                         if (status == 200) {
                             const QString who = json.value(QLatin1String("name")).toString();
                             done(true, who.isEmpty() ? QObject::tr("Signed in to %1.").arg(account.host)
                                                      : QObject::tr("Signed in to %1 as %2.").arg(account.host, who));
                         } else if (status == 401 || status == 403) {
                             done(false, QObject::tr("%1 didn't take the token. It needs the read_api scope.")
                                             .arg(account.host));
                         } else {
                             done(false, QObject::tr("Couldn't reach %1: %2").arg(account.host, error));
                         }
                     });
}

void GitLabResolver::resolve(const QUrl &url, const Done &done)
{
    const auto found = m_links->accountFor(id(), url);
    const Ref ref = refOf(url);
    if (!found || !ref.valid()) {
        done(std::nullopt, {});
        return;
    }
    auto c = std::make_shared<Context>();
    c->account = found->first;
    c->site = found->second;
    c->ref = ref;
    c->done = done;
    c->base = QStringLiteral("/api/v4/projects/") + enc(ref.project);
    Links *links = m_links;
    links->accounts()->token(c->account, [this, links, c](const QByteArray &token) {
        if (token.isEmpty()) {
            c->done(std::nullopt, QObject::tr("The keychain has no token for %1: sign in again in Settings, Accounts.")
                                      .arg(c->account.host));
            return;
        }
        c->token = token;
        std::weak_ptr<Context> weak = c;
        c->get = [links, weak](const QString &path, const Links::BytesReply &reply) {
            if (const auto c = weak.lock())
                links->requestBytes(c->site, c->account, c->token, path, reply);
        };
        read(c);
    });
}

void GitLabResolver::read(const std::shared_ptr<Context> &c)
{
    const Ref &ref = c->ref;
    const QString base = c->base;
    // What failed, and where it was asked
    const auto fail = [c](int status, const QString &error) {
        c->done(std::nullopt, QStringLiteral("%1: %2").arg(c->site, status == 404 ? QObject::tr("Not found") : error));
    };
    LinkInfo info;
    info.site = QStringLiteral("GitLab · %1").arg(ref.project);

    if (ref.kind == QLatin1String("mr")) {
        c->get(base + QStringLiteral("/merge_requests/") + ref.id, [c, base, fail, info](int status, const QByteArray &body,
                                                                                          const QString &error) mutable {
            if (status != 200)
                return fail(status, error);
            const QJsonObject mr = objectOf(body);
            info.title = QStringLiteral("!") + c->ref.id;
            info.summary = mr.value(QLatin1String("title")).toString();
            info.status = stateWord(mr);
            const QString state = mr.value(QLatin1String("state")).toString();
            info.tone = info.status == QObject::tr("Draft") ? QStringLiteral("todo") : toneOf(state);
            add(info, QObject::tr("Branches"), QStringLiteral("%1 → %2").arg(mr.value(QLatin1String("source_branch")).toString(),
                                                                         mr.value(QLatin1String("target_branch")).toString()));
            add(info, QObject::tr("Author"), mr.value(QLatin1String("author")).toObject().value(QLatin1String("name")).toString());
            add(info, QObject::tr("Reviewers"), namesOf(mr.value(QLatin1String("reviewers")).toArray()));
            const QJsonObject pipeline = mr.value(QLatin1String("head_pipeline")).toObject();
            add(info, QObject::tr("Pipeline"), statusWord(pipeline.value(QLatin1String("status")).toString()));
            const QString changes = mr.value(QLatin1String("changes_count")).toString();
            add(info, QObject::tr("Changes"), changes.isEmpty() ? QString() : QObject::tr("%1 files").arg(changes));
            add(info, QObject::tr("Labels"), stringsOf(mr.value(QLatin1String("labels")).toArray()));
            add(info, QObject::tr("Milestone"), mr.value(QLatin1String("milestone")).toObject().value(QLatin1String("title")).toString());
            const int notes = mr.value(QLatin1String("user_notes_count")).toInt();
            add(info, QObject::tr("Comments"), notes > 0 ? QString::number(notes) : QString());
            add(info, QObject::tr("Updated"), when(mr.value(QLatin1String("updated_at")).toString()));
            // Who approved: a request of its own, without it all the rest
            c->get(base + QStringLiteral("/merge_requests/") + c->ref.id + QStringLiteral("/approvals"),
                   [c, info](int status, const QByteArray &body, const QString &) mutable {
                       if (status == 200) {
                           QJsonArray people;
                           for (const QJsonValue &a : objectOf(body).value(QLatin1String("approved_by")).toArray())
                               people.append(a.toObject().value(QLatin1String("user")));
                           const QString approved = namesOf(people);
                           if (!approved.isEmpty())
                               info.fields.insert(3, {QObject::tr("Approved by"), approved});
                       }
                       c->done(info, {});
                   });
        });
    } else if (ref.kind == QLatin1String("issue")) {
        c->get(base + QStringLiteral("/issues/") + ref.id, [c, fail, info](int status, const QByteArray &body,
                                                                          const QString &error) mutable {
            if (status != 200)
                return fail(status, error);
            const QJsonObject issue = objectOf(body);
            info.title = QStringLiteral("#") + c->ref.id;
            info.summary = issue.value(QLatin1String("title")).toString();
            info.status = stateWord(issue);
            info.tone = issue.value(QLatin1String("state")).toString() == QLatin1String("closed") ? QStringLiteral("done")
                                                                                                    : QStringLiteral("progress");
            add(info, QObject::tr("Assignees"), namesOf(issue.value(QLatin1String("assignees")).toArray()));
            add(info, QObject::tr("Author"), issue.value(QLatin1String("author")).toObject().value(QLatin1String("name")).toString());
            add(info, QObject::tr("Labels"), stringsOf(issue.value(QLatin1String("labels")).toArray()));
            add(info, QObject::tr("Milestone"), issue.value(QLatin1String("milestone")).toObject().value(QLatin1String("title")).toString());
            add(info, QObject::tr("Due"), issue.value(QLatin1String("due_date")).toString());
            const int weight = issue.value(QLatin1String("weight")).toInt();
            add(info, QObject::tr("Weight"), weight > 0 ? QString::number(weight) : QString());
            const int notes = issue.value(QLatin1String("user_notes_count")).toInt();
            add(info, QObject::tr("Comments"), notes > 0 ? QString::number(notes) : QString());
            add(info, QObject::tr("Updated"), when(issue.value(QLatin1String("updated_at")).toString()));
            c->done(info, {});
        });
    } else if (ref.kind == QLatin1String("pipeline")) {
        c->get(base + QStringLiteral("/pipelines/") + ref.id, [c, base, fail, info](int status, const QByteArray &body,
                                                                                   const QString &error) mutable {
            if (status != 200)
                return fail(status, error);
            const QJsonObject p = objectOf(body);
            const QString state = p.value(QLatin1String("status")).toString();
            info.title = QObject::tr("Pipeline #%1").arg(c->ref.id);
            info.summary = QStringLiteral("%1 · %2").arg(p.value(QLatin1String("ref")).toString(),
                                                        p.value(QLatin1String("sha")).toString().left(8));
            info.status = statusWord(state);
            info.tone = toneOf(state);
            const int seconds = p.value(QLatin1String("duration")).toInt();
            const QString duration = seconds > 0 ? QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'))
                                                 : QString();
            const QString by = p.value(QLatin1String("user")).toObject().value(QLatin1String("name")).toString();
            const QString created = when(p.value(QLatin1String("created_at")).toString());
            // Its stages, in order, each with how its jobs went
            c->get(base + QStringLiteral("/pipelines/") + c->ref.id + QStringLiteral("/jobs?per_page=100&include_retried=false"),
                   [c, info, duration, by, created](int status, const QByteArray &body, const QString &) mutable {
                       if (status == 200) {
                           QStringList stages;
                           QHash<QString, QMap<QString, int>> counts;
                           QJsonArray jobs = arrayOf(body);
                           // The API lists the newest first
                           for (qsizetype i = jobs.size() - 1; i >= 0; --i) {
                               const QJsonObject job = jobs.at(i).toObject();
                               const QString stage = job.value(QLatin1String("stage")).toString();
                               if (!stages.contains(stage))
                                   stages << stage;
                               ++counts[stage][statusWord(job.value(QLatin1String("status")).toString())];
                           }
                           for (const QString &stage : std::as_const(stages)) {
                               QStringList parts;
                               const QMap<QString, int> &byStatus = counts.value(stage);
                               for (auto it = byStatus.cbegin(); it != byStatus.cend(); ++it)
                                   parts << QStringLiteral("%1 %2").arg(it.value()).arg(it.key().toLower());
                               QString name = stage;
                               if (!name.isEmpty())
                                   name[0] = name.at(0).toUpper();
                               add(info, name, parts.join(QStringLiteral(", ")));
                           }
                       }
                       add(info, QObject::tr("Duration"), duration);
                       add(info, QObject::tr("Started by"), by);
                       add(info, QObject::tr("Created"), created);
                       c->done(info, {});
                   });
        });
    } else if (ref.kind == QLatin1String("job")) {
        c->get(base + QStringLiteral("/jobs/") + ref.id, [c, fail, info](int status, const QByteArray &body,
                                                                        const QString &error) mutable {
            if (status != 200)
                return fail(status, error);
            const QJsonObject job = objectOf(body);
            const QString state = job.value(QLatin1String("status")).toString();
            info.title = job.value(QLatin1String("name")).toString();
            info.summary = QObject::tr("Job #%1 in stage %2").arg(c->ref.id, job.value(QLatin1String("stage")).toString());
            info.status = statusWord(state);
            info.tone = toneOf(state);
            add(info, QObject::tr("Pipeline"), QStringLiteral("#%1").arg(job.value(QLatin1String("pipeline")).toObject()
                                                                           .value(QLatin1String("id")).toVariant().toString()));
            add(info, QObject::tr("Branch"), job.value(QLatin1String("ref")).toString());
            const double seconds = job.value(QLatin1String("duration")).toDouble();
            add(info, QObject::tr("Duration"), seconds > 0 ? QStringLiteral("%1:%2").arg(int(seconds) / 60)
                                                                 .arg(int(seconds) % 60, 2, 10, QLatin1Char('0'))
                                                           : QString());
            c->done(info, {});
        });
    } else if (ref.kind == QLatin1String("file")) {
        c->get(base + QStringLiteral("/repository/files/") + enc(ref.path) + QStringLiteral("/raw?ref=") + enc(ref.ref),
               [c, fail, info](int status, const QByteArray &body, const QString &error) mutable {
                   if (status != 200)
                       return fail(status, error);
                   const Ref &ref = c->ref;
                   info.title = QFileInfo(ref.path).fileName();
                   info.kind = QStringLiteral("code");
                   const QString where = QStringLiteral("%1 @ %2").arg(ref.path, ref.ref);
                   if (body.contains('\0')) {
                       // Not text: its name and where it is
                       info.summary = where;
                       c->done(info, {});
                       return;
                   }
                   const QStringList lines = QString::fromUtf8(body).split(QLatin1Char('\n'));
                   const int from = ref.from > 0 ? std::min<int>(ref.from, lines.size()) : 1;
                   const int to = ref.from > 0 ? std::min<int>(std::min(ref.to, from + kMaxLines - 1), lines.size())
                                               : std::min<int>(kFileLines, lines.size());
                   info.code = lines.mid(from - 1, to - from + 1).join(QLatin1Char('\n'));
                   info.firstLine = from;
                   info.language = Highlight::language(QFileInfo(ref.path).suffix(), info.code);
                   info.summary = ref.from > 0 ? QObject::tr("%1, lines %2–%3").arg(where).arg(from).arg(to) : where;
                   c->done(info, {});
               });
    } else if (ref.kind == QLatin1String("commit")) {
        c->get(base + QStringLiteral("/repository/commits/") + ref.id + QStringLiteral("?stats=true"),
               [c, base, fail, info](int status, const QByteArray &body, const QString &error) mutable {
                   if (status != 200)
                       return fail(status, error);
                   const QJsonObject commit = objectOf(body);
                   info.title = commit.value(QLatin1String("short_id")).toString();
                   info.summary = commit.value(QLatin1String("title")).toString();
                   const QString pipeline = commit.value(QLatin1String("last_pipeline")).toObject().value(QLatin1String("status")).toString();
                   if (!pipeline.isEmpty()) {
                       info.status = statusWord(pipeline);
                       info.tone = toneOf(pipeline);
                   }
                   add(info, QObject::tr("Author"), commit.value(QLatin1String("author_name")).toString());
                   add(info, QObject::tr("Date"), when(commit.value(QLatin1String("committed_date")).toString()));
                   const QJsonObject stats = commit.value(QLatin1String("stats")).toObject();
                   if (!stats.isEmpty())
                       add(info, QObject::tr("Changes"), QStringLiteral("+%1 −%2").arg(stats.value(QLatin1String("additions")).toInt())
                                                                                    .arg(stats.value(QLatin1String("deletions")).toInt()));
                   c->get(base + QStringLiteral("/repository/commits/") + c->ref.id + QStringLiteral("/refs?type=branch&per_page=10"),
                          [c, info](int status, const QByteArray &body, const QString &) mutable {
                              if (status == 200) {
                                  QStringList branches;
                                  for (const QJsonValue &b : arrayOf(body))
                                      branches << b.toObject().value(QLatin1String("name")).toString();
                                  add(info, QObject::tr("Branches"), branches.join(QStringLiteral(", ")));
                              }
                              c->done(info, {});
                          });
               });
    } else if (ref.kind == QLatin1String("release") || ref.kind == QLatin1String("tag")) {
        const bool release = ref.kind == QLatin1String("release");
        c->get(base + (release ? QStringLiteral("/releases/") : QStringLiteral("/repository/tags/")) + enc(ref.id),
               [c, fail, info, release](int status, const QByteArray &body, const QString &error) mutable {
                   if (status != 200)
                       return fail(status, error);
                   const QJsonObject r = objectOf(body);
                   if (release) {
                       info.title = r.value(QLatin1String("name")).toString(c->ref.id);
                       info.summary = excerptOf(r.value(QLatin1String("description")).toString());
                       add(info, QObject::tr("Tag"), r.value(QLatin1String("tag_name")).toString());
                       add(info, QObject::tr("Released"), when(r.value(QLatin1String("released_at")).toString()));
                       const int assets = r.value(QLatin1String("assets")).toObject().value(QLatin1String("count")).toInt();
                       add(info, QObject::tr("Assets"), assets > 0 ? QString::number(assets) : QString());
                   } else {
                       info.title = r.value(QLatin1String("name")).toString(c->ref.id);
                       const QJsonObject commit = r.value(QLatin1String("commit")).toObject();
                       info.summary = r.value(QLatin1String("message")).toString().isEmpty()
                           ? commit.value(QLatin1String("title")).toString()
                           : excerptOf(r.value(QLatin1String("message")).toString());
                       add(info, QObject::tr("Commit"), commit.value(QLatin1String("short_id")).toString());
                       add(info, QObject::tr("Date"), when(commit.value(QLatin1String("committed_date")).toString()));
                   }
                   c->done(info, {});
               });
    } else if (ref.kind == QLatin1String("compare")) {
        const QString from = ref.id.section(QStringLiteral("..."), 0, 0);
        const QString to = ref.id.section(QStringLiteral("..."), 1);
        c->get(base + QStringLiteral("/repository/compare?from=") + enc(from) + QStringLiteral("&to=") + enc(to),
               [c, fail, info, from, to](int status, const QByteArray &body, const QString &error) mutable {
                   if (status != 200)
                       return fail(status, error);
                   const QJsonObject cmp = objectOf(body);
                   const qsizetype commits = cmp.value(QLatin1String("commits")).toArray().size();
                   const qsizetype files = cmp.value(QLatin1String("diffs")).toArray().size();
                   info.title = QStringLiteral("%1 → %2").arg(from, to);
                   info.summary = QObject::tr("%1 commits, %2 files changed").arg(commits).arg(files);
                   c->done(info, {});
               });
    } else if (ref.kind == QLatin1String("milestone")) {
        // The address has the milestone's number in the project, the API
        // wants its id
        c->get(base + QStringLiteral("/milestones?iids[]=") + ref.id, [c, base, fail, info](int status, const QByteArray &body,
                                                                                           const QString &error) mutable {
            const QJsonObject m = arrayOf(body).first().toObject();
            if (status != 200 || m.isEmpty())
                return fail(status == 200 ? 404 : status, error);
            info.title = m.value(QLatin1String("title")).toString();
            info.summary = excerptOf(m.value(QLatin1String("description")).toString());
            const QString state = m.value(QLatin1String("state")).toString();
            info.status = state == QLatin1String("closed") ? QObject::tr("Closed") : QObject::tr("Open");
            info.tone = state == QLatin1String("closed") ? QStringLiteral("done") : QStringLiteral("progress");
            add(info, QObject::tr("Due"), m.value(QLatin1String("due_date")).toString());
            const QString id = m.value(QLatin1String("id")).toVariant().toString();
            c->get(base + QStringLiteral("/milestones/") + id + QStringLiteral("/issues?per_page=100"),
                   [c, info](int status, const QByteArray &body, const QString &) mutable {
                       if (status == 200) {
                           const QJsonArray issues = arrayOf(body);
                           const auto closed = std::count_if(issues.begin(), issues.end(), [](const QJsonValue &i) {
                               return i.toObject().value(QLatin1String("state")).toString() == QLatin1String("closed");
                           });
                           if (!issues.isEmpty())
                               info.fields.prepend({QObject::tr("Progress"),
                                                    QObject::tr("%1 of %2 issues closed").arg(closed).arg(issues.size())});
                       }
                       c->done(info, {});
                   });
        });
    } else if (ref.kind == QLatin1String("wiki")) {
        c->get(base + QStringLiteral("/wikis/") + enc(ref.id), [c, fail, info](int status, const QByteArray &body,
                                                                              const QString &error) mutable {
            if (status != 200)
                return fail(status, error);
            const QJsonObject page = objectOf(body);
            info.title = page.value(QLatin1String("title")).toString();
            info.summary = excerptOf(page.value(QLatin1String("content")).toString());
            c->done(info, {});
        });
    } else if (ref.kind == QLatin1String("snippet")) {
        c->get(base + QStringLiteral("/snippets/") + ref.id, [c, base, fail, info](int status, const QByteArray &body,
                                                                                  const QString &error) mutable {
            if (status != 200)
                return fail(status, error);
            const QJsonObject snippet = objectOf(body);
            info.title = snippet.value(QLatin1String("title")).toString();
            info.summary = snippet.value(QLatin1String("file_name")).toString();
            info.kind = QStringLiteral("code");
            const QString file = info.summary;
            c->get(base + QStringLiteral("/snippets/") + c->ref.id + QStringLiteral("/raw"),
                   [c, info, file](int status, const QByteArray &body, const QString &) mutable {
                       if (status == 200 && !body.contains('\0')) {
                           info.code = QString::fromUtf8(body).split(QLatin1Char('\n')).mid(0, kFileLines).join(QLatin1Char('\n'));
                           info.language = Highlight::language(QFileInfo(file).suffix(), info.code);
                       }
                       c->done(info, {});
                   });
        });
    } else {
        // The project: about it, its default branch's pipeline, languages,
        // latest release
        c->get(base, [c, base, fail, info](int status, const QByteArray &body, const QString &error) mutable {
            if (status != 200)
                return fail(status, error);
            const QJsonObject p = objectOf(body);
            info.title = p.value(QLatin1String("name_with_namespace")).toString();
            info.summary = p.value(QLatin1String("description")).toString().simplified();
            info.image = p.value(QLatin1String("avatar_url")).toString();
            info.site = QStringLiteral("GitLab · %1").arg(c->site);
            const QString branch = p.value(QLatin1String("default_branch")).toString();
            add(info, QObject::tr("Default branch"), branch);
            const int stars = p.value(QLatin1String("star_count")).toInt();
            add(info, QObject::tr("Stars"), stars > 0 ? QString::number(stars) : QString());
            add(info, QObject::tr("Last activity"), when(p.value(QLatin1String("last_activity_at")).toString()));
            struct Rest {
                LinkInfo info;
                int pending = 3;
            };
            auto rest = std::make_shared<Rest>();
            rest->info = info;
            const auto settle = [c, rest] {
                if (--rest->pending == 0)
                    c->done(rest->info, {});
            };
            c->get(base + QStringLiteral("/pipelines?per_page=1&ref=") + enc(branch),
                   [rest, settle](int status, const QByteArray &body, const QString &) {
                       const QString state = arrayOf(body).first().toObject().value(QLatin1String("status")).toString();
                       if (status == 200 && !state.isEmpty()) {
                           rest->info.status = statusWord(state);
                           rest->info.tone = toneOf(state);
                       }
                       settle();
                   });
            c->get(base + QStringLiteral("/languages"), [rest, settle](int status, const QByteArray &body, const QString &) {
                if (status == 200) {
                    QList<QPair<double, QString>> shares;
                    const QJsonObject langs = objectOf(body);
                    for (auto it = langs.begin(); it != langs.end(); ++it)
                        shares.append({it.value().toDouble(), it.key()});
                    std::sort(shares.begin(), shares.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
                    QStringList top;
                    for (const auto &[share, name] : shares.mid(0, 3))
                        top << QStringLiteral("%1 %2%").arg(name).arg(std::round(share));
                    add(rest->info, QObject::tr("Languages"), top.join(QStringLiteral(", ")));
                }
                settle();
            });
            c->get(base + QStringLiteral("/releases?per_page=1"), [rest, settle](int status, const QByteArray &body, const QString &) {
                const QJsonObject r = arrayOf(body).first().toObject();
                if (status == 200 && !r.isEmpty())
                    add(rest->info, QObject::tr("Latest release"),
                        QStringLiteral("%1 (%2)").arg(r.value(QLatin1String("name")).toString(),
                                                      when(r.value(QLatin1String("released_at")).toString())));
                settle();
            });
        });
    }
}

void GitLabResolver::list(const Account &account, const QString &site, const QByteArray &token, const QString &path,
                          const QString &kind, const std::function<void(const QVariantList &)> &done)
{
    m_links->requestBytes(site, account, token, path, [kind, done](int status, const QByteArray &body, const QString &) {
        QVariantList results;
        if (status == 200) {
            for (const QJsonValue &v : arrayOf(body)) {
                const QJsonObject o = v.toObject();
                if (kind == QLatin1String("project")) {
                    results << QVariantMap{{QStringLiteral("title"), o.value(QLatin1String("name_with_namespace")).toString()},
                                           {QStringLiteral("detail"), o.value(QLatin1String("description")).toString().simplified().left(80)},
                                           {QStringLiteral("url"), o.value(QLatin1String("web_url")).toString()}};
                    continue;
                }
                const QString ref = o.value(QLatin1String("references")).toObject().value(QLatin1String("full")).toString();
                const QString state = o.value(QLatin1String("state")).toString();
                results << QVariantMap{{QStringLiteral("title"), o.value(QLatin1String("title")).toString()},
                                       {QStringLiteral("detail"), ref},
                                       {QStringLiteral("status"), stateWord(o)},
                                       {QStringLiteral("tone"), state == QLatin1String("merged") || state == QLatin1String("closed")
                                                                    ? QStringLiteral("done") : QStringLiteral("progress")},
                                       {QStringLiteral("url"), o.value(QLatin1String("web_url")).toString()}};
            }
        }
        done(results);
    });
}

void GitLabResolver::search(const QString &query, const Results &done)
{
    struct Search {
        QVariantList results;
        int pending = 1; // until all are asked
        Results done;
    };
    auto search = std::make_shared<Search>();
    search->done = done;
    const auto settle = [search] {
        if (--search->pending == 0)
            search->done(search->results);
    };
    const auto collect = [search, settle](const QVariantList &found) {
        search->results += found;
        settle();
    };
    const bool mine = query == QLatin1String("mr:mine");
    const bool review = query == QLatin1String("mr:review");
    for (const Account &account : m_links->accounts()->all()) {
        if (account.provider != id())
            continue;
        ++search->pending;
        m_links->accounts()->token(account, [this, account, query, mine, review, search, collect, settle](const QByteArray &token) {
            if (token.isEmpty())
                return settle();
            const QString site = account.host;
            if (mine) {
                list(account, site, token, QStringLiteral("/api/v4/merge_requests?scope=created_by_me&state=opened&per_page=20"),
                     QStringLiteral("mr"), collect);
            } else if (review) {
                // By their user name: who they are first
                m_links->request(site, account, token, QStringLiteral("/api/v4/user"),
                                 [this, account, site, token, collect](int status, const QJsonObject &me, const QString &) {
                                     if (status != 200)
                                         return collect({});
                                     list(account, site, token,
                                          QStringLiteral("/api/v4/merge_requests?scope=all&state=opened&per_page=20&reviewer_username=")
                                              + enc(me.value(QLatin1String("username")).toString()),
                                          QStringLiteral("mr"), collect);
                                 });
            } else {
                search->pending += 2;
                const QString q = enc(query);
                list(account, site, token, QStringLiteral("/api/v4/search?scope=projects&per_page=5&search=") + q,
                     QStringLiteral("project"), collect);
                list(account, site, token, QStringLiteral("/api/v4/search?scope=merge_requests&per_page=5&search=") + q,
                     QStringLiteral("mr"), collect);
                list(account, site, token, QStringLiteral("/api/v4/search?scope=issues&per_page=5&search=") + q,
                     QStringLiteral("issue"), collect);
            }
        });
    }
    settle();
}

void GitLabResolver::reviews(const Results &done)
{
    search(QStringLiteral("mr:review"), done);
}
