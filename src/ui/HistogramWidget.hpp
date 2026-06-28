#pragma once

#include "../render/DevelopPipeline.hpp"
#include <QWidget>

namespace mylr {

class HistogramWidget : public QWidget {
    Q_OBJECT
public:
    // Draggable tone zones (Lightroom-style direct histogram manipulation).
    enum Zone { ZoneBlacks = 0, ZoneExposure = 1, ZoneWhites = 2, ZoneNone = -1 };

    explicit HistogramWidget(QWidget* parent = nullptr);

    void setHistogram(const HistogramData& data);
    void setChannelMode(int mode) { m_channelMode = mode; update(); }

signals:
    // Dragging on the histogram adjusts a tone parameter. dyFromPress is the
    // vertical pixels dragged since press (negative = up = brighten). The
    // receiver maps this onto the relevant Basic setting relative to its value
    // at toneDragBegin, and commits history on toneDragEnd.
    void toneDragBegin(int zone);
    void toneDragged(int zone, int dyFromPress);
    void toneDragEnd();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    Zone zoneForX(int x) const;

    HistogramData m_data;
    int m_channelMode = 0;
    Zone m_dragZone = ZoneNone;
    int m_pressY = 0;
};

} // namespace mylr
