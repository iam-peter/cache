// BoardModel, continued: what other applications drop or paste. Whatever
// arrives becomes Markdown, the same way for the canvas and for a note
// being edited, for pasting and for dropping.

#include "boardmodel.h"

#include "markdown.h"

#include <QBuffer>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextFragment>
#include <QTextImageFormat>

#include <algorithm>
#include <cmath>
#include <memory>

namespace {

const QStringList kImageExt = {"png", "jpg", "jpeg", "gif", "webp", "bmp", "svg", "tif", "tiff"};
const QStringList kVideoExt = {"mp4", "webm", "mkv", "mov", "avi", "m4v", "ogv"};
const QStringList kTextExt = {"md", "markdown", "txt", "text"};
const QString kAppItems = QStringLiteral("application/x-cache-items");
// Put on the clipboard with a note's Markdown: pasting it back takes that
const QString kAppMarkdown = QStringLiteral("application/x-cache-markdown");

// Text from another application: UTF-16 when it says so (as Firefox's
// often is), else UTF-8
QString decodeText(const QByteArray &data)
{
    if (data.startsWith("\xff\xfe") || data.startsWith("\xfe\xff")
        || (data.size() >= 4 && data.at(1) == '\0' && data.at(3) == '\0')) {
        auto decode = QStringDecoder(QStringDecoder::Utf16);
        return QString(decode(data)).remove(QChar(0xfeff));
    }
    return QString::fromUtf8(data);
}

// HTML that carries more than an image: text worth keeping with its formatting
bool richText(const QString &html)
{
    static const QRegularExpression tags(QStringLiteral("<[^>]*>"));
    static const QRegularExpression formatting(
        QStringLiteral(R"(<(b|strong|i|em|u|s|a\s|ul|ol|li|table|h[1-6]|pre|code|blockquote|br|p|div)\b)"),
        QRegularExpression::CaseInsensitiveOption);
    QString text = html;
    text.remove(tags);
    return !text.trimmed().isEmpty() && formatting.match(html).hasMatch();
}

// A file name for something from the web
QString nameFromUrl(const QUrl &url, const QString &fallback)
{
    QString name = QFileInfo(url.path()).completeBaseName();
    name.replace(QRegularExpression(QStringLiteral(R"([^\w.-]+)")), QStringLiteral("-"));
    return name.isEmpty() ? fallback : name.left(60);
}

} // namespace

// ---------------------------------------------------------------------------
// Entry points

QStringList BoardModel::pasteFromClipboard(double cx, double cy, int targetRow)
{
    return insertMime(QGuiApplication::clipboard()->mimeData(), cx, cy, targetRow, false);
}

static QMimeData *mimeFromFormats(const QVariantMap &formats)
{
    auto *mime = new QMimeData;
    for (auto it = formats.cbegin(); it != formats.cend(); ++it) {
        const QVariant &v = it.value();
        mime->setData(it.key(), v.typeId() == QMetaType::QByteArray ? v.toByteArray() : v.toString().toUtf8());
    }
    return mime;
}

QStringList BoardModel::dropData(const QVariantMap &formats, double cx, double cy, int targetRow)
{
    std::unique_ptr<QMimeData> mime(mimeFromFormats(formats));
    // Dropped onto a note, everything goes into it
    return insertMime(mime.get(), cx, cy, targetRow, true);
}

QString BoardModel::clipboardMarkdown()
{
    bool media = false;
    return markdownFromMime(QGuiApplication::clipboard()->mimeData(), true, &media);
}

QString BoardModel::dropMarkdown(const QVariantMap &formats)
{
    std::unique_ptr<QMimeData> mime(mimeFromFormats(formats));
    bool media = false;
    QString md = markdownFromMime(mime.get(), true, &media);
    // A drop of plain text into an editor: the text itself
    if (md.isEmpty() && mime->hasText())
        md = mime->text();
    return md;
}

