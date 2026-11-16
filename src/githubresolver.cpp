#include "githubresolver.h"

#include "highlight.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QRegularExpression>
#include <QTextDocumentFragment>

#include <algorithm>
#include <cmath>
#include <memory>

namespace {

constexpr int kFileLines = 30;
constexpr int kMaxLines = 200;
constexpr qsizetype kExcerpt = 280;

QString enc(const QString &text)
{
    return QString::fromUtf8(QUrl::toPercentEncoding(text));
}

// Each part of a path encoded, the slashes kept
QString encPath(const QString &path)
{
    return QString::fromUtf8(QUrl::toPercentEncoding(path, "/"));
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

QString loginsOf(const QJsonArray &people)
{
    QStringList names;
    for (const QJsonValue &p : people)
        names << p.toObject().value(QLatin1String("login")).toString();
    names.removeAll(QString());
    return names.join(QStringLiteral(", "));
}

QString labelsOf(const QJsonArray &labels)
{
    QStringList names;
    for (const QJsonValue &l : labels)
        names << l.toObject().value(QLatin1String("name")).toString();
    names.removeAll(QString());
    return names.join(QStringLiteral(", "));
}

QString excerptOf(const QString &text)
{
    QString plain = QTextDocumentFragment::fromMarkdown(text).toPlainText().simplified();
    if (plain.size() > kExcerpt)
        plain = plain.left(kExcerpt).section(QLatin1Char(' '), 0, -2) + QStringLiteral("…");
    return plain;
}

QString duration(qint64 seconds)
{
    return seconds > 0 ? QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0')) : QString();
}

void add(LinkInfo &info, const QString &name, const QString &value)
{
    if (!value.isEmpty())
        info.fields.append({name, value});
}

// A run's, job's or check's outcome as people say it, and its tone
QPair<QString, QString> outcomeOf(const QString &status, const QString &conclusion)
{
    if (status != QLatin1String("completed")) {
        if (status == QLatin1String("queued") || status == QLatin1String("waiting") || status == QLatin1String("pending"))
            return {QObject::tr("Queued"), QStringLiteral("progress")};
        return {QObject::tr("Running"), QStringLiteral("progress")};
    }
    if (conclusion == QLatin1String("success"))
        return {QObject::tr("Passed"), QStringLiteral("done")};
    if (conclusion == QLatin1String("failure") || conclusion == QLatin1String("timed_out")
        || conclusion == QLatin1String("startup_failure"))
        return {QObject::tr("Failed"), QStringLiteral("failed")};
    if (conclusion == QLatin1String("cancelled"))
        return {QObject::tr("Canceled"), QStringLiteral("todo")};
    if (conclusion == QLatin1String("skipped") || conclusion == QLatin1String("neutral"))
        return {QObject::tr("Skipped"), QStringLiteral("todo")};
    return {conclusion, QStringLiteral("todo")};
}

// Check runs, all told: {"Failed", "failed", "5 passed, 1 failed"}
struct Checks
{
    QString status, tone, counts;
};
Checks checksOf(const QJsonArray &runs)
{
    QMap<QString, int> counts;
    bool failed = false, running = false;
    for (const QJsonValue &v : runs) {
        const QJsonObject run = v.toObject();
        const auto [word, tone] = outcomeOf(run.value(QLatin1String("status")).toString(),
                                            run.value(QLatin1String("conclusion")).toString());
        ++counts[word.toLower()];
        failed = failed || tone == QLatin1String("failed");
        running = running || tone == QLatin1String("progress");
    }
    if (counts.isEmpty())
        return {};
    QStringList parts;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        parts << QStringLiteral("%1 %2").arg(it.value()).arg(it.key());
    return failed    ? Checks{QObject::tr("Failed"), QStringLiteral("failed"), parts.join(QStringLiteral(", "))}
         : running   ? Checks{QObject::tr("Running"), QStringLiteral("progress"), parts.join(QStringLiteral(", "))}
                     : Checks{QObject::tr("Passed"), QStringLiteral("done"), parts.join(QStringLiteral(", "))};
}

// An issue's or pull request's state
QPair<QString, QString> stateOf(const QJsonObject &item)
{
    const QString state = item.value(QLatin1String("state")).toString();
    if (item.value(QLatin1String("merged")).toBool() || !item.value(QLatin1String("merged_at")).toString().isEmpty())
        return {QObject::tr("Merged"), QStringLiteral("done")};
    if (state == QLatin1String("open"))
        return item.value(QLatin1String("draft")).toBool() ? QPair<QString, QString>{QObject::tr("Draft"), QStringLiteral("todo")}
                                                         : QPair<QString, QString>{QObject::tr("Open"), QStringLiteral("progress")};
    if (state == QLatin1String("closed"))
        return {QObject::tr("Closed"), QStringLiteral("done")};
    return {state, QStringLiteral("todo")};
}

QString decode64(const QString &content)
{
    return QString::fromUtf8(QByteArray::fromBase64(content.toLatin1()));
}

} // namespace

struct GitHubResolver::Context
{
    Account account;
    QString api;    // the API's host
    QString prefix; // and its path ("" or "/api/v3")
    QByteArray token;
    Ref ref;
    Done done;
    QString repo;   // <prefix>/repos/<owner>/<name>
    std::function<void(const QString &path, const Links::BytesReply &reply)> get;
};

GitHubResolver::GitHubResolver(Links *links)
    : m_links(links)
{
}

QPair<QString, QString> GitHubResolver::apiOf(const QString &host)
{
    if (host == QLatin1String("github.com") || host == QLatin1String("gist.github.com"))
        return {QStringLiteral("api.github.com"), QString()};
    return {host, QStringLiteral("/api/v3")};
}

GitHubResolver::Ref GitHubResolver::refOf(const QUrl &url)
{
    Ref ref;
    const QString host = url.host().toLower();
    const QStringList parts = url.path(QUrl::FullyDecoded).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    static const QRegularExpression digits(QStringLiteral(R"(^\d+$)"));
    static const QRegularExpression sha(QStringLiteral(R"(^[0-9a-f]{7,40}$)"));
    if (host == QLatin1String("gist.github.com")) {
        // gist.github.com/<user>/<id>
        const QString id = parts.value(parts.size() >= 2 ? 1 : 0);
        static const QRegularExpression gist(QStringLiteral(R"(^[0-9a-f]{8,}$)"));
        if (gist.match(id).hasMatch()) {
            ref.kind = QStringLiteral("gist");
            ref.id = id;
        }
        return ref;
    }
    // Not a repository: GitHub's own pages
    static const QStringList reserved{QStringLiteral("orgs"),     QStringLiteral("settings"), QStringLiteral("notifications"),
                                      QStringLiteral("marketplace"), QStringLiteral("explore"), QStringLiteral("topics"),
                                      QStringLiteral("search"),   QStringLiteral("pulls"),    QStringLiteral("issues"),
                                      QStringLiteral("login"),    QStringLiteral("sponsors"), QStringLiteral("features"),
                                      QStringLiteral("enterprise"), QStringLiteral("about"),  QStringLiteral("apps")};
    if (parts.size() < 2 || reserved.contains(parts.first()))
        return {};
    ref.repo = parts.at(0) + QLatin1Char('/') + parts.at(1);
    ref.kind = QStringLiteral("repo");
    const QString what = parts.value(2);
    const QString id = parts.value(3);
    if (what.isEmpty() || what == QLatin1String("tree")) {
        return ref;
    } else if ((what == QLatin1String("pull") || what == QLatin1String("issues")) && digits.match(id).hasMatch()) {
        ref.kind = what == QLatin1String("pull") ? QStringLiteral("pr") : QStringLiteral("issue");
        ref.id = id;
    } else if (what == QLatin1String("commit") && sha.match(id).hasMatch()) {
        ref.kind = QStringLiteral("commit");
        ref.id = id;
    } else if (what == QLatin1String("actions") && parts.value(3) == QLatin1String("runs")
               && digits.match(parts.value(4)).hasMatch()) {
        // …/actions/runs/<run>[/job/<job>]
        if (parts.value(5) == QLatin1String("job") && digits.match(parts.value(6)).hasMatch()) {
            ref.kind = QStringLiteral("job");
            ref.id = parts.value(6);
        } else {
            ref.kind = QStringLiteral("run");
            ref.id = parts.value(4);
        }
    } else if (what == QLatin1String("blob") && parts.size() >= 5) {
        // The first part is the branch or commit; one with slashes in its
        // name can't be told apart from the path
        ref.kind = QStringLiteral("file");
        ref.ref = id;
        ref.path = parts.mid(4).join(QLatin1Char('/'));
        static const QRegularExpression lines(QStringLiteral(R"(^L(\d+)(?:-L?(\d+))?$)"));
        const QRegularExpressionMatch m = lines.match(url.fragment());
        if (m.hasMatch()) {
            ref.from = m.captured(1).toInt();
            ref.to = m.captured(2).isEmpty() ? ref.from : m.captured(2).toInt();
        }
    } else if (what == QLatin1String("releases") && id == QLatin1String("tag") && parts.size() >= 5) {
        ref.kind = QStringLiteral("release");
        ref.id = parts.mid(4).join(QLatin1Char('/'));
    } else if (what == QLatin1String("compare") && parts.size() >= 4) {
        ref.kind = QStringLiteral("compare");
        ref.id = parts.mid(3).join(QLatin1Char('/'));
    } else if (what == QLatin1String("milestone") && digits.match(id).hasMatch()) {
        ref.kind = QStringLiteral("milestone");
        ref.id = id;
    } else {
        // Discussions, wikis, projects: not in the REST API; their page
        // is read as it is
        return {};
    }
    return ref;
}

std::optional<Account> GitHubResolver::accountOf(const QUrl &url) const
{
    QString host = Links::authority(url);
    if (host == QLatin1String("gist.github.com") || host == QLatin1String("www.github.com"))
        host = QStringLiteral("github.com");
    return m_links->accounts()->find(id(), host);
}

bool GitHubResolver::handles(const QUrl &url) const
{
    return Links::secure(url) && refOf(url).valid() && accountOf(url).has_value();
}

void GitHubResolver::signIn(const Account &account, const QByteArray &token, const SignedIn &done)
{
    const auto [api, prefix] = apiOf(account.host);
    m_links->request(api, account, token, prefix + QStringLiteral("/user"),
                     [account, done](int status, const QJsonObject &json, const QString &error) {
                         if (status == 200) {
                             QString who = json.value(QLatin1String("name")).toString();
                             if (who.isEmpty())
                                 who = json.value(QLatin1String("login")).toString();
                             done(true, QObject::tr("Signed in to %1 as %2.").arg(account.host, who));
                         } else if (status == 401 || status == 403) {
                             done(false, QObject::tr("%1 didn't take the token.").arg(account.host));
                         } else {
                             done(false, QObject::tr("Couldn't reach %1: %2").arg(account.host, error));
                         }
                     });
}

void GitHubResolver::resolve(const QUrl &url, const Done &done)
{
    const std::optional<Account> account = accountOf(url);
    const Ref ref = refOf(url);
    if (!account || !ref.valid()) {
        done(std::nullopt, {});
        return;
    }
    auto c = std::make_shared<Context>();
    c->account = *account;
    std::tie(c->api, c->prefix) = apiOf(account->host);
    c->ref = ref;
    c->done = done;
    c->repo = c->prefix + QStringLiteral("/repos/") + ref.repo;
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
                links->requestBytes(c->api, c->account, c->token, path, reply);
        };
        read(c);
    });
}

