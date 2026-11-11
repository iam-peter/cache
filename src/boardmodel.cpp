#include "boardmodel.h"

#include <QBuffer>
#include <QClipboard>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QSaveFile>
#include <QSet>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QUuid>

#include <algorithm>
#include <cmath>

namespace {

const QStringList kImageExt = {"png", "jpg", "jpeg", "gif", "webp", "bmp", "svg", "tif", "tiff"};
const QStringList kVideoExt = {"mp4", "webm", "mkv", "mov", "avi", "m4v", "ogv"};
const QStringList kTextExt = {"md", "markdown", "txt", "text"};
constexpr int kUndoLimit = 200;
// A line holding only ![name](target): embedded media
const QRegularExpression kMediaLine(QStringLiteral(R"(^\s*!\[([^\]]*)\]\(([^)\s]+)\)\s*$)"));
// ![name](video) anywhere in the text
const QRegularExpression kVideoImage(QStringLiteral(R"(!\[([^\]]*)\]\(([^)\s]+\.(?:mp4|webm|mkv|mov|avi|m4v|ogv))\))"),
                                     QRegularExpression::CaseInsensitiveOption);

constexpr double kMaxMediaSide = 720.0;

QString newId()
{
    return QUuid::createUuid().toString(QUuid::Id128).left(12);
}

QString timestamp()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss"));
}

bool differs(double a, double b)
{
    return std::abs(a - b) > 0.01;
}

QByteArray fileHash(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(&file);
    return hash.result();
}

bool sameContent(const QString &a, const QString &b)
{
    const QFileInfo fa(a), fb(b);
    return fa.isFile() && fb.isFile() && fa.size() == fb.size() && fileHash(a) == fileHash(b);
}

} // namespace

BoardModel::BoardModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(700);
    connect(&m_saveTimer, &QTimer::timeout, this, &BoardModel::save);
    connect(qApp, &QCoreApplication::aboutToQuit, this, &BoardModel::save);
}

BoardModel::~BoardModel()
{
    save();
}

int BoardModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant BoardModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || !valid(index.row()))
        return {};
    const BoardItem &it = m_items.at(index.row());
    switch (role) {
    case IdRole: return it.id;
    case TypeRole: return it.type;
    case PosXRole: return it.x;
    case PosYRole: return it.y;
    case WidthRole: return it.w;
    case HeightRole: return it.h;
    case ZRole: return it.z;
    case ColorRole: return it.color;
    case TextRole: return it.text;
    case SourceRole:
        return it.source.isEmpty() ? QUrl() : QUrl::fromLocalFile(absPath(it.source));
    case TitleRole: return titleFor(it);
    case SearchRole: return titleFor(it) + QLatin1Char('\n') + it.text;
    default: return {};
    }
}

QHash<int, QByteArray> BoardModel::roleNames() const
{
    return {
        {IdRole, "itemId"},
        {TypeRole, "itemType"},
        {PosXRole, "posX"},
        {PosYRole, "posY"},
        {WidthRole, "itemWidth"},
        {HeightRole, "itemHeight"},
        {ZRole, "itemZ"},
        {ColorRole, "itemColor"},
        {TextRole, "itemText"},
        {SourceRole, "itemSource"},
        {TitleRole, "title"},
        {SearchRole, "searchText"},
    };
}

QUrl BoardModel::projectUrl() const
{
    return m_projectPath.isEmpty() ? QUrl() : QUrl::fromLocalFile(m_projectPath);
}

QString BoardModel::assetsDir() const
{
    if (m_projectPath.isEmpty())
        return {};
    QDir dir(m_projectPath);
    dir.mkpath(QStringLiteral("assets"));
    return dir.filePath(QStringLiteral("assets"));
}

// ---------------------------------------------------------------------------
// Loading and saving

