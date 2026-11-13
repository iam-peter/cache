#include "boardmodel.h"

#include "highlight.h"
#include "markdown.h"

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
#include <QLocale>
#include <QMimeData>
#include <QPointer>
#include <QSaveFile>
#include <QSet>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QTextList>
#include <QUuid>

#include <functional>

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
    m_writer.setMaxThreadCount(1);
    connect(&m_saveTimer, &QTimer::timeout, this, &BoardModel::saveInBackground);
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
    case ShapeRole: return it.shape;
    case PointsRole: {
        QVariantList points;
        points.reserve(it.points.size());
        for (const QPointF &p : it.points)
            points.append(p);
        return points;
    }
    case SizeRole: return it.size;
    case CollapsedRole: return it.collapsed;
    case HiddenRole: return m_hidden.contains(it.id);
    case DescendantsRole: return m_descendants.value(it.id);
    case ChildCountRole: return m_childCount.value(it.id);
    case SectionIdRole: return m_sectionOf.value(it.id);
    case SectionTitleRole: {
        const int section = indexOf(m_sectionOf.value(it.id));
        return section >= 0 ? m_items.at(section).text : QString();
    }
    case SectionOrderRole: // a section's own place; for the rest, that of their section
        return it.type == QLatin1String("section") ? m_sectionOrder.value(it.id, -1)
                                                   : m_sectionOrder.value(m_sectionOf.value(it.id), -1);
    case ReadingOrderRole: return m_readingOrder.value(it.id);
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
        {ShapeRole, "itemShape"},
        {PointsRole, "itemPoints"},
        {SizeRole, "itemSize"},
        {CollapsedRole, "itemCollapsed"},
        {HiddenRole, "itemHidden"},
        {DescendantsRole, "descendants"},
        {ChildCountRole, "childCount"},
        {SectionIdRole, "sectionId"},
        {SectionTitleRole, "sectionTitle"},
        {SectionOrderRole, "sectionOrder"},
        {ReadingOrderRole, "readingOrder"},
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
    m_links.clear();
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
                BoardItem it = itemFromJson(o);
                if (it.type == QLatin1String("note")) {
                    const QString rel = o.value(QStringLiteral("file"))
                                            .toString(QStringLiteral("notes/%1.md").arg(it.id));
                    QFile md(dir.filePath(rel));
                    if (md.open(QIODevice::ReadOnly))
                        it.text = QString::fromUtf8(md.readAll());
                    m_savedTexts.insert(it.id, it.text);
                } else if (it.type == QLatin1String("image") || it.type == QLatin1String("video")) {
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
            const QJsonArray links = root.value(QStringLiteral("links")).toArray();
            QSet<QString> ids;
            for (const BoardItem &it : std::as_const(m_items))
                ids.insert(it.id);
            for (const QJsonValue &value : links) {
                const BoardLink link = linkFromJson(value.toObject());
                if (ids.contains(link.from) && ids.contains(link.to))
                    m_links.append(link);
            }
        }
        adoptLooseNotes();
    }
    updateDerived();
    endResetModel();

    emit projectChanged();
    emit countChanged();
    emit undoStateChanged();
    emit linksChanged();
    textsChanged();
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
    m_links.clear();
    m_undo.clear();
    m_redo.clear();
    m_savedTexts.clear();
    m_projectPath.clear();
    m_hasView = false;
    updateDerived();
    endResetModel();
    emit projectChanged();
    emit countChanged();
    emit undoStateChanged();
    emit linksChanged();
    textsChanged();
}

void BoardModel::save()
{
    m_saveTimer.stop();
    m_writer.waitForDone();
    const QList<FileWrite> writes = takeWrites();
    if (!writes.isEmpty() && writeFiles(writes))
        emit saved();
}

void BoardModel::saveInBackground()
{
    m_saveTimer.stop();
    const QList<FileWrite> writes = takeWrites();
    if (writes.isEmpty())
        return;
    const QPointer<BoardModel> self(this);
    m_writer.start([writes, self] {
        const bool ok = writeFiles(writes);
        QMetaObject::invokeMethod(qApp, [self, ok] {
            if (!self)
                return;
            if (ok) {
                emit self->saved();
            } else { // try again, writing every note
                self->m_savedTexts.clear();
                self->markDirty();
            }
        }, Qt::QueuedConnection);
    });
}

