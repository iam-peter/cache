#include "pageresolver.h"

#include "confluenceresolver.h"
#include "markdown.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTextDocumentFragment>

namespace {

constexpr qint64 kPageLimit = 1024 * 1024;

QString plain(const QString &html)
{
    return QTextDocumentFragment::fromHtml(html).toPlainText().simplified();
}

// Every <meta>'s content by its property or name, lower case
QHash<QString, QString> metaOf(const QString &html)
{
    static const QRegularExpression tag(QStringLiteral(R"(<meta\b[^>]*>)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression attribute(QStringLiteral(R"re(([\w:-]+)\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+)))re"));
    QHash<QString, QString> meta;
    for (auto t = tag.globalMatch(html); t.hasNext();) {
        QString key, content;
        for (auto a = attribute.globalMatch(t.next().captured()); a.hasNext();) {
            const QRegularExpressionMatch m = a.next();
            const QString name = m.captured(1).toLower();
            const QString value = m.captured(2) + m.captured(3) + m.captured(4);
            if (name == QLatin1String("property") || name == QLatin1String("name"))
                key = value.toLower();
            else if (name == QLatin1String("content"))
                content = value;
        }
        if (!key.isEmpty() && !content.isEmpty() && !meta.contains(key))
            meta.insert(key, content);
    }
    return meta;
}

QString siteOf(const QUrl &url)
{
    QString host = url.host();
    if (host.startsWith(QLatin1String("www.")))
        host = host.mid(4);
    return host;
}

} // namespace

PageResolver::PageResolver(Links *links)
    : m_links(links)
{
}

bool PageResolver::handles(const QUrl &url) const
{
    return url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http");
}

void PageResolver::resolve(const QUrl &url, const Done &done)
{
    const QString key = Markdown::issueKey(url.toString());
    if (!key.isEmpty() || ConfluenceResolver::looksLike(url)) {
        const ConfluenceResolver::Page page = ConfluenceResolver::pageOf(url);
        LinkInfo info;
        info.title = !key.isEmpty() ? key : !page.title.isEmpty() ? page.title : siteOf(url);
        info.site = siteOf(url);
        info.summary = !key.isEmpty()
            ? QObject::tr("Add an account for %1 in Settings to see this issue.").arg(Links::authority(url))
            : QObject::tr("Add an account for %1 in Settings to see this page.").arg(Links::authority(url));
        done(info, {});
        return;
    }
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Cache"));
    request.setRawHeader("Accept", "text/html");
    request.setTransferTimeout(15000);
    QNetworkReply *reply = m_links->network()->get(request);
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) {
        if (received > kPageLimit)
            reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, url, done] {
        reply->deleteLater();
        const QString type = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        if (reply->error() != QNetworkReply::NoError) {
            done(std::nullopt, reply->errorString());
            return;
        }
        if (!type.isEmpty() && !type.contains(QLatin1String("html"))) {
            // A file, not a page: its name will do
            LinkInfo info;
            info.title = url.fileName().isEmpty() ? siteOf(url) : url.fileName();
            info.site = siteOf(url);
            done(info, {});
            return;
        }
        done(infoOf(reply->url(), QString::fromUtf8(reply->readAll().left(kPageLimit))), {});
    });
}

LinkInfo PageResolver::infoOf(const QUrl &url, const QString &html)
{
    const QHash<QString, QString> meta = metaOf(html);
    const auto first = [&meta](std::initializer_list<const char *> keys) {
        for (const char *k : keys) {
            const QString v = meta.value(QLatin1String(k));
            if (!v.trimmed().isEmpty())
                return v;
        }
        return QString();
    };
    LinkInfo info;
    info.title = plain(first({"og:title", "twitter:title"}));
    if (info.title.isEmpty()) {
        static const QRegularExpression title(QStringLiteral(R"(<title[^>]*>(.*?)</title>)"),
                                              QRegularExpression::CaseInsensitiveOption
                                                  | QRegularExpression::DotMatchesEverythingOption);
        info.title = plain(title.match(html).captured(1));
    }
    if (info.title.isEmpty())
        info.title = siteOf(url);
    info.summary = plain(first({"og:description", "twitter:description", "description"}));
    const QString image = first({"og:image", "og:image:url", "twitter:image"}).trimmed();
    if (!image.isEmpty()) {
        const QUrl resolved = url.resolved(QUrl(plain(image)));
        if (resolved.scheme() == QLatin1String("https") || resolved.scheme() == QLatin1String("http"))
            info.image = resolved.toString();
    }
    const QString site = plain(first({"og:site_name"}));
    info.site = site.isEmpty() ? siteOf(url) : site;
    return info;
}