void BoardModel::load(const QString &projectDir)
{
    save();

    beginResetModel();
    m_items.clear();
    m_undo.clear();
    m_redo.clear();
    m_lastUndoKey.clear();
    m_savedTexts.clear();
    m_dirty = false;
    m_hasView = false;
    m_viewX = m_viewY = 0;
    m_viewZoom = 1;
    m_projectPath = projectDir.isEmpty() ? QString() : QDir(projectDir).absolutePath();

    if (!m_projectPath.isEmpty()) {
        const QDir dir(m_projectPath);
        QFile file(dir.filePath(QStringLiteral("canvas.json")));
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
            const QJsonObject view = root.value(QStringLiteral("view")).toObject();
            if (!view.isEmpty()) {
                m_hasView = true;
                m_viewX = view.value(QStringLiteral("x")).toDouble();
                m_viewY = view.value(QStringLiteral("y")).toDouble();
                m_viewZoom = view.value(QStringLiteral("zoom")).toDouble(1.0);
            }
            const QJsonArray items = root.value(QStringLiteral("items")).toArray();
            for (const QJsonValue &value : items) {
                const QJsonObject o = value.toObject();
                BoardItem it;
                it.id = o.value(QStringLiteral("id")).toString();
                if (it.id.isEmpty())
                    it.id = newId();
                it.type = o.value(QStringLiteral("type")).toString(QStringLiteral("note"));
                it.x = o.value(QStringLiteral("x")).toDouble();
                it.y = o.value(QStringLiteral("y")).toDouble();
                it.w = o.value(QStringLiteral("w")).toDouble(320);
                it.h = o.value(QStringLiteral("h")).toDouble(220);
                it.z = o.value(QStringLiteral("z")).toInt();
                it.color = o.value(QStringLiteral("color")).toString();
                it.source = o.value(QStringLiteral("source")).toString();
                if (it.type == QLatin1String("note")) {
                    const QString rel = o.value(QStringLiteral("file"))
                                            .toString(QStringLiteral("notes/%1.md").arg(it.id));
                    QFile md(dir.filePath(rel));
                    if (md.open(QIODevice::ReadOnly))
                        it.text = QString::fromUtf8(md.readAll());
                    m_savedTexts.insert(it.id, it.text);
                } else {
                    // Earlier versions put media on the canvas as cards of
                    // their own; now media lives inside notes
                    it.text = mediaMarkdown(it.source) + QLatin1Char('\n');
                    it.type = QStringLiteral("note");
                    it.source.clear();
                    it.w = std::max(it.w + 32, 200.0);
                    it.h += 32;
                    m_dirty = true;
                }
                m_items.append(it);
            }
        }
        adoptLooseNotes();
    }
    endResetModel();

    emit projectChanged();
    emit countChanged();
    emit undoStateChanged();
    emit viewRestored();
}

// Markdown files dropped into notes/ from outside the app show up on the canvas.
void BoardModel::adoptLooseNotes()
{
    const QDir notes(QDir(m_projectPath).filePath(QStringLiteral("notes")));
    if (!notes.exists())
        return;
    QSet<QString> known;
    for (const BoardItem &it : std::as_const(m_items))
        known.insert(it.id);

    const QRectF bounds = itemsBounds();
    double x = bounds.isEmpty() ? 0 : bounds.right() + 60;
    double y = bounds.isEmpty() ? 0 : bounds.top();
    const QFileInfoList files = notes.entryInfoList({QStringLiteral("*.md")}, QDir::Files, QDir::Name);
    for (const QFileInfo &fi : files) {
        const QString id = fi.completeBaseName();
        if (known.contains(id))
            continue;
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly))
            continue;
        BoardItem it = makeNote(x, y, QString::fromUtf8(f.readAll()));
        it.id = id;
        m_savedTexts.insert(id, it.text);
        m_items.append(it);
        y += it.h + 30;
        m_dirty = true;
    }
    if (m_dirty)
        m_saveTimer.start();
}

void BoardModel::close()
{
    save();
    beginResetModel();
    m_items.clear();
    m_undo.clear();
    m_redo.clear();
    m_savedTexts.clear();
    m_projectPath.clear();
    m_hasView = false;
    endResetModel();
    emit projectChanged();
    emit countChanged();
    emit undoStateChanged();
}

void BoardModel::save()
{
    m_saveTimer.stop();
    if (!m_dirty || m_projectPath.isEmpty())
        return;
    QDir dir(m_projectPath);
    if (!dir.exists()) { // project was removed underneath us
        m_dirty = false;
        return;
    }
    dir.mkpath(QStringLiteral("notes"));

    QSet<QString> liveNotes;
    QJsonArray items;
    for (const BoardItem &it : std::as_const(m_items)) {
        QJsonObject o{
            {QStringLiteral("id"), it.id},
            {QStringLiteral("type"), it.type},
            {QStringLiteral("x"), std::round(it.x * 10) / 10},
            {QStringLiteral("y"), std::round(it.y * 10) / 10},
            {QStringLiteral("w"), std::round(it.w * 10) / 10},
            {QStringLiteral("h"), std::round(it.h * 10) / 10},
            {QStringLiteral("z"), it.z},
        };
        if (it.type == QLatin1String("note")) {
            const QString rel = QStringLiteral("notes/%1.md").arg(it.id);
            o.insert(QStringLiteral("file"), rel);
            if (!it.color.isEmpty())
                o.insert(QStringLiteral("color"), it.color);
            liveNotes.insert(it.id);
            const auto saved = m_savedTexts.constFind(it.id);
            if (saved == m_savedTexts.cend() || *saved != it.text || !QFile::exists(dir.filePath(rel))) {
                QSaveFile md(dir.filePath(rel));
                if (md.open(QIODevice::WriteOnly)) {
                    md.write(it.text.toUtf8());
                    if (md.commit())
                        m_savedTexts.insert(it.id, it.text);
                }
            }
        } else {
            o.insert(QStringLiteral("source"), it.source);
        }
        items.append(o);
    }

    // Notes deleted from the canvas lose their Markdown file. Assets are kept,
    // so undo and duplicates never point at a missing file.
    for (auto i = m_savedTexts.begin(); i != m_savedTexts.end();) {
        if (!liveNotes.contains(i.key())) {
            QFile::remove(dir.filePath(QStringLiteral("notes/%1.md").arg(i.key())));
            i = m_savedTexts.erase(i);
        } else {
            ++i;
        }
    }

    QJsonObject root{
        {QStringLiteral("version"), 1},
        {QStringLiteral("items"), items},
    };
    if (m_hasView) {
        root.insert(QStringLiteral("view"), QJsonObject{
                                                {QStringLiteral("x"), m_viewX},
                                                {QStringLiteral("y"), m_viewY},
                                                {QStringLiteral("zoom"), m_viewZoom},
                                            });
    }
    QSaveFile file(dir.filePath(QStringLiteral("canvas.json")));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        if (file.commit())
            m_dirty = false;
    }
}