bool BoardModel::writeFiles(const QList<FileWrite> &writes)
{
    bool ok = true;
    for (const FileWrite &w : writes) {
        if (w.remove) {
            QFile::remove(w.path);
            continue;
        }
        QDir().mkpath(QFileInfo(w.path).absolutePath());
        QSaveFile file(w.path);
        ok = file.open(QIODevice::WriteOnly) && file.write(w.data) == w.data.size() && file.commit() && ok;
    }
    return ok;
}

QList<BoardModel::FileWrite> BoardModel::takeWrites()
{
    if (!m_dirty || m_projectPath.isEmpty())
        return {};
    QDir dir(m_projectPath);
    m_dirty = false;
    if (!dir.exists()) // project was removed underneath us
        return {};

    QList<FileWrite> writes;
    QSet<QString> liveNotes;
    QJsonArray items;
    for (const BoardItem &it : std::as_const(m_items)) {
        if (it.type == QLatin1String("note")) {
            liveNotes.insert(it.id);
            const auto saved = m_savedTexts.constFind(it.id);
            const QString path = dir.filePath(QStringLiteral("notes/%1.md").arg(it.id));
            if (saved == m_savedTexts.cend() || *saved != it.text || !QFile::exists(path)) {
                writes.append({path, it.text.toUtf8()});
                m_savedTexts.insert(it.id, it.text);
            }
        }
        items.append(toJson(it, false));
    }

    // Notes deleted from the canvas lose their Markdown file. Assets are kept,
    // so undo and duplicates never point at a missing file.
    for (auto i = m_savedTexts.begin(); i != m_savedTexts.end();) {
        if (!liveNotes.contains(i.key())) {
            writes.append({dir.filePath(QStringLiteral("notes/%1.md").arg(i.key())), {}, true});
            i = m_savedTexts.erase(i);
        } else {
            ++i;
        }
    }

    QJsonArray links;
    for (const BoardLink &link : std::as_const(m_links))
        links.append(toJson(link));
    QJsonObject root{
        {QStringLiteral("version"), 2},
        {QStringLiteral("items"), items},
        {QStringLiteral("links"), links},
    };
    if (m_hasView) {
        root.insert(QStringLiteral("view"), QJsonObject{
                                                {QStringLiteral("x"), m_viewX},
                                                {QStringLiteral("y"), m_viewY},
                                                {QStringLiteral("zoom"), m_viewZoom},
                                            });
    }
    writes.append({dir.filePath(QStringLiteral("canvas.json")), QJsonDocument(root).toJson(QJsonDocument::Indented)});
    return writes;
}

void BoardModel::markDirty()
{
    m_dirty = true;
    m_saveTimer.start();
}

const QList<int> BoardModel::kTextRoles{TextRole, TitleRole, SearchRole};
const QList<int> BoardModel::kGeometryRoles{PosXRole, PosYRole, WidthRole, HeightRole};

void BoardModel::emitRow(int row, const QList<int> &roles)
{
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, roles);
}

// ---------------------------------------------------------------------------
// Undo

void BoardModel::pushUndo(const QString &coalesceKey)
{
    if (!coalesceKey.isEmpty() && coalesceKey == m_lastUndoKey)
        return;
    m_undo.append(m_state);
    if (m_undo.size() > kUndoLimit)
        m_undo.removeFirst();
    m_redo.clear();
    m_lastUndoKey = coalesceKey;
    emit undoStateChanged();
}

void BoardModel::restore(BoardState state)
{
    beginResetModel();
    m_state = std::move(state);
    updateDerived();
    endResetModel();
    m_lastUndoKey.clear();
    emit countChanged();
    emit undoStateChanged();
    emit linksChanged();
    textsChanged();
    markDirty();
}

void BoardModel::undo()
{
    if (m_undo.isEmpty())
        return;
    m_redo.append(m_state);
    restore(m_undo.takeLast());
}