void GitHubResolver::read(const std::shared_ptr<Context> &c)
{
    const Ref &ref = c->ref;
    const QString repo = c->repo;
    const auto fail = [c](int status, const QString &error) {
        c->done(std::nullopt, QStringLiteral("%1: %2").arg(c->account.host, status == 404 ? QObject::tr("Not found") : error));
    };
    LinkInfo info;
    info.site = QStringLiteral("GitHub · %1").arg(ref.repo);
    // Checks on a commit, into `info` (status unless it has one, and a field)
    const auto withChecks = [c](const QString &sha, LinkInfo info, bool asStatus) {
        c->get(c->repo + QStringLiteral("/commits/") + sha + QStringLiteral("/check-runs?per_page=100"),
               [c, info, asStatus](int status, const QByteArray &body, const QString &) mutable {
                   if (status == 200) {
                       const Checks checks = checksOf(objectOf(body).value(QLatin1String("check_runs")).toArray());
                       if (!checks.status.isEmpty()) {
                           if (asStatus) {
                               info.status = checks.status;
                               info.tone = checks.tone;
                           }
                           info.fields.prepend({QObject::tr("Checks"), checks.counts});
                       }
                   }
                   c->done(info, {});
               });
    };

    if (ref.kind == QLatin1String("pr")) {
        c->get(repo + QStringLiteral("/pulls/") + ref.id, [c, repo, fail, info, withChecks](int status, const QByteArray &body,
                                                                                           const QString &error) mutable {
            if (status != 200)
                return fail(status, error);
            const QJsonObject pr = objectOf(body);
            info.title = QStringLiteral("#") + c->ref.id;
            info.summary = pr.value(QLatin1String("title")).toString();
            std::tie(info.status, info.tone) = stateOf(pr);
            add(info, QObject::tr("Branches"),
                QStringLiteral("%1 → %2").arg(pr.value(QLatin1String("head")).toObject().value(QLatin1String("ref")).toString(),
                                              pr.value(QLatin1String("base")).toObject().value(QLatin1String("ref")).toString()));
            add(info, QObject::tr("Author"), pr.value(QLatin1String("user")).toObject().value(QLatin1String("login")).toString());
            add(info, QObject::tr("Reviewers"), loginsOf(pr.value(QLatin1String("requested_reviewers")).toArray()));
            add(info, QObject::tr("Changes"), QObject::tr("+%1 −%2 in %3 files")
                                                  .arg(pr.value(QLatin1String("additions")).toInt())
                                                  .arg(pr.value(QLatin1String("deletions")).toInt())
                                                  .arg(pr.value(QLatin1String("changed_files")).toInt()));
            add(info, QObject::tr("Labels"), labelsOf(pr.value(QLatin1String("labels")).toArray()));
            add(info, QObject::tr("Milestone"), pr.value(QLatin1String("milestone")).toObject().value(QLatin1String("title")).toString());
            const int comments = pr.value(QLatin1String("comments")).toInt() + pr.value(QLatin1String("review_comments")).toInt();
            add(info, QObject::tr("Comments"), comments > 0 ? QString::number(comments) : QString());
            add(info, QObject::tr("Updated"), when(pr.value(QLatin1String("updated_at")).toString()));
            const QString head = pr.value(QLatin1String("head")).toObject().value(QLatin1String("sha")).toString();
            // Who approved (each reviewer's latest review), then the checks
            c->get(repo + QStringLiteral("/pulls/") + c->ref.id + QStringLiteral("/reviews?per_page=100"),
                   [c, info, head, withChecks](int status, const QByteArray &body, const QString &) mutable {
                       if (status == 200) {
                           QMap<QString, QString> latest;
                           for (const QJsonValue &v : arrayOf(body)) {
                               const QJsonObject review = v.toObject();
                               const QString state = review.value(QLatin1String("state")).toString();
                               if (state != QLatin1String("COMMENTED"))
                                   latest[review.value(QLatin1String("user")).toObject().value(QLatin1String("login")).toString()] = state;
                           }
                           QStringList approved, changes;
                           for (auto it = latest.cbegin(); it != latest.cend(); ++it) {
                               if (it.value() == QLatin1String("APPROVED"))
                                   approved << it.key();
                               else if (it.value() == QLatin1String("CHANGES_REQUESTED"))
                                   changes << it.key();
                           }
                           if (!changes.isEmpty())
                               info.fields.insert(3, {QObject::tr("Changes asked by"), changes.join(QStringLiteral(", "))});
                           if (!approved.isEmpty())
                               info.fields.insert(3, {QObject::tr("Approved by"), approved.join(QStringLiteral(", "))});
                       }
                       if (head.isEmpty())
                           c->done(info, {});
                       else
                           withChecks(head, info, false);
                   });
        });
    } else if (ref.kind == QLatin1String("issue")) {
        c->get(repo + QStringLiteral("/issues/") + ref.id, [c, fail, info](int status, const QByteArray &body,
                                                                          const QString &error) mutable {
            if (status != 200)
                return fail(status, error);
            const QJsonObject issue = objectOf(body);
            info.title = QStringLiteral("#") + c->ref.id;
            info.summary = issue.value(QLatin1String("title")).toString();
            std::tie(info.status, info.tone) = stateOf(issue);
            if (issue.value(QLatin1String("state_reason")).toString() == QLatin1String("not_planned"))
                info.status = QObject::tr("Not planned");
            add(info, QObject::tr("Assignees"), loginsOf(issue.value(QLatin1String("assignees")).toArray()));
            add(info, QObject::tr("Author"), issue.value(QLatin1String("user")).toObject().value(QLatin1String("login")).toString());
            add(info, QObject::tr("Labels"), labelsOf(issue.value(QLatin1String("labels")).toArray()));
            add(info, QObject::tr("Milestone"), issue.value(QLatin1String("milestone")).toObject().value(QLatin1String("title")).toString());
            const int comments = issue.value(QLatin1String("comments")).toInt();
            add(info, QObject::tr("Comments"), comments > 0 ? QString::number(comments) : QString());
            add(info, QObject::tr("Updated"), when(issue.value(QLatin1String("updated_at")).toString()));
            c->done(info, {});
        });
    } else if (ref.kind == QLatin1String("commit")) {
        c->get(repo + QStringLiteral("/commits/") + ref.id, [c, fail, info, withChecks](int status, const QByteArray &body,
                                                                                       const QString &error) mutable {
            if (status != 200)
                return fail(status, error);
            const QJsonObject commit = objectOf(body);
            const QJsonObject inner = commit.value(QLatin1String("commit")).toObject();
            info.title = commit.value(QLatin1String("sha")).toString().left(7);
            info.summary = inner.value(QLatin1String("message")).toString().section(QLatin1Char('\n'), 0, 0);
            add(info, QObject::tr("Author"), inner.value(QLatin1String("author")).toObject().value(QLatin1String("name")).toString());
            add(info, QObject::tr("Date"), when(inner.value(QLatin1String("author")).toObject().value(QLatin1String("date")).toString()));
            const QJsonObject stats = commit.value(QLatin1String("stats")).toObject();
            add(info, QObject::tr("Changes"), QObject::tr("+%1 −%2 in %3 files")
                                                  .arg(stats.value(QLatin1String("additions")).toInt())
                                                  .arg(stats.value(QLatin1String("deletions")).toInt())
                                                  .arg(commit.value(QLatin1String("files")).toArray().size()));
            withChecks(commit.value(QLatin1String("sha")).toString(), info, true);
        });
    } else if (ref.kind == QLatin1String("run")) {
        c->get(repo + QStringLiteral("/actions/runs/") + ref.id, [c, repo, fail, info](int status, const QByteArray &body,
                                                                                      const QString &error) mutable {
            if (status != 200)
                return fail(status, error);
            const QJsonObject run = objectOf(body);
            info.title = QStringLiteral("%1 #%2").arg(run.value(QLatin1String("name")).toString())
                             .arg(run.value(QLatin1String("run_number")).toInt());
            info.summary = QStringLiteral("%1 · %2").arg(run.value(QLatin1String("head_branch")).toString(),
                                                        run.value(QLatin1String("display_title")).toString());
            std::tie(info.status, info.tone) = outcomeOf(run.value(QLatin1String("status")).toString(),
                                                         run.value(QLatin1String("conclusion")).toString());
            const QDateTime started = QDateTime::fromString(run.value(QLatin1String("run_started_at")).toString(), Qt::ISODate);
            const QDateTime updated = QDateTime::fromString(run.value(QLatin1String("updated_at")).toString(), Qt::ISODate);
            const QString took = started.isValid() && updated.isValid() ? duration(started.secsTo(updated)) : QString();
            const QString by = run.value(QLatin1String("actor")).toObject().value(QLatin1String("login")).toString();
            const QString event = run.value(QLatin1String("event")).toString();
            // Its jobs, each with how it went
            c->get(repo + QStringLiteral("/actions/runs/") + c->ref.id + QStringLiteral("/jobs?per_page=100"),
                   [c, info, took, by, event](int status, const QByteArray &body, const QString &) mutable {
                       if (status == 200) {
                           for (const QJsonValue &v : objectOf(body).value(QLatin1String("jobs")).toArray()) {
                               const QJsonObject job = v.toObject();
                               add(info, job.value(QLatin1String("name")).toString(),
                                   outcomeOf(job.value(QLatin1String("status")).toString(),
                                             job.value(QLatin1String("conclusion")).toString()).first);
                           }
                       }
                       add(info, QObject::tr("Duration"), took);
                       add(info, QObject::tr("Started by"), event.isEmpty() ? by : QStringLiteral("%1 (%2)").arg(by, event));
                       c->done(info, {});
                   });
        });
    } else if (ref.kind == QLatin1String("job")) {
        c->get(repo + QStringLiteral("/actions/jobs/") + ref.id, [c, fail, info](int status, const QByteArray &body,
                                                                                const QString &error) mutable {
            if (status != 200)
                return fail(status, error);
            const QJsonObject job = objectOf(body);
            info.title = job.value(QLatin1String("name")).toString();
            info.summary = QObject::tr("Job of %1").arg(job.value(QLatin1String("workflow_name")).toString());
            std::tie(info.status, info.tone) = outcomeOf(job.value(QLatin1String("status")).toString(),
                                                         job.value(QLatin1String("conclusion")).toString());
            QStringList steps;
            for (const QJsonValue &v : job.value(QLatin1String("steps")).toArray()) {
                const QJsonObject step = v.toObject();
                if (step.value(QLatin1String("conclusion")).toString() == QLatin1String("failure"))
                    steps << step.value(QLatin1String("name")).toString();
            }
            add(info, QObject::tr("Failed steps"), steps.join(QStringLiteral(", ")));
            add(info, QObject::tr("Branch"), job.value(QLatin1String("head_branch")).toString());
            const QDateTime started = QDateTime::fromString(job.value(QLatin1String("started_at")).toString(), Qt::ISODate);
            const QDateTime completed = QDateTime::fromString(job.value(QLatin1String("completed_at")).toString(), Qt::ISODate);
            if (started.isValid() && completed.isValid())
                add(info, QObject::tr("Duration"), duration(started.secsTo(completed)));
            c->done(info, {});
        });
    } else if (ref.kind == QLatin1String("file")) {
        c->get(repo + QStringLiteral("/contents/") + encPath(ref.path) + QStringLiteral("?ref=") + enc(ref.ref),
               [c, fail, info](int status, const QByteArray &body, const QString &error) mutable {
                   if (status != 200)
                       return fail(status, error);
                   const Ref &ref = c->ref;
                   const QJsonObject file = objectOf(body);
                   info.title = QFileInfo(ref.path).fileName();
                   info.kind = QStringLiteral("code");
                   const QString where = QStringLiteral("%1 @ %2").arg(ref.path, ref.ref);
                   const QByteArray raw = QByteArray::fromBase64(file.value(QLatin1String("content")).toString().toLatin1());
                   if (raw.isEmpty() || raw.contains('\0')) {
                       info.summary = where;
                       c->done(info, {});
                       return;
                   }
                   const QStringList lines = QString::fromUtf8(raw).split(QLatin1Char('\n'));
                   const int from = ref.from > 0 ? std::min<int>(ref.from, lines.size()) : 1;
                   const int to = ref.from > 0 ? std::min<int>(std::min(ref.to, from + kMaxLines - 1), lines.size())
                                               : std::min<int>(kFileLines, lines.size());
                   info.code = lines.mid(from - 1, to - from + 1).join(QLatin1Char('\n'));
                   info.firstLine = from;
                   info.language = Highlight::language(QFileInfo(ref.path).suffix(), info.code);
                   info.summary = ref.from > 0 ? QObject::tr("%1, lines %2–%3").arg(where).arg(from).arg(to) : where;
                   c->done(info, {});
               });
    } else if (ref.kind == QLatin1String("release")) {
        c->get(repo + QStringLiteral("/releases/tags/") + enc(ref.id), [c, fail, info](int status, const QByteArray &body,
                                                                                      const QString &error) mutable {
            if (status != 200)
                return fail(status, error);
            const QJsonObject r = objectOf(body);
            info.title = r.value(QLatin1String("name")).toString(c->ref.id);
            if (info.title.isEmpty())
                info.title = c->ref.id;
            info.summary = excerptOf(r.value(QLatin1String("body")).toString());
            if (r.value(QLatin1String("prerelease")).toBool()) {
                info.status = QObject::tr("Pre-release");
                info.tone = QStringLiteral("progress");
            }
            add(info, QObject::tr("Tag"), r.value(QLatin1String("tag_name")).toString());
            add(info, QObject::tr("Released"), when(r.value(QLatin1String("published_at")).toString()));
            const qsizetype assets = r.value(QLatin1String("assets")).toArray().size();
            add(info, QObject::tr("Assets"), assets > 0 ? QString::number(assets) : QString());
            c->done(info, {});
        });
    } else if (ref.kind == QLatin1String("compare")) {
        c->get(repo + QStringLiteral("/compare/") + encPath(ref.id), [c, fail, info](int status, const QByteArray &body,
                                                                                    const QString &error) mutable {
            if (status != 200)
                return fail(status, error);
            const QJsonObject cmp = objectOf(body);
            info.title = QString(c->ref.id).replace(QStringLiteral("..."), QStringLiteral(" → "));
            info.summary = QObject::tr("%1 commits ahead, %2 behind, %3 files changed")
                               .arg(cmp.value(QLatin1String("ahead_by")).toInt())
                               .arg(cmp.value(QLatin1String("behind_by")).toInt())
                               .arg(cmp.value(QLatin1String("files")).toArray().size());
            c->done(info, {});
        });
    } else if (ref.kind == QLatin1String("milestone")) {
        c->get(repo + QStringLiteral("/milestones/") + ref.id, [c, fail, info](int status, const QByteArray &body,
                                                                              const QString &error) mutable {
            if (status != 200)
                return fail(status, error);
            const QJsonObject m = objectOf(body);
            info.title = m.value(QLatin1String("title")).toString();
            info.summary = excerptOf(m.value(QLatin1String("description")).toString());
            const bool closed = m.value(QLatin1String("state")).toString() == QLatin1String("closed");
            info.status = closed ? QObject::tr("Closed") : QObject::tr("Open");
            info.tone = closed ? QStringLiteral("done") : QStringLiteral("progress");
            const int open = m.value(QLatin1String("open_issues")).toInt();
            const int done = m.value(QLatin1String("closed_issues")).toInt();
            if (open + done > 0)
                add(info, QObject::tr("Progress"), QObject::tr("%1 of %2 issues closed").arg(done).arg(open + done));
            add(info, QObject::tr("Due"), when(m.value(QLatin1String("due_on")).toString()));
            c->done(info, {});
        });
    } else if (ref.kind == QLatin1String("gist")) {
        c->get(c->prefix + QStringLiteral("/gists/") + ref.id, [c, fail](int status, const QByteArray &body,
                                                                        const QString &error) {
            if (status != 200)
                return fail(status, error);
            const QJsonObject gist = objectOf(body);
            LinkInfo info;
            info.site = QStringLiteral("GitHub Gist · %1").arg(gist.value(QLatin1String("owner")).toObject()
                                                                .value(QLatin1String("login")).toString());
            info.kind = QStringLiteral("code");
            const QJsonObject files = gist.value(QLatin1String("files")).toObject();
            const QJsonObject first = files.begin() != files.end() ? files.begin().value().toObject() : QJsonObject();
            const QString name = first.value(QLatin1String("filename")).toString();
            info.title = gist.value(QLatin1String("description")).toString();
            if (info.title.isEmpty())
                info.title = name;
            info.summary = files.size() > 1 ? QObject::tr("%1 and %2 more files").arg(name).arg(files.size() - 1) : name;
            info.code = first.value(QLatin1String("content")).toString().split(QLatin1Char('\n')).mid(0, kFileLines)
                            .join(QLatin1Char('\n'));
            info.language = Highlight::language(QFileInfo(name).suffix(), info.code);
            c->done(info, {});
        });
    } else {
        // The repository: about it, its default branch's checks, languages,
        // latest release
        c->get(repo, [c, repo, fail, info](int status, const QByteArray &body, const QString &error) mutable {
            if (status != 200)
                return fail(status, error);
            const QJsonObject r = objectOf(body);
            info.title = r.value(QLatin1String("full_name")).toString();
            info.summary = r.value(QLatin1String("description")).toString().simplified();
            info.image = r.value(QLatin1String("owner")).toObject().value(QLatin1String("avatar_url")).toString();
            info.site = QStringLiteral("GitHub · %1").arg(c->account.host);
            const QString branch = r.value(QLatin1String("default_branch")).toString();
            add(info, QObject::tr("Default branch"), branch);
            const int stars = r.value(QLatin1String("stargazers_count")).toInt();
            add(info, QObject::tr("Stars"), stars > 0 ? QString::number(stars) : QString());
            const int issues = r.value(QLatin1String("open_issues_count")).toInt();
            add(info, QObject::tr("Open issues and PRs"), issues > 0 ? QString::number(issues) : QString());
            add(info, QObject::tr("Last push"), when(r.value(QLatin1String("pushed_at")).toString()));
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
            c->get(repo + QStringLiteral("/commits/") + enc(branch) + QStringLiteral("/check-runs?per_page=100"),
                   [rest, settle](int status, const QByteArray &body, const QString &) {
                       const Checks checks = checksOf(objectOf(body).value(QLatin1String("check_runs")).toArray());
                       if (status == 200 && !checks.status.isEmpty()) {
                           rest->info.status = checks.status;
                           rest->info.tone = checks.tone;
                       }
                       settle();
                   });
            c->get(repo + QStringLiteral("/languages"), [rest, settle](int status, const QByteArray &body, const QString &) {
                if (status == 200) {
                    // Bytes of each: as shares
                    QList<QPair<double, QString>> shares;
                    double total = 0;
                    const QJsonObject langs = objectOf(body);
                    for (auto it = langs.begin(); it != langs.end(); ++it) {
                        shares.append({it.value().toDouble(), it.key()});
                        total += it.value().toDouble();
                    }
                    std::sort(shares.begin(), shares.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
                    QStringList top;
                    for (const auto &[bytes, name] : shares.mid(0, 3))
                        top << QStringLiteral("%1 %2%").arg(name).arg(std::round(100 * bytes / std::max(1.0, total)));
                    add(rest->info, QObject::tr("Languages"), top.join(QStringLiteral(", ")));
                }
                settle();
            });
            c->get(repo + QStringLiteral("/releases/latest"), [rest, settle](int status, const QByteArray &body, const QString &) {
                const QJsonObject r = objectOf(body);
                if (status == 200 && !r.isEmpty())
                    add(rest->info, QObject::tr("Latest release"),
                        QStringLiteral("%1 (%2)").arg(r.value(QLatin1String("name")).toString(r.value(QLatin1String("tag_name")).toString()),
                                                      when(r.value(QLatin1String("published_at")).toString())));
                settle();
            });
        });
    }
}

void GitHubResolver::search(const QString &query, const Results &done)
{
    struct Search {
        QVariantList results;
        int pending = 1;
        Results done;
    };
    auto search = std::make_shared<Search>();
    search->done = done;
    const auto settle = [search] {
        if (--search->pending == 0)
            search->done(search->results);
    };
    const bool mine = query == QLatin1String("pr:mine");
    const bool review = query == QLatin1String("pr:review");
    for (const Account &account : m_links->accounts()->all()) {
        if (account.provider != id())
            continue;
        const auto [api, prefix] = apiOf(account.host);
        // Issues and pull requests as the search API gives them
        const auto issues = [this, account, api = api, search, settle](const QByteArray &token, const QString &q) {
            ++search->pending;
            m_links->request(api, account, token,
                             apiOf(account.host).second + QStringLiteral("/search/issues?per_page=8&q=") + enc(q),
                             [search, settle](int status, const QJsonObject &json, const QString &) {
                                 if (status == 200) {
                                     for (const QJsonValue &v : json.value(QLatin1String("items")).toArray()) {
                                         const QJsonObject item = v.toObject();
                                         const auto [word, tone] = stateOf(item);
                                         const bool pr = item.contains(QLatin1String("pull_request"));
                                         const QString repo = item.value(QLatin1String("repository_url")).toString()
                                                                  .section(QLatin1Char('/'), -2);
                                         search->results << QVariantMap{
                                             {QStringLiteral("title"), item.value(QLatin1String("title")).toString()},
                                             {QStringLiteral("detail"), QStringLiteral("%1#%2").arg(repo)
                                                                            .arg(item.value(QLatin1String("number")).toInt())},
                                             {QStringLiteral("status"), pr && item.value(QLatin1String("pull_request")).toObject()
                                                                                  .value(QLatin1String("merged_at")).isString()
                                                                            ? QObject::tr("Merged") : word},
                                             {QStringLiteral("tone"), tone},
                                             {QStringLiteral("url"), item.value(QLatin1String("html_url")).toString()}};
                                     }
                                 }
                                 settle();
                             });
        };
        ++search->pending;
        m_links->accounts()->token(account, [this, account, api = api, prefix = prefix, query, mine, review, search, settle,
                                             issues](const QByteArray &token) {
            if (!token.isEmpty()) {
                if (mine) {
                    issues(token, QStringLiteral("is:pr is:open author:@me"));
                } else if (review) {
                    issues(token, QStringLiteral("is:pr is:open review-requested:@me"));
                } else {
                    issues(token, query);
                    ++search->pending;
                    m_links->request(api, account, token,
                                     prefix + QStringLiteral("/search/repositories?per_page=5&q=") + enc(query),
                                     [search, settle](int status, const QJsonObject &json, const QString &) {
                                         if (status == 200) {
                                             for (const QJsonValue &v : json.value(QLatin1String("items")).toArray()) {
                                                 const QJsonObject r = v.toObject();
                                                 search->results << QVariantMap{
                                                     {QStringLiteral("title"), r.value(QLatin1String("full_name")).toString()},
                                                     {QStringLiteral("detail"), r.value(QLatin1String("description")).toString()
                                                                                    .simplified().left(80)},
                                                     {QStringLiteral("url"), r.value(QLatin1String("html_url")).toString()}};
                                             }
                                         }
                                         settle();
                                     });
                }
            }
            settle();
        });
    }
    settle();
}

void GitHubResolver::reviews(const Results &done)
{
    search(QStringLiteral("pr:review"), done);
}
