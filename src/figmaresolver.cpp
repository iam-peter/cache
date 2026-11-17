#include "figmaresolver.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSet>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace {

// The longest side a node is rendered at, in pixels, at first: sharp in a
// card; larger only when it is shown larger, up to kMaxSide
constexpr double kPictureSide = 1600;
constexpr int kMaxSide = 4096;

bool figmaHost(const QString &host)
{
    return host == QLatin1String("figma.com") || host.endsWith(QLatin1String(".figma.com"));
}

QString when(const QString &text)
{
    const QDateTime t = QDateTime::fromString(text, Qt::ISODateWithMs);
    return t.isValid() ? QLocale().toString(t.toLocalTime(), QLocale::ShortFormat) : QString();
}

// "FRAME" as people say it
QString typeName(const QString &type)
{
    static const QHash<QString, const char *> names{
        {QStringLiteral("CANVAS"), QT_TRANSLATE_NOOP("Figma", "Page")},
        {QStringLiteral("FRAME"), QT_TRANSLATE_NOOP("Figma", "Frame")},
        {QStringLiteral("SECTION"), QT_TRANSLATE_NOOP("Figma", "Section")},
        {QStringLiteral("GROUP"), QT_TRANSLATE_NOOP("Figma", "Group")},
        {QStringLiteral("COMPONENT"), QT_TRANSLATE_NOOP("Figma", "Component")},
        {QStringLiteral("COMPONENT_SET"), QT_TRANSLATE_NOOP("Figma", "Component set")},
        {QStringLiteral("INSTANCE"), QT_TRANSLATE_NOOP("Figma", "Instance")},
        {QStringLiteral("TEXT"), QT_TRANSLATE_NOOP("Figma", "Text")},
        {QStringLiteral("STICKY"), QT_TRANSLATE_NOOP("Figma", "Sticky")},
        {QStringLiteral("SLIDE"), QT_TRANSLATE_NOOP("Figma", "Slide")},
    };
    const char *name = names.value(type);
    if (name)
        return QCoreApplication::translate("Figma", name);
    QString text = type.toLower().replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!text.isEmpty())
        text[0] = text.at(0).toUpper();
    return text;
}

QString editorName(const QString &editor)
{
    if (editor == QLatin1String("figjam"))
        return QStringLiteral("FigJam");
    if (editor == QLatin1String("slides"))
        return QStringLiteral("Figma Slides");
    return QStringLiteral("Figma");
}

} // namespace

FigmaResolver::FigmaResolver(Links *links)
    : m_links(links)
{
}

FigmaResolver::Ref FigmaResolver::refOf(const QUrl &url)
{
    Ref ref;
    if (!figmaHost(url.host().toLower()))
        return ref;
    static const QStringList kinds{QStringLiteral("file"),   QStringLiteral("design"), QStringLiteral("proto"),
                                   QStringLiteral("board"),  QStringLiteral("slides"), QStringLiteral("deck"),
                                   QStringLiteral("make"),   QStringLiteral("embed")};
    const QStringList parts = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() < 2 || !kinds.contains(parts.at(0)))
        return ref;
    static const QRegularExpression key(QStringLiteral(R"(^[A-Za-z0-9]{10,}$)"));
    ref.file = parts.at(1);
    // A branch: its own key
    if (parts.size() >= 4 && parts.at(2) == QLatin1String("branch"))
        ref.file = parts.at(3);
    if (!key.match(ref.file).hasMatch())
        return {};
    ref.node = QUrlQuery(url).queryItemValue(QStringLiteral("node-id"), QUrl::FullyDecoded);
    ref.node.replace(QLatin1Char('-'), QLatin1Char(':'));
    static const QRegularExpression node(QStringLiteral(R"(^[A-Za-z0-9:;]+$)"));
    if (!node.match(ref.node).hasMatch())
        ref.node.clear();
    return ref;
}

