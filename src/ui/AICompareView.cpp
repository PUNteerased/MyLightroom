#include "AICompareView.hpp"

#include <QPainter>

namespace mylr {

AICompareView::AICompareView(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QStringLiteral("background-color: #232323;"));
    setMinimumSize(400, 300);
}

void AICompareView::setReferenceImage(const QImage& image) { m_reference = image; update(); }
void AICompareView::setBeforeImage(const QImage& image) { m_before = image; update(); }
void AICompareView::setAfterImage(const QImage& image) { m_after = image; update(); }

void AICompareView::setInfo(const QString& sceneType, float confidence) {
    m_sceneType = sceneType;
    m_confidence = confidence;
    update();
}

void AICompareView::drawPanel(QPainter& p, const QRect& rect, const QImage& img,
                              const QString& label) {
    p.fillRect(rect, QColor(28, 28, 28));
    p.setPen(QColor(60, 60, 60));
    p.drawRect(rect.adjusted(0, 0, -1, -1));

    QRect imgArea = rect.adjusted(8, 8, -8, -28);
    if (!img.isNull()) {
        QImage scaled = img.scaled(imgArea.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const int ox = imgArea.x() + (imgArea.width() - scaled.width()) / 2;
        const int oy = imgArea.y() + (imgArea.height() - scaled.height()) / 2;
        p.drawImage(QPoint(ox, oy), scaled);
    } else {
        p.setPen(QColor(110, 110, 110));
        p.drawText(imgArea, Qt::AlignCenter, QStringLiteral("(empty)"));
    }

    p.setPen(QColor(200, 200, 200));
    p.drawText(QRect(rect.left(), rect.bottom() - 22, rect.width(), 20), Qt::AlignCenter, label);
}

void AICompareView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(35, 35, 35));

    const int infoH = 28;
    const int panelW = width() / 3;
    const int panelH = height() - infoH;

    drawPanel(p, QRect(0, 0, panelW, panelH), m_reference, QStringLiteral("Reference Look"));
    drawPanel(p, QRect(panelW, 0, panelW, panelH), m_before, QStringLiteral("Before"));
    drawPanel(p, QRect(2 * panelW, 0, width() - 2 * panelW, panelH), m_after,
              QStringLiteral("After (AI Match)"));

    QString info = QStringLiteral("Scene: %1").arg(m_sceneType.isEmpty() ? QStringLiteral("-")
                                                                         : m_sceneType);
    if (m_confidence >= 0.f)
        info += QStringLiteral("    Confidence: %1%").arg(static_cast<int>(m_confidence * 100));
    p.fillRect(QRect(0, panelH, width(), infoH), QColor(26, 26, 26));
    p.setPen(QColor(190, 190, 190));
    p.drawText(QRect(12, panelH, width() - 12, infoH), Qt::AlignVCenter | Qt::AlignLeft, info);
}

} // namespace mylr
