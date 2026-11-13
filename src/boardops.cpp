// BoardModel, continued: what works on several items at once, connectors,
// mindmaps, sections, templates, and the state derived from all of them.

#include "boardmodel.h"

#include "markdown.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QPainter>
#include <QPainterPathStroker>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>

namespace {

QString newId()
{
    return QUuid::createUuid().toString(QUuid::Id128).left(12);
}

double rounded(double v)
{
    return std::round(v * 10) / 10;
}

bool isSection(const BoardItem &it) { return it.type == QLatin1String("section"); }
bool isStroke(const BoardItem &it) { return it.type == QLatin1String("stroke"); }

constexpr double kChildGap = 80;  // between a mindmap parent and its children
constexpr double kSiblingGap = 24; // between siblings

} // namespace

// ---------------------------------------------------------------------------
// JSON

QJsonObject BoardModel::toJson(const BoardItem &it, bool inlineText)
{
    QJsonObject o{
        {QStringLiteral("id"), it.id},
        {QStringLiteral("type"), it.type},
        {QStringLiteral("x"), rounded(it.x)},
        {QStringLiteral("y"), rounded(it.y)},
        {QStringLiteral("w"), rounded(it.w)},
        {QStringLiteral("h"), rounded(it.h)},
        {QStringLiteral("z"), it.z},
    };
    if (!it.color.isEmpty())
        o.insert(QStringLiteral("color"), it.color);
    if (it.type == QLatin1String("note") && !inlineText)
        o.insert(QStringLiteral("file"), QStringLiteral("notes/%1.md").arg(it.id));
    else if (!it.text.isEmpty())
        o.insert(QStringLiteral("text"), it.text);
    if (!it.shape.isEmpty())
        o.insert(QStringLiteral("shape"), it.shape);
    if (it.size > 0)
        o.insert(QStringLiteral("size"), rounded(it.size));
    if (it.collapsed)
        o.insert(QStringLiteral("collapsed"), true);
    if (!it.source.isEmpty())
        o.insert(QStringLiteral("source"), it.source);
    if (!it.points.isEmpty()) {
        QJsonArray points;
        for (const QPointF &p : it.points) {
            points.append(rounded(p.x()));
            points.append(rounded(p.y()));
        }
        o.insert(QStringLiteral("points"), points);
    }
    return o;
}

BoardItem BoardModel::itemFromJson(const QJsonObject &o)
{
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
    it.text = o.value(QStringLiteral("text")).toString();
    it.source = o.value(QStringLiteral("source")).toString();
    it.shape = o.value(QStringLiteral("shape")).toString();
    it.size = o.value(QStringLiteral("size")).toDouble();
    it.collapsed = o.value(QStringLiteral("collapsed")).toBool();
    const QJsonArray points = o.value(QStringLiteral("points")).toArray();
    for (qsizetype i = 0; i + 1 < points.size(); i += 2)
        it.points.append(QPointF(points.at(i).toDouble(), points.at(i + 1).toDouble()));
    return it;
}

QJsonObject BoardModel::toJson(const BoardLink &link)
{
    QJsonObject o{
        {QStringLiteral("id"), link.id},
        {QStringLiteral("from"), link.from},
        {QStringLiteral("to"), link.to},
        {QStringLiteral("style"), link.style},
        {QStringLiteral("arrow"), link.arrow},
    };
    if (!link.label.isEmpty())
        o.insert(QStringLiteral("label"), link.label);
    if (!link.color.isEmpty())
        o.insert(QStringLiteral("color"), link.color);
    if (link.dashed)
        o.insert(QStringLiteral("dashed"), true);
    if (!link.fromSide.isEmpty())
        o.insert(QStringLiteral("fromSide"), link.fromSide);
    if (!link.toSide.isEmpty())
        o.insert(QStringLiteral("toSide"), link.toSide);
    return o;
}

BoardLink BoardModel::linkFromJson(const QJsonObject &o)
{
    BoardLink link;
    link.id = o.value(QStringLiteral("id")).toString();
    if (link.id.isEmpty())
        link.id = newId();
    link.from = o.value(QStringLiteral("from")).toString();
    link.to = o.value(QStringLiteral("to")).toString();
    link.label = o.value(QStringLiteral("label")).toString();
    link.style = o.value(QStringLiteral("style")).toString(QStringLiteral("curve"));
    link.arrow = o.value(QStringLiteral("arrow")).toString(QStringLiteral("end"));
    link.color = o.value(QStringLiteral("color")).toString();
    link.dashed = o.value(QStringLiteral("dashed")).toBool();
    link.fromSide = o.value(QStringLiteral("fromSide")).toString();
    link.toSide = o.value(QStringLiteral("toSide")).toString();
    return link;
}

QVariantList BoardModel::linksVariant() const
{
    QVariantList list;
    list.reserve(m_links.size());
    for (const BoardLink &link : m_links) {
        list.append(QVariantMap{
            {QStringLiteral("id"), link.id},
            {QStringLiteral("from"), link.from},
            {QStringLiteral("to"), link.to},
            {QStringLiteral("fromSide"), link.fromSide},
            {QStringLiteral("toSide"), link.toSide},
            {QStringLiteral("label"), link.label},
            {QStringLiteral("style"), link.style},
            {QStringLiteral("arrow"), link.arrow},
            {QStringLiteral("color"), link.color},
            {QStringLiteral("dashed"), link.dashed},
            {QStringLiteral("hidden"), m_hidden.contains(link.from) || m_hidden.contains(link.to)},
        });
    }
    return list;
}

// ---------------------------------------------------------------------------
// Derived state

void BoardModel::emitAll(const QList<int> &roles)
{
    if (m_items.isEmpty())
        return;
    emit dataChanged(index(0), index(int(m_items.size()) - 1), roles);
}