QString BoardModel::filesMarkdown(const QVariantList &urls)
{
    QStringList parts;
    for (const QVariant &v : urls) {
        const QString abs = toLocalPath(v.typeId() == QMetaType::QUrl ? v.toUrl().toString() : v.toString());
        if (QFileInfo(abs).isFile())
            parts << markdownForFile(abs, true);
    }
    parts.removeAll(QString());
    return parts.join(QStringLiteral("\n\n"));
}

int BoardModel::importUrls(const QVariantList &urls, double cx, double cy, int targetRow)
{
    QList<QUrl> list;
    for (const QVariant &v : urls)
        list << (v.typeId() == QMetaType::QUrl ? v.toUrl() : QUrl(v.toString()));
    QMimeData mime;
    mime.setUrls(list);
    const QStringList ids = insertMime(&mime, cx, cy, targetRow, true);
    return ids.isEmpty() ? -1 : indexOf(ids.last());
}

// ---------------------------------------------------------------------------
// From MIME data to the canvas

// What the canvas does with it, in this order:
//   items copied from a canvas      copies of them
//   files                           notes from text and Markdown files, tables
//                                   from CSV, the rest into one note (images
//                                   and videos shown, other files attached)
//   anything else                   Markdown (see markdownFromMime) in a new
//                                   note, or appended to targetRow's note when
//                                   it is media or appendText is set (a drop)
QStringList BoardModel::insertMime(const QMimeData *mime, double cx, double cy, int targetRow, bool appendText)
{
    if (!mime || m_projectPath.isEmpty())
        return {};
    const bool targetIsNote = valid(targetRow) && m_items.at(targetRow).type == QLatin1String("note");

    // Items copied from a canvas, centred on (cx, cy)
    if (mime->hasFormat(kAppItems)) {
        const QJsonObject root = QJsonDocument::fromJson(mime->data(kAppItems)).object();
        BoardState state;
        for (const QJsonValue &v : root.value(QStringLiteral("items")).toArray())
            state.items.append(itemFromJson(v.toObject()));
        for (const QJsonValue &v : root.value(QStringLiteral("links")).toArray())
            state.links.append(linkFromJson(v.toObject()));
        if (!state.items.isEmpty()) {
            QRectF bounds;
            for (const BoardItem &it : std::as_const(state.items))
                bounds = bounds.united(QRectF(it.x, it.y, it.w, it.h));
            pushUndo();
            return insertState(state, cx - bounds.center().x(), cy - bounds.center().y());
        }
    }

    // Local files
    QStringList files;
    for (const QUrl &url : mime->urls()) {
        if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile())
            files << url.toLocalFile();
    }
    if (!files.isEmpty()) {
        pushUndo();
        QStringList ids;
        QStringList parts;
        double offset = 0;
        for (const QString &abs : std::as_const(files)) {
            const QString suffix = QFileInfo(abs).suffix().toLower();
            if (kTextExt.contains(suffix) || suffix == QLatin1String("csv") || suffix == QLatin1String("tsv")) {
                // Notes of their own
                const int row = placeMarkdownNote(markdownForFile(abs, false), cx + offset, cy + offset);
                ids << m_items.at(row).id;
                offset += 36;
            } else {
                parts << markdownForFile(abs, false);
            }
        }
        parts.removeAll(QString());
        if (!parts.isEmpty()) {
            const QString md = parts.join(QStringLiteral("\n\n"));
            if (targetIsNote) {
                appendToNote(targetRow, md);
                ids << m_items.at(targetRow).id;
            } else {
                ids << m_items.at(placeMarkdownNote(md, cx + offset, cy + offset)).id;
            }
        }
        return ids;
    }

    bool media = false;
    QString md = markdownFromMime(mime, false, &media);
    if (md.isEmpty())
        return {};
    pushUndo();
    if (targetIsNote && (media || appendText)) {
        appendToNote(targetRow, md);
        return {m_items.at(targetRow).id};
    }
    return {m_items.at(placeMarkdownNote(md, cx, cy)).id};
}

