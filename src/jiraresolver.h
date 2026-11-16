#pragma once

#include "links.h"

#include <QJsonObject>

// Jira issues, on the hosts there is an account for: Jira Cloud signs in
// with an email and an API token, Jira Data Center with a personal access
// token. Both answer to REST API 2.
//
// The token goes only to the site the account gives for a link (its own
// host, an old address's new site, or for an account of every Atlassian
// site that site), over TLS, and no redirect is followed: a sign-in page
// elsewhere gets nothing.
class JiraResolver : public LinkResolver
{
public:
    explicit JiraResolver(Links *links);

    QString id() const override { return QStringLiteral("jira"); }
    QString name() const override { return QStringLiteral("Jira"); }
    bool handles(const QUrl &url) const override;
    void resolve(const QUrl &url, const Done &done) override;
    void signIn(const Account &account, const QByteArray &token, const SignedIn &done) override;

    // An issue as the API gives it, as a link's details
    static LinkInfo infoOf(const QString &key, const QJsonObject &issue);

private:
    // The account for a link and the site to ask: by the link's host, or
    // by the Atlassian site that host turned out to be an address of
    std::optional<std::pair<Account, QString>> accountFor(const QUrl &url) const;
    using Reply = std::function<void(int status, const QJsonObject &json, const QString &error)>;
    // `site`: the account's host, or the one it gives for a link (siteFor)
    void get(const QString &site, const Account &account, const QByteArray &token, const QString &path,
             const Reply &reply);

    Links *m_links;
};