void BoardModel::updateDerived()
{
    // Folded mindmap branches: everything a collapsed item's connectors lead
    // to, and on from there
    QHash<QString, QStringList> children;
    for (const BoardLink &link : std::as_const(m_links)) {
        if (link.from != link.to && !children.value(link.from).contains(link.to))
            children[link.from].append(link.to);
    }
    QSet<QString> hidden;
    QHash<QString, int> descendants;
    QHash<QString, int> childCount;
    for (auto it = children.cbegin(); it != children.cend(); ++it)
        childCount.insert(it.key(), int(it.value().size()));
    for (const BoardItem &it : std::as_const(m_items)) {
        if (!it.collapsed)
            continue;
        QSet<QString> seen{it.id};
        QStringList todo = children.value(it.id);
        while (!todo.isEmpty()) {
            const QString next = todo.takeLast();
            if (seen.contains(next))
                continue;
            seen.insert(next);
            hidden.insert(next);
            todo += children.value(next);
        }
        descendants.insert(it.id, int(seen.size()) - 1);
    }

    // Reading order: rows of 120 px, left to right
    QList<int> order(m_items.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [this](int a, int b) {
        const BoardItem &ia = m_items.at(a), &ib = m_items.at(b);
        const int ra = int(std::floor(ia.y / 120.0)), rb = int(std::floor(ib.y / 120.0));
        return ra != rb ? ra < rb : ia.x < ib.x;
    });
    QHash<QString, int> reading;
    for (int i = 0; i < order.size(); ++i)
        reading.insert(m_items.at(order.at(i)).id, i);

    // Each item belongs to the smallest section its centre lies in
    QHash<QString, QString> sectionOf;
    QHash<QString, int> sectionOrder;
    int nth = 0;
    for (int i : std::as_const(order)) {
        if (isSection(m_items.at(i)))
            sectionOrder.insert(m_items.at(i).id, nth++);
    }
    for (const BoardItem &it : std::as_const(m_items)) {
        if (isSection(it))
            continue;
        const QPointF centre(it.x + it.w / 2, it.y + it.h / 2);
        double best = -1;
        for (const BoardItem &s : std::as_const(m_items)) {
            if (!isSection(s) || !QRectF(s.x, s.y, s.w, s.h).contains(centre))
                continue;
            const double area = s.w * s.h;
            if (best < 0 || area < best) {
                best = area;
                sectionOf.insert(it.id, s.id);
            }
        }
    }

    const bool treeChanged = hidden != m_hidden || descendants != m_descendants || childCount != m_childCount;
    const bool sectionsChanged = sectionOf != m_sectionOf || sectionOrder != m_sectionOrder;
    const bool readingChanged = reading != m_readingOrder;
    m_hidden = std::move(hidden);
    m_descendants = std::move(descendants);
    m_childCount = std::move(childCount);
    m_sectionOf = std::move(sectionOf);
    m_sectionOrder = std::move(sectionOrder);
    m_readingOrder = std::move(reading);

    QList<int> roles;
    if (treeChanged)
        roles << HiddenRole << DescendantsRole << ChildCountRole;
    if (sectionsChanged)
        roles << SectionIdRole << SectionTitleRole << SectionOrderRole;
    if (readingChanged)
        roles << ReadingOrderRole;
    if (!roles.isEmpty())
        emitAll(roles);
    if (treeChanged)
        emit linksChanged(); // their hidden flags
}

void BoardModel::structureChanged()
{
    updateDerived();
    emit linksChanged();
    markDirty();
}

void BoardModel::textsChanged()
{
    QVariantList edges;
    QSet<QString> seen;
    const QString project = QFileInfo(m_projectPath).fileName();
    for (const BoardItem &it : std::as_const(m_items)) {
        if (it.type != QLatin1String("note") || !it.text.contains(QLatin1String("[[")))
            continue;
        for (const Markdown::WikiLink &link : Markdown::wikiLinks(it.text)) {
            QString target = link.target;
            const qsizetype slash = target.indexOf(QLatin1Char('/'));
            if (slash > 0) {
                if (target.left(slash).compare(project, Qt::CaseInsensitive) != 0)
                    continue; // another project's note
                target = target.mid(slash + 1);
            }
            const QString to = noteByTitle(target);
            const QString key = it.id + QLatin1Char('>') + to;
            if (to.isEmpty() || to == it.id || seen.contains(key))
                continue;
            seen.insert(key);
            edges.append(QVariantMap{{QStringLiteral("from"), it.id}, {QStringLiteral("to"), to}});
        }
    }
    if (edges != m_wikiEdges) {
        m_wikiEdges = edges;
        emit wikiEdgesChanged();
    }
}

// ---------------------------------------------------------------------------
// Creating

int BoardModel::addItem(const QString &type, double x, double y, double w, double h, const QString &kind,
                        const QString &text)
{
    if (m_projectPath.isEmpty())
        return -1;
    BoardItem it;
    it.id = newId();
    it.type = type;
    it.x = x;
    it.y = y;
    it.w = std::max(20.0, w);
    it.h = std::max(20.0, h);
    it.shape = kind;
    it.text = text;
    // Sections lie under the rest, so a new one goes to the very bottom
    it.z = type == QLatin1String("section") ? bottomZ() - 1 : topZ() + 1;
    if (type == QLatin1String("text"))
        it.size = 24;
    pushUndo();
    return appendItem(it);
}

