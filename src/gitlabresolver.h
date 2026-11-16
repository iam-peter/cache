#pragma once

#include "links.h"

// GitLab, on gitlab.com or a GitLab of one's own: an account per host with
// a personal access token (read_api), sent as a bearer token to that host's
// API only. What a link points to, read through REST API v4:
//
//   merge requests   !iid, title, state, branches, author, reviewers and who
//                    approved, pipeline, files changed, labels, milestone
//   pipelines, jobs  status, stages and their jobs, duration, who started it
//   files            the lines a link points to (#L40-60), as code
//   issues           #iid, title, state, assignees, labels, milestone, due
//   commits          message, author, lines added and removed, pipeline, branches
//   repositories     name, description, default branch's pipeline, languages,
//                    latest release, stars
//   releases, tags, compares, milestones (progress), wiki pages, snippets
class GitLabResolver : public LinkResolver
{
public:
    explicit GitLabResolver(Links *links);

    QString id() const override { return QStringLiteral("gitlab"); }
    QString name() const override { return QStringLiteral("GitLab"); }
    bool handles(const QUrl &url) const override;
    void resolve(const QUrl &url, const Done &done) override;
    void signIn(const Account &account, const QByteArray &token, const SignedIn &done) override;
    // Projects, merge requests and issues with `query`; "mr:mine" gives the
    // user's open merge requests, "mr:review" those waiting for their review
    void search(const QString &query, const Results &done) override;
    // The merge requests waiting for the user's review, on every GitLab
    void reviews(const Results &done);

    // What a link points to
    struct Ref
    {
        QString project; // "group/sub/project"
        QString kind;    // project, mr, issue, commit, pipeline, job, file,
                         // release, tag, compare, milestone, wiki, snippet
        QString id;      // iid, sha, id, tag, slug, or "from...to"
        QString ref;     // a file's branch or commit
        QString path;    // a file's path
        int from = 0;    // a file's lines, 1-based; 0: from the start
        int to = 0;
        bool valid() const { return !project.isEmpty() && !kind.isEmpty(); }
    };
    static Ref refOf(const QUrl &url);

private:
    struct Context;
    void read(const std::shared_ptr<Context> &c);
    void list(const Account &account, const QString &site, const QByteArray &token, const QString &path,
              const QString &kind, const std::function<void(const QVariantList &)> &done);

    Links *m_links;
};
