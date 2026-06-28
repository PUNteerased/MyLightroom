#include "ColorWheelWidget.hpp"

#include <QConicalGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QtMath>

namespace mylr {

ColorWheelWidget::ColorWheelWidget(const QString& title, QWidget* parent)
    : QWidget(parent), m_title(title) {
    setMinimumSize(110, 140);
}

QRectF ColorWheelWidget::wheelRect() const {
    const int dim = qMin(width(), height() - 34);
    const qreal size = qMax(40, dim - 24);
    const qreal x = (width() - size) / 2.0;
    return QRectF(x, 18, size, size);
}

QRectF ColorWheelWidget::lumaBarRect() const {
    return QRectF(10, height() - 14, width() - 20, 8);
}

void ColorWheelWidget::setWheel(const ColorWheel& wheel) {
    m_wheel = wheel;
    update();
}

void ColorWheelWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(34, 34, 34));

    p.setPen(QColor(170, 170, 170));
    p.drawText(QRect(0, 0, width(), 16), Qt::AlignCenter, m_title);

    const QRectF wr = wheelRect();
    const QPointF center = wr.center();
    const qreal radius = wr.width() / 2.0;

    QConicalGradient grad(center, 0);
    for (int i = 0; i <= 360; i += 30) {
        grad.setColorAt(i / 360.0, QColor::fromHsv(i % 360, 200, 230));
    }
    p.setBrush(grad);
    p.setPen(QPen(QColor(60, 60, 60), 1));
    p.drawEllipse(wr);

    // Desaturated center.
    QRadialGradient center_grad(center, radius);
    center_grad.setColorAt(0.0, QColor(120, 120, 120, 230));
    center_grad.setColorAt(0.7, QColor(120, 120, 120, 0));
    p.setBrush(center_grad);
    p.setPen(Qt::NoPen);
    p.drawEllipse(wr);

    // Selector dot.
    const qreal angle = qDegreesToRadians(m_wheel.hue);
    const qreal r = qBound(0.f, m_wheel.saturation / 100.f, 1.f) * radius;
    const QPointF dot(center.x() + std::cos(angle) * r, center.y() - std::sin(angle) * r);
    p.setBrush(Qt::white);
    p.setPen(QPen(QColor(20, 20, 20), 1));
    p.drawEllipse(dot, 4, 4);

    // Luminance bar.
    const QRectF lr = lumaBarRect();
    QLinearGradient lg(lr.topLeft(), lr.topRight());
    lg.setColorAt(0.0, QColor(20, 20, 20));
    lg.setColorAt(0.5, QColor(128, 128, 128));
    lg.setColorAt(1.0, QColor(245, 245, 245));
    p.setBrush(lg);
    p.setPen(QPen(QColor(60, 60, 60), 1));
    p.drawRoundedRect(lr, 3, 3);
    const qreal lx = lr.left() + (m_wheel.luminance + 100.f) / 200.f * lr.width();
    p.setBrush(Qt::white);
    p.drawEllipse(QPointF(lx, lr.center().y()), 4, 4);
}

void ColorWheelWidget::updateFromPos(const QPoint& pos) {
    if (m_draggingLuma) {
        const QRectF lr = lumaBarRect();
        const float t = qBound(0.f, (pos.x() - static_cast<float>(lr.left())) /
                                        static_cast<float>(lr.width()),
                               1.f);
        m_wheel.luminance = t * 200.f - 100.f;
        update();
        emit wheelChanged(m_wheel);
        return;
    }
    const QRectF wr = wheelRect();
    const QPointF center = wr.center();
    const qreal radius = wr.width() / 2.0;
    const qreal dx = pos.x() - center.x();
    const qreal dy = center.y() - pos.y();
    qreal angle = qRadiansToDegrees(std::atan2(dy, dx));
    if (angle < 0) angle += 360.0;
    const qreal dist = qBound(0.0, std::sqrt(dx * dx + dy * dy) / radius, 1.0);
    m_wheel.hue = static_cast<float>(angle);
    m_wheel.saturation = static_cast<float>(dist * 100.0);
    update();
    emit wheelChanged(m_wheel);
}

void ColorWheelWidget::mousePressEvent(QMouseEvent* e) {
    if (lumaBarRect().adjusted(-6, -6, 6, 6).contains(e->pos())) {
        m_draggingLuma = true;
    } else {
        m_draggingWheel = true;
    }
    updateFromPos(e->pos());
}

void ColorWheelWidget::mouseMoveEvent(QMouseEvent* e) {
    if (m_draggingWheel || m_draggingLuma)
        updateFromPos(e->pos());
}

void ColorWheelWidget::mouseReleaseEvent(QMouseEvent*) {
    m_draggingWheel = false;
    m_draggingLuma = false;
}

} // namespace mylr