namespace {

// Ramer-Douglas-Peucker: the points that matter for the line's shape
void simplify(const QList<QPointF> &pts, qsizetype first, qsizetype last, double tolerance, QList<bool> &keep)
{
    if (last <= first + 1)
        return;
    const QPointF a = pts.at(first), b = pts.at(last);
    const QPointF ab = b - a;
    const double len = std::hypot(ab.x(), ab.y());
    double worst = -1;
    qsizetype index = -1;
    for (qsizetype i = first + 1; i < last; ++i) {
        const QPointF ap = pts.at(i) - a;
        const double d = len > 0 ? std::abs(ab.x() * ap.y() - ab.y() * ap.x()) / len : std::hypot(ap.x(), ap.y());
        if (d > worst) {
            worst = d;
            index = i;
        }
    }
    if (worst > tolerance) {
        keep[index] = true;
        simplify(pts, first, index, tolerance, keep);
        simplify(pts, index, last, tolerance, keep);
    }
}

} // namespace

QList<QPointF> BoardModel::smoothStroke(const QList<QPointF> &points, double tolerance)
{
    if (points.size() < 3)
        return points;
    QList<bool> keep(points.size(), false);
    keep.first() = keep.last() = true;
    simplify(points, 0, points.size() - 1, tolerance, keep);
    QList<QPointF> kept;
    for (qsizetype i = 0; i < points.size(); ++i) {
        if (keep.at(i))
            kept.append(points.at(i));
    }
    // Chaikin, twice: corners cut into curves, the ends stay where they are
    for (int pass = 0; pass < 2 && kept.size() > 2; ++pass) {
        QList<QPointF> next{kept.first()};
        for (qsizetype i = 0; i + 1 < kept.size(); ++i) {
            const QPointF p = kept.at(i), q = kept.at(i + 1);
            next.append(p * 0.75 + q * 0.25);
            next.append(p * 0.25 + q * 0.75);
        }
        next.append(kept.last());
        kept = next;
    }
    return kept;
}

QString BoardModel::strokeOutline(const QVariantList &points, double width) const
{
    if (points.isEmpty())
        return {};
    QPainterPath line(points.first().toPointF());
    for (qsizetype i = 1; i < points.size(); ++i)
        line.lineTo(points.at(i).toPointF());
    if (points.size() == 1) // a dot
        line.lineTo(points.first().toPointF() + QPointF(0.01, 0));
    QPainterPathStroker stroker;
    stroker.setWidth(width);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    const QPainterPath outline = stroker.createStroke(line);
    QString d;
    d.reserve(outline.elementCount() * 16);
    const auto num = [](double v) { return QString::number(v, 'f', 2); };
    for (int i = 0; i < outline.elementCount(); ++i) {
        const QPainterPath::Element e = outline.elementAt(i);
        switch (e.type) {
        case QPainterPath::MoveToElement:
            d += QLatin1Char('M') + num(e.x) + QLatin1Char(' ') + num(e.y);
            break;
        case QPainterPath::LineToElement:
            d += QLatin1Char('L') + num(e.x) + QLatin1Char(' ') + num(e.y);
            break;
        case QPainterPath::CurveToElement:
            if (i + 2 < outline.elementCount()) {
                const QPainterPath::Element c2 = outline.elementAt(i + 1), end = outline.elementAt(i + 2);
                d += QLatin1Char('C') + num(e.x) + QLatin1Char(' ') + num(e.y) + QLatin1Char(' ') + num(c2.x)
                     + QLatin1Char(' ') + num(c2.y) + QLatin1Char(' ') + num(end.x) + QLatin1Char(' ') + num(end.y);
                i += 2;
            }
            break;
        case QPainterPath::CurveToDataElement:
            break;
        }
    }
    return d;
}

int BoardModel::addStroke(const QVariantList &points, const QString &kind, const QString &color, double width)
{
    if (m_projectPath.isEmpty() || points.size() < 2)
        return -1;
    QList<QPointF> raw;
    for (const QVariant &v : points)
        raw.append(v.toPointF());
    const QList<QPointF> list = smoothStroke(raw, 0.8);
    QRectF bounds;
    for (const QPointF &p : list)
        bounds = bounds.isNull() ? QRectF(p, QSizeF(0.01, 0.01)) : bounds.united(QRectF(p, QSizeF(0.01, 0.01)));
    const double pad = width / 2 + 2;
    bounds.adjust(-pad, -pad, pad, pad);
    BoardItem it;
    it.id = newId();
    it.type = QStringLiteral("stroke");
    it.shape = kind;
    it.color = color;
    it.size = width;
    it.x = bounds.x();
    it.y = bounds.y();
    it.w = bounds.width();
    it.h = bounds.height();
    it.z = topZ() + 1;
    for (const QPointF &p : std::as_const(list))
        it.points.append(p - bounds.topLeft());
    pushUndo();
    return appendItem(it);
}

void BoardModel::setShape(int row, const QString &shape)
{
    if (!valid(row) || m_items.at(row).shape == shape)
        return;
    pushUndo();
    m_items[row].shape = shape;
    emitRow(row, {ShapeRole});
    markDirty();
}

void BoardModel::setSize(int row, double size)
{
    if (!valid(row) || std::abs(m_items.at(row).size - size) < 0.01)
        return;
    pushUndo();
    m_items[row].size = size;
    emitRow(row, {SizeRole});
    markDirty();
}

void BoardModel::toggleTask(int row, int task)
{
    if (!valid(row))
        return;
    const QString text = Markdown::toggleTask(m_items.at(row).text, task);
    if (text == m_items.at(row).text)
        return;
    pushUndo();
    m_items[row].text = text;
    emitRow(row, kTextRoles);
    markDirty();
}

// ---------------------------------------------------------------------------
// Several at once

void BoardModel::moveItems(const QVariantList &positions)
{
    bool pushed = false;
    for (const QVariant &v : positions) {
        const QVariantMap p = v.toMap();
        const int row = indexOf(p.value(QStringLiteral("id")).toString());
        if (!valid(row))
            continue;
        const double x = p.value(QStringLiteral("x")).toDouble();
        const double y = p.value(QStringLiteral("y")).toDouble();
        if (std::abs(m_items.at(row).x - x) < 0.01 && std::abs(m_items.at(row).y - y) < 0.01)
            continue;
        if (!pushed) {
            pushUndo();
            pushed = true;
        }
        m_items[row].x = x;
        m_items[row].y = y;
        emitRow(row, {PosXRole, PosYRole});
    }
    if (pushed) {
        updateDerived();
        markDirty();
    }
}