// The best of what the MIME data holds, as Markdown:
//   cells of a spreadsheet          a table
//   rich text (Teams, Outlook,      Markdown with its formatting; images in it
//   Word, web pages)                saved to assets/ or fetched from the web
//   an image                        saved to assets/ and shown
//   web URLs                        an image fetched from there, or a link
//                                   titled like the page
//   plain text                      itself (empty for an editor, which
//                                   pastes plain text itself)
// Files are the caller's (forEditor: here too). `media` says whether the
// result is media, which a note takes even when pasted with it selected.
QString BoardModel::markdownFromMime(const QMimeData *mime, bool forEditor, bool *media)
{
    *media = false;
    if (!mime || m_projectPath.isEmpty())
        return {};

    // Our own note: its Markdown as it is
    if (mime->hasFormat(kAppMarkdown) && mime->hasText())
        return forEditor ? QString() : mime->text();
    if (forEditor && mime->hasFormat(kAppItems) && mime->hasText())
        return {};

    if (forEditor) {
        QStringList parts;
        for (const QUrl &url : mime->urls()) {
            if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile())
                parts << markdownForFile(url.toLocalFile(), true);
        }
        parts.removeAll(QString());
        if (!parts.isEmpty()) {
            *media = true;
            return parts.join(QStringLiteral("\n\n"));
        }
    }

    const QString text = mime->hasText() ? mime->text() : QString();
    const QString table = Markdown::tableFromCells(text);
    if (!table.isEmpty())
        return table;

    if (mime->hasHtml()) {
        const QString html = mime->html().isEmpty() ? decodeText(mime->data(QStringLiteral("text/html"))) : mime->html();
        if (richText(html)) {
            const QString md = markdownFromHtml(html);
            if (!md.trimmed().isEmpty())
                return md;
        }
    }

    QImage image;
    if (mime->hasImage())
        image = qvariant_cast<QImage>(mime->imageData());
    for (const char *format : {"image/png", "image/jpeg", "image/webp", "image/gif", "image/bmp"}) {
        if (image.isNull() && mime->hasFormat(QLatin1String(format)))
            image.loadFromData(mime->data(QLatin1String(format)));
    }
    if (!image.isNull()) {
        const QString rel = saveImageAsset(image, QStringLiteral("paste"));
        if (!rel.isEmpty()) {
            *media = true;
            return mediaMarkdown(rel);
        }
    }

    // Web URLs: dragged links and images
    QList<QUrl> web;
    for (const QUrl &url : mime->urls()) {
        if (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https"))
            web << url;
    }
    if (web.isEmpty() && Markdown::isWebUrl(text))
        web << QUrl(text.trimmed());
    if (!web.isEmpty()) {
        QStringList parts;
        for (const QUrl &url : std::as_const(web)) {
            bool isImage = false;
            parts << webMarkdown(url, mime, &isImage);
            *media = *media || isImage;
        }
        return parts.join(web.size() > 1 ? QStringLiteral("\n") : QString());
    }

    if (forEditor || text.trimmed().isEmpty())
        return {};
    return text;
}

