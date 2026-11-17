#pragma once

#include "links.h"

// Figma files and what in them a link points to, read with a personal
// access token through Figma's REST API: the frame, component or page as
// a picture Figma renders, its name and size, the file it is in and when
// that last changed. Design files, FigJam boards, prototypes, slides and
// branches alike.
//
// The token goes to the API only (X-Figma-Token); the pictures are on
// Figma's image servers and are loaded without it.
class FigmaResolver : public LinkResolver
{
public:
    explicit FigmaResolver(Links *links);

    QString id() const override { return QStringLiteral("figma"); }
    QString name() const override { return QStringLiteral("Figma"); }
    bool handles(const QUrl &url) const override;
    void resolve(const QUrl &url, const Done &done) override;
    // A frame rendered `pixels` large on its longest side (up to 4× its size)
    bool sharpen(const QUrl &url, int pixels, const Done &done) override;
    void signIn(const Account &account, const QByteArray &token, const SignedIn &done) override;
    // Its accounts are for figma.com, whatever the link's host
    QString fixedHost() const override { return QStringLiteral("figma.com"); }

    // Files of the account's teams whose name has `query`'s words (Figma
    // can't search their insides). The list is read once a quarter hour.
    void search(const QString &query, const Results &done) override;

    // What a link points to: the file (a branch's own key) and the node,
    // as the API names it ("12:34" for the link's node-id=12-34)
    struct Ref
    {
        QString file;
        QString node;
        bool valid() const { return !file.isEmpty(); }
    };
    static Ref refOf(const QUrl &url);

    // Tests: where the API is
    void setApi(const QString &base) { m_api = base; }

private:
    // What a link points to, its render (if a frame) at least `pixels`
    // large, else as large as before or the usual size
    void read(const QUrl &url, int pixels, const Done &done);
    void get(const QByteArray &token, const QString &path, const Links::JsonReply &reply);

    void listFiles(const Account &account, const std::function<void()> &done);

    Links *m_links;
    QString m_api = QStringLiteral("https://api.figma.com");
    QVariantList m_files; // [{key, name, project, thumbnail}] of the teams
    // Their libraries' published component sets and components (not a
    // set's variants): [{name, file, node, set, thumbnail, page}]
    QVariantList m_components;
    QDateTime m_filesRead;
    QStringList m_filesOf; // the teams they are of
    QStringList m_listErrors; // what Figma said when a team couldn't be listed
    QList<std::function<void()>> m_listing; // waiting for the list
};
