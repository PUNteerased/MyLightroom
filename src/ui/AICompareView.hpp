#pragma once

#include <QImage>
#include <QWidget>

namespace mylr {

// AI module center view: shows the reference look, the original ("before"),
// and the AI-matched result ("after") side by side with scene info.
class AICompareView : public QWidget {
    Q_OBJECT
public:
    explicit AICompareView(QWidget* parent = nullptr);

    void setReferenceImage(const QImage& image);
    void setBeforeImage(const QImage& image);
    void setAfterImage(const QImage& image);
    void setInfo(const QString& sceneType, float confidence);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void drawPanel(QPainter& p, const QRect& rect, const QImage& img, const QString& label);

    QImage m_reference;
    QImage m_before;
    QImage m_after;
    QString m_sceneType;
    float m_confidence = -1.f;
};

} // namespace mylr