bool FigmaResolver::handles(const QUrl &url) const
{
    return url.scheme() == QLatin1String("https") && refOf(url).valid()
           && m_links->accounts()->find(id(), fixedHost()).has_value();
}

void FigmaResolver::get(const QByteArray &token, const QString &path, const Links::JsonReply &reply)
{
    QNetworkRequest request(QUrl(m_api + path));
    request.setRawHeader("X-Figma-Token", token);
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Cache"));
    // Rendering a large frame takes Figma a while
    request.setTransferTimeout(60000);
    QNetworkReply *r = m_links->network()->get(request);
    QObject::connect(r, &QNetworkReply::finished, r, [r, reply] {
        r->deleteLater();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // A bare list too (the folder endpoints may answer one): as "items"
        const QJsonDocument document = QJsonDocument::fromJson(r->readAll());
        const QJsonObject json = document.isArray() ? QJsonObject{{QStringLiteral("items"), document.array()}}
                                                    : document.object();
        QString error = r->errorString();
        if (json.value(QLatin1String("err")).isString())
            error = json.value(QLatin1String("err")).toString();
        else if (json.value(QLatin1String("message")).isString())
            error = json.value(QLatin1String("message")).toString();
        reply(status, json, error);
    });
}

void FigmaResolver::signIn(const Account &, const QByteArray &token, const SignedIn &done)
{
    get(token, QStringLiteral("/v1/me"), [done](int status, const QJsonObject &json, const QString &error) {
        if (status == 200) {
            const QString who = json.value(QLatin1String("handle")).toString();
            done(true, who.isEmpty() ? QObject::tr("Signed in to Figma.") : QObject::tr("Signed in to Figma as %1.").arg(who));
        } else if (status == 401 || status == 403) {
            done(false, QObject::tr("Figma didn't take the token: %1").arg(error));
        } else {
            done(false, QObject::tr("Couldn't reach Figma: %1").arg(error));
        }
    });
}

void FigmaResolver::resolve(const QUrl &url, const Done &done)
{
    read(url, 0, done);
}

bool FigmaResolver::sharpen(const QUrl &url, int pixels, const Done &done)
{
    if (refOf(url).node.isEmpty())
        return false; // a file's thumbnail is as Figma makes it
    read(url, pixels, done);
    return true;
}

