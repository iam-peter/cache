#include "jiraresolver.h"

#include "markdown.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>

namespace {

// "2026-11-12T10:00:00.000+0100", as Jira writes times
QDateTime parseTime(const QString &text)
{
    QString t = text;
    static const QRegularExpression offset(QStringLiteral(R"(([+-]\d\d)(\d\d)$)"));
    t.replace(offset, QStringLiteral("\\1:\\2"));
    return QDateTime::fromString(t, Qt::ISODateWithMs);
}

QString nameOf(const QJsonValue &value, const char *field = "name")
{
    return value.toObject().value(QLatin1String(field)).toString();
}

} // namespace

JiraResolver::JiraResolver(Links *links)
    : m_links(links)
{
}

bool JiraResolver::handles(const QUrl &url) const
{
    const QString host = Links::authority(url);
    const bool secure = url.scheme() == QLatin1String("https")
                        || (url.scheme() == QLatin1String("http") && Links::loopback(host));
    return secure && !Markdown::issueKey(url.toString()).isEmpty()
           && m_links->accounts()->find(id(), host).has_value();
}

void JiraResolver::resolve(const QUrl &url, const Done &done)
{
    const std::optional<Account> account = m_links->accounts()->find(id(), Links::authority(url));
    const QString key = Markdown::issueKey(url.toString());
    if (!account || key.isEmpty()) {
        done(std::nullopt, {});
        return;
    }
    const Account a = *account;
    m_links->accounts()->token(a, [this, a, key, done](const QByteArray &token) {
        if (token.isEmpty()) {
            done(std::nullopt, QStringLiteral("no token for %1").arg(a.host));
            return;
        }
        get(a, token,
            QStringLiteral("/rest/api/2/issue/%1?fields=summary,status,issuetype,priority,assignee,reporter,updated")
                .arg(key),
            [key, done](int status, const QJsonObject &json, const QString &error) {
                if (status == 200)
                    done(infoOf(key, json), {});
                else
                    done(std::nullopt, error);
            });
    });
}

void JiraResolver::signIn(const Account &account, const QByteArray &token, const SignedIn &done)
{
    get(account, token, QStringLiteral("/rest/api/2/myself"),
        [account, done](int status, const QJsonObject &json, const QString &error) {
            if (status == 200) {
                const QString who = json.value(QLatin1String("displayName")).toString();
                done(true, who.isEmpty() ? QObject::tr("Signed in to %1.").arg(account.host)
                                         : QObject::tr("Signed in to %1 as %2.").arg(account.host, who));
            } else if (status == 401 || status == 403) {
                done(false, account.user.isEmpty()
                                ? QObject::tr("%1 didn't take the token. Jira Cloud also needs the email it belongs to.")
                                      .arg(account.host)
                                : QObject::tr("%1 didn't take the email and token.").arg(account.host));
            } else if (status >= 300 && status < 400) {
                done(false, QObject::tr("%1 sent us to a sign-in page. Is it the Jira host?").arg(account.host));
            } else {
                done(false, QObject::tr("Couldn't reach %1: %2").arg(account.host, error));
            }
        });
}

LinkInfo JiraResolver::infoOf(const QString &key, const QJsonObject &issue)
{
    const QJsonObject fields = issue.value(QLatin1String("fields")).toObject();
    LinkInfo info;
    info.title = issue.value(QLatin1String("key")).toString(key);
    info.summary = fields.value(QLatin1String("summary")).toString();
    const QJsonObject status = fields.value(QLatin1String("status")).toObject();
    info.status = status.value(QLatin1String("name")).toString();
    const QString category = nameOf(status.value(QLatin1String("statusCategory")), "key");
    info.tone = category == QLatin1String("done")            ? QStringLiteral("done")
                : category == QLatin1String("indeterminate") ? QStringLiteral("progress")
                                                             : QStringLiteral("todo");

    const auto add = [&info](const QString &name, const QString &value) {
        if (!value.isEmpty())
            info.fields.append({name, value});
    };
    add(QObject::tr("Type"), nameOf(fields.value(QLatin1String("issuetype"))));
    add(QObject::tr("Priority"), nameOf(fields.value(QLatin1String("priority"))));
    const QString assignee = nameOf(fields.value(QLatin1String("assignee")), "displayName");
    add(QObject::tr("Assignee"), assignee.isEmpty() ? QObject::tr("Unassigned") : assignee);
    add(QObject::tr("Reporter"), nameOf(fields.value(QLatin1String("reporter")), "displayName"));
    const QDateTime updated = parseTime(fields.value(QLatin1String("updated")).toString());
    if (updated.isValid())
        add(QObject::tr("Updated"), QLocale().toString(updated.toLocalTime(), QLocale::ShortFormat));
    return info;
}

void JiraResolver::get(const Account &account, const QByteArray &token, const QString &path, const Reply &reply)
{
    // Built from the account, never from the link: the token goes to its host
    QUrl url(QStringLiteral("%1://%2%3")
                 .arg(Links::loopback(account.host) ? QStringLiteral("http") : QStringLiteral("https"),
                      account.host, path));
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Authorization", account.user.isEmpty()
                                              ? "Bearer " + token
                                              : "Basic " + (account.user.toUtf8() + ':' + token).toBase64());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Cache"));
    request.setTransferTimeout(15000);
    QNetworkReply *r = m_links->network()->get(request);
    QObject::connect(r, &QNetworkReply::finished, r, [r, reply] {
        r->deleteLater();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject json = QJsonDocument::fromJson(r->readAll()).object();
        QString error = r->errorString();
        const QJsonArray messages = json.value(QLatin1String("errorMessages")).toArray();
        if (!messages.isEmpty())
            error = messages.first().toString();
        reply(status, json, error);
    });
}
