#include "figmaresolver.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QUrlQuery>

#include <cmath>

namespace {

// The longest side a node is rendered at, in pixels: sharp in a card
constexpr double kPictureSide = 1600;

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
        const QJsonObject json = QJsonDocument::fromJson(r->readAll()).object();
        QString error = r->errorString();
        if (json.value(QLatin1String("err")).isString())
            error = json.value(QLatin1String("err")).toString();
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
    const std::optional<Account> account = m_links->accounts()->find(id(), fixedHost());
    const Ref ref = refOf(url);
    if (!account || !ref.valid()) {
        done(std::nullopt, {});
        return;
    }
    m_links->accounts()->token(*account, [this, ref, done](const QByteArray &token) {
        if (token.isEmpty()) {
            done(std::nullopt, QObject::tr("The keychain has no Figma token: sign in again in Settings, Accounts."));
            return;
        }
        const QString file = QString::fromUtf8(QUrl::toPercentEncoding(ref.file));
        const QString node = QString::fromUtf8(QUrl::toPercentEncoding(ref.node));
        // Just the file's top (its pages), or the node: never all of it
        const QString path = ref.node.isEmpty() ? QStringLiteral("/v1/files/%1?depth=1").arg(file)
                                                : QStringLiteral("/v1/files/%1/nodes?ids=%2&depth=1").arg(file, node);
        get(token, path, [this, ref, token, file, node, done](int status, const QJsonObject &json, const QString &error) {
            if (status != 200) {
                done(std::nullopt, QStringLiteral("Figma: %1").arg(error));
                return;
            }
            LinkInfo info;
            info.kind = QStringLiteral("design");
            const QString fileName = json.value(QLatin1String("name")).toString();
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
            // A page has no box: rendered at 1:1; else sharp, not huge
            const double longest = std::max(w, h);
            const double scale = longest > 0 ? std::clamp(kPictureSide / longest, 0.01, 2.0) : 1.0;
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