void BoardModel::removeItems(const QStringList &ids)
{
    const QSet<QString> doomed(ids.cbegin(), ids.cend());
    bool any = false;
    for (const BoardItem &it : std::as_const(m_items))
        any = any || doomed.contains(it.id);
    if (!any)
        return;
    pushUndo();
    for (int row = int(m_items.size()) - 1; row >= 0; --row) {
        if (!doomed.contains(m_items.at(row).id))
            continue;
        beginRemoveRows({}, row, row);
        m_items.removeAt(row);
        endRemoveRows();
    }
    m_links.removeIf([&](const BoardLink &l) { return doomed.contains(l.from) || doomed.contains(l.to); });
    emit countChanged();
    structureChanged();
    textsChanged();
}

BoardState BoardModel::stateOf(const QStringList &ids) const
{
    const QSet<QString> wanted(ids.cbegin(), ids.cend());
    BoardState state;
    for (const BoardItem &it : m_items) {
        if (wanted.contains(it.id))
            state.items.append(it);
    }
    for (const BoardLink &link : m_links) {
        if (wanted.contains(link.from) && wanted.contains(link.to))
            state.links.append(link);
    }
    return state;
}

// Adds copies of the items and the links among them, moved by (dx, dy),
// under new ids. Call pushUndo() first.
QStringList BoardModel::insertState(const BoardState &state, double dx, double dy)
{
    QHash<QString, QString> ids;
    QStringList added;
    int z = topZ();
    int low = bottomZ();
    QList<BoardItem> items = state.items;
    std::sort(items.begin(), items.end(), [](const BoardItem &a, const BoardItem &b) { return a.z < b.z; });
    for (BoardItem it : std::as_const(items)) {
        const QString id = newId();
        ids.insert(it.id, id);
        it.id = id;
        it.x += dx;
        it.y += dy;
        it.z = isSection(it) ? --low : ++z;
        const int row = int(m_items.size());
        beginInsertRows({}, row, row);
        m_items.append(it);
        endInsertRows();
        added << id;
    }
    for (BoardLink link : state.links) {
        if (!ids.contains(link.from) || !ids.contains(link.to))
            continue;
        link.id = newId();
        link.from = ids.value(link.from);
        link.to = ids.value(link.to);
        m_links.append(link);
    }
    emit countChanged();
    structureChanged();
    textsChanged();
    return added;
}

QStringList BoardModel::duplicateItems(const QStringList &ids)
{
    const BoardState state = stateOf(ids);
    if (state.items.isEmpty())
        return {};
    pushUndo();
    return insertState(state, 28, 28);
}

void BoardModel::setColorOf(const QStringList &ids, const QString &color)
{
    bool pushed = false;
    for (const QString &id : ids) {
        const int row = indexOf(id);
        if (!valid(row) || m_items.at(row).color == color)
            continue;
        if (!pushed) {
            pushUndo();
            pushed = true;
        }
        m_items[row].color = color;
        emitRow(row, {ColorRole});
    }
    if (pushed)
        markDirty();
}

void BoardModel::align(const QStringList &ids, const QString &edge)
{
    const QRectF bounds = boundsOf(ids);
    if (ids.size() < 2 || bounds.isEmpty())
        return;
    QVariantList positions;
    for (const QString &id : ids) {
        const int row = indexOf(id);
        if (!valid(row))
            continue;
        const BoardItem &it = m_items.at(row);
        double x = it.x, y = it.y;
        if (edge == QLatin1String("left"))
            x = bounds.left();
        else if (edge == QLatin1String("hcenter"))
            x = bounds.center().x() - it.w / 2;
        else if (edge == QLatin1String("right"))
            x = bounds.right() - it.w;
        else if (edge == QLatin1String("top"))
            y = bounds.top();
        else if (edge == QLatin1String("vcenter"))
            y = bounds.center().y() - it.h / 2;
        else if (edge == QLatin1String("bottom"))
            y = bounds.bottom() - it.h;
        positions.append(QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("x"), x}, {QStringLiteral("y"), y}});
    }
    moveItems(positions);
}

// Equal gaps between the items, the outermost ones staying where they are
bool BoardModel::matchSize(const QStringList &ids, const QString &sourceId, const QString &what)
{
    const int source = indexOf(sourceId);
    if (!valid(source))
        return false;
    const double w = m_items.at(source).w, h = m_items.at(source).h;
    const bool width = what != QLatin1String("height"), height = what != QLatin1String("width");
    bool pushed = false;
    for (const QString &id : ids) {
        const int row = indexOf(id);
        if (!valid(row) || row == source || isStroke(m_items.at(row)))
            continue;
        const BoardItem &it = m_items.at(row);
        const double newW = width ? w : it.w;
        const double newH = height && it.type != QLatin1String("text") ? h : it.h;
        if (qFuzzyCompare(newW, it.w) && qFuzzyCompare(newH, it.h))
            continue;
        if (!pushed) {
            pushUndo();
            pushed = true;
        }
        m_items[row].w = newW;
        m_items[row].h = newH;
        emitRow(row, {WidthRole, HeightRole});
    }
    if (pushed)
        markDirty();
    return pushed;
}