void BoardModel::markDirty()
{
    m_dirty = true;
    m_saveTimer.start();
}

void BoardModel::emitRow(int row)
{
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx);
}

// ---------------------------------------------------------------------------
// Undo

void BoardModel::pushUndo(const QString &coalesceKey)
{
    if (!coalesceKey.isEmpty() && coalesceKey == m_lastUndoKey)
        return;
    m_undo.append(m_items);
    if (m_undo.size() > kUndoLimit)
        m_undo.removeFirst();
    m_redo.clear();
    m_lastUndoKey = coalesceKey;
    emit undoStateChanged();
}

void BoardModel::restore(QList<BoardItem> items)
{
    beginResetModel();
    m_items = std::move(items);
    endResetModel();
    m_lastUndoKey.clear();
    emit countChanged();
    emit undoStateChanged();
    markDirty();
}

void BoardModel::undo()
{
    if (m_undo.isEmpty())
        return;
    m_redo.append(m_items);
    restore(m_undo.takeLast());
}

void BoardModel::redo()
{
    if (m_redo.isEmpty())
        return;
    m_undo.append(m_items);
    restore(m_redo.takeLast());
}

// ---------------------------------------------------------------------------
// Creating items

BoardItem BoardModel::makeNote(double x, double y, const QString &text) const
{
    BoardItem it;
    it.id = newId();
    it.type = QStringLiteral("note");
    it.x = x;
    it.y = y;
    it.w = 340;
    it.text = text;
    it.z = topZ() + 1;
    if (text.isEmpty()) {
        it.h = 200;
    } else {
        // Rough fit: ~42 characters per line at the default width
        int lines = 0;
        for (const QString &line : text.split(QLatin1Char('\n')))
            lines += 1 + line.size() / 42;
        it.h = std::clamp(48.0 + lines * 21.0, 120.0, 520.0);
    }
    return it;
}

int BoardModel::appendItem(const BoardItem &item)
{
    const int row = m_items.size();
    beginInsertRows({}, row, row);
    m_items.append(item);
    endInsertRows();
    emit countChanged();
    markDirty();
    return row;
}

int BoardModel::addNote(double x, double y, const QString &text)
{
    if (m_projectPath.isEmpty())
        return -1;
    pushUndo();
    return appendItem(makeNote(x, y, text));
}

// Markdown that embeds a project file: ![name](assets/name.png). Videos use
// the same syntax; the note shows a player for them.
QString BoardModel::mediaMarkdown(const QString &rel)
{
    QString name = QFileInfo(rel).fileName();
    name.replace(QLatin1Char('['), QLatin1Char('(')).replace(QLatin1Char(']'), QLatin1Char(')'));
    return QStringLiteral("![%1](%2)").arg(name, QString::fromUtf8(QUrl::toPercentEncoding(rel, "/")));
}

// Height a piece of media takes up in a note `width` wide
double BoardModel::mediaHeight(const QString &rel, double width) const
{
    if (mediaType(rel) == QLatin1String("video"))
        return width * 9.0 / 16.0 + 12;
    const QSizeF size = displaySize(absPath(rel), QStringLiteral("image"));
    return std::min(size.width(), width) * size.height() / std::max(1.0, size.width()) + 12;
}

QStringList BoardModel::importMedia(const QStringList &paths)
{
    QStringList rels;
    for (const QString &path : paths) {
        const QString abs = toLocalPath(path);
        if (mediaType(abs).isEmpty() || !QFileInfo(abs).isFile())
            continue;
        const QString rel = importAsset(abs);
        if (!rel.isEmpty())
            rels << rel;
    }
    return rels;
}

// A new note holding the media, sized to show it
int BoardModel::placeMediaNote(const QStringList &rels, double cx, double cy)
{
    const QSizeF first = displaySize(absPath(rels.first()), mediaType(rels.first()));
    const double w = std::clamp(first.width() + 32, 300.0, 560.0);
    double h = 32;
    QStringList parts;
    for (const QString &rel : rels) {
        parts << mediaMarkdown(rel);
        h += mediaHeight(rel, w - 32);
    }
    BoardItem it = makeNote(0, 0, parts.join(QStringLiteral("\n\n")) + QLatin1Char('\n'));
    it.w = w;
    it.h = std::clamp(h, 120.0, 900.0);
    it.x = cx - it.w / 2;
    it.y = cy - it.h / 2;
    return appendItem(it);
}

