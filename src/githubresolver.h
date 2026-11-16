#pragma once

#include "links.h"

// GitHub, on github.com or GitHub Enterprise: an account per host with a
// personal access token, sent as a bearer token to that host's API only
// (api.github.com for github.com, <host>/api/v3 for Enterprise). What a
// link points to, read through its REST API:
//
//   pull requests    #n, title, state (open, draft, merged, closed),
//                    branches, author, reviewers and who approved, checks,
//                    lines added and removed, files, labels, milestone
//   Actions runs     status, each job and how it went, duration, who
//                    started it; single jobs too
//   files            the lines a link points to (#L40-L60), as code
//   issues           #n, title, state, assignees, labels, milestone
//   commits          message, author, lines added and removed, checks
//   repositories     description, default branch's checks, languages,
//                    latest release, stars
//   releases, compares, milestones (issues closed), gists
//
// Without an account a link is read as the web page it is (GitHub's pages
// describe themselves well).
class GitHubResolver : public LinkResolver
{
public:
    explicit GitHubResolver(Links *links);

    QString id() const override { return QStringLiteral("github"); }
    QString name() const override { return QStringLiteral("GitHub"); }
    bool handles(const QUrl &url) const override;
    void resolve(const QUrl &url, const Done &done) override;
    void signIn(const Account &account, const QByteArray &token, const SignedIn &done) override;
    // Repositories, pull requests and issues with `query`; "pr:mine" gives
    // the user's open pull requests, "pr:review" those waiting for their
    // review
    void search(const QString &query, const Results &done) override;
    void reviews(const Results &done) override;

    // What a link points to
    struct Ref
    {
        QString repo;   // "owner/name"; empty for a gist
        QString kind;   // repo, pr, issue, commit, run, job, file, release,
                        // compare, milestone, gist
        QString id;     // number, sha, run id, tag, "a...b", gist id
        QString ref;    // a file's branch or commit
        QString path;   // a file's path
        int from = 0;   // a file's lines, 1-based; 0: from the start
        int to = 0;
        bool valid() const { return !kind.isEmpty() && (!repo.isEmpty() || kind == QLatin1String("gist")); }
    };
    static Ref refOf(const QUrl &url);
    // Where a host's API is: api.github.com for github.com (and its gists),
    // else the host's /api/v3. {host, path prefix}
    static QPair<QString, QString> apiOf(const QString &host);

private:
    struct Context;
    void read(const std::shared_ptr<Context> &c);
    std::optional<Account> accountOf(const QUrl &url) const;

    Links *m_links;
};