void BoardModel::distribute(const QStringList &ids, bool horizontally)
{
    QList<int> rows;
    for (const QString &id : ids) {
        const int row = indexOf(id);
        if (valid(row))
            rows << row;
    }
    if (rows.size() < 3)
        return;
    std::sort(rows.begin(), rows.end(), [&](int a, int b) {
        return horizontally ? m_items.at(a).x < m_items.at(b).x : m_items.at(a).y < m_items.at(b).y;
    });
    const BoardItem &first = m_items.at(rows.first());
    const BoardItem &last = m_items.at(rows.last());
    double total = 0;
    for (int row : std::as_const(rows))
        total += horizontally ? m_items.at(row).w : m_items.at(row).h;
    const double span = horizontally ? (last.x + last.w - first.x) : (last.y + last.h - first.y);
    const double gap = (span - total) / (rows.size() - 1);
    double at = horizontally ? first.x : first.y;
    QVariantList positions;
    for (int row : std::as_const(rows)) {
        const BoardItem &it = m_items.at(row);
        positions.append(QVariantMap{{QStringLiteral("id"), it.id},
                                     {QStringLiteral("x"), horizontally ? at : it.x},
                                     {QStringLiteral("y"), horizontally ? it.y : at}});
        at += (horizontally ? it.w : it.h) + gap;
    }
    moveItems(positions);
}

// ---------------------------------------------------------------------------
// Connectors

namespace {

bool isSide(const QString &side)
{
    return side.isEmpty() || side == QLatin1String("top") || side == QLatin1String("right")
           || side == QLatin1String("bottom") || side == QLatin1String("left");
}

} // namespace

QString BoardModel::addLink(const QString &from, const QString &to, const QString &fromSide, const QString &toSide)
{
    if (indexOf(from) < 0 || indexOf(to) < 0 || !isSide(fromSide) || !isSide(toSide))
        return {};
    for (qsizetype i = 0; i < m_links.size(); ++i) {
        const BoardLink &l = m_links.at(i);
        // Already there
        if (l.from == from && l.to == to && l.fromSide == fromSide && l.toSide == toSide)
            return l.id;
        // The same connection the other way round: one connector, pointing
        // both ways
        if (from != to && l.from == to && l.to == from && l.fromSide == toSide && l.toSide == fromSide) {
            if (l.arrow == QLatin1String("end")) {
                pushUndo();
                m_links[i].arrow = QStringLiteral("both");
                emit linksChanged();
                markDirty();
            }
            return l.id;
        }
    }
    pushUndo();
    BoardLink link;
    link.id = newId();
    link.from = from;
    link.to = to;
    link.fromSide = fromSide;
    link.toSide = toSide;
    m_links.append(link);
    structureChanged();
    return link.id;
}

void BoardModel::removeLink(const QString &id)
{
    for (qsizetype i = 0; i < m_links.size(); ++i) {
        if (m_links.at(i).id != id)
            continue;
        pushUndo();
        m_links.removeAt(i);
        structureChanged();
        return;
    }
}

void BoardModel::setLink(const QString &id, const QVariantMap &values)
{
    for (qsizetype i = 0; i < m_links.size(); ++i) {
        if (m_links.at(i).id != id)
            continue;
        BoardLink link = m_links.at(i);
        // Its ends: onto another item, or onto another side
        for (const char *end : {"from", "to"}) {
            const QString key = QLatin1String(end);
            if (values.contains(key) && indexOf(values.value(key).toString()) >= 0)
                (key == QLatin1String("from") ? link.from : link.to) = values.value(key).toString();
            const QString sideKey = key + QStringLiteral("Side");
            if (values.contains(sideKey) && isSide(values.value(sideKey).toString()))
                (key == QLatin1String("from") ? link.fromSide : link.toSide) = values.value(sideKey).toString();
        }
        if (values.contains(QStringLiteral("label")))
            link.label = values.value(QStringLiteral("label")).toString();
        if (values.contains(QStringLiteral("style")))
            link.style = values.value(QStringLiteral("style")).toString();
        if (values.contains(QStringLiteral("arrow")))
            link.arrow = values.value(QStringLiteral("arrow")).toString();
        if (values.contains(QStringLiteral("color")))
            link.color = values.value(QStringLiteral("color")).toString();
        if (values.contains(QStringLiteral("dashed")))
            link.dashed = values.value(QStringLiteral("dashed")).toBool();
        const BoardLink &old = m_links.at(i);
        if (link.label == old.label && link.style == old.style && link.arrow == old.arrow && link.color == old.color
            && link.dashed == old.dashed && link.from == old.from && link.to == old.to
            && link.fromSide == old.fromSide && link.toSide == old.toSide)
            return;
        const bool ends = link.from != old.from || link.to != old.to;
        // Typing a label is one undo step
        pushUndo(values.size() == 1 && values.contains(QStringLiteral("label")) ? QStringLiteral("label:") + id
                                                                                  : QString());
        m_links[i] = link;
        if (ends)
            structureChanged(); // the mindmap's shape changed
        else {
            emit linksChanged();
            markDirty();
        }
        return;
    }
}

// ---------------------------------------------------------------------------
// Mindmaps

QString BoardModel::parentOf(const QString &id) const
{
    for (const BoardLink &link : m_links) {
        if (link.to == id && link.from != id)
            return link.from;
    }
    return {};
}

QStringList BoardModel::childrenOf(const QString &id) const
{
    QList<int> rows;
    for (const BoardLink &link : m_links) {
        const int row = link.from == id && link.to != id ? indexOf(link.to) : -1;
        if (row >= 0 && !rows.contains(row))
            rows << row;
    }
    std::sort(rows.begin(), rows.end(), [this](int a, int b) { return m_items.at(a).y < m_items.at(b).y; });
    QStringList ids;
    for (int row : std::as_const(rows))
        ids << m_items.at(row).id;
    return ids;
}

