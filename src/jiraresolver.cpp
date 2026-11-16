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
    return Links::secure(url) && !Markdown::issueKey(url.toString()).isEmpty()
           && m_links->accountFor(id(), url).has_value();
}

void JiraResolver::resolve(const QUrl &url, const Done &done)
{
    const auto found = m_links->accountFor(id(), url);
    const QString key = Markdown::issueKey(url.toString());
    if (!found || key.isEmpty()) {
        done(std::nullopt, {});
        return;
    }
    const Account a = found->first;
    const QString site = found->second;
    m_links->accounts()->token(a, [this, a, site, key, done](const QByteArray &token) {
        if (token.isEmpty()) {
            done(std::nullopt, QObject::tr("The keychain has no token for %1: sign in again in Settings, Accounts.")
                                   .arg(a.everySite() ? QObject::tr("every atlassian.net site") : a.host));
            return;
        }
        m_links->request(site, a, token,
            QStringLiteral("/rest/api/2/issue/%1?fields=summary,status,issuetype,priority,assignee,reporter,updated")
                .arg(key),
            [site, key, done](int status, const QJsonObject &json, const QString &error) {
                if (status == 200) {
                    LinkInfo info = infoOf(key, json);
                    info.site = QStringLiteral("Jira · %1").arg(site);
                    done(info, {});
                } else {
                    // Said where it was asked: an old address may point to
                    // the wrong site
                    done(std::nullopt, QStringLiteral("%1: %2").arg(site, error));
                }
            });
    });
}

void JiraResolver::signIn(const Account &account, const QByteArray &token, const SignedIn &done)
{
    // Jira's own; a host with only Confluence answers to Confluence's
    const QStringList paths{QStringLiteral("/rest/api/2/myself"), QStringLiteral("/wiki/rest/api/user/current"),
                            QStringLiteral("/rest/api/user/current")};
    auto attempt = std::make_shared<std::function<void(int)>>();
    *attempt = [this, account, token, done, paths, attempt](int i) {
        m_links->request(account.host, account, token, paths.at(i),
            [account, done, paths, attempt, i](int status, const QJsonObject &json, const QString &error) {
                if (status == 404 && i + 1 < paths.size()) {
                    (*attempt)(i + 1);
                    return;
                }
                if (status == 200) {
                    QString who = json.value(QLatin1String("displayName")).toString();
                    if (who.isEmpty())
                        who = json.value(QLatin1String("publicName")).toString();
                    done(true, who.isEmpty() ? QObject::tr("Signed in to %1.").arg(account.host)
                                             : QObject::tr("Signed in to %1 as %2.").arg(account.host, who));
                } else if (status == 401 || status == 403) {
                    done(false, account.user.isEmpty()
                                    ? QObject::tr("%1 didn't take the token. Atlassian Cloud also needs the email it belongs to.")
                                          .arg(account.host)
                                    : QObject::tr("%1 didn't take the email and token.").arg(account.host));
                } else if (status >= 300 && status < 400) {
                    done(false, QObject::tr("%1 sent us to a sign-in page. Is it the Jira or Confluence host?")
                                    .arg(account.host));
                } else {
                    done(false, QObject::tr("Couldn't reach %1: %2").arg(account.host, error));
                }
                *attempt = nullptr; // done: let go of itself
            });
    };
    (*attempt)(0);
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