void BoardModel::redo()
{
    if (m_redo.isEmpty())
        return;
    m_undo.append(m_state);
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
    updateDerived();
    if (item.type == QLatin1String("note"))
        textsChanged();
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
    emitRow(row, kTextRoles + QList<int>{HeightRole});
    updateDerived();
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

int BoardModel::itemAt(double x, double y) const
{
    int found = -1;
    for (int i = 0; i < m_items.size(); ++i) {
        const BoardItem &it = m_items.at(i);
        if (m_hidden.contains(it.id) || !QRectF(it.x, it.y, it.w, it.h).contains(x, y))
            continue;
        // Sections lie under everything else
        const auto depth = [](const BoardItem &b) {
            return b.type == QLatin1String("section") ? qint64(b.z) - (qint64(1) << 40) : qint64(b.z);
        };
        if (found < 0 || depth(it) >= depth(m_items.at(found)))
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
    emitRow(row, kGeometryRoles);
    updateDerived();
    markDirty();
}

void BoardModel::setText(int row, const QString &text)
{
    if (!valid(row) || m_items.at(row).text == text)
        return;
    // Consecutive autosaves of one editing session form a single undo step
    pushUndo(QStringLiteral("text:") + m_items.at(row).id);
    m_items[row].text = text;
    emitRow(row, kTextRoles);
    if (m_items.at(row).type == QLatin1String("section"))
        emitAll({SectionTitleRole});
    textsChanged();
    markDirty();
}

void BoardModel::setColor(int row, const QString &color)
{
    if (!valid(row) || m_items.at(row).color == color)
        return;
    pushUndo();
    m_items[row].color = color;
    emitRow(row, {ColorRole});
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
    emitRow(row, {ZRole});
    markDirty();
}

void BoardModel::lower(int row)
{
    if (!valid(row))
        return;
    m_items[row].z = bottomZ() - 1;
    emitRow(row, {ZRole});
    markDirty();
}

void BoardModel::remove(int row)
{
    if (!valid(row))
        return;
    removeItems({m_items.at(row).id});
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
        {QStringLiteral("shape"), it.shape},
        {QStringLiteral("size"), it.size},
        {QStringLiteral("collapsed"), it.collapsed},
        {QStringLiteral("hidden"), m_hidden.contains(it.id)},
        {QStringLiteral("childCount"), m_childCount.value(it.id)},
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
    if (item.type == QLatin1String("note"))
        return titleForText(item.text);
    if (item.type == QLatin1String("stroke"))
        return QCoreApplication::translate("BoardModel", "Drawing");
    const QString first = item.text.trimmed().section(QLatin1Char('\n'), 0, 0).left(80);
    if (!first.isEmpty())
        return first;
    if (item.type == QLatin1String("section"))
        return QCoreApplication::translate("BoardModel", "Section");
    if (item.type == QLatin1String("shape"))
        return QCoreApplication::translate("BoardModel", "Shape");
    return QCoreApplication::translate("BoardModel", "Text");
}

QString BoardModel::titleForText(const QString &markdown)
{
    const QStringList lines = markdown.split(QLatin1Char('\n'));
    QString media;
    QString table;
    for (QString line : lines) {
        line = line.trimmed();
        while (line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char('>')))
            line.remove(0, 1);
        line = line.trimmed();
        if (line.startsWith(QLatin1String("- ")) || line.startsWith(QLatin1String("* ")))
            line = line.mid(2).trimmed();
        if (line.startsWith(QLatin1String("[ ] ")) || line.startsWith(QLatin1String("[x] "))
            || line.startsWith(QLatin1String("[X] ")))
            line = line.mid(4).trimmed(); // a task
        if (line.startsWith(QLatin1String("```")))
            continue; // a code fence
        line.remove(QStringLiteral("**"));
        line.remove(QLatin1Char('`'));
        if (line.startsWith(QLatin1Char('<'))) // raw HTML such as an <img> tag
            continue;
        if (line.startsWith(QLatin1String("!["))) { // embedded media
            if (media.isEmpty())
                media = line.mid(2, line.indexOf(QLatin1Char(']')) - 2);
            continue;
        }
        if (line.startsWith(QLatin1Char('|'))) { // a table: its header, failing all else
            if (table.isEmpty() && !line.contains(QStringLiteral("---"))) {
                QStringList cells = line.split(QLatin1Char('|'));
                for (QString &cell : cells)
                    cell = cell.trimmed();
                cells.removeAll(QString());
                table = cells.join(QStringLiteral(" · "));
            }
            continue;
        }
        // Links read as their text
        static const QRegularExpression link(QStringLiteral(R"(\[([^\]]*)\]\([^)]*\))"));
        line.replace(link, QStringLiteral("\\1"));
        line.remove(QStringLiteral("[["));
        line.remove(QStringLiteral("]]"));
        if (!line.isEmpty())
            return line.left(80);
    }
    if (!media.isEmpty())
        return media;
    return table.isEmpty() ? QCoreApplication::translate("BoardModel", "Untitled note") : table.left(80);
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
    mime->setHtml(clipboardHtml(it.text));
    mime->setText(it.text);
    // Pasted back into the app, the Markdown is taken as it is, and on a
    // canvas the note comes back whole: its size and colour too
    mime->setData(QStringLiteral("application/x-cache-markdown"), QByteArray());
    mime->setData(QStringLiteral("application/x-cache-items"),
                  QJsonDocument(QJsonObject{{QStringLiteral("items"), QJsonArray{toJson(it, true)}},
                                            {QStringLiteral("links"), QJsonArray()}})
                      .toJson(QJsonDocument::Compact));

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

namespace {

QString bodyOf(const QString &html)
{
    const qsizetype open = html.indexOf(QLatin1String("<body"));
    const qsizetype start = html.indexOf(QLatin1Char('>'), open) + 1;
    const qsizetype end = html.lastIndexOf(QLatin1String("</body>"));
    return open >= 0 && start > 0 && end > start ? html.mid(start, end - start) : html;
}

// Colours for code pasted elsewhere, which is usually light: GitHub's
const QVariantMap &pasteCodeColors()
{
    static const QVariantMap colors{
        {QStringLiteral("keyword"), QColor(0xcf, 0x22, 0x2e)},
        {QStringLiteral("type"), QColor(0x95, 0x38, 0x00)},
        {QStringLiteral("string"), QColor(0x0a, 0x30, 0x69)},
        {QStringLiteral("number"), QColor(0x05, 0x50, 0xae)},
        {QStringLiteral("comment"), QColor(0x6e, 0x77, 0x81)},
        {QStringLiteral("preprocessor"), QColor(0xcf, 0x22, 0x2e)},
        {QStringLiteral("function"), QColor(0x82, 0x50, 0xdf)},
    };
    return colors;
}

const QRegularExpression kWikiLine(QStringLiteral(R"(^\s*(!?)\[\[([^\[\]|]+)(?:\|([^\[\]]*))?\]\]\s*$)"));

} // namespace

// Markdown -> HTML through QTextDocument. On screen images are file URLs
// scaled to maxWidth, tasks are task:<n> links (n counted from firstTask)
// and [[wiki links]] note: links. For the clipboard images are embedded as
// data: URIs, as other applications (Teams, mail) cannot read the
// project's files, and tasks and links are plain text.
QString BoardModel::renderHtml(const QString &markdown, double maxWidth, int pixelSize, const QVariantMap &colors,
                               bool forClipboard, const QStringList &highlight, int firstTask) const
{
    const QColor linkColor = colors.value(QStringLiteral("link")).value<QColor>();
    const QColor mutedColor = colors.value(QStringLiteral("muted")).value<QColor>();
    const QColor highlightColor = colors.value(QStringLiteral("highlight")).value<QColor>();

    // A video can't show inside text: there it is a link that plays it
    QString source = forClipboard ? Markdown::unlinkify(markdown) : Markdown::linkify(markdown);
    // Qt's Markdown reader drops images without alt text: their file name
    static const QRegularExpression unnamed(QStringLiteral(R"(!\[\]\(([^)\s]*?)([^/)\s]+)\))"));
    source.replace(unnamed, QStringLiteral("![\\2](\\1\\2)"));
    source.replace(kVideoImage, QStringLiteral("[▶ \\1](\\2)"));

    QTextDocument doc;
    QFont font = QGuiApplication::font();
    font.setPixelSize(pixelSize);
    doc.setDefaultFont(font);
    doc.setMarkdown(source);

    // Task list items: out of their list, a box to tick in front
    int task = firstTask;
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        const QTextBlockFormat::MarkerType marker = block.blockFormat().marker();
        if (marker == QTextBlockFormat::MarkerType::NoMarker)
            continue;
        const bool done = marker == QTextBlockFormat::MarkerType::Checked;
        int indent = block.blockFormat().indent();
        if (QTextList *list = block.textList()) {
            indent = list->format().indent();
            list->remove(block);
        }
        // The format as it is now, out of the list (the old one would put
        // the block back into it)
        QTextBlockFormat format = block.blockFormat();
        format.clearProperty(QTextFormat::ObjectIndex);
        format.setIndent(indent);
        format.setMarker(QTextBlockFormat::MarkerType::NoMarker);
        QTextCursor cursor(block);
        cursor.setBlockFormat(format);
        if (done) { // the text of a done task fades and is struck out
            cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            QTextCharFormat struck;
            struck.setFontStrikeOut(true);
            if (mutedColor.isValid())
                struck.setForeground(mutedColor);
            cursor.mergeCharFormat(struck);
            cursor.clearSelection();
            cursor.movePosition(QTextCursor::StartOfBlock);
        }
        QTextCharFormat box;
        if (!forClipboard) {
            box.setAnchor(true);
            box.setAnchorHref(QStringLiteral("task:%1").arg(task));
            if (linkColor.isValid())
                box.setForeground(done ? linkColor : QColor(colors.value(QStringLiteral("text")).value<QColor>()));
        }
        box.setFontWeight(QFont::Bold);
        cursor.insertText(done ? QStringLiteral("☑") : QStringLiteral("☐"), box);
        cursor.insertText(QStringLiteral(" "), QTextCharFormat());
        ++task;
    }

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
                if (format.anchorHref().startsWith(QLatin1String("task:")))
                    continue;
                if (linkColor.isValid())
                    format.setForeground(linkColor);
                else
                    format.clearForeground(); // the pasting app's link colour
                if (format.anchorHref().startsWith(QLatin1String("note:")))
                    format.setFontUnderline(false);
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
        const bool taskRun = next.isValid() && !inList && block.blockFormat().indent() > 0
                             && next.blockFormat().indent() > 0;
        if ((inList && !listEnds) || taskRun || QTextCursor(block).currentTable())
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

    const QString html = doc.toHtml();
    // For the clipboard only the body, without Qt's fonts, so the text takes
    // on the look of wherever it is pasted
    return forClipboard ? bodyOf(html) : html;
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

// The note as pieces for display, see the header. `emit` gets each
// piece: text runs as Markdown, the rest as they are.
namespace {

struct Piece {
    enum Kind { Text, Video, Code, Embed, File } kind = Text;
    QString markdown;
    QString target; // video file, or wiki link target
    QString name;
    QString language;
    int line = 0;
    bool expanded = false;
};

QList<Piece> piecesOf(const QString &markdown, const std::function<QString(const QString &)> &resolve,
                      const std::function<bool(const QString &)> &isVideo)
{
    QList<Piece> pieces;
    QStringList chunk;
    const auto flush = [&] {
        const QString md = chunk.join(QLatin1Char('\n'));
        chunk.clear();
        if (!md.trimmed().isEmpty())
            pieces.append({Piece::Text, md});
    };
    const QStringList lines = markdown.split(QLatin1Char('\n'));
    const QList<Markdown::Fence> fences = Markdown::fences(markdown);
    qsizetype fence = 0;
    for (int i = 0; i < lines.size(); ++i) {
        if (fence < fences.size() && fences.at(fence).startLine == i) {
            const Markdown::Fence &f = fences.at(fence++);
            flush();
            Piece code{Piece::Code, f.code};
            code.language = f.language;
            code.line = f.startLine;
            pieces.append(code);
            i = f.endLine;
            continue;
        }
        const QString &line = lines.at(i);
        const QRegularExpressionMatch media = kMediaLine.match(line);
        if (media.hasMatch()) {
            const QString local = resolve(media.captured(2));
            if (!local.isEmpty() && isVideo(local)) {
                flush();
                Piece video{Piece::Video};
                video.target = local;
                video.name = media.captured(1);
                pieces.append(video);
                continue;
            }
        }
        // A link alone on its line to a file of the project: an attachment
        static const QRegularExpression fileLine(QStringLiteral(R"(^\s*\[([^\]]+)\]\(([^)\s]+)\)\s*$)"));
        const QRegularExpressionMatch file = fileLine.match(line);
        if (file.hasMatch() && !file.captured(2).contains(QLatin1Char(':'))) {
            const QString local = resolve(file.captured(2));
            if (!local.isEmpty() && QFileInfo(local).isFile() && !isVideo(local)) {
                flush();
                Piece attachment{Piece::File};
                attachment.target = local;
                attachment.name = file.captured(1);
                attachment.line = i;
                pieces.append(attachment);
                continue;
            }
        }
        const QRegularExpressionMatch wiki = kWikiLine.match(line);
        if (wiki.hasMatch()) {
            flush();
            Piece embed{Piece::Embed};
            embed.target = wiki.captured(2).trimmed();
            embed.name = wiki.captured(3).trimmed();
            embed.line = i;
            embed.expanded = !wiki.captured(1).isEmpty();
            pieces.append(embed);
            continue;
        }
        chunk << line;
    }
    flush();
    return pieces;
}

} // namespace

QVariantList BoardModel::renderSegments(const QString &markdown, double maxWidth, int pixelSize,
                                        const QVariantMap &colors, const QStringList &highlight) const
{
    QVariantList segments;
    int task = 0;
    const QList<Piece> pieces = piecesOf(
        markdown, [this](const QString &t) { return resolveLocal(t); },
        [](const QString &path) { return mediaType(path) == QLatin1String("video"); });
    for (const Piece &piece : pieces) {
        switch (piece.kind) {
        case Piece::Text:
            segments.append(QVariantMap{
                {QStringLiteral("kind"), QStringLiteral("text")},
                {QStringLiteral("html"), renderHtml(piece.markdown, maxWidth, pixelSize, colors, false, highlight, task)}});
            task += int(Markdown::tasks(piece.markdown).size());
            break;
        case Piece::Video:
            segments.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("video")},
                                        {QStringLiteral("url"), QUrl::fromLocalFile(piece.target)},
                                        {QStringLiteral("name"), piece.name}});
            break;
        case Piece::Code: {
            const QString language = Highlight::language(piece.language, piece.markdown);
            segments.append(QVariantMap{
                {QStringLiteral("kind"), QStringLiteral("code")},
                {QStringLiteral("code"), piece.markdown},
                {QStringLiteral("language"), piece.language.isEmpty() ? QString() : language},
                {QStringLiteral("html"), QStringLiteral("<pre style=\"margin:0; white-space:pre-wrap;\">")
                                             + Highlight::toHtml(piece.markdown, language, colors)
                                             + QStringLiteral("</pre>")}});
            break;
        }
        case Piece::File: {
            const QFileInfo fi(piece.target);
            segments.append(QVariantMap{
                {QStringLiteral("kind"), QStringLiteral("file")},
                {QStringLiteral("name"), piece.name},
                {QStringLiteral("url"), QUrl::fromLocalFile(piece.target)},
                {QStringLiteral("folder"), QUrl::fromLocalFile(fi.absolutePath())},
                {QStringLiteral("extension"), fi.suffix().toUpper()},
                {QStringLiteral("size"), QLocale().formattedDataSize(fi.size(), 1, QLocale::DataSizeTraditionalFormat)}});
            break;
        }
        case Piece::Embed:
            segments.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("embed")},
                                        {QStringLiteral("target"), piece.target},
                                        {QStringLiteral("alias"), piece.name},
                                        {QStringLiteral("line"), piece.line},
                                        {QStringLiteral("expanded"), piece.expanded}});
            break;
        }
    }
    return segments;
}