QString BoardModel::addConnectedNote(const QString &from, double x, double y, const QString &fromSide)
{
    const int source = indexOf(from);
    if (source < 0)
        return {};
    pushUndo();
    BoardItem note = makeNote(x, y, {});
    note.w = std::clamp(m_items.at(source).w, 200.0, 340.0);
    note.h = 120;
    note.color = m_items.at(source).color;
    appendItem(note);
    BoardLink link;
    link.id = newId();
    link.from = from;
    link.to = note.id;
    link.fromSide = isSide(fromSide) ? fromSide : QString();
    m_links.append(link);
    structureChanged();
    return note.id;
}

QString BoardModel::addChild(const QString &parentId)
{
    const int row = indexOf(parentId);
    if (row < 0)
        return {};
    if (m_items.at(row).collapsed)
        toggleCollapsed(parentId);
    const BoardItem &parent = m_items.at(row);
    double x = parent.x + parent.w + kChildGap;
    double y = parent.y;
    const QStringList children = childrenOf(parentId);
    if (!children.isEmpty()) { // below the last child
        const BoardItem &last = m_items.at(indexOf(children.last()));
        x = last.x;
        y = last.y + last.h + kSiblingGap;
    }
    return addConnectedNote(parentId, x, y);
}

QString BoardModel::addSibling(const QString &id)
{
    const int row = indexOf(id);
    if (row < 0)
        return {};
    const QString parent = parentOf(id);
    if (parent.isEmpty()) {
        pushUndo();
        const BoardItem &it = m_items.at(row);
        BoardItem note = makeNote(it.x, it.y + it.h + kSiblingGap, {});
        note.w = it.w;
        note.h = 120;
        appendItem(note);
        return note.id;
    }
    const BoardItem &it = m_items.at(row);
    // Right under this one; siblings further down make room
    const double y = it.y + it.h + kSiblingGap;
    const double x = it.x;
    const QString added = addConnectedNote(parent, x, y);
    const int newRow = indexOf(added);
    const double shift = m_items.at(newRow).h + kSiblingGap;
    for (const QString &sibling : childrenOf(parent)) {
        const int r = indexOf(sibling);
        if (sibling != added && sibling != id && m_items.at(r).y >= y - 1) {
            m_items[r].y += shift;
            emitRow(r, {PosYRole});
        }
    }
    updateDerived();
    return added;
}

void BoardModel::toggleCollapsed(const QString &id)
{
    const int row = indexOf(id);
    if (row < 0)
        return;
    pushUndo();
    m_items[row].collapsed = !m_items.at(row).collapsed;
    emitRow(row, {CollapsedRole});
    structureChanged();
}

void BoardModel::tidy(const QString &rootId)
{
    if (indexOf(rootId) < 0)
        return;
    // Heights of the subtrees first, then place each child column to the
    // right of its parent, centred on it
    QHash<QString, double> heights;
    QSet<QString> seen;
    std::function<double(const QString &)> measure = [&](const QString &id) -> double {
        seen.insert(id);
        const BoardItem &it = m_items.at(indexOf(id));
        double children = 0;
        int n = 0;
        if (!it.collapsed) {
            for (const QString &child : childrenOf(id)) {
                if (seen.contains(child))
                    continue;
                children += measure(child);
                ++n;
            }
        }
        if (n > 1)
            children += (n - 1) * kSiblingGap;
        const double h = std::max(it.h, children);
        heights.insert(id, h);
        return h;
    };
    measure(rootId);

    QVariantList positions;
    QSet<QString> placed;
    std::function<void(const QString &, double, double)> place = [&](const QString &id, double x, double top) {
        placed.insert(id);
        const BoardItem &it = m_items.at(indexOf(id));
        const double h = heights.value(id);
        const double y = top + (h - it.h) / 2;
        positions.append(QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("x"), x}, {QStringLiteral("y"), y}});
        if (it.collapsed)
            return;
        QStringList kids;
        double kidsHeight = 0;
        for (const QString &child : childrenOf(id)) {
            if (placed.contains(child) || !heights.contains(child))
                continue;
            kids << child;
            kidsHeight += heights.value(child);
        }
        if (kids.isEmpty())
            return;
        kidsHeight += (kids.size() - 1) * kSiblingGap;
        double at = top + (h - kidsHeight) / 2;
        for (const QString &child : std::as_const(kids)) {
            place(child, x + it.w + kChildGap, at);
            at += heights.value(child) + kSiblingGap;
        }
    };
    const BoardItem &root = m_items.at(indexOf(rootId));
    place(rootId, root.x, root.y + root.h / 2 - heights.value(rootId) / 2);
    moveItems(positions);
}

// ---------------------------------------------------------------------------
// Queries

QVariantMap BoardModel::getById(const QString &id) const
{
    return get(indexOf(id));
}

QRectF BoardModel::boundsOf(const QStringList &ids) const
{
    QRectF bounds;
    for (const QString &id : ids) {
        const int row = indexOf(id);
        if (valid(row)) {
            const BoardItem &it = m_items.at(row);
            bounds = bounds.united(QRectF(it.x, it.y, it.w, it.h));
        }
    }
    return bounds;
}

QString BoardModel::targetAt(double x, double y, const QString &except) const
{
    int found = -1;
    for (int i = 0; i < m_items.size(); ++i) {
        const BoardItem &it = m_items.at(i);
        if (it.id == except || isSection(it) || isStroke(it) || m_hidden.contains(it.id))
            continue;
        if (QRectF(it.x, it.y, it.w, it.h).contains(x, y) && (found < 0 || it.z >= m_items.at(found).z))
            found = i;
    }
    return found >= 0 ? m_items.at(found).id : QString();
}

QStringList BoardModel::itemsIn(double x, double y, double w, double h) const
{
    const QRectF area = QRectF(x, y, w, h).normalized();
    QStringList ids;
    for (const BoardItem &it : m_items) {
        if (m_hidden.contains(it.id))
            continue;
        const QRectF r(it.x, it.y, it.w, it.h);
        // Sections only when wholly inside, so selecting their contents
        // doesn't pick them too
        if (isSection(it) ? area.contains(r) : area.intersects(r))
            ids << it.id;
    }
    return ids;
}

