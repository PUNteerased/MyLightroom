#include "ToneCurveEditor.hpp"

#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>

namespace mylr {

ToneCurveEditor::ToneCurveEditor(QWidget* parent) : QWidget(parent) {
    setMinimumSize(200, 160);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_curve.mode = ToneCurveSettings::Mode::Point;
    if (m_curve.points.size() < 2)
        m_curve.points = {{0, 0}, {255, 255}};
}

void ToneCurveEditor::setCurve(const ToneCurveSettings& curve) {
    m_curve = curve;
    m_curve.mode = ToneCurveSettings::Mode::Point;
    if (m_curve.points.size() < 2)
        m_curve.points = {{0, 0}, {255, 255}};
    sortPoints();
    update();
}

void ToneCurveEditor::sortPoints() {
    std::sort(m_curve.points.begin(), m_curve.points.end(),
              [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
}

QPointF ToneCurveEditor::curveToWidget(const QPointF& pt) const {
    const float w = width() - 2 * Margin;
    const float h = height() - 2 * Margin;
    const float x = Margin + (pt.x() / 255.f) * w;
    const float y = Margin + (1.f - pt.y() / 255.f) * h;
    return {x, y};
}

QPointF ToneCurveEditor::widgetToCurve(const QPointF& pt) const {
    const float w = width() - 2 * Margin;
    const float h = height() - 2 * Margin;
    const float cx = qBound(0.f, (static_cast<float>(pt.x()) - Margin) / w, 1.f) * 255.f;
    const float cy = qBound(0.f, 1.f - (static_cast<float>(pt.y()) - Margin) / h, 1.f) * 255.f;
    return {cx, cy};
}

int ToneCurveEditor::hitTest(const QPoint& pos) const {
    for (int i = 0; i < m_curve.points.size(); ++i) {
        const QPointF wp = curveToWidget(m_curve.points[i]);
        if (QLineF(wp, pos).length() <= HitRadius)
            return i;
    }
    return -1;
}

void ToneCurveEditor::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect area(Margin, Margin, width() - 2 * Margin, height() - 2 * Margin);

    p.fillRect(rect(), QColor(24, 24, 24));
    p.fillRect(area, QColor(34, 34, 34));

    p.setPen(QPen(QColor(60, 60, 60), 1));
    for (int i = 1; i < 4; ++i) {
        const int x = area.left() + area.width() * i / 4;
        const int y = area.top() + area.height() * i / 4;
        p.drawLine(x, area.top(), x, area.bottom());
        p.drawLine(area.left(), y, area.right(), y);
    }
    p.setPen(QPen(QColor(70, 70, 70), 1, Qt::DashLine));
    p.drawLine(area.bottomLeft(), area.topRight());

    // Curve polyline sampled through linear interpolation of the points.
    QPainterPath path;
    for (int px = 0; px <= area.width(); ++px) {
        const float t = static_cast<float>(px) / area.width() * 255.f;
        float y = t;
        for (int i = 1; i < m_curve.points.size(); ++i) {
            if (t <= m_curve.points[i].x()) {
                const float x0 = m_curve.points[i - 1].x();
                const float y0 = m_curve.points[i - 1].y();
                const float x1 = m_curve.points[i].x();
                const float y1 = m_curve.points[i].y();
                const float f = x1 > x0 ? (t - x0) / (x1 - x0) : 0.f;
                y = y0 + f * (y1 - y0);
                break;
            }
        }
        const QPointF wp = curveToWidget({t, y});
        if (px == 0) path.moveTo(wp);
        else path.lineTo(wp);
    }
    p.setPen(QPen(QColor(230, 230, 230), 2));
    p.drawPath(path);

    // Control points.
    for (int i = 0; i < m_curve.points.size(); ++i) {
        const QPointF wp = curveToWidget(m_curve.points[i]);
        p.setBrush(i == m_activePoint ? QColor(100, 160, 255) : QColor(220, 220, 220));
        p.setPen(QPen(QColor(20, 20, 20), 1));
        p.drawEllipse(wp, 4, 4);
    }
}

void ToneCurveEditor::mousePressEvent(QMouseEvent* e) {
    m_activePoint = hitTest(e->pos());
    if (m_activePoint < 0) {
        // Add a new point on left click within the area.
        const QPointF cp = widgetToCurve(e->pos());
        m_curve.points.append(cp);
        sortPoints();
        m_activePoint = -1;
        for (int i = 0; i < m_curve.points.size(); ++i) {
            if (qFuzzyCompare(m_curve.points[i].x() + 1, cp.x() + 1) &&
                qFuzzyCompare(m_curve.points[i].y() + 1, cp.y() + 1)) {
                m_activePoint = i;
                break;
            }
        }
        update();
        emit curveChanged(m_curve);
    }
    update();
}

void ToneCurveEditor::mouseMoveEvent(QMouseEvent* e) {
    if (m_activePoint < 0) return;
    const bool isEndpoint = (m_activePoint == 0 || m_activePoint == m_curve.points.size() - 1);
    QPointF cp = widgetToCurve(e->pos());
    if (isEndpoint) {
        // Endpoints keep their x; only y is adjustable.
        cp.setX(m_curve.points[m_activePoint].x());
    } else {
        const float minX = m_curve.points[m_activePoint - 1].x() + 1.f;
        const float maxX = m_curve.points[m_activePoint + 1].x() - 1.f;
        cp.setX(qBound(minX, static_cast<float>(cp.x()), maxX));
    }
    m_curve.points[m_activePoint] = cp;
    update();
    emit curveChanged(m_curve);
}

void ToneCurveEditor::mouseReleaseEvent(QMouseEvent*) {
    m_activePoint = -1;
    update();
}

void ToneCurveEditor::mouseDoubleClickEvent(QMouseEvent* e) {
    const int idx = hitTest(e->pos());
    // Remove interior points on double click.
    if (idx > 0 && idx < m_curve.points.size() - 1) {
        m_curve.points.remove(idx);
        m_activePoint = -1;
        update();
        emit curveChanged(m_curve);
    }
}

} // namespace mylr