void BoardModel::setEmbed(int row, int line, bool embedded)
{
    if (!valid(row))
        return;
    setText(row, Markdown::setEmbed(m_items.at(row).text, line, embedded));
}

// A note as HTML for other applications: rich text with the images
// inside, code in coloured blocks, tasks as ☐ and ☑
static QString clipboardHtmlOf(const BoardModel *board, const QString &markdown,
                               const std::function<QString(const QString &, bool)> &render,
                               const std::function<QString(const QString &)> &resolve,
                               const std::function<bool(const QString &)> &isVideo)
{
    Q_UNUSED(board)
    QString html;
    for (const Piece &piece : piecesOf(markdown, resolve, isVideo)) {
        switch (piece.kind) {
        case Piece::Text:
            html += render(piece.markdown, false);
            break;
        case Piece::Video:
            html += QStringLiteral("<p><a href=\"%1\">▶ %2</a></p>")
                        .arg(QUrl::fromLocalFile(piece.target).toString(), piece.name.toHtmlEscaped());
            break;
        case Piece::Code: {
            const QString language = Highlight::language(piece.language, piece.markdown);
            html += QStringLiteral("<pre style=\"background:#f6f8fa; border:1px solid #d0d7de; border-radius:6px; "
                                   "padding:8px 12px; font-family:Consolas,'JetBrains Mono',monospace; "
                                   "font-size:13px; color:#1f2328; white-space:pre-wrap;\">")
                    + Highlight::toHtml(piece.markdown, language, pasteCodeColors()) + QStringLiteral("</pre>");
            break;
        }
        case Piece::File:
            html += QStringLiteral("<p><a href=\"%1\">📎 %2</a></p>")
                        .arg(QUrl::fromLocalFile(piece.target).toString(), piece.name.toHtmlEscaped());
            break;
        case Piece::Embed:
            html += QStringLiteral("<p><b>↪ %1</b></p>")
                        .arg((piece.name.isEmpty() ? piece.target : piece.name).toHtmlEscaped());
            break;
        }
    }
    return QStringLiteral("<html><body>") + html + QStringLiteral("</body></html>");
}