QStringList BoardModel::contentsOf(const QString &sectionId) const
{
    const int row = indexOf(sectionId);
    if (row < 0 || !isSection(m_items.at(row)))
        return {};
    const BoardItem &s = m_items.at(row);
    const QRectF area(s.x, s.y, s.w, s.h);
    QStringList ids;
    for (const BoardItem &it : m_items) {
        if (it.id != s.id && area.contains(QRectF(it.x, it.y, it.w, it.h)))
            ids << it.id;
    }
    return ids;
}

QString BoardModel::noteByTitle(const QString &title) const
{
    const QString wanted = title.trimmed();
    for (const BoardItem &it : m_items) {
        if (it.type == QLatin1String("note") && titleForText(it.text).compare(wanted, Qt::CaseInsensitive) == 0)
            return it.id;
    }
    return {};
}

// ---------------------------------------------------------------------------
// Clipboard and export

BoardModel::Prompt BoardModel::promptOf(const QStringList &ids) const
{
    BoardState state = stateOf(ids);
    std::sort(state.items.begin(), state.items.end(), [this](const BoardItem &a, const BoardItem &b) {
        return m_readingOrder.value(a.id) < m_readingOrder.value(b.id);
    });
    // ![alt](target) and [text](target)
    static const QRegularExpression linkRe(QStringLiteral(R"((!?)\[([^\]]*)\]\(([^)\s]+)\))"));
    Prompt prompt;
    QStringList parts;
    for (const BoardItem &it : std::as_const(state.items)) {
        if (it.type != QLatin1String("note") && it.type != QLatin1String("text"))
            continue;
        QString text = Markdown::unlinkify(it.text).trimmed();
        if (text.isEmpty())
            continue;
        if (prompt.title.isEmpty())
            prompt.title = titleFor(it);
        QString out;
        qsizetype last = 0;
        for (auto m = linkRe.globalMatch(text); m.hasNext();) {
            const QRegularExpressionMatch match = m.next();
            out += text.mid(last, match.capturedStart() - last);
            last = match.capturedEnd();
            const QString local = resolveLocal(match.captured(3));
            const QFileInfo fi(local);
            if (local.isEmpty() || !fi.isFile()) { // the web, or gone: as it is
                out += match.captured(0);
                continue;
            }
            const QString name = fi.fileName();
            if (mediaType(local) == QLatin1String("video")) { // a chat can't take it
                out += QStringLiteral("[Video: %1]").arg(name);
                continue;
            }
            if (!prompt.attachments.contains(fi.absoluteFilePath()))
                prompt.attachments << fi.absoluteFilePath();
            const QString label = match.captured(2).isEmpty() ? name : match.captured(2);
            out += match.captured(1) + QLatin1Char('[') + label + QStringLiteral("](") + name + QLatin1Char(')');
        }
        out += text.mid(last);
        parts << out;
    }
    prompt.text = parts.join(QStringLiteral("\n\n---\n\n"));
    if (state.items.size() > 1 && parts.size() > 1)
        prompt.title = QFileInfo(m_projectPath).fileName();
    return prompt;
}

QVariantMap BoardModel::copyAsPrompt(const QStringList &ids)
{
    const Prompt prompt = promptOf(ids);
    if (prompt.text.isEmpty())
        return {};
    auto *mime = new QMimeData;
    mime->setText(prompt.text); // terminals and text fields take this
    QStringList files;
    if (!prompt.attachments.isEmpty()) {
        // A fresh folder each time, so earlier prompts don't linger
        QDir dir(tempPath(QStringLiteral("prompt")));
        dir.removeRecursively();
        QDir().mkpath(dir.path());
        static const QRegularExpression unsafe(QStringLiteral(R"([\\/:*?"<>|\x00-\x1f]+)"));
        QString name = prompt.title;
        name.replace(unsafe, QStringLiteral("-"));
        name = name.trimmed().left(60).trimmed();
        if (name.isEmpty())
            name = QStringLiteral("Prompt");
        const QString md = dir.filePath(name + QStringLiteral(".md"));
        QSaveFile file(md);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(prompt.text.toUtf8() + '\n');
            file.commit();
        }
        files << md << prompt.attachments;
        QList<QUrl> urls;
        QByteArray gnome = "copy";
        for (const QString &f : std::as_const(files)) {
            urls << QUrl::fromLocalFile(f);
            gnome += '\n' + QUrl::fromLocalFile(f).toEncoded();
        }
        mime->setUrls(urls);
        // GNOME's file managers and some apps look for this instead
        mime->setData(QStringLiteral("x-special/gnome-copied-files"), gnome);
    }
    QGuiApplication::clipboard()->setMimeData(mime);
    return {{QStringLiteral("text"), prompt.text},
            {QStringLiteral("files"), files},
            {QStringLiteral("attachments"), int(prompt.attachments.size())}};
}

