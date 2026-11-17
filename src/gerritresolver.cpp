#include "gerritresolver.h"

#include "highlight.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QRegularExpression>
#include <QTimeZone>

#include <algorithm>
#include <memory>

namespace {

constexpr int kFileLines = 30;
constexpr qsizetype kExcerpt = 280;

QString enc(const QString &text)
{
    return QString::fromUtf8(QUrl::toPercentEncoding(text));
}

// Gerrit's times: "2026-11-10 09:12:33.000000000", in UTC
QString when(const QString &text)
{
    QDateTime t = QDateTime::fromString(text.left(19), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    if (!t.isValid())
        return {};
    t.setTimeZone(QTimeZone::UTC);
    return QLocale().toString(t.toLocalTime(), QLocale::ShortFormat);
}

QString nameOf(const QJsonObject &account)
{
    for (const char *key : {"name", "display_name", "username", "email"}) {
        const QString name = account.value(QLatin1String(key)).toString();
        if (!name.isEmpty())
            return name;
    }
    return {};
}

void add(LinkInfo &info, const QString &name, const QString &value)
{
    if (!value.isEmpty())
        info.fields.append({name, value});
}

// A change's state as people say it, and its tone
// The label holding a change back, as people say it: "Verified −1"
// (its lowest vote, with the votes known), else "Verified rejected"
QString rejectedBy(const QJsonObject &labels)
{
    // A reviewer's veto before a bot's
    QStringList names = labels.keys();
    std::stable_sort(names.begin(), names.end(), [](const QString &a, const QString &b) {
        return (a == QLatin1String("Code-Review")) > (b == QLatin1String("Code-Review"));
    });
    for (const QString &name : std::as_const(names)) {
        const QJsonObject label = labels.value(name).toObject();
        if (!label.contains(QLatin1String("rejected")))
            continue;
        int lowest = 0;
        for (const QJsonValue &v : label.value(QLatin1String("all")).toArray())
            lowest = std::min(lowest, v.toObject().value(QLatin1String("value")).toInt());
        return lowest < 0 ? QStringLiteral("%1 \u2212%2").arg(name, QString::number(-lowest))
                          : QObject::tr("%1 rejected").arg(name);
    }
    return {};
}

// A change's state as people say it, and its tone. `mergeable`: whether it
// still merges, when known (an open change's)
QPair<QString, QString> stateOf(const QJsonObject &change, std::optional<bool> mergeable = std::nullopt)
{
    const QString status = change.value(QLatin1String("status")).toString();
    if (status == QLatin1String("MERGED"))
        return {QObject::tr("Merged"), QStringLiteral("done")};
    if (status == QLatin1String("ABANDONED"))
        return {QObject::tr("Abandoned"), QStringLiteral("todo")};
    if (!mergeable && change.value(QLatin1String("mergeable")).isBool())
        mergeable = change.value(QLatin1String("mergeable")).toBool();
    if (mergeable == false)
        return {QObject::tr("Merge conflict"), QStringLiteral("failed")};
    if (change.value(QLatin1String("work_in_progress")).toBool())
        return {QObject::tr("WIP"), QStringLiteral("todo")};
    // Held back by any label (a bot's too); approved by a reviewer
    const QJsonObject labels = change.value(QLatin1String("labels")).toObject();
    const QString rejected = rejectedBy(labels);
    if (!rejected.isEmpty())
        return {rejected, QStringLiteral("failed")};
    if (labels.value(QLatin1String("Code-Review")).toObject().contains(QLatin1String("approved")))
        return {QObject::tr("Approved"), QStringLiteral("done")};
    return {QObject::tr("Open"), QStringLiteral("progress")};
}

// A label's votes, highest first: "+2 Ada Lovelace, −1 Bob"
QString votesOf(const QJsonObject &label)
{
    QList<QPair<int, QString>> votes;
    for (const QJsonValue &v : label.value(QLatin1String("all")).toArray()) {
        const QJsonObject vote = v.toObject();
        const int value = vote.value(QLatin1String("value")).toInt();
        if (value != 0)
            votes.append({value, nameOf(vote)});
    }
    std::stable_sort(votes.begin(), votes.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    QStringList parts;
    for (const auto &[value, name] : votes)
        parts << (value > 0 ? QStringLiteral("+%1 %2").arg(QString::number(value), name)
                            : QStringLiteral("−%1 %2").arg(QString::number(-value), name));
    return parts.join(QStringLiteral(", "));
}

// A commit message's body without its subject and footers, and the
// footers' tasks (Task-number, Fixes)
struct Message
{
    QString body;
    QStringList tasks;
};
Message messageOf(const QString &message)
{
    static const QRegularExpression footer(QStringLiteral(R"(^([A-Za-z][A-Za-z0-9-]*):\s*(.*)$)"));
    QStringList lines = message.split(QLatin1Char('\n'));
    if (!lines.isEmpty())
        lines.removeFirst(); // the subject
    Message m;
    // The last paragraph, if each of its lines is a footer
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    qsizetype start = lines.size();
    while (start > 0 && footer.match(lines.at(start - 1)).hasMatch())
        --start;
    if (start < lines.size() && (start == 0 || lines.at(start - 1).trimmed().isEmpty())) {
        for (qsizetype i = start; i < lines.size(); ++i) {
            const auto match = footer.match(lines.at(i));
            const QString key = match.captured(1).toLower();
            if (key == QLatin1String("task-number") || key == QLatin1String("fixes"))
                m.tasks << match.captured(2).trimmed();
        }
        lines = lines.mid(0, start);
    }
    m.body = lines.join(QLatin1Char(' ')).simplified();
    if (m.body.size() > kExcerpt)
        m.body = m.body.left(kExcerpt).section(QLatin1Char(' '), 0, -2) + QStringLiteral("…");
    return m;
}

QString scheme(const QString &host)
{
    return Links::loopback(host) ? QStringLiteral("http") : QStringLiteral("https");
}

} // namespace

GerritResolver::GerritResolver(Links *links)
    : m_links(links)
{
}

GerritResolver::Ref GerritResolver::refOf(const QUrl &url)
{
    QString path = url.path();
    QString fragment = url.fragment();
    // Old links: https://host/#/c/12345/
    if ((path.isEmpty() || path.endsWith(QLatin1Char('/'))) && fragment.startsWith(QLatin1String("/c/"))) {
        path = path.chopped(path.endsWith(QLatin1Char('/')) ? 1 : 0) + fragment;
        fragment.clear();
    }
    // A comment's or a tab's page is the change's
    static const QRegularExpression comment(QStringLiteral(R"(/comment/[^/]+/?$)"));
    path.remove(comment);

    Ref ref;
    static const QRegularExpression full(
        QStringLiteral(R"(^(.*?)/c/(.+?)/\+/(\d+)(?:/(?:\d+\.\.)?(\d+)(?:/(.+?))?)?/?$)"));
    static const QRegularExpression bare(QStringLiteral(R"(^(.*?)/c/(\d+)(?:/(?:\d+\.\.)?(\d+)(?:/(.+?))?)?/?$)"));
    static const QRegularExpression number(QStringLiteral(R"(^/(\d+)/?$)"));
    if (const auto m = full.match(path); m.hasMatch()) {
        ref.base = m.captured(1);
        ref.project = m.captured(2);
        ref.number = m.captured(3).toInt();
        ref.patchSet = m.captured(4).toInt();
        ref.path = m.captured(5);
    } else if (const auto m = bare.match(path); m.hasMatch()) {
        ref.base = m.captured(1);
        ref.number = m.captured(2).toInt();
        ref.patchSet = m.captured(3).toInt();
        ref.path = m.captured(4);
    } else if (const auto m = number.match(path); m.hasMatch()) {
        ref.number = m.captured(1).toInt();
    }
    // A file's line: #40, or #b40 on the side it was before
    static const QRegularExpression line(QStringLiteral(R"(^b?(\d+)$)"));
    if (!ref.path.isEmpty())
        ref.line = line.match(fragment).captured(1).toInt();
    return ref;
}

QString GerritResolver::changeId(const Ref &ref)
{
    return ref.project.isEmpty() ? QString::number(ref.number)
                                 : enc(ref.project) + QLatin1Char('~') + QString::number(ref.number);
}

QByteArray GerritResolver::jsonOf(const QByteArray &body)
{
    return body.startsWith(")]}'") ? body.mid(body.indexOf('\n') + 1) : body;
}

bool GerritResolver::handles(const QUrl &url) const
{
    return Links::secure(url) && refOf(url).valid() && m_links->accounts()->find(id(), Links::authority(url)).has_value();
}

void GerritResolver::signIn(const Account &account, const QByteArray &token, const SignedIn &done)
{
    m_links->requestBytes(account.host, account, token, QStringLiteral("/a/accounts/self"),
                          [account, done](int status, const QByteArray &body, const QString &error) {
                              if (status == 200) {
                                  const QJsonObject me = QJsonDocument::fromJson(jsonOf(body)).object();
                                  done(true, QObject::tr("Signed in to %1 as %2.").arg(account.host, nameOf(me)));
                              } else if (status == 401 || status == 403) {
                                  done(false, QObject::tr("%1 didn't take the username and HTTP password.").arg(account.host));
                              } else {
                                  done(false, QObject::tr("Couldn't reach %1: %2").arg(account.host, error));
                              }
                          });
}

void GerritResolver::resolve(const QUrl &url, const Done &done)
{
    const std::optional<Account> account = m_links->accounts()->find(id(), Links::authority(url));
    const Ref ref = refOf(url);
    if (!account || !ref.valid()) {
        done(std::nullopt, {});
        return;
    }
    const Account a = *account;
    m_links->accounts()->token(a, [this, a, ref, done](const QByteArray &token) {
        if (token.isEmpty()) {
            done(std::nullopt, QObject::tr("The keychain has no password for %1: sign in again in Settings, Accounts.")
                                   .arg(a.host));
            return;
        }
        if (ref.path.isEmpty())
            readChange(a, token, ref, done);
        else
            readFile(a, token, ref, done);
    });
}

void GerritResolver::readChange(const Account &account, const QByteArray &token, const Ref &ref, const Done &done)
{
    const QString path = ref.base + QStringLiteral("/a/changes/") + changeId(ref)
                         + QStringLiteral("?o=DETAILED_LABELS&o=DETAILED_ACCOUNTS&o=CURRENT_REVISION&o=CURRENT_COMMIT"
                                          "&o=CURRENT_FILES");
    Links *links = m_links;
    m_links->requestBytes(account.host, account, token, path,
                          [links, account, token, ref, done](int status, const QByteArray &body, const QString &error) {
        if (status != 200) {
            done(std::nullopt, QStringLiteral("%1: %2").arg(account.host, status == 404 ? QObject::tr("Not found") : error));
            return;
        }
        const QJsonObject change = QJsonDocument::fromJson(jsonOf(body)).object();
        LinkInfo info;
        info.title = change.value(QLatin1String("subject")).toString();
        info.label = QString::number(change.value(QLatin1String("_number")).toInt(ref.number));
        // Its branch too: an issue's cherry-picks share their subject
        info.site = QStringLiteral("Gerrit · %1 · %2").arg(change.value(QLatin1String("project")).toString(ref.project),
                                                          change.value(QLatin1String("branch")).toString());
        if (info.site.endsWith(QLatin1String(" · ")))
            info.site.chop(3);
        std::tie(info.status, info.tone) = stateOf(change);

        const QString current = change.value(QLatin1String("current_revision")).toString();
        const QJsonObject revision = change.value(QLatin1String("revisions")).toObject().value(current).toObject();
        const Message message = messageOf(revision.value(QLatin1String("commit")).toObject()
                                              .value(QLatin1String("message")).toString());
        info.summary = message.body;

        // Each label's votes, the ones that decide first
        const QJsonObject labels = change.value(QLatin1String("labels")).toObject();
        QStringList names = labels.keys();
        std::stable_sort(names.begin(), names.end(), [](const QString &a, const QString &b) {
            return (a == QLatin1String("Code-Review")) > (b == QLatin1String("Code-Review"));
        });
        // Every label, voted on or not yet
        for (const QString &name : std::as_const(names)) {
            const QString votes = votesOf(labels.value(name).toObject());
            info.fields.append({name, votes.isEmpty() ? QObject::tr("no votes") : votes});
        }
        // Shown as a badge beside the state
        info.unresolved = change.value(QLatin1String("unresolved_comment_count")).toInt();
        add(info, QObject::tr("Owner"), nameOf(change.value(QLatin1String("owner")).toObject()));
        int files = 0;
        const QJsonObject changed = revision.value(QLatin1String("files")).toObject();
        for (auto it = changed.begin(); it != changed.end(); ++it)
            files += it.key().startsWith(QLatin1Char('/')) ? 0 : 1; // not /COMMIT_MSG, /MERGE_LIST
        add(info, QObject::tr("Changes"), (files == 1 ? QObject::tr("+%1 −%2 in %3 file") : QObject::tr("+%1 −%2 in %3 files"))
                                              .arg(QString::number(change.value(QLatin1String("insertions")).toInt()),
                                                   QString::number(change.value(QLatin1String("deletions")).toInt()),
                                                   QString::number(files)));
        // Whose turn it is
        QStringList attention;
        const QJsonObject set = change.value(QLatin1String("attention_set")).toObject();
        for (auto it = set.begin(); it != set.end(); ++it)
            attention << nameOf(it.value().toObject().value(QLatin1String("account")).toObject());
        attention.removeAll(QString());
        add(info, QObject::tr("Waiting for"), attention.join(QStringLiteral(", ")));
        const int patchSet = revision.value(QLatin1String("_number")).toInt();
        add(info, QObject::tr("Patch set"), patchSet > 0 ? QString::number(patchSet) : QString());
        add(info, QObject::tr("Tasks"), message.tasks.join(QStringLiteral(", ")));
        add(info, QObject::tr("Topic"), change.value(QLatin1String("topic")).toString());
        add(info, QObject::tr("Updated"), when(change.value(QLatin1String("updated")).toString()));
        // An open change: whether it still merges, which Gerrit says apart
        // (unless it says so in the change)
        if (change.value(QLatin1String("status")).toString() != QLatin1String("NEW")
            || change.value(QLatin1String("mergeable")).isBool()) {
            done(info, {});
            return;
        }
        links->requestBytes(account.host, account, token,
                            ref.base + QStringLiteral("/a/changes/") + changeId(ref) + QStringLiteral("/revisions/current/mergeable"),
                            [change, info, done](int status, const QByteArray &body, const QString &) mutable {
            const QJsonObject merge = QJsonDocument::fromJson(jsonOf(body)).object();
            if (status == 200 && merge.value(QLatin1String("mergeable")).isBool())
                std::tie(info.status, info.tone) = stateOf(change, merge.value(QLatin1String("mergeable")).toBool());
            done(info, {});
        });
    });
}

void GerritResolver::readFile(const Account &account, const QByteArray &token, const Ref &ref, const Done &done)
{
    const QString revision = ref.patchSet > 0 ? QString::number(ref.patchSet) : QStringLiteral("current");
    const QString path = ref.base + QStringLiteral("/a/changes/") + changeId(ref) + QStringLiteral("/revisions/") + revision
                         + QStringLiteral("/files/") + enc(ref.path) + QStringLiteral("/content");
    m_links->requestBytes(account.host, account, token, path,
                          [account, ref, done](int status, const QByteArray &body, const QString &error) {
        if (status != 200) {
            done(std::nullopt, QStringLiteral("%1: %2").arg(account.host, status == 404 ? QObject::tr("Not found") : error));
            return;
        }
        LinkInfo info;
        info.title = QFileInfo(ref.path).fileName();
        info.kind = QStringLiteral("code");
        info.site = QStringLiteral("Gerrit · %1").arg(ref.project.isEmpty() ? account.host : ref.project);
        const QString where = ref.patchSet > 0
            ? QObject::tr("%1 in change %2, patch set %3").arg(ref.path, QString::number(ref.number),
                                                                 QString::number(ref.patchSet))
            : QObject::tr("%1 in change %2").arg(ref.path, QString::number(ref.number));
        // The content, base64 encoded
        const QByteArray raw = QByteArray::fromBase64(body.trimmed());
        if (raw.isEmpty() || raw.contains('\0')) {
            info.summary = where;
            done(info, {});
            return;
        }
        const QStringList lines = QString::fromUtf8(raw).split(QLatin1Char('\n'));
        // A few lines before the one pointed at, for what leads up to it
        const int from = ref.line > 0 ? std::clamp<int>(ref.line - 3, 1, lines.size()) : 1;
        const int to = std::min<int>(from + kFileLines - 1, lines.size());
        info.code = lines.mid(from - 1, to - from + 1).join(QLatin1Char('\n'));
        info.firstLine = from;
        info.language = Highlight::language(QFileInfo(ref.path).suffix(), info.code);
        info.summary = ref.line > 0 ? QObject::tr("%1, line %2").arg(where, QString::number(ref.line)) : where;
        done(info, {});
    });
}

void GerritResolver::search(const QString &query, const Results &done)
{
    const QString q = query == QLatin1String("gerrit:mine") ? QStringLiteral("is:open owner:self")
                    : query == QLatin1String("gerrit:review")
                        ? QStringLiteral("is:open -is:wip reviewer:self -owner:self attention:self")
                        : query;
    this->query(q, 8, done);
}

void GerritResolver::query(const QString &q, int n, const Results &done)
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
    for (const Account &account : m_links->accounts()->all()) {
        if (account.provider != id())
            continue;
        ++search->pending;
        m_links->accounts()->token(account, [this, account, q, n, search, settle](const QByteArray &token) {
            if (token.isEmpty())
                return settle();
            m_links->requestBytes(account.host, account, token,
                                  QStringLiteral("/a/changes/?n=%1&o=LABELS&q=").arg(n) + enc(q),
                                  [account, search, settle](int status, const QByteArray &body, const QString &) {
                if (status == 200) {
                    for (const QJsonValue &v : QJsonDocument::fromJson(jsonOf(body)).array()) {
                        const QJsonObject change = v.toObject();
                        const auto [word, tone] = stateOf(change);
                        const QString project = change.value(QLatin1String("project")).toString();
                        const int number = change.value(QLatin1String("_number")).toInt();
                        search->results << QVariantMap{
                            {QStringLiteral("title"), change.value(QLatin1String("subject")).toString()},
                            {QStringLiteral("detail"), QStringLiteral("%1 · %2").arg(project, QString::number(number))},
                            {QStringLiteral("status"), word},
                            {QStringLiteral("tone"), tone},
                            {QStringLiteral("url"), QStringLiteral("%1://%2/c/%3/+/%4")
                                                        .arg(scheme(account.host), account.host, project,
                                                             QString::number(number))}};
                    }
                }
                settle();
            });
        });
    }
    settle();
}

void GerritResolver::reviews(const Results &done)
{
    search(QStringLiteral("gerrit:review"), done);
}

void GerritResolver::related(const QString &key, const Results &done)
{
    query(QStringLiteral("message:\"%1\" -is:abandoned").arg(key), 25, done);
}
