#pragma once

#include "links.h"

// Any web page, for link cards: its title, description, picture and site,
// as the page announces them (Open Graph, else <title> and the
// description). Nothing is signed in to.
//
// A link to an issue on a host without an account isn't fetched (that
// would only bring the sign-in page): its card shows the key and how to
// see more.
class PageResolver : public LinkResolver
{
public:
    explicit PageResolver(Links *links);

    QString id() const override { return QStringLiteral("page"); }
    QString name() const override { return QStringLiteral("Web page"); }
    bool handles(const QUrl &url) const override;
    void resolve(const QUrl &url, const Done &done) override;
    void signIn(const Account &, const QByteArray &, const SignedIn &done) override { done(false, {}); }

    // What `html`, served from `url`, says about itself
    static LinkInfo infoOf(const QUrl &url, const QString &html);

private:
    Links *m_links;
};
