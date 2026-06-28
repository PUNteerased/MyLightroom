#include "CropOverlay.hpp"
#include <QPainter>
#include <QMouseEvent>

namespace mylr {

CropOverlay::CropOverlay(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setMouseTracking(true);
}

void CropOverlay::setGeometrySettings(const GeometrySettings& geom) {
    m_geom = geom;
    update();
}

void CropOverlay::setImageSize(const QSize& size) {
    m_imageSize = size;
    update();
}

QRectF CropOverlay::normalizedToWidget() const {
    if (m_imageSize.isEmpty()) return rect();
    const float sx = width() / float(m_imageSize.width());
    const float sy = height() / float(m_imageSize.height());
    const float scale = qMin(sx, sy);
    const float dw = m_imageSize.width() * scale;
    const float dh = m_imageSize.height() * scale;
    const float ox = (width() - dw) * 0.5f;
    const float oy = (height() - dh) * 0.5f;
    return QRectF(ox + m_geom.cropLeft * dw, oy + m_geom.cropTop * dh,
                  (m_geom.cropRight - m_geom.cropLeft) * dw,
                  (m_geom.cropBottom - m_geom.cropTop) * dh);
}

void CropOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF crop = normalizedToWidget();

    p.fillRect(rect(), QColor(0, 0, 0, 100));
    p.setCompositionMode(QPainter::CompositionMode_Clear);
    p.fillRect(crop, Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

    p.setPen(QPen(Qt::white, 1, Qt::DashLine));
    p.drawRect(crop);

    for (int i = 1; i < 3; ++i) {
        const qreal x = crop.left() + crop.width() * i / 3.0;
        const qreal y = crop.top() + crop.height() * i / 3.0;
        p.drawLine(QPointF(x, crop.top()), QPointF(x, crop.bottom()));
        p.drawLine(QPointF(crop.left(), y), QPointF(crop.right(), y));
    }
}

void CropOverlay::mousePressEvent(QMouseEvent* e) {
    m_dragging = true;
    m_dragStart = e->pos();
}

void CropOverlay::mouseMoveEvent(QMouseEvent* e) {
    if (!m_dragging || m_imageSize.isEmpty()) return;
    const QPoint delta = e->pos() - m_dragStart;
    const float dx = delta.x() / float(width());
    const float dy = delta.y() / float(height());
    m_geom.cropLeft = qBound(0.f, m_geom.cropLeft + dx, m_geom.cropRight - 0.05f);
    m_geom.cropTop = qBound(0.f, m_geom.cropTop + dy, m_geom.cropBottom - 0.05f);
    m_geom.cropRight = qBound(m_geom.cropLeft + 0.05f, m_geom.cropRight + dx, 1.f);
    m_geom.cropBottom = qBound(m_geom.cropTop + 0.05f, m_geom.cropBottom + dy, 1.f);
    m_dragStart = e->pos();
    update();
    emit cropChanged(m_geom);
}

void CropOverlay::mouseReleaseEvent(QMouseEvent*) {
    m_dragging = false;
}

} // namespace mylr