// Adds the media to the end of an existing note, growing it to fit
void BoardModel::appendMediaToNote(int row, const QStringList &rels)
{
    BoardItem &it = m_items[row];
    QStringList parts;
    double grow = 0;
    for (const QString &rel : rels) {
        parts << mediaMarkdown(rel);
        grow += mediaHeight(rel, it.w - 32);
    }
    QString text = it.text;
    while (text.endsWith(QLatin1Char('\n')))
        text.chop(1);
    it.text = (text.isEmpty() ? QString() : text + QStringLiteral("\n\n")) + parts.join(QStringLiteral("\n\n"))
              + QLatin1Char('\n');
    it.h = std::min(it.h + grow, std::max(it.h, 1200.0));
    emitRow(row);
    markDirty();
}

int BoardModel::addMedia(const QVariantList &paths, double cx, double cy, int targetRow)
{
    if (m_projectPath.isEmpty())
        return -1;
    QStringList list;
    for (const QVariant &v : paths)
        list << (v.typeId() == QMetaType::QUrl ? v.toUrl().toString() : v.toString());
    const QStringList rels = importMedia(list);
    if (rels.isEmpty())
        return -1;
    pushUndo();
    if (valid(targetRow)) {
        appendMediaToNote(targetRow, rels);
        return targetRow;
    }
    return placeMediaNote(rels, cx, cy);
}

int BoardModel::importUrls(const QVariantList &urls, double cx, double cy, int targetRow)
{
    if (m_projectPath.isEmpty())
        return -1;
    QStringList media;
    QStringList texts;
    for (const QVariant &value : urls) {
        const QUrl url = value.toUrl();
        const QString abs = url.isLocalFile() ? url.toLocalFile() : toLocalPath(value.toString());
        if (!QFileInfo(abs).isFile())
            continue;
        if (kTextExt.contains(QFileInfo(abs).suffix().toLower()))
            texts << abs;
        else if (!mediaType(abs).isEmpty())
            media << abs;
    }
    const QStringList rels = importMedia(media);
    if (rels.isEmpty() && texts.isEmpty())
        return -1;

    pushUndo();
    int last = -1;
    double offset = 0;
    for (const QString &abs : std::as_const(texts)) {
        QFile f(abs);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        last = appendItem(makeNote(cx - 170 + offset, cy - 100 + offset, QString::fromUtf8(f.readAll())));
        offset += 36;
    }
    if (!rels.isEmpty()) {
        if (valid(targetRow)) {
            appendMediaToNote(targetRow, rels);
            last = targetRow;
        } else {
            last = placeMediaNote(rels, cx + offset, cy + offset);
        }
    }
    return last;
}

int BoardModel::addImageData(const QByteArray &data, double cx, double cy, int targetRow)
{
    if (m_projectPath.isEmpty())
        return -1;
    QImage image;
    if (!image.loadFromData(data))
        return -1;
    const QString rel = saveImageAsset(image, QStringLiteral("drop"));
    if (rel.isEmpty())
        return -1;
    pushUndo();
    if (valid(targetRow)) {
        appendMediaToNote(targetRow, {rel});
        return targetRow;
    }
    return placeMediaNote({rel}, cx, cy);
}

int BoardModel::pasteFromClipboard(double cx, double cy, int targetRow)
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (!mime || m_projectPath.isEmpty())
        return -1;

    if (mime->hasUrls()) {
        QVariantList local;
        const QList<QUrl> urls = mime->urls();
        for (const QUrl &url : urls) {
            if (url.isLocalFile())
                local.append(url);
        }
        if (!local.isEmpty()) {
            const int row = importUrls(local, cx, cy, targetRow);
            if (row >= 0)
                return row;
        }
    }
    if (mime->hasImage()) {
        const QImage image = qvariant_cast<QImage>(mime->imageData());
        const QString rel = saveImageAsset(image, QStringLiteral("paste"));
        if (!rel.isEmpty()) {
            pushUndo();
            if (valid(targetRow)) {
                appendMediaToNote(targetRow, {rel});
                return targetRow;
            }
            return placeMediaNote({rel}, cx, cy);
        }
    }
    if (mime->hasText() && !mime->text().trimmed().isEmpty()) {
        pushUndo();
        return appendItem(makeNote(cx - 170, cy - 60, mime->text()));
    }
    return -1;
}

int BoardModel::itemAt(double x, double y) const
{
    int found = -1;
    for (int i = 0; i < m_items.size(); ++i) {
        const BoardItem &it = m_items.at(i);
        if (QRectF(it.x, it.y, it.w, it.h).contains(x, y) && (found < 0 || it.z >= m_items.at(found).z))
            found = i;
    }
    return found;
}

