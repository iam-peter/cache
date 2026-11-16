#pragma once

#include "links.h"

// Wikipedia articles, in any language: the article's opening lines, its
// short description and its lead picture, from Wikipedia's page summary
// API. Nothing to sign in to. Read for link cards and hover cards only,
// like any web page: never just because a note shows a link.
class WikipediaResolver : public LinkResolver
{
public:
    explicit WikipediaResolver(Links *links);

    QString id() const override { return QStringLiteral("wikipedia"); }
    QString name() const override { return QStringLiteral("Wikipedia"); }
    bool handles(const QUrl &url) const override;
    void resolve(const QUrl &url, const Done &done) override;
    void signIn(const Account &, const QByteArray &, const SignedIn &done) override { done(false, {}); }
    bool signsIn() const override { return false; }

    // The article a link names: its wiki's host ("de.wikipedia.org", the
    // mobile one's too) and its title as in the address; empty for none
    struct Article
    {
        QString host;
        QString title;
        bool valid() const { return !title.isEmpty(); }
    };
    static Article articleOf(const QUrl &url);
    static LinkInfo infoOf(const QString &host, const QJsonObject &summary);

    // Tests: where the wikis are ("http://127.0.0.1:1234")
    void setApi(const QString &base) { m_api = base; }

private:
    Links *m_links;
    QString m_api;
};
