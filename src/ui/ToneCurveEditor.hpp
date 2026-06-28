#pragma once

#include "../core/DevelopSettings.hpp"
#include <QWidget>

namespace mylr {

// Point-based tone curve editor (Lightroom "Point Curve" style).
// Points are stored in 0..255 coordinate space inside ToneCurveSettings.
class ToneCurveEditor : public QWidget {
    Q_OBJECT
public:
    explicit ToneCurveEditor(QWidget* parent = nullptr);

    void setCurve(const ToneCurveSettings& curve);
    ToneCurveSettings curve() const { return m_curve; }

signals:
    void curveChanged(const ToneCurveSettings& curve);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    QSize sizeHint() const override { return {260, 200}; }

private:
    QPointF curveToWidget(const QPointF& pt) const;
    QPointF widgetToCurve(const QPointF& pt) const;
    int hitTest(const QPoint& pos) const;
    void sortPoints();

    ToneCurveSettings m_curve;
    int m_activePoint = -1;
    static constexpr int Margin = 10;
    static constexpr int HitRadius = 10;
};

} // namespace mylr
