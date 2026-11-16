#pragma once

#include "links.h"

#include <QJsonObject>

// Jira issues, on the hosts there is an account for. Its accounts are the
// Atlassian ones: Atlassian Cloud signs in with an email and an API token
// (which Confluence reads with too), Data Center with a personal access
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
    QString name() const override { return QStringLiteral("Atlassian"); }
    bool handles(const QUrl &url) const override;
    void resolve(const QUrl &url, const Done &done) override;
    void signIn(const Account &account, const QByteArray &token, const SignedIn &done) override;

    // Issues whose text has `query`, on every Atlassian site there is an
    // account for; an issue key also finds that issue
    void search(const QString &query, const Results &done) override;

    // An issue as the API gives it, as a link's details
    static LinkInfo infoOf(const QString &key, const QJsonObject &issue);

private:
    Links *m_links;
};