void BoardModel::copyItems(const QStringList &ids) const
{
    BoardState state = stateOf(ids);
    if (state.items.isEmpty())
        return;
    QJsonArray items;
    QJsonArray links;
    QString html;
    QStringList text;
    std::sort(state.items.begin(), state.items.end(), [this](const BoardItem &a, const BoardItem &b) {
        return m_readingOrder.value(a.id) < m_readingOrder.value(b.id);
    });
    for (const BoardItem &it : std::as_const(state.items)) {
        items.append(toJson(it, true));
        if (it.type == QLatin1String("note")) {
            html += clipboardHtml(it.text).remove(QStringLiteral("<html><body>")).remove(QStringLiteral("</body></html>"));
            text << it.text.trimmed();
        } else if (!it.text.trimmed().isEmpty() && !isStroke(it)) {
            html += QStringLiteral("<p>%1</p>").arg(it.text.toHtmlEscaped());
            text << it.text.trimmed();
        }
    }
    for (const BoardLink &link : std::as_const(state.links))
        links.append(toJson(link));
    auto *mime = new QMimeData;
    mime->setData(QStringLiteral("application/x-cache-items"),
                  QJsonDocument(QJsonObject{{QStringLiteral("items"), items}, {QStringLiteral("links"), links}})
                      .toJson(QJsonDocument::Compact));
    mime->setHtml(QStringLiteral("<html><body>") + html + QStringLiteral("</body></html>"));
    mime->setText(text.join(QStringLiteral("\n\n")));
    mime->setData(QStringLiteral("application/x-cache-markdown"), QByteArray());
    QGuiApplication::clipboard()->setMimeData(mime);
}

QString BoardModel::tempPath(const QString &name) const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                        + QStringLiteral("/cache-") + QString::number(QCoreApplication::applicationPid());
    QDir().mkpath(dir);
    return QDir(dir).filePath(name);
}

bool BoardModel::copyFile(const QString &from, const QString &to) const
{
    const QString target = toLocalPath(to);
    QFile::remove(target);
    return QFile::copy(toLocalPath(from), target);
}

void BoardModel::copyText(const QString &text) const
{
    QGuiApplication::clipboard()->setText(text);
}

bool BoardModel::copyImageFile(const QString &path) const
{
    const QImage image(toLocalPath(path));
    if (image.isNull())
        return false;
    QGuiApplication::clipboard()->setImage(image);
    return true;
}

bool BoardModel::writePdf(const QString &imagePath, const QString &pdfPath) const
{
    const QImage image(toLocalPath(imagePath));
    if (image.isNull())
        return false;
    QPdfWriter pdf(toLocalPath(pdfPath));
    pdf.setTitle(QFileInfo(m_projectPath).fileName());
    pdf.setCreator(QCoreApplication::applicationName());
    // One page the shape of the picture, at 144 dpi (it is rendered at 2x)
    pdf.setResolution(144);
    pdf.setPageSize(QPageSize(QSizeF(image.width() / 144.0 * 25.4, image.height() / 144.0 * 25.4),
                              QPageSize::Millimeter));
    pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
    QPainter painter(&pdf);
    if (!painter.isActive())
        return false;
    const QRect page = painter.viewport();
    painter.drawImage(QRect(QPoint(0, 0), image.size().scaled(page.size(), Qt::KeepAspectRatio)), image);
    return painter.end();
}

// ---------------------------------------------------------------------------
// Templates: a canvas fragment (items with their text, and links) in a
// JSON file, plus a name and description

QVariantList BoardModel::templates(const QString &workspaceRoot) const
{
    QVariantList list;
    const auto read = [&](const QString &path, bool builtIn) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return;
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        list.append(QVariantMap{
            {QStringLiteral("path"), path},
            {QStringLiteral("name"), o.value(QStringLiteral("name")).toString(QFileInfo(path).completeBaseName())},
            {QStringLiteral("description"), o.value(QStringLiteral("description")).toString()},
            {QStringLiteral("builtIn"), builtIn},
        });
    };
    QDirIterator builtIns(QStringLiteral(":/qt/qml/Cache/templates"), {QStringLiteral("*.json")});
    QStringList paths;
    while (builtIns.hasNext())
        paths << builtIns.next();
    paths.sort();
    for (const QString &path : std::as_const(paths))
        read(path, true);
    if (!workspaceRoot.isEmpty()) {
        const QDir own(QDir(workspaceRoot).filePath(QStringLiteral(".templates")));
        for (const QFileInfo &fi : own.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name))
            read(fi.absoluteFilePath(), false);
    }
    return list;
}

QStringList BoardModel::insertTemplate(const QString &path, double cx, double cy)
{
    QFile f(path.startsWith(QLatin1String(":/")) ? path : toLocalPath(path));
    if (m_projectPath.isEmpty() || !f.open(QIODevice::ReadOnly))
        return {};
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    BoardState state;
    for (const QJsonValue &v : root.value(QStringLiteral("items")).toArray())
        state.items.append(itemFromJson(v.toObject()));
    for (const QJsonValue &v : root.value(QStringLiteral("links")).toArray())
        state.links.append(linkFromJson(v.toObject()));
    if (state.items.isEmpty())
        return {};
    QRectF bounds;
    for (const BoardItem &it : std::as_const(state.items))
        bounds = bounds.united(QRectF(it.x, it.y, it.w, it.h));
    pushUndo();
    return insertState(state, cx - bounds.center().x(), cy - bounds.center().y());
}

bool BoardModel::saveTemplate(const QStringList &ids, const QString &name, const QString &workspaceRoot)
{
    const BoardState state = stateOf(ids);
    if (state.items.isEmpty() || name.trimmed().isEmpty() || workspaceRoot.isEmpty())
        return false;
    QJsonArray items;
    for (const BoardItem &it : state.items)
        items.append(toJson(it, true));
    QJsonArray links;
    for (const BoardLink &link : state.links)
        links.append(toJson(link));
    QString file = name.trimmed();
    file.replace(QLatin1Char('/'), QLatin1Char('-'));
    QDir dir(workspaceRoot);
    dir.mkpath(QStringLiteral(".templates"));
    QSaveFile out(dir.filePath(QStringLiteral(".templates/%1.json").arg(file)));
    if (!out.open(QIODevice::WriteOnly))
        return false;
    out.write(QJsonDocument(QJsonObject{
                                {QStringLiteral("name"), name.trimmed()},
                                {QStringLiteral("items"), items},
                                {QStringLiteral("links"), links},
                            })
                  .toJson(QJsonDocument::Indented));
    return out.commit();
}