// ---------------------------------------------------------------------------
// Editing items

void BoardModel::setGeometry(int row, double x, double y, double w, double h)
{
    if (!valid(row))
        return;
    const BoardItem &cur = m_items.at(row);
    if (!differs(cur.x, x) && !differs(cur.y, y) && !differs(cur.w, w) && !differs(cur.h, h))
        return;
    pushUndo(); // before taking a mutable reference: pushing detaches m_items
    BoardItem &it = m_items[row];
    it.x = x;
    it.y = y;
    it.w = w;
    it.h = h;
    emitRow(row);
    markDirty();
}

void BoardModel::setText(int row, const QString &text)
{
    if (!valid(row) || m_items.at(row).text == text)
        return;
    // Consecutive autosaves of one editing session form a single undo step
    pushUndo(QStringLiteral("text:") + m_items.at(row).id);
    m_items[row].text = text;
    emitRow(row);
    markDirty();
}

void BoardModel::setColor(int row, const QString &color)
{
    if (!valid(row) || m_items.at(row).color == color)
        return;
    pushUndo();
    m_items[row].color = color;
    emitRow(row);
    markDirty();
}

void BoardModel::raise(int row)
{
    if (!valid(row))
        return;
    const int top = topZ();
    const int z = m_items.at(row).z;
    const bool alone = std::count_if(m_items.cbegin(), m_items.cend(),
                                     [top](const BoardItem &i) { return i.z == top; }) == 1;
    if (z == top && alone)
        return;
    m_items[row].z = top + 1;
    emitRow(row);
    markDirty();
}

void BoardModel::lower(int row)
{
    if (!valid(row))
        return;
    m_items[row].z = bottomZ() - 1;
    emitRow(row);
    markDirty();
}

void BoardModel::remove(int row)
{
    if (!valid(row))
        return;
    pushUndo();
    beginRemoveRows({}, row, row);
    m_items.removeAt(row);
    endRemoveRows();
    emit countChanged();
    markDirty();
}

int BoardModel::duplicate(int row)
{
    if (!valid(row))
        return -1;
    pushUndo();
    BoardItem copy = m_items.at(row);
    copy.id = newId();
    copy.x += 28;
    copy.y += 28;
    copy.z = topZ() + 1;
    return appendItem(copy);
}

int BoardModel::topZ() const
{
    int z = 0;
    for (const BoardItem &it : m_items)
        z = std::max(z, it.z);
    return z;
}

int BoardModel::bottomZ() const
{
    int z = 0;
    for (const BoardItem &it : m_items)
        z = std::min(z, it.z);
    return z;
}

// ---------------------------------------------------------------------------
// Queries

int BoardModel::indexOf(const QString &id) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).id == id)
            return i;
    }
    return -1;
}

QVariantMap BoardModel::get(int row) const
{
    if (!valid(row))
        return {};
    const BoardItem &it = m_items.at(row);
    return {
        {QStringLiteral("id"), it.id},
        {QStringLiteral("type"), it.type},
        {QStringLiteral("x"), it.x},
        {QStringLiteral("y"), it.y},
        {QStringLiteral("w"), it.w},
        {QStringLiteral("h"), it.h},
        {QStringLiteral("z"), it.z},
        {QStringLiteral("color"), it.color},
        {QStringLiteral("text"), it.text},
        {QStringLiteral("title"), titleFor(it)},
        {QStringLiteral("rel"), it.source},
        {QStringLiteral("source"), it.source.isEmpty() ? QUrl() : QUrl::fromLocalFile(absPath(it.source))},
    };
}

QRectF BoardModel::itemsBounds() const
{
    QRectF bounds;
    for (const BoardItem &it : m_items)
        bounds = bounds.united(QRectF(it.x, it.y, it.w, it.h));
    return bounds;
}

QString BoardModel::titleFor(const BoardItem &item)
{
    if (item.type != QLatin1String("note"))
        return QFileInfo(item.source).fileName();
    return titleForText(item.text);
}

QString BoardModel::titleForText(const QString &markdown)
{
    const QStringList lines = markdown.split(QLatin1Char('\n'));
    QString media;
    for (QString line : lines) {
        line = line.trimmed();
        while (line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char('>')))
            line.remove(0, 1);
        line = line.trimmed();
        if (line.startsWith(QLatin1String("- ")) || line.startsWith(QLatin1String("* ")))
            line = line.mid(2).trimmed();
        line.remove(QStringLiteral("**"));
        line.remove(QLatin1Char('`'));
        if (line.startsWith(QLatin1Char('<'))) // raw HTML such as an <img> tag
            continue;
        if (line.startsWith(QLatin1String("!["))) { // embedded media
            if (media.isEmpty())
                media = line.mid(2, line.indexOf(QLatin1Char(']')) - 2);
            continue;
        }
        if (!line.isEmpty())
            return line.left(80);
    }
    return media.isEmpty() ? QCoreApplication::translate("BoardModel", "Untitled note") : media;
}

