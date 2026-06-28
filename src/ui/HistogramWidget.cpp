#include "HistogramWidget.hpp"
#include <QMouseEvent>
#include <QPainter>

namespace mylr {

HistogramWidget::HistogramWidget(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(100);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::SizeVerCursor);
    setToolTip(QStringLiteral("Drag: left = Blacks, middle = Exposure, right = Whites"));
}

HistogramWidget::Zone HistogramWidget::zoneForX(int x) const {
    const float t = width() > 0 ? static_cast<float>(x) / width() : 0.5f;
    if (t < 0.25f) return ZoneBlacks;
    if (t > 0.75f) return ZoneWhites;
    return ZoneExposure;
}

void HistogramWidget::setHistogram(const HistogramData& data) {
    m_data = data;
    update();
}

void HistogramWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(30, 30, 30));

    if (m_data.luminance.isEmpty()) return;

    const int bins = HistogramData::BinCount;
    const int w = width();
    const int h = height() - 4;

    long long maxVal = 1;
    for (int i = 0; i < bins; ++i)
        maxVal = qMax(maxVal, static_cast<long long>(m_data.luminance[i]));

    auto drawChannel = [&](const QVector<int>& ch, const QColor& color) {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        QPolygon poly;
        poly << QPoint(0, h);
        for (int i = 0; i < bins; ++i) {
            const int barH = static_cast<int>(static_cast<double>(ch[i]) / maxVal * h);
            poly << QPoint(i * w / bins, h - barH);
        }
        poly << QPoint(w, h);
        p.drawPolygon(poly);
    };

    if (m_channelMode == 0 || m_channelMode == 4) {
        drawChannel(m_data.red, QColor(180, 60, 60, 80));
        drawChannel(m_data.green, QColor(60, 180, 60, 80));
        drawChannel(m_data.blue, QColor(60, 60, 180, 80));
    }
    if (m_channelMode == 0 || m_channelMode == 1)
        drawChannel(m_data.luminance, QColor(200, 200, 200, 120));

    p.setPen(QColor(80, 80, 80));
    p.drawRect(0, 0, w - 1, h);

    // Subtle dividers showing the draggable tone zones (Blacks | Exposure | Whites).
    p.setPen(QColor(70, 70, 70, 160));
    p.drawLine(w / 4, 0, w / 4, h);
    p.drawLine(w * 3 / 4, 0, w * 3 / 4, h);

    // Clipping indicators: top-left lights up on shadow clip, top-right on
    // highlight clip (white = clipping, grey = none).
    const bool lowClip = m_data.clipLow > 0;
    const bool highClip = (m_data.clipHighR + m_data.clipHighG + m_data.clipHighB) > 0;
    auto drawIndicator = [&](bool left, bool on) {
        const int s = 9;
        QPolygon tri;
        if (left)
            tri << QPoint(1, 1) << QPoint(1 + s, 1) << QPoint(1, 1 + s);
        else
            tri << QPoint(w - 2, 1) << QPoint(w - 2 - s, 1) << QPoint(w - 2, 1 + s);
        p.setPen(Qt::NoPen);
        p.setBrush(on ? QColor(245, 245, 245) : QColor(70, 70, 70));
        p.drawPolygon(tri);
    };
    drawIndicator(true, lowClip);
    drawIndicator(false, highClip);
}

void HistogramWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(e);
        return;
    }
    m_dragZone = zoneForX(e->pos().x());
    m_pressY = e->pos().y();
    emit toneDragBegin(m_dragZone);
}

void HistogramWidget::mouseMoveEvent(QMouseEvent* e) {
    if (m_dragZone == ZoneNone) return;
    emit toneDragged(m_dragZone, e->pos().y() - m_pressY);
}

void HistogramWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (m_dragZone == ZoneNone) {
        QWidget::mouseReleaseEvent(e);
        return;
    }
    m_dragZone = ZoneNone;
    emit toneDragEnd();
}

} // namespace mylr