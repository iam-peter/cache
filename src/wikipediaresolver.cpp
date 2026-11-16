#include "wikipediaresolver.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>

namespace {

constexpr qsizetype kExtract = 480;

} // namespace

WikipediaResolver::WikipediaResolver(Links *links)
    : m_links(links)
{
}

WikipediaResolver::Article WikipediaResolver::articleOf(const QUrl &url)
{
    static const QRegularExpression wiki(QStringLiteral(R"(^([a-z][a-z0-9-]*)(?:\.m)?\.wikipedia\.org$)"));
    const QRegularExpressionMatch m = wiki.match(url.host().toLower());
    const QString path = url.path(QUrl::FullyEncoded);
    if (!m.hasMatch() || !path.startsWith(QLatin1String("/wiki/")))
        return {};
    // The summary API wants the language's own host, not the mobile one
    return {m.captured(1) + QStringLiteral(".wikipedia.org"), path.mid(6)};
}

bool WikipediaResolver::handles(const QUrl &url) const
{
    return (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http")) && articleOf(url).valid();
}

void WikipediaResolver::resolve(const QUrl &url, const Done &done)
{
    const Article article = articleOf(url);
    if (!article.valid()) {
        done(std::nullopt, {});
        return;
    }
    const QString base = m_api.isEmpty() ? QStringLiteral("https://") + article.host : m_api;
    QNetworkRequest request(QUrl(base + QStringLiteral("/api/rest_v1/page/summary/") + article.title));
    request.setRawHeader("Accept", "application/json");
    // As Wikimedia asks: who is calling
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Cache (https://github.com/iam-peter/cache)"));
    request.setTransferTimeout(15000);
    QNetworkReply *reply = m_links->network()->get(request);
    const QString host = article.host;
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, host, done] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
        if (status != 200) {
            const QString detail = json.value(QLatin1String("detail")).toString();
            done(std::nullopt, QStringLiteral("Wikipedia: %1").arg(detail.isEmpty() ? reply->errorString() : detail));
            return;
        }
        done(infoOf(host, json), {});
    });
}

LinkInfo WikipediaResolver::infoOf(const QString &host, const QJsonObject &summary)
{
    LinkInfo info;
    info.title = summary.value(QLatin1String("title")).toString();
    QString extract = summary.value(QLatin1String("extract")).toString().simplified();
    if (extract.size() > kExtract)
        extract = extract.left(kExtract).section(QLatin1Char(' '), 0, -2) + QStringLiteral("…");
    info.summary = extract;
    info.image = summary.value(QLatin1String("thumbnail")).toObject().value(QLatin1String("source")).toString();
    info.site = QStringLiteral("Wikipedia · %1").arg(host.section(QLatin1Char('.'), 0, 0));
    const QString description = summary.value(QLatin1String("description")).toString();
    if (!description.isEmpty())
        info.fields.append({QObject::tr("About"), description});
    if (summary.value(QLatin1String("type")).toString() == QLatin1String("disambiguation"))
        info.fields.append({QObject::tr("Kind"), QObject::tr("Disambiguation page")});
    return info;
}
