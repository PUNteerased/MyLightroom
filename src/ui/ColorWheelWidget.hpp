#pragma once

#include "../core/DevelopSettings.hpp"
#include <QWidget>

namespace mylr {

// A color-grading wheel: angle = hue, radius = saturation, plus a luminance
// slider underneath. Mirrors Lightroom's Color Grading control.
class ColorWheelWidget : public QWidget {
    Q_OBJECT
public:
    explicit ColorWheelWidget(const QString& title, QWidget* parent = nullptr);

    void setWheel(const ColorWheel& wheel);
    ColorWheel wheel() const { return m_wheel; }

signals:
    void wheelChanged(const ColorWheel& wheel);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    QSize sizeHint() const override { return {120, 150}; }

private:
    QRectF wheelRect() const;
    QRectF lumaBarRect() const;
    void updateFromPos(const QPoint& pos);

    QString m_title;
    ColorWheel m_wheel;
    bool m_draggingWheel = false;
    bool m_draggingLuma = false;
};

} // namespace mylr