// ---------------------------------------------------------------------------
// Clipboard

bool BoardModel::clipboardHasImage() const
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    return mime && mime->hasImage();
}

QString BoardModel::saveClipboardImage()
{
    if (m_projectPath.isEmpty())
        return {};
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (!mime || !mime->hasImage())
        return {};
    const QString rel = saveImageAsset(qvariant_cast<QImage>(mime->imageData()), QStringLiteral("paste"));
    return rel.isEmpty() ? QString() : absPath(rel);
}

QStringList BoardModel::clipboardMediaFiles() const
{
    QStringList files;
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (!mime || !mime->hasUrls())
        return files;
    const QList<QUrl> urls = mime->urls();
    for (const QUrl &url : urls) {
        if (url.isLocalFile() && !mediaType(url.toLocalFile()).isEmpty())
            files << url.toLocalFile();
    }
    return files;
}

// Rich text with the images inside, for pasting into Teams, mail or an
// office document; Markdown for plain-text editors. A note that is only one
// image also goes as that image, for applications that only take images.
void BoardModel::copyToClipboard(int row) const
{
    if (!valid(row))
        return;
    const BoardItem &it = m_items.at(row);
    auto *mime = new QMimeData;
    mime->setHtml(renderHtml(it.text, 720, 14, QColor(), true));
    mime->setText(it.text);

    QStringList images;
    QString rest = it.text;
    for (const QString &line : it.text.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch match = kMediaLine.match(line);
        if (!match.hasMatch())
            continue;
        const QString local = resolveLocal(match.captured(2));
        if (mediaType(local) == QLatin1String("image"))
            images << local;
        rest.remove(line);
    }
    if (images.size() == 1 && rest.trimmed().isEmpty())
        mime->setImageData(QImage(images.first()));
    QGuiApplication::clipboard()->setMimeData(mime);
}

// ---------------------------------------------------------------------------
// Assets

QString BoardModel::absPath(const QString &rel) const
{
    if (rel.isEmpty() || QDir::isAbsolutePath(rel))
        return rel;
    return QDir(m_projectPath).filePath(rel);
}

QString BoardModel::toLocalPath(const QString &pathOrUrl) const
{
    if (pathOrUrl.startsWith(QLatin1String("file:")))
        return QUrl(pathOrUrl).toLocalFile();
    return absPath(pathOrUrl);
}

QString BoardModel::mediaType(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (kImageExt.contains(suffix))
        return QStringLiteral("image");
    if (kVideoExt.contains(suffix))
        return QStringLiteral("video");
    return {};
}

QString BoardModel::uniqueAssetPath(const QString &base, const QString &extension) const
{
    const QDir dir(m_projectPath);
    QString rel = QStringLiteral("assets/%1.%2").arg(base, extension);
    for (int n = 2; QFileInfo::exists(dir.filePath(rel)); ++n)
        rel = QStringLiteral("assets/%1-%2.%3").arg(base).arg(n).arg(extension);
    return rel;
}

// Copies a file into assets/ unless it already lives inside the project.
QString BoardModel::importAsset(const QString &abs)
{
    const QFileInfo fi(abs);
    const QString root = QDir(m_projectPath).absolutePath() + QLatin1Char('/');
    if (fi.absoluteFilePath().startsWith(root))
        return fi.absoluteFilePath().mid(root.size());
    QDir(m_projectPath).mkpath(QStringLiteral("assets"));
    QString base = fi.completeBaseName();
    if (base.isEmpty())
        base = QStringLiteral("file");
    // Adding the same file twice reuses the copy already in assets/
    const QString suffix = fi.suffix().toLower();
    const QDir assets(absPath(QStringLiteral("assets")));
    const QStringList candidates = assets.entryList({base + QLatin1Char('.') + suffix,
                                                     base + QStringLiteral("-*.") + suffix},
                                                    QDir::Files);
    for (const QString &name : candidates) {
        if (sameContent(abs, assets.filePath(name)))
            return QStringLiteral("assets/") + name;
    }
    const QString rel = uniqueAssetPath(base, suffix);
    if (!QFile::copy(abs, absPath(rel)))
        return {};
    return rel;
}

QString BoardModel::saveImageAsset(const QImage &image, const QString &prefix)
{
    if (image.isNull() || m_projectPath.isEmpty())
        return {};
    QDir(m_projectPath).mkpath(QStringLiteral("assets"));
    const QString rel = uniqueAssetPath(prefix + QLatin1Char('-') + timestamp(), QStringLiteral("png"));
    return image.save(absPath(rel), "PNG") ? rel : QString();
}