QString BoardModel::clipboardHtml(const QString &markdown) const
{
    return clipboardHtmlOf(
        this, markdown,
        [this](const QString &md, bool) { return renderHtml(md, 720, 14, {}, true); },
        [this](const QString &t) { return resolveLocal(t); },
        [](const QString &path) { return mediaType(path) == QLatin1String("video"); });
}

// Writes every card into one Markdown file, in reading order (top to bottom,
// left to right), so a canvas can be shared as a plain document.
QString BoardModel::exportMarkdown()
{
    if (m_projectPath.isEmpty())
        return {};
    save();
    QList<BoardItem> ordered;
    for (const BoardItem &it : std::as_const(m_items)) {
        if (it.type != QLatin1String("stroke") && it.type != QLatin1String("section"))
            ordered.append(it);
    }
    // Section by section, then reading order inside each
    std::stable_sort(ordered.begin(), ordered.end(), [this](const BoardItem &a, const BoardItem &b) {
        const int sa = m_sectionOrder.value(m_sectionOf.value(a.id), -1);
        const int sb = m_sectionOrder.value(m_sectionOf.value(b.id), -1);
        if (sa != sb)
            return sa < sb;
        return m_readingOrder.value(a.id) < m_readingOrder.value(b.id);
    });
    QStringList parts;
    QString section = QStringLiteral("\x01");
    for (const BoardItem &it : std::as_const(ordered)) {
        const QString in = m_sectionOf.value(it.id);
        if (in != section) {
            section = in;
            const int row = indexOf(in);
            if (row >= 0)
                parts << QStringLiteral("## ") + titleFor(m_items.at(row));
        }
        if (!it.text.trimmed().isEmpty())
            parts << it.text.trimmed();
    }
    const QString path = QDir(m_projectPath).filePath(QStringLiteral("export.md"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(parts.join(QStringLiteral("\n\n")).toUtf8() + '\n');
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
