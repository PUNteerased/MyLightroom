#pragma once

#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QRectF>
#include <functional>

namespace mylr {

// Navigator preview: shows the current image and (when zoomed) a rectangle for
// the visible region. Clicking or dragging recenters the main viewport. No new
// signals / Q_OBJECT, so it needs no moc pass.
class NavigatorLabel : public QLabel {
public:
    using QLabel::QLabel;

    std::function<void(QPointF)> onNavigate;  // normalized 0..1 image point

    void setVisibleRegion(const QRectF& normalized) {
        m_visible = normalized;
        update();
    }

protected:
    void paintEvent(QPaintEvent* e) override {
        QLabel::paintEvent(e);
        if (pixmap().isNull()) return;
        if (m_visible.isNull() || (m_visible.width() >= 1.0 && m_visible.height() >= 1.0))
            return;
        const QRect area = pixmapRect();
        QPainter p(this);
        p.setPen(QPen(QColor(240, 240, 240), 1));
        p.setBrush(Qt::NoBrush);
        const QRectF r(area.x() + m_visible.x() * area.width(),
                       area.y() + m_visible.y() * area.height(),
                       m_visible.width() * area.width(),
                       m_visible.height() * area.height());
        p.drawRect(r);
    }

    void mousePressEvent(QMouseEvent* e) override { navigate(e->pos()); }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (e->buttons() & Qt::LeftButton) navigate(e->pos());
    }

private:
    QRect pixmapRect() const {
        const QPixmap pm = pixmap();
        const int pw = pm.width();
        const int ph = pm.height();
        return QRect((width() - pw) / 2, (height() - ph) / 2, pw, ph);
    }

    void navigate(const QPoint& pos) {
        if (!onNavigate) return;
        const QRect area = pixmapRect();
        if (area.width() <= 0 || area.height() <= 0) return;
        const double nx = double(pos.x() - area.x()) / area.width();
        const double ny = double(pos.y() - area.y()) / area.height();
        if (nx < 0 || nx > 1 || ny < 0 || ny > 1) return;
        onNavigate(QPointF(nx, ny));
    }

    QRectF m_visible{0, 0, 1, 1};
};

} // namespace mylr
