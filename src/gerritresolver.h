#pragma once

#include "links.h"

// Gerrit Code Review, such as codereview.qt-project.org: an account per host
// with the user's Gerrit username and an HTTP password (made in Gerrit's
// Settings, HTTP Credentials), sent with HTTP Basic to that host's REST API
// (its /a/ paths) only. What a link points to:
//
//   changes   subject, state (open, work in progress, approved, rejected,
//             merged, abandoned), each label's votes and who gave them
//             (Code-Review, Sanity-Review…), owner, branch, lines added and
//             removed, whose attention it waits for, unresolved comments,
//             patch set, the commit message, its Task-number and Fixes
//   files     a file of a patch set, from the line a link points to, as code
//
// Gerrit has no address of its own, so only hosts with an account are read
// this way.
class GerritResolver : public LinkResolver
{
public:
    explicit GerritResolver(Links *links);

    QString id() const override { return QStringLiteral("gerrit"); }
    QString name() const override { return QStringLiteral("Gerrit"); }
    bool needsUser() const override { return true; }
    bool handles(const QUrl &url) const override;
    void resolve(const QUrl &url, const Done &done) override;
    void signIn(const Account &account, const QByteArray &token, const SignedIn &done) override;
    // Changes matching `query`, in Gerrit's search syntax or as words;
    // "gerrit:mine" gives the user's open changes, "gerrit:review" those
    // waiting for their review
    void search(const QString &query, const Results &done) override;
    void reviews(const Results &done) override;
    // The changes whose commit message names issue `key` (its Task-number
    // or Fixes), but not abandoned ones
    bool relates() const override { return true; }
    void related(const QString &key, const Results &done) override;

    // What a link points to
    struct Ref
    {
        QString base;    // where Gerrit lives on its host: "" or "/gerrit"
        QString project; // "qt/qtbase"; empty when a link doesn't say
        int number = 0;  // the change's
        int patchSet = 0; // 0: the current one
        QString path;    // a file's, in that patch set
        int line = 0;    // the line a file link points to; 0: the start
        bool valid() const { return number > 0; }
    };
    static Ref refOf(const QUrl &url);
    // A change's id for the REST API: "qt%2Fqtbase~600000", or the number
    static QString changeId(const Ref &ref);
    // Gerrit's JSON without the )]}' it starts with
    static QByteArray jsonOf(const QByteArray &body);

private:
    // Changes matching Gerrit query `q` on every account's host, at most `n`
    // from each
    void query(const QString &q, int n, const Results &done);
    void readChange(const Account &account, const QByteArray &token, const Ref &ref, const Done &done);
    void readFile(const Account &account, const QByteArray &token, const Ref &ref, const Done &done);

    Links *m_links;
};