QSizeF BoardModel::displaySize(const QString &abs, const QString &type) const
{
    QSizeF size(640, 360);
    if (type == QLatin1String("image")) {
        const QSize native = QImageReader(abs).size();
        if (native.isValid()) {
            const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
            size = QSizeF(native) / dpr;
        } else {
            size = QSizeF(400, 300);
        }
    }
    const double scale = std::min(1.0, kMaxMediaSide / std::max(size.width(), size.height()));
    size *= scale;
    return QSizeF(std::max(40.0, size.width()), std::max(30.0, size.height()));
}

bool BoardModel::isMediaFile(const QString &pathOrUrl) const
{
    return !mediaType(toLocalPath(pathOrUrl)).isEmpty();
}

QVariantMap BoardModel::mediaInfo(const QString &pathOrUrl)
{
    const QString abs = toLocalPath(pathOrUrl);
    const QString type = mediaType(abs);
    if (m_projectPath.isEmpty() || type.isEmpty() || !QFileInfo::exists(abs))
        return {};
    const QString rel = importAsset(abs);
    if (rel.isEmpty())
        return {};
    QSizeF size(640, 360);
    if (type == QLatin1String("image")) {
        const QSize native = QImageReader(absPath(rel)).size();
        const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
        size = native.isValid() ? QSizeF(native) / dpr : QSizeF(400, 300);
    }
    return {
        {QStringLiteral("rel"), rel},
        {QStringLiteral("type"), type},
        {QStringLiteral("name"), QFileInfo(rel).fileName()},
        {QStringLiteral("width"), size.width()},
        {QStringLiteral("height"), size.height()},
    };
}

QString BoardModel::resolveLocal(const QString &target) const
{
    const QUrl url(target);
    if (url.isLocalFile())
        return url.toLocalFile();
    if (!url.scheme().isEmpty() && url.scheme().size() > 1) // http: and friends; not C:
        return {};
    return absPath(QUrl::fromPercentEncoding(target.toUtf8()));
}

// Markdown -> HTML through QTextDocument. On screen images are file URLs
// scaled to maxWidth; for the clipboard they are embedded as data: URIs,
// as other applications (Teams, mail) cannot read the project's files.
QString BoardModel::renderHtml(const QString &markdown, double maxWidth, int pixelSize, const QColor &linkColor,
                               bool forClipboard, const QStringList &highlight,
                               const QColor &highlightColor) const
{
    // A video can't show inside text: there it is a link that plays it
    QString source = markdown;
    source.replace(kVideoImage, QStringLiteral("[▶ \\1](\\2)"));

    QTextDocument doc;
    QFont font = QGuiApplication::font();
    font.setPixelSize(pixelSize);
    doc.setDefaultFont(font);
    doc.setMarkdown(source);

    struct Run {
        int position;
        int length;
        QTextCharFormat format;
    };
    QList<Run> runs;
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid())
                continue;
            QTextCharFormat format = fragment.charFormat();
            if (format.isAnchor() && !format.isImageFormat()) {
                if (linkColor.isValid())
                    format.setForeground(linkColor);
                else
                    format.clearForeground(); // the pasting app's link colour
                if (forClipboard) {
                    const QString local = resolveLocal(format.anchorHref());
                    if (!local.isEmpty())
                        format.setAnchorHref(QUrl::fromLocalFile(local).toString());
                }
                runs.append({fragment.position(), fragment.length(), format});
                continue;
            }
            if (!format.isImageFormat())
                continue;
            QTextImageFormat image = format.toImageFormat();
            const QString local = resolveLocal(image.name());
            if (local.isEmpty())
                continue; // remote image: leave it alone

            auto cached = m_imageSizes.constFind(local);
            if (cached == m_imageSizes.cend()) {
                const QSize native = QImageReader(local).size();
                cached = m_imageSizes.insert(local, native.isValid() ? QSizeF(native) / dpr : QSizeF());
            }
            const QSizeF native = *cached;
            const double limit = forClipboard ? std::min(maxWidth, 720.0) : maxWidth;
            if (native.width() > 0) {
                const double w = std::min(image.width() > 0 ? image.width() : native.width(), limit);
                image.setWidth(std::round(w));
                image.setHeight(std::round(w * native.height() / native.width()));
            }
            image.setName(forClipboard ? dataUri(local) : QUrl::fromLocalFile(local).toString());
            runs.append({fragment.position(), fragment.length(), image});
        }
    }

    // Paragraph spacing: toHtml() keeps only explicit margins, so give
    // paragraphs and the end of each list some room
    const double gap = pixelSize * 0.55;
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        const QTextBlock next = block.next();
        const bool inList = block.textList() != nullptr;
        const bool listEnds = inList && (!next.isValid() || next.textList() != block.textList());
        if ((inList && !listEnds) || QTextCursor(block).currentTable())
            continue;
        QTextBlockFormat format = block.blockFormat();
        if (format.bottomMargin() < gap) {
            format.setBottomMargin(gap);
            QTextCursor(block).setBlockFormat(format);
        }
    }
    for (const Run &run : std::as_const(runs)) {
        QTextCursor cursor(&doc);
        cursor.setPosition(run.position);
        cursor.setPosition(run.position + run.length, QTextCursor::KeepAnchor);
        cursor.setCharFormat(run.format);
    }

    if (highlightColor.isValid()) {
        QTextCharFormat mark;
        mark.setBackground(highlightColor);
        for (const QString &term : highlight) {
            if (term.isEmpty())
                continue;
            for (QTextCursor found = doc.find(term); !found.isNull(); found = doc.find(term, found))
                found.mergeCharFormat(mark);
        }
    }

    QString html = doc.toHtml();
    if (forClipboard) {
        // Only the body, without Qt's fonts, so the text takes on the look
        // of wherever it is pasted
        const qsizetype open = html.indexOf(QLatin1String("<body"));
        const qsizetype start = html.indexOf(QLatin1Char('>'), open) + 1;
        const qsizetype end = html.lastIndexOf(QLatin1String("</body>"));
        if (open >= 0 && start > 0 && end > start)
            html = QStringLiteral("<html><body>") + html.mid(start, end - start) + QStringLiteral("</body></html>");
    }
    return html;
}