// A dropped or picked file in a note: images and videos shown, text and
// Markdown files as their text (forEditor) or a note's, CSV as a table,
// anything else attached as a link to its copy in assets/
QString BoardModel::markdownForFile(const QString &abs, bool forEditor)
{
    Q_UNUSED(forEditor)
    const QFileInfo fi(abs);
    const QString suffix = fi.suffix().toLower();
    if (kImageExt.contains(suffix) || kVideoExt.contains(suffix)) {
        const QString rel = importAsset(abs);
        return rel.isEmpty() ? QString() : mediaMarkdown(rel);
    }
    if (kTextExt.contains(suffix) || suffix == QLatin1String("csv") || suffix == QLatin1String("tsv")) {
        QFile f(abs);
        if (f.open(QIODevice::ReadOnly) && f.size() < 2 * 1024 * 1024) {
            const QString text = QString::fromUtf8(f.readAll());
            if (suffix == QLatin1String("csv") || suffix == QLatin1String("tsv")) {
                const QString table = Markdown::tableFromCells(
                    text, suffix == QLatin1String("csv") ? QLatin1Char(',') : QLatin1Char('\t'));
                if (!table.isEmpty())
                    return QStringLiteral("## ") + fi.completeBaseName() + QStringLiteral("\n\n") + table;
            } else {
                return text;
            }
        }
    }
    // Attached: its own line, so the note shows it as a file
    const QString rel = importAsset(abs);
    if (rel.isEmpty())
        return {};
    return Markdown::link(fi.fileName(), QString::fromUtf8(QUrl::toPercentEncoding(rel, "/")));
}

// Rich text as Markdown. Its images come along: embedded ones (data: URIs,
// local files) are saved to assets/, those on the web are fetched in the
// background.
QString BoardModel::markdownFromHtml(const QString &html)
{
    QTextDocument doc;
    doc.setHtml(html);

    struct Image {
        int position;
        int length;
        QTextImageFormat format;
    };
    QList<Image> images;
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || !fragment.charFormat().isImageFormat())
                continue;
            QTextImageFormat image = fragment.charFormat().toImageFormat();
            const QString src = image.name();
            QString rel;
            if (src.startsWith(QLatin1String("data:image/"))) {
                const qsizetype comma = src.indexOf(QLatin1Char(','));
                QImage decoded;
                if (comma > 0 && decoded.loadFromData(QByteArray::fromBase64(src.mid(comma + 1).toLatin1())))
                    rel = saveImageAsset(decoded, QStringLiteral("paste"));
            } else if (src.startsWith(QLatin1String("file:"))) {
                const QString local = QUrl(src).toLocalFile();
                if (QFileInfo(local).isFile())
                    rel = importAsset(local);
            }
            if (!rel.isEmpty()) {
                image.setName(QString::fromUtf8(QUrl::toPercentEncoding(rel, "/")));
                images.append({fragment.position(), fragment.length(), image});
            }
        }
    }
    for (const Image &image : std::as_const(images)) {
        QTextCursor cursor(&doc);
        cursor.setPosition(image.position);
        cursor.setPosition(image.position + image.length, QTextCursor::KeepAnchor);
        cursor.setCharFormat(image.format);
    }

    QString md = doc.toMarkdown(QTextDocument::MarkdownDialectGitHub);
    // Tidy what the writer leaves: trailing spaces, runs of blank lines
    QStringList lines = md.split(QLatin1Char('\n'));
    for (QString &line : lines) {
        while (line.endsWith(QLatin1Char(' ')) && !line.endsWith(QLatin1String("  ")))
            line.chop(1);
    }
    md = lines.join(QLatin1Char('\n'));
    md.replace(QRegularExpression(QStringLiteral("\n{3,}")), QStringLiteral("\n\n"));
    md = md.trimmed();

    // Images on the web: fetched, the reference replaced when they are here
    static const QRegularExpression remote(QStringLiteral(R"(!\[([^\]]*)\]\((https?://[^)\s]+)\))"));
    for (auto it = remote.globalMatch(md); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        fetchImage(m.captured(), QUrl(m.captured(2)), m.captured(1));
    }
    return md;
}

