#pragma once

#include "../core/DevelopSettings.hpp"
#include <QWidget>

namespace mylr {

class CropOverlay : public QWidget {
    Q_OBJECT
public:
    explicit CropOverlay(QWidget* parent = nullptr);

    void setGeometrySettings(const GeometrySettings& geom);
    GeometrySettings geometrySettings() const { return m_geom; }
    void setImageSize(const QSize& size);

signals:
    void cropChanged(const GeometrySettings& geom);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QRectF normalizedToWidget() const;
    GeometrySettings m_geom;
    QSize m_imageSize;
    bool m_dragging = false;
    QPoint m_dragStart;
};

} // namespace mylr