QString BoardModel::dataUri(const QString &path) const
{
    QImage image(path);
    if (image.isNull())
        return QUrl::fromLocalFile(path).toString();
    if (image.width() > 1600)
        image = image.scaledToWidth(1600, Qt::SmoothTransformation);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    const bool opaque = !image.hasAlphaChannel();
    // Photos as JPEG keep the clipboard small; screenshots stay sharp as PNG
    const bool photo = opaque && QFileInfo(path).suffix().toLower().startsWith(QLatin1String("jp"));
    image.save(&buffer, photo ? "JPEG" : "PNG", photo ? 90 : -1);
    return QStringLiteral("data:image/%1;base64,").arg(photo ? QStringLiteral("jpeg") : QStringLiteral("png"))
           + QString::fromLatin1(bytes.toBase64());
}

// The note as pieces for display: runs of rich text, and the videos that
// stand on a line of their own, which get a player.
QVariantList BoardModel::renderSegments(const QString &markdown, double maxWidth, int pixelSize,
                                        const QColor &linkColor, const QStringList &highlight,
                                        const QColor &highlightColor) const
{
    QVariantList segments;
    QStringList chunk;
    bool fenced = false;
    const auto flush = [&] {
        const QString md = chunk.join(QLatin1Char('\n'));
        chunk.clear();
        if (md.trimmed().isEmpty())
            return;
        segments.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("text")},
                                    {QStringLiteral("html"), renderHtml(md, maxWidth, pixelSize, linkColor, false, highlight, highlightColor)}});
    };
    const QStringList lines = markdown.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1String("```")) || trimmed.startsWith(QLatin1String("~~~")))
            fenced = !fenced;
        if (!fenced) {
            const QRegularExpressionMatch match = kMediaLine.match(line);
            if (match.hasMatch()) {
                const QString local = resolveLocal(match.captured(2));
                if (!local.isEmpty() && mediaType(local) == QLatin1String("video")) {
                    flush();
                    segments.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("video")},
                                                {QStringLiteral("url"), QUrl::fromLocalFile(local)},
                                                {QStringLiteral("name"), match.captured(1)}});
                    continue;
                }
            }
        }
        chunk << line;
    }
    flush();
    return segments;
}

// Writes every card into one Markdown file, in reading order (top to bottom,
// left to right), so a canvas can be shared as a plain document.
QString BoardModel::exportMarkdown()
{
    if (m_projectPath.isEmpty())
        return {};
    save();
    QList<BoardItem> ordered = m_items;
    std::sort(ordered.begin(), ordered.end(), [](const BoardItem &a, const BoardItem &b) {
        const int rowA = int(std::floor(a.y / 120.0));
        const int rowB = int(std::floor(b.y / 120.0));
        return rowA != rowB ? rowA < rowB : a.x < b.x;
    });
    QStringList parts;
    for (const BoardItem &it : std::as_const(ordered)) {
        if (it.type == QLatin1String("note"))
            parts << it.text.trimmed();
        else if (it.type == QLatin1String("image"))
            parts << QStringLiteral("![%1](%2)").arg(QFileInfo(it.source).fileName(), it.source);
        else
            parts << QStringLiteral("[Video: %1](%2)").arg(QFileInfo(it.source).fileName(), it.source);
    }
    const QString path = QDir(m_projectPath).filePath(QStringLiteral("export.md"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(parts.join(QStringLiteral("\n\n---\n\n")).toUtf8() + '\n');
    return file.commit() ? path : QString();
}

void BoardModel::setView(double centerX, double centerY, double zoom)
{
    if (m_projectPath.isEmpty())
        return;
    if (m_hasView && !differs(m_viewX, centerX) && !differs(m_viewY, centerY) && !differs(m_viewZoom, zoom))
        return;
    m_hasView = true;
    m_viewX = centerX;
    m_viewY = centerY;
    m_viewZoom = zoom;
    markDirty();
}
