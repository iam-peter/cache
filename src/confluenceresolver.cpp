#include "confluenceresolver.h"

#include <QJsonArray>
#include <QLocale>
#include <QRegularExpression>
#include <QTextDocumentFragment>
#include <QUrlQuery>

namespace {

// The Atlassian accounts Jira's sign-in makes
const QString kAccounts = QStringLiteral("jira");
constexpr qsizetype kExcerpt = 280;

const QRegularExpression kStart(QStringLiteral(R"(/(?:spaces|display|pages|x)/)"));
const QRegularExpression kPageId(
    QStringLiteral(R"(/(?:pages|blog/\d{4}/\d{2}/\d{2})/(?:edit-v2/|edit/)?(\d+)(?:/|$))"));
const QRegularExpression kTiny(QStringLiteral(R"(/x/([A-Za-z0-9_-]+)/?$)"));
const QRegularExpression kDisplay(QStringLiteral(R"(/display/([^/]+)/([^/]+)/?$)"));

QString nameOf(const QJsonValue &value, const char *field)
{
    return value.toObject().value(QLatin1String(field)).toString();
}

QString when(const QString &text)
{
    const QDateTime t = QDateTime::fromString(text, Qt::ISODateWithMs);
    return t.isValid() ? QLocale().toString(t.toLocalTime(), QLocale::ShortFormat) : QString();
}

// A search result's excerpt as plain text, shortened
QString plainExcerpt(const QString &excerpt)
{
    QString text = excerpt;
    text.remove(QStringLiteral("@@@hl@@@")).remove(QStringLiteral("@@@endhl@@@"));
    text = QTextDocumentFragment::fromHtml(text).toPlainText().simplified();
    if (text.size() > kExcerpt)
        text = text.left(kExcerpt).section(QLatin1Char(' '), 0, -2) + QStringLiteral("…");
    return text;
}

} // namespace

ConfluenceResolver::ConfluenceResolver(Links *links)
    : m_links(links)
{
}

ConfluenceResolver::Page ConfluenceResolver::pageOf(const QUrl &url)
{
    Page page;
    const QString path = url.path(QUrl::FullyDecoded);
    const QRegularExpressionMatch start = kStart.match(path);
    if (!start.hasMatch())
        return page;
    page.context = path.left(start.capturedStart());
    const QString pageId = QUrlQuery(url).queryItemValue(QStringLiteral("pageId"));
    if (path.endsWith(QLatin1String("viewpage.action")) && !pageId.isEmpty()) {
        static const QRegularExpression digits(QStringLiteral(R"(^\d+$)"));
        if (digits.match(pageId).hasMatch())
            page.id = pageId;
    } else if (const QRegularExpressionMatch m = kPageId.match(path); m.hasMatch()) {
        page.id = m.captured(1);
    } else if (const QRegularExpressionMatch m = kTiny.match(path); m.hasMatch()) {
        page.id = idOfTiny(m.captured(1));
    } else if (const QRegularExpressionMatch m = kDisplay.match(path); m.hasMatch()) {
        page.space = m.captured(1);
        page.title = QString(m.captured(2)).replace(QLatin1Char('+'), QLatin1Char(' '));
    }
    return page;
}

bool ConfluenceResolver::looksLike(const QUrl &url)
{
    // Not any site's /pages/123: only where Confluence's own paths show
    const QString path = url.path();
    return pageOf(url).valid()
           && (path.startsWith(QLatin1String("/wiki/")) || path.contains(QLatin1String("viewpage.action"))
               || path.contains(QLatin1String("/display/")));
}

QString ConfluenceResolver::idOfTiny(const QString &code)
{
    // The id's bytes, least significant first, in Base64 with - and _ for
    // / and +, and without the padding
    QString b64 = code;
    b64.replace(QLatin1Char('-'), QLatin1Char('/')).replace(QLatin1Char('_'), QLatin1Char('+'));
    while (b64.size() % 4)
        b64 += QLatin1Char('A');
    const QByteArray bytes = QByteArray::fromBase64(b64.toLatin1());
    if (bytes.isEmpty() || bytes.size() > 8)
        return {};
    quint64 id = 0;
    for (qsizetype i = bytes.size() - 1; i >= 0; --i)
        id = (id << 8) | quint8(bytes.at(i));
    return id > 0 ? QString::number(id) : QString();
}

QString ConfluenceResolver::tinyOf(const QString &id)
{
    quint64 n = id.toULongLong();
    QByteArray bytes;
    while (n > 0) {
        bytes.append(char(n & 0xff));
        n >>= 8;
    }
    QString code = QString::fromLatin1(bytes.toBase64(QByteArray::OmitTrailingEquals));
    code.replace(QLatin1Char('/'), QLatin1Char('-')).replace(QLatin1Char('+'), QLatin1Char('_'));
    while (code.endsWith(QLatin1Char('A')))
        code.chop(1);
    return code;
}

bool ConfluenceResolver::handles(const QUrl &url) const
{
    return Links::secure(url) && pageOf(url).valid() && m_links->accountFor(kAccounts, url).has_value();
}