void FigmaResolver::read(const QUrl &url, int pixels, const Done &done)
{
    const std::optional<Account> account = m_links->accounts()->find(id(), fixedHost());
    const Ref ref = refOf(url);
    if (!account || !ref.valid()) {
        done(std::nullopt, {});
        return;
    }
    m_links->accounts()->token(*account, [this, url, pixels, ref, done](const QByteArray &token) {
        if (token.isEmpty()) {
            done(std::nullopt, QObject::tr("The keychain has no Figma token: sign in again in Settings, Accounts."));
            return;
        }
        const QString file = QString::fromUtf8(QUrl::toPercentEncoding(ref.file));
        const QString node = QString::fromUtf8(QUrl::toPercentEncoding(ref.node));
        // Just the file's top (its pages), or the node: never all of it
        const QString path = ref.node.isEmpty() ? QStringLiteral("/v1/files/%1?depth=1").arg(file)
                                                : QStringLiteral("/v1/files/%1/nodes?ids=%2&depth=1").arg(file, node);
        get(token, path, [this, url, pixels, ref, token, file, node, done](int status, const QJsonObject &json, const QString &error) {
            if (status != 200) {
                done(std::nullopt, QStringLiteral("Figma: %1").arg(error));
                return;
            }
            LinkInfo info;
            info.kind = QStringLiteral("design");
            const QString fileName = json.value(QLatin1String("name")).toString();
            info.version = json.value(QLatin1String("version")).toString();
            const QString edited = when(json.value(QLatin1String("lastModified")).toString());
            const QString editor = json.value(QLatin1String("editorType")).toString();
            info.site = editorName(editor);
            info.image = json.value(QLatin1String("thumbnailUrl")).toString();
            const auto add = [&info](const QString &name, const QString &value) {
                if (!value.isEmpty())
                    info.fields.append({name, value});
            };
            if (ref.node.isEmpty()) {
                // The file: its pages
                info.title = fileName;
                QStringList pages;
                for (const QJsonValue &page : json.value(QLatin1String("document")).toObject()
                                                  .value(QLatin1String("children")).toArray())
                    pages << page.toObject().value(QLatin1String("name")).toString();
                pages.removeAll(QString());
                if (!pages.isEmpty())
                    info.summary = QObject::tr("Pages: %1").arg(pages.mid(0, 6).join(QStringLiteral(", "))
                                                                + (pages.size() > 6 ? QStringLiteral(", …") : QString()));
                add(QObject::tr("Pages"), pages.isEmpty() ? QString() : QString::number(pages.size()));
                add(QObject::tr("Edited"), edited);
                // The file as it was: its thumbnail there is still right
                const std::optional<LinkInfo> before = m_links->known(id(), url.toString());
                if (before && !info.version.isEmpty() && before->version == info.version && !before->image.isEmpty()) {
                    info.image = before->image;
                    info.remoteImage = before->remoteImage;
                }
                done(info, {});
                return;
            }

            // The node: its own picture, rendered by Figma
            const QJsonObject document = json.value(QLatin1String("nodes")).toObject().value(ref.node).toObject()
                                             .value(QLatin1String("document")).toObject();
            if (document.isEmpty()) {
                done(std::nullopt, QObject::tr("Figma: no such frame in “%1”").arg(fileName));
                return;
            }
            const QString type = document.value(QLatin1String("type")).toString();
            info.title = document.value(QLatin1String("name")).toString();
            info.summary = QObject::tr("%1 in %2").arg(typeName(type), fileName);
            const QJsonObject box = document.value(QLatin1String("absoluteBoundingBox")).toObject();
            const double w = box.value(QLatin1String("width")).toDouble();
            const double h = box.value(QLatin1String("height")).toDouble();
            add(QObject::tr("File"), fileName);
            if (w > 0 && h > 0)
                add(QObject::tr("Size"), QStringLiteral("%1 × %2").arg(std::round(w)).arg(std::round(h)));
            add(QObject::tr("Edited"), edited);
            // How large: as asked, or as large as it was needed before, or
            // the usual (twice its size, at most kPictureSide); at most 4×
            // its size and kMaxSide
            const std::optional<LinkInfo> before = m_links->known(id(), url.toString());
            const double longest = std::max(w, h);
            const int usual = longest > 0 ? int(std::round(longest * std::min(2.0, kPictureSide / longest))) : 0;
            const int most = longest > 0 ? std::min(kMaxSide, int(std::round(longest * 4))) : kMaxSide;
            const int wanted = std::min(most, std::max({pixels, usual, before ? before->renderSize : 0}));
            // The file as it was, and a render as large: still right, no new one
            if (before && !info.version.isEmpty() && before->version == info.version && !before->image.isEmpty()
                && before->renderSize >= wanted) {
                info.image = before->image;
                info.remoteImage = before->remoteImage;
                info.renderSize = before->renderSize;
                done(info, {});
                return;
            }
            // A page has no box: rendered at 1:1
            const double scale = longest > 0 ? std::clamp(wanted / longest, 0.01, 4.0) : 1.0;
            info.renderSize = longest > 0 ? int(std::round(longest * scale)) : 0;
            // In one arg(): the encoded id's %3A would be taken for a %3
            get(token, QStringLiteral("/v1/images/%1?ids=%2&format=png&scale=%3")
                           .arg(file, node, QString::number(scale, 'g', 3)),
                [ref, info, done](int status, const QJsonObject &images, const QString &) mutable {
                    const QString picture = images.value(QLatin1String("images")).toObject().value(ref.node).toString();
                    // Without it, the file's thumbnail
                    if (status == 200 && !picture.isEmpty())
                        info.image = picture;
                    done(info, {});
                });
        });
    });
}

