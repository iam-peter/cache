#pragma once

#include "links.h"

#include <QJsonObject>

// Jira issues, on the hosts there is an account for: Jira Cloud signs in
// with an email and an API token, Jira Data Center with a personal access
// token. Both answer to REST API 2.
//
// The token goes to the account's own host only, over TLS, and no redirect
// is followed: a sign-in page elsewhere gets nothing.
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
    using Reply = std::function<void(int status, const QJsonObject &json, const QString &error)>;
    void get(const Account &account, const QByteArray &token, const QString &path, const Reply &reply);

    Links *m_links;
};