void ConfluenceResolver::resolve(const QUrl &url, const Done &done)
{
    const auto found = m_links->accountFor(kAccounts, url);
    const Page page = pageOf(url);
    if (!found || !page.valid()) {
        done(std::nullopt, {});
        return;
    }
    const Account account = found->first;
    const QString site = found->second;
    // Atlassian Cloud has Confluence under /wiki, whatever the link's host
    const QString api = (Links::atlassianSite(site) ? QStringLiteral("/wiki") : page.context) + QStringLiteral("/rest/api");
    const QString expand = QStringLiteral("expand=space,version,history");
    const QString path = !page.id.isEmpty()
        ? QStringLiteral("%1/content/%2?%3").arg(api, page.id, expand)
        : QStringLiteral("%1/content?spaceKey=%2&title=%3&%4")
              .arg(api, QString::fromUtf8(QUrl::toPercentEncoding(page.space)),
                   QString::fromUtf8(QUrl::toPercentEncoding(page.title)), expand);
    Links *links = m_links;
    links->accounts()->token(account, [links, account, site, api, path, done](const QByteArray &token) {
        if (token.isEmpty()) {
            done(std::nullopt, QObject::tr("The keychain has no token for %1: sign in again in Settings, Accounts.")
                                   .arg(account.everySite() ? QObject::tr("every atlassian.net site") : account.host));
            return;
        }
        links->request(site, account, token, path,
            [links, account, site, api, token, done](int status, const QJsonObject &json, const QString &error) {
                // By id the page itself; by space and title the first found
                QJsonObject content = json;
                if (json.contains(QLatin1String("results")))
                    content = json.value(QLatin1String("results")).toArray().first().toObject();
                if (status != 200 || content.value(QLatin1String("id")).toString().isEmpty()) {
                    done(std::nullopt, QStringLiteral("%1: %2").arg(
                                           site, status == 200 ? QObject::tr("No such page") : error));
                    return;
                }
                // The start of its text, as search gives it: small, unlike
                // the page's whole body. Without it the page still shows.
                const QString id = content.value(QLatin1String("id")).toString();
                links->request(site, account, token,
                               QStringLiteral("%1/search?limit=1&cql=%2")
                                   .arg(api, QString::fromUtf8(QUrl::toPercentEncoding(QStringLiteral("id=") + id))),
                               [content, site, done](int status, const QJsonObject &found, const QString &) {
                                   const QString excerpt = status == 200
                                       ? found.value(QLatin1String("results")).toArray().first().toObject()
                                             .value(QLatin1String("excerpt")).toString()
                                       : QString();
                                   LinkInfo info = infoOf(content, plainExcerpt(excerpt));
                                   info.site = QStringLiteral("Confluence · %1").arg(site);
                                   done(info, {});
                               });
            });
    });
}

LinkInfo ConfluenceResolver::infoOf(const QJsonObject &content, const QString &excerpt)
{
    LinkInfo info;
    info.title = content.value(QLatin1String("title")).toString();
    info.summary = excerpt;
    const auto add = [&info](const QString &name, const QString &value) {
        if (!value.isEmpty())
            info.fields.append({name, value});
    };
    if (content.value(QLatin1String("type")).toString() == QLatin1String("blogpost"))
        add(QObject::tr("Type"), QObject::tr("Blog post"));
    add(QObject::tr("Space"), nameOf(content.value(QLatin1String("space")), "name"));
    const QJsonObject version = content.value(QLatin1String("version")).toObject();
    add(QObject::tr("Updated"), when(version.value(QLatin1String("when")).toString()));
    add(QObject::tr("Updated by"), nameOf(version.value(QLatin1String("by")), "displayName"));
    add(QObject::tr("Created by"),
        nameOf(content.value(QLatin1String("history")).toObject().value(QLatin1String("createdBy")), "displayName"));
    return info;
}

void ConfluenceResolver::search(const QString &query, const Results &done)
{
    struct Search {
        QVariantList results;
        int pending = 1; // until all are asked
        Results done;
    };
    auto search = std::make_shared<Search>();
    search->done = done;
    const auto settle = [search] {
        if (--search->pending == 0)
            search->done(search->results);
    };
    QString text = query;
    text.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));
    const QString cql = QStringLiteral("text ~ \"%1\" and type in (page, blogpost) order by lastmodified desc").arg(text);
    for (const QString &site : m_links->atlassianSites()) {
        const auto found = m_links->accountFor(kAccounts, QUrl(QStringLiteral("https://") + site + QLatin1Char('/')));
        if (!found)
            continue;
        const Account account = found->first;
        const QString host = found->second;
        const QString path = (Links::atlassianSite(host) ? QStringLiteral("/wiki") : QString())
                             + QStringLiteral("/rest/api/search?limit=8&cql=")
                             + QString::fromUtf8(QUrl::toPercentEncoding(cql));
        ++search->pending;
        Links *links = m_links;
        links->accounts()->token(account, [links, account, host, path, search, settle](const QByteArray &token) {
            if (token.isEmpty()) {
                settle();
                return;
            }
            links->request(host, account, token, path, [host, search, settle](int status, const QJsonObject &json, const QString &) {
                if (status == 200) {
                    // Addresses are relative to the base the answer gives
                    const QString base = json.value(QLatin1String("_links")).toObject().value(QLatin1String("base")).toString();
                    for (const QJsonValue &value : json.value(QLatin1String("results")).toArray()) {
                        const QJsonObject r = value.toObject();
                        const QString title = plainExcerpt(r.value(QLatin1String("title")).toString());
                        const QString url = r.value(QLatin1String("url")).toString();
                        if (title.isEmpty() || url.isEmpty())
                            continue;
                        const QString space = r.value(QLatin1String("resultGlobalContainer")).toObject()
                                                  .value(QLatin1String("title")).toString();
                        search->results << QVariantMap{
                            {QStringLiteral("title"), title},
                            {QStringLiteral("detail"), space.isEmpty() ? host : QStringLiteral("%1 · %2").arg(space, host)},
                            {QStringLiteral("summary"), plainExcerpt(r.value(QLatin1String("excerpt")).toString())},
                            {QStringLiteral("url"), url.startsWith(QLatin1String("http")) ? url : base + url}};
                    }
                }
                settle();
            });
        });
    }
    settle();
}