// A web URL: an image (by its name, or as the drag says) is fetched into
// assets/; anything else is a link, named as the drag names it or as the
// page's title once that has been fetched
QString BoardModel::webMarkdown(const QUrl &url, const QMimeData *mime, bool *media)
{
    const QString address = url.toString();
    const QString suffix = QFileInfo(url.path()).suffix().toLower();
    const QString html = mime && mime->hasHtml() ? mime->html() : QString();
    const bool imageInHtml = html.contains(QStringLiteral("<img"), Qt::CaseInsensitive)
                             && html.contains(address.toHtmlEscaped());
    if (kImageExt.contains(suffix) || imageInHtml) {
        *media = true;
        const QString name = QFileInfo(url.path()).fileName().isEmpty() ? QStringLiteral("image")
                                                                        : QFileInfo(url.path()).fileName();
        const QString before = QStringLiteral("![%1](%2)").arg(name, address);
        fetchImage(before, url, name);
        return before;
    }

    // A name for it from the drag: Firefox's second line, a link's text
    QString title;
    if (mime && mime->hasFormat(QStringLiteral("text/x-moz-url"))) {
        const QStringList lines = decodeText(mime->data(QStringLiteral("text/x-moz-url"))).split(QLatin1Char('\n'));
        if (lines.size() > 1)
            title = lines.at(1).trimmed();
    }
    if (title.isEmpty() && !html.isEmpty()) {
        const QRegularExpression anchor(QStringLiteral(R"(<a\b[^>]*href\s*=\s*["']?%1["']?[^>]*>(.*?)</a>)")
                                            .arg(QRegularExpression::escape(address.toHtmlEscaped())),
                                        QRegularExpression::CaseInsensitiveOption
                                            | QRegularExpression::DotMatchesEverythingOption);
        const QRegularExpressionMatch m = anchor.match(html);
        if (m.hasMatch())
            title = QTextDocumentFragment::fromHtml(m.captured(1)).toPlainText().trimmed();
    }
    if (!title.isEmpty() && title != address)
        return Markdown::link(title, address);
    const QString before = Markdown::link(address, address);
    fetchTitle(before, url);
    return before;
}

// ---------------------------------------------------------------------------
// Notes for imported Markdown

double BoardModel::estimateHeight(const QString &markdown, double width) const
{
    static const QRegularExpression mediaLine(QStringLiteral(R"(^\s*!\[[^\]]*\]\(([^)\s]+)\)\s*$)"));
    double h = 48;
    const int perLine = std::max(20, int((width - 32) / 7.5));
    for (const QString &line : markdown.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch m = mediaLine.match(line);
        const QString local = m.hasMatch() ? resolveLocal(m.captured(1)) : QString();
        if (!local.isEmpty() && QFileInfo(local).isFile())
            h += mediaHeight(QDir(m_projectPath).relativeFilePath(local), width - 32);
        else
            h += 21.0 * (1 + int(line.size()) / perLine);
    }
    return h;
}

int BoardModel::placeMarkdownNote(const QString &markdown, double cx, double cy)
{
    BoardItem it = makeNote(0, 0, markdown);
    // Wide enough for a table's rows, or for the first image
    double width = 340;
    for (const QString &line : markdown.split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1Char('|')))
            width = std::max(width, std::min(760.0, line.size() * 7.5 + 40));
    }
    static const QRegularExpression firstMedia(QStringLiteral(R"(^\s*!\[[^\]]*\]\(([^)\s]+)\)\s*$)"),
                                               QRegularExpression::MultilineOption);
    const QRegularExpressionMatch m = firstMedia.match(markdown);
    if (m.hasMatch()) {
        const QString local = resolveLocal(m.captured(1));
        if (QFileInfo(local).isFile())
            width = std::clamp(displaySize(local, mediaType(local)).width() + 32, width, 560.0);
    }
    it.w = width;
    it.h = std::clamp(estimateHeight(markdown, width), 120.0, 900.0);
    it.x = cx - it.w / 2;
    it.y = cy - std::min(it.h, 200.0) / 2;
    return appendItem(it);
}

void BoardModel::appendToNote(int row, const QString &markdown)
{
    BoardItem &it = m_items[row];
    QString text = it.text;
    while (text.endsWith(QLatin1Char('\n')))
        text.chop(1);
    it.text = (text.isEmpty() ? QString() : text + QStringLiteral("\n\n")) + markdown + QLatin1Char('\n');
    it.h = std::min(it.h + estimateHeight(markdown, it.w) - 40, std::max(it.h, 1200.0));
    emitRow(row);
    updateDerived();
    textsChanged();
    markDirty();
}

