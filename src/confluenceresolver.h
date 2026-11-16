#pragma once

#include "links.h"

// Confluence pages, read with the Atlassian accounts (Jira's sign-in): on
// Atlassian Cloud one email and API token serve both. A page's title,
// space, an excerpt, who changed it last and when.
//
// Links it knows: /wiki/spaces/KEY/pages/123/Title, blog posts, edit
// links, viewpage.action?pageId=123, short /x/AbC links and, on Data
// Center, /display/KEY/Title. Whatever comes before those in the path is
// where its REST API is ("/wiki" on Atlassian Cloud).
class ConfluenceResolver : public LinkResolver
{
public:
    explicit ConfluenceResolver(Links *links);

    QString id() const override { return QStringLiteral("confluence"); }
    QString name() const override { return QStringLiteral("Confluence"); }
    bool handles(const QUrl &url) const override;
    void resolve(const QUrl &url, const Done &done) override;
    void signIn(const Account &, const QByteArray &, const SignedIn &done) override { done(false, {}); }
    bool signsIn() const override { return false; }

    // Pages and blog posts whose text has `query`, on every Atlassian
    // site there is an account for
    void search(const QString &query, const Results &done) override;

    // Where a link points in Confluence
    struct Page
    {
        QString context; // the path before the page's: "/wiki", "/confluence" or ""
        QString id;      // the page's id, when the link has it
        QString space;   // else its space and title (/display/KEY/Title)
        QString title;
        bool valid() const { return !id.isEmpty() || (!space.isEmpty() && !title.isEmpty()); }
    };
    static Page pageOf(const QUrl &url);
    // Clearly a Confluence link, on any host: asked about which site it is
    static bool looksLike(const QUrl &url);
    // The page id a short link (/x/AbC) stands for, and back
    static QString idOfTiny(const QString &code);
    static QString tinyOf(const QString &id);
    // A page as the API gives it (content, expanded with its space,
    // version and history), and the start of its text
    static LinkInfo infoOf(const QJsonObject &content, const QString &excerpt);

private:
    Links *m_links;
};