namespace {

// The items of a list Figma answered with, however it wraps them (the
// folder endpoints' answers aren't documented): a bare array, or under
// one of `keys`
QJsonArray itemsOf(const QJsonObject &json, std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        const QJsonValue value = json.value(QLatin1String(key));
        if (value.isArray())
            return value.toArray();
    }
    return json.value(QLatin1String("items")).toArray();
}

QString idOf(const QJsonObject &item)
{
    return item.value(QLatin1String("id")).toVariant().toString();
}

// Folders can nest; this deep, and this many, is plenty for a palette
constexpr int kFolderDepth = 4;
constexpr int kMaxFolders = 300;
// A team library's components, a page at a time
constexpr int kLibraryPage = 1000;
constexpr int kLibraryPages = 40;
// Component results shown for a search, best first
constexpr int kComponentResults = 12;

} // namespace

void FigmaResolver::listFiles(const Account &account, const std::function<void()> &done)
{
    m_listing << done;
    if (m_listing.size() > 1)
        return; // already being read
    struct Listing {
        QVariantList files;
        QVariantList components;
        QStringList errors;
        QHash<QString, QString> libraryErrors; // by team: said only if its files were listed
        QSet<QString> frames;                  // "file node" of the frames listed
        int pending = 1;
        int folders = 0;
        // A folder's files and subfolders, named by its path
        std::function<void(const QString &id, const QString &name, int depth)> walk;
        // A page of a team library's component sets or components
        std::function<void(const QString &team, const QString &kind, const QString &after, int page)> library;
    };
    auto listing = std::make_shared<Listing>();
    const QStringList teams = account.teams;
    const auto settle = [this, listing, teams] {
        if (--listing->pending > 0)
            return;
        listing->walk = nullptr; // they hold the listing
        listing->library = nullptr;
        for (auto it = listing->libraryErrors.cbegin(); it != listing->libraryErrors.cend(); ++it) {
            const QString team = QStringLiteral("team %1").arg(it.key());
            if (std::none_of(listing->errors.cbegin(), listing->errors.cend(),
                             [&](const QString &e) { return e.startsWith(team + QLatin1Char(':')); }))
                listing->errors << it.value();
        }
        m_files = listing->files;
        m_components = listing->components;
        m_listErrors = listing->errors;
        m_filesRead = QDateTime::currentDateTimeUtc();
        m_filesOf = teams;
        const auto waiting = std::exchange(m_listing, {});
        for (const auto &w : waiting)
            w();
    };
    const auto addFiles = [listing](const QJsonObject &json, const QString &folder) {
        for (const QJsonValue &f : itemsOf(json, {"files"})) {
            const QJsonObject file = f.toObject();
            const QString key = file.value(QLatin1String("key")).toString();
            if (key.isEmpty())
                continue;
            listing->files << QVariantMap{
                {QStringLiteral("key"), key},
                {QStringLiteral("name"), file.value(QLatin1String("name")).toString()},
                {QStringLiteral("project"), folder},
                {QStringLiteral("thumbnail"), file.value(QLatin1String("thumbnail_url")).toString()}};
        }
    };
    m_links->accounts()->token(account, [this, teams, listing, settle, addFiles](const QByteArray &token) {
        if (token.isEmpty()) {
            listing->errors << QObject::tr("the keychain has no Figma token: sign in again in Settings, Accounts");
            settle();
            return;
        }
        listing->walk = [this, token, listing, settle, addFiles](const QString &id, const QString &name, int depth) {
            if (++listing->folders > kMaxFolders)
                return;
            listing->pending += 2;
            get(token, QStringLiteral("/v2/folders/%1/files").arg(id),
                [settle, addFiles, name](int status, const QJsonObject &json, const QString &) {
                    if (status == 200)
                        addFiles(json, name);
                    settle();
                });
            get(token, QStringLiteral("/v2/folders/%1/folders").arg(id),
                [listing, settle, name, depth](int status, const QJsonObject &json, const QString &) {
                    if (status == 200 && depth < kFolderDepth && listing->walk) {
                        for (const QJsonValue &v : itemsOf(json, {"folders"})) {
                            const QJsonObject sub = v.toObject();
                            listing->walk(idOf(sub), name + QStringLiteral(" / ") + sub.value(QLatin1String("name")).toString(),
                                          depth + 1);
                        }
                    }
                    settle();
                });
        };
        // Published component sets and components (team_library_content:read)
        listing->library = [this, token, listing, settle](const QString &team, const QString &kind, const QString &after,
                                                          int page) {
            ++listing->pending;
            QString path = QStringLiteral("/v1/teams/%1/%2?page_size=%3").arg(team, kind, QString::number(kLibraryPage));
            if (!after.isEmpty())
                path += QStringLiteral("&after=") + after;
            get(token, path, [listing, settle, team, kind, after, page](int status, const QJsonObject &json, const QString &error) {
                if (status != 200) {
                    // Said once a team, and only if nothing else is found
                    if (kind == QLatin1String("component_sets") && page == 0)
                        listing->libraryErrors[team] = (status == 403
                            ? QObject::tr("team %1's components: %2 The token needs team_library_content:read.").arg(team, error)
                            : QObject::tr("team %1's components: %2").arg(team, error));
                    settle();
                    return;
                }
                const QJsonObject meta = json.value(QLatin1String("meta")).toObject();
                const QJsonArray items = meta.value(QLatin1String(kind == QLatin1String("components") ? "components"
                                                                                                         : "component_sets")).toArray();
                const bool sets = kind == QLatin1String("component_sets");
                for (const QJsonValue &v : items) {
                    const QJsonObject item = v.toObject();
                    const QJsonObject frame = item.value(QLatin1String("containing_frame")).toObject();
                    const QString file = item.value(QLatin1String("file_key")).toString();
                    // The frame it is in, once: a page of buttons, say
                    const QString frameNode = frame.value(QLatin1String("nodeId")).toString(frame.value(QLatin1String("node_id")).toString());
                    const QString frameName = frame.value(QLatin1String("name")).toString();
                    if (!frameNode.isEmpty() && !frameName.isEmpty() && !listing->frames.contains(file + QLatin1Char(' ') + frameNode)) {
                        listing->frames.insert(file + QLatin1Char(' ') + frameNode);
                        listing->components << QVariantMap{
                            {QStringLiteral("name"), frameName},
                            {QStringLiteral("file"), file},
                            {QStringLiteral("node"), frameNode},
                            {QStringLiteral("kind"), QStringLiteral("frame")},
                            {QStringLiteral("page"), frame.value(QLatin1String("pageName")).toString()}};
                    }
                    // A set's variants: the set stands for them
                    if (!sets && (!frame.value(QLatin1String("containingComponentSet")).toString().isEmpty()
                                  || !frame.value(QLatin1String("containingStateGroup")).toString().isEmpty()
                                  || frame.value(QLatin1String("containingStateGroup")).isObject()))
                        continue;
                    listing->components << QVariantMap{
                        {QStringLiteral("name"), item.value(QLatin1String("name")).toString()},
                        {QStringLiteral("description"), item.value(QLatin1String("description")).toString()},
                        {QStringLiteral("file"), file},
                        {QStringLiteral("node"), item.value(QLatin1String("node_id")).toString()},
                        {QStringLiteral("kind"), sets ? QStringLiteral("set") : QStringLiteral("component")},
                        {QStringLiteral("thumbnail"), item.value(QLatin1String("thumbnail_url")).toString()},
                        {QStringLiteral("page"), frame.value(QLatin1String("pageName")).toString()}};
                }
                // More after these, while Figma gives a cursor on (its pages
                // may be smaller than asked for)
                const QString next = meta.value(QLatin1String("cursor")).toObject().value(QLatin1String("after")).toVariant().toString();
                if (!items.isEmpty() && !next.isEmpty() && next != after && page + 1 < kLibraryPages && listing->library)
                    listing->library(team, kind, next, page + 1);
                settle();
            });
        };
        for (const QString &team : teams) {
            listing->library(team, QStringLiteral("component_sets"), {}, 0);
            listing->library(team, QStringLiteral("components"), {}, 0);
            ++listing->pending;
            // Folders (folders:read); else, for tokens made before them,
            // projects (projects:read)
            get(token, QStringLiteral("/v2/teams/%1/folders").arg(team),
                [this, token, team, listing, settle, addFiles](int status, const QJsonObject &json, const QString &error) {
                    if (status == 200) {
                        for (const QJsonValue &v : itemsOf(json, {"folders"})) {
                            const QJsonObject folder = v.toObject();
                            if (listing->walk)
                                listing->walk(idOf(folder), folder.value(QLatin1String("name")).toString(), 0);
                        }
                        settle();
                        return;
                    }
                    const QString folderError = error;
                    const int folderStatus = status;
                    get(token, QStringLiteral("/v1/teams/%1/projects").arg(team),
                        [this, token, team, listing, settle, addFiles, folderError, folderStatus](
                            int status, const QJsonObject &json, const QString &error) {
                            if (status != 200) {
                                // Said in the palette: a wrong id, or a token
                                // without the folders:read scope
                                // Refused: not a team (an organisation's id, say),
                                // or a token without folders:read
                                listing->errors << (folderStatus == 403 || status == 403
                                    ? QObject::tr("team %1: %2 Is it a team's id, the number after /team/ in its address "
                                                  "(not the organisation's, after /files/)? The token needs folders:read too.")
                                          .arg(team, folderStatus == 403 ? folderError : error)
                                    : QObject::tr("team %1: %2").arg(team, folderError));
                                settle();
                                return;
                            }
                            for (const QJsonValue &value : itemsOf(json, {"projects"})) {
                                const QJsonObject project = value.toObject();
                                const QString name = project.value(QLatin1String("name")).toString();
                                ++listing->pending;
                                get(token, QStringLiteral("/v1/projects/%1/files").arg(idOf(project)),
                                    [settle, addFiles, name](int status, const QJsonObject &json, const QString &) {
                                        if (status == 200)
                                            addFiles(json, name);
                                        settle();
                                    });
                            }
                            settle();
                        });
                });
        }
        settle();
    });
}