// ---------------------------------------------------------------------------
// From the web, in the background

void BoardModel::fetch(const QUrl &url, qint64 limit, const std::function<void(const QByteArray &, const QString &)> &done)
{
    if (!m_network)
        m_network = new QNetworkAccessManager(this);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Cache/0.2"));
    request.setTransferTimeout(15000);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply, limit](qint64 received, qint64) {
        if (received > limit)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        const bool ok = reply->error() == QNetworkReply::NoError;
        done(ok ? reply->readAll() : QByteArray(), reply->header(QNetworkRequest::ContentTypeHeader).toString());
    });
}

void BoardModel::fetchImage(const QString &before, const QUrl &url, const QString &name)
{
    const QString project = m_projectPath;
    fetch(url, 30 * 1024 * 1024, [this, before, url, name, project](const QByteArray &data, const QString &) {
        if (project != m_projectPath)
            return; // another project by now
        QBuffer buffer;
        buffer.setData(data);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        const QString format = QString::fromLatin1(reader.format());
        QString after;
        if (!data.isEmpty() && !format.isEmpty() && reader.canRead()) {
            const QString rel = saveBytesAsset(data, nameFromUrl(url, QStringLiteral("image")),
                                               format == QLatin1String("jpeg") ? QStringLiteral("jpg") : format);
            if (!rel.isEmpty())
                after = mediaMarkdown(rel);
        }
        // Not to be had (signed in only, gone): a link to it instead
        if (after.isEmpty())
            after = Markdown::link(name.isEmpty() ? url.toString() : name, url.toString());
        replaceInNotes(before, after);
    });
}

void BoardModel::fetchTitle(const QString &before, const QUrl &url)
{
    fetch(url, 512 * 1024, [this, before, url](const QByteArray &data, const QString &type) {
        if (data.isEmpty() || (!type.isEmpty() && !type.contains(QLatin1String("html"))))
            return; // the link stays as it is
        const QString page = QString::fromUtf8(data.left(512 * 1024));
        static const QRegularExpression og(
            QStringLiteral(R"(<meta[^>]+property\s*=\s*["']og:title["'][^>]+content\s*=\s*["']([^"']+)["'])"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression titleTag(QStringLiteral(R"(<title[^>]*>(.*?)</title>)"),
                                                 QRegularExpression::CaseInsensitiveOption
                                                     | QRegularExpression::DotMatchesEverythingOption);
        QRegularExpressionMatch m = og.match(page);
        if (!m.hasMatch())
            m = titleTag.match(page);
        if (!m.hasMatch())
            return;
        const QString title = QTextDocumentFragment::fromHtml(m.captured(1)).toPlainText().simplified();
        if (!title.isEmpty())
            replaceInNotes(before, Markdown::link(title, url.toString()));
    });
}

void BoardModel::replaceInNotes(const QString &before, const QString &after)
{
    bool changed = false;
    for (int row = 0; row < m_items.size(); ++row) {
        if (m_items.at(row).type != QLatin1String("note") || !m_items.at(row).text.contains(before))
            continue;
        m_items[row].text.replace(before, after);
        emitRow(row);
        changed = true;
    }
    if (changed) {
        textsChanged();
        markDirty();
    }
    // A note being edited holds its own copy of the text
    emit textReplaced(before, after);
}

QString BoardModel::saveBytesAsset(const QByteArray &data, const QString &base, const QString &extension)
{
    if (m_projectPath.isEmpty())
        return {};
    QDir(m_projectPath).mkpath(QStringLiteral("assets"));
    const QString rel = uniqueAssetPath(base, extension);
    QFile f(absPath(rel));
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size())
        return {};
    return rel;
}
