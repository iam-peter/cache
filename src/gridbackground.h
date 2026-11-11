#pragma once

#include <QColor>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

// Dot grid behind the canvas. Drawn as scene graph geometry so panning and
// zooming only rebuild a vertex buffer, never a texture.
class GridBackground : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(qreal offsetX READ offsetX WRITE setOffsetX NOTIFY changed)
    Q_PROPERTY(qreal offsetY READ offsetY WRITE setOffsetY NOTIFY changed)
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY changed)
    Q_PROPERTY(qreal spacing READ spacing WRITE setSpacing NOTIFY changed)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY changed)

public:
    explicit GridBackground(QQuickItem *parent = nullptr);

    qreal offsetX() const { return m_offsetX; }
    qreal offsetY() const { return m_offsetY; }
    qreal zoom() const { return m_zoom; }
    qreal spacing() const { return m_spacing; }
    QColor color() const { return m_color; }

    void setOffsetX(qreal v);
    void setOffsetY(qreal v);
    void setZoom(qreal v);
    void setSpacing(qreal v);
    void setColor(const QColor &c);

signals:
    void changed();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    qreal m_offsetX = 0;
    qreal m_offsetY = 0;
    qreal m_zoom = 1;
    qreal m_spacing = 24;
    QColor m_color = QColor(128, 128, 128, 90);
};
