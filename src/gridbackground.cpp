#include "gridbackground.h"

#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>

#include <cmath>

GridBackground::GridBackground(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents);
}

void GridBackground::setOffsetX(qreal v)
{
    if (qFuzzyCompare(m_offsetX, v))
        return;
    m_offsetX = v;
    emit changed();
    update();
}

void GridBackground::setOffsetY(qreal v)
{
    if (qFuzzyCompare(m_offsetY, v))
        return;
    m_offsetY = v;
    emit changed();
    update();
}

void GridBackground::setZoom(qreal v)
{
    if (qFuzzyCompare(m_zoom, v) || v <= 0)
        return;
    m_zoom = v;
    emit changed();
    update();
}

void GridBackground::setSpacing(qreal v)
{
    if (qFuzzyCompare(m_spacing, v) || v <= 1)
        return;
    m_spacing = v;
    emit changed();
    update();
}

void GridBackground::setColor(const QColor &c)
{
    if (m_color == c)
        return;
    m_color = c;
    emit changed();
    update();
}

void GridBackground::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    update();
}

QSGNode *GridBackground::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    auto *node = static_cast<QSGGeometryNode *>(oldNode);
    if (!node) {
        node = new QSGGeometryNode;
        auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        node->setGeometry(geometry);
        node->setFlag(QSGNode::OwnsGeometry);
        node->setMaterial(new QSGFlatColorMaterial);
        node->setFlag(QSGNode::OwnsMaterial);
    }

    // Double the world spacing until dots are at least 14px apart on screen,
    // so zooming out thins the grid instead of turning it into noise.
    qreal worldStep = m_spacing;
    while (worldStep * m_zoom < 14)
        worldStep *= 2;
    const qreal step = worldStep * m_zoom;

    qreal startX = std::fmod(m_offsetX, step);
    qreal startY = std::fmod(m_offsetY, step);
    if (startX < 0)
        startX += step;
    if (startY < 0)
        startY += step;

    const int cols = width() > 0 ? int(std::ceil((width() - startX) / step)) + 1 : 0;
    const int rows = height() > 0 ? int(std::ceil((height() - startY) / step)) + 1 : 0;
    const qreal r = std::clamp(0.6 + 0.5 * m_zoom, 0.8, 1.4); // half the dot size

    QSGGeometry *geometry = node->geometry();
    geometry->allocate(std::max(0, cols * rows * 6));
    QSGGeometry::Point2D *v = geometry->vertexDataAsPoint2D();
    for (int j = 0; j < rows; ++j) {
        const float y = float(startY + j * step);
        for (int i = 0; i < cols; ++i) {
            const float x = float(startX + i * step);
            const float x0 = x - r, x1 = x + r, y0 = y - r, y1 = y + r;
            v[0].set(x0, y0);
            v[1].set(x1, y0);
            v[2].set(x1, y1);
            v[3].set(x0, y0);
            v[4].set(x1, y1);
            v[5].set(x0, y1);
            v += 6;
        }
    }
    static_cast<QSGFlatColorMaterial *>(node->material())->setColor(m_color);
    node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
    return node;
}