void FigmaResolver::search(const QString &query, const Results &done)
{
    const std::optional<Account> account = m_links->accounts()->find(id(), fixedHost());
    if (!account || account->teams.isEmpty()) {
        done({});
        return;
    }
    const QStringList words = query.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const auto filter = [this, words, done] {
        QVariantList results;
        for (const QVariant &v : std::as_const(m_files)) {
            const QVariantMap file = v.toMap();
            const QString name = file.value(QStringLiteral("name")).toString();
            const QString lower = name.toLower();
            if (!std::all_of(words.cbegin(), words.cend(), [&](const QString &w) { return lower.contains(w); }))
                continue;
            results << QVariantMap{
                {QStringLiteral("title"), name},
                {QStringLiteral("detail"), QStringLiteral("Figma · %1").arg(file.value(QStringLiteral("project")).toString())},
                {QStringLiteral("image"), file.value(QStringLiteral("thumbnail"))},
                {QStringLiteral("url"), QStringLiteral("https://www.figma.com/design/%1/%2")
                                            .arg(file.value(QStringLiteral("key")).toString(),
                                                 QString::fromUtf8(QUrl::toPercentEncoding(QString(name).replace(QLatin1Char(' '), QLatin1Char('-')))))}};
            if (results.size() >= 8)
                break;
        }
        // Components, component sets and their frames by name (or
        // description), best first: the name itself, then names starting
        // with it, then a word of the name; sets before components before
        // frames. Named with the file they are in.
        QHash<QString, QString> fileNames;
        for (const QVariant &v : std::as_const(m_files))
            fileNames.insert(v.toMap().value(QStringLiteral("key")).toString(), v.toMap().value(QStringLiteral("name")).toString());
        const QString phrase = words.join(QLatin1Char(' '));
        struct Hit {
            int rank;
            QVariantMap item;
        };
        QList<Hit> hits;
        // The first word at the start of a word of the name
        const QRegularExpression atWord(QStringLiteral("(^|[^\\p{L}\\p{N}])") + QRegularExpression::escape(words.value(0)));
        for (const QVariant &v : std::as_const(m_components)) {
            const QVariantMap c = v.toMap();
            const QString name = c.value(QStringLiteral("name")).toString().toLower();
            const QString text = name + QLatin1Char(' ') + c.value(QStringLiteral("description")).toString().toLower();
            if (!std::all_of(words.cbegin(), words.cend(), [&](const QString &w) { return text.contains(w); }))
                continue;
            const int match = name == phrase ? 0
                            : name.startsWith(phrase) ? 1
                            : atWord.match(name).hasMatch() ? 2
                            : name.contains(words.value(0)) ? 3 : 4;
            const QString kind = c.value(QStringLiteral("kind")).toString();
            const int order = kind == QLatin1String("set") ? 0 : kind == QLatin1String("component") ? 1 : 2;
            hits.append({match * 3 + order, c});
        }
        std::stable_sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) { return a.rank < b.rank; });
        for (const Hit &hit : hits.mid(0, kComponentResults)) {
            const QVariantMap &c = hit.item;
            const QString file = fileNames.value(c.value(QStringLiteral("file")).toString(), c.value(QStringLiteral("page")).toString());
            const QString kind = c.value(QStringLiteral("kind")).toString();
            results << QVariantMap{
                {QStringLiteral("title"), c.value(QStringLiteral("name")).toString()},
                {QStringLiteral("detail"), kind == QLatin1String("set") ? QObject::tr("Figma · %1 · component set").arg(file)
                                         : kind == QLatin1String("frame") ? QObject::tr("Figma · %1 · frame").arg(file)
                                                                          : QObject::tr("Figma · %1 · component").arg(file)},
                {QStringLiteral("image"), c.value(QStringLiteral("thumbnail"))},
                {QStringLiteral("url"), QStringLiteral("https://www.figma.com/design/%1/%2?node-id=%3")
                                            .arg(c.value(QStringLiteral("file")).toString(),
                                                 QString::fromUtf8(QUrl::toPercentEncoding(QString(file.isEmpty() ? QStringLiteral("Figma") : file)
                                                                                               .replace(QLatin1Char(' '), QLatin1Char('-')))),
                                                 c.value(QStringLiteral("node")).toString().replace(QLatin1Char(':'), QLatin1Char('-')))}};
        }
        // Nothing found, and a team couldn't be listed: why
        if (results.isEmpty()) {
            for (const QString &error : std::as_const(m_listErrors))
                results << QVariantMap{{QStringLiteral("title"), QObject::tr("Figma couldn't list %1").arg(error)},
                                       {QStringLiteral("error"), true}};
        }
        done(results);
    };
    const bool fresh = m_filesRead.isValid() && m_filesRead.secsTo(QDateTime::currentDateTimeUtc()) < Links::kFreshSeconds
                       && m_filesOf == account->teams;
    if (fresh)
        filter();
    else
        listFiles(*account, filter);
}
