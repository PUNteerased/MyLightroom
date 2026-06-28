#include "ImageViewport.hpp"
#include "CropOverlay.hpp"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QtMath>

namespace mylr {

ImageViewport::ImageViewport(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(400, 300);
    setStyleSheet(QStringLiteral("background-color: #2d2d2d;"));

    m_cropOverlay = new CropOverlay(this);
    m_cropOverlay->hide();
    connect(m_cropOverlay, &CropOverlay::cropChanged, this,
            [this](const GeometrySettings& geom) { emit cropGeometryChanged(geom); });
}

void ImageViewport::setImage(const QImage& image) {
    const bool sizeChanged = image.size() != m_image.size();
    m_image = image;
    if (m_cropMode && !m_image.isNull())
        m_cropOverlay->setImageSize(m_image.size());
    if (sizeChanged) {
        // A different photo resets the view to FIT.
        m_fitMode = true;
        m_dragging = false;
    }
    update();
    emit viewChanged();
}

bool ImageViewport::zoomable() const {
    return !m_cropMode && !m_compare && !m_referenceMode && !m_image.isNull();
}

void ImageViewport::setReferenceImage(const QImage& image) {
    m_referenceImage = image;
    if (m_referenceMode) update();
}

void ImageViewport::setReferenceMode(bool enabled) {
    m_referenceMode = enabled;
    if (enabled) m_fitMode = true;
    update();
}

float ImageViewport::fitScale() const {
    if (m_image.isNull()) return 1.f;
    return qMin(static_cast<float>(width()) / m_image.width(),
               static_cast<float>(height()) / m_image.height());
}

void ImageViewport::clampPan() {
    if (m_image.isNull()) return;
    const int iw = m_image.width();
    const int ih = m_image.height();
    if (iw <= width()) m_panOffset.setX((width() - iw) / 2);
    else m_panOffset.setX(qBound(width() - iw, m_panOffset.x(), 0));
    if (ih <= height()) m_panOffset.setY((height() - ih) / 2);
    else m_panOffset.setY(qBound(height() - ih, m_panOffset.y(), 0));
}

QRectF ImageViewport::visibleRegionNormalized() const {
    if (m_image.isNull() || m_fitMode || m_compare || m_cropMode)
        return QRectF(0, 0, 1, 1);
    const float iw = m_image.width();
    const float ih = m_image.height();
    const float x = qBound(0.f, -m_panOffset.x() / iw, 1.f);
    const float y = qBound(0.f, -m_panOffset.y() / ih, 1.f);
    const float w = qBound(0.f, width() / iw, 1.f);
    const float h = qBound(0.f, height() / ih, 1.f);
    return QRectF(x, y, w, h);
}

void ImageViewport::centerViewOnNormalized(QPointF n) {
    if (m_image.isNull() || m_fitMode) return;
    const float imgX = qBound(0.0, n.x(), 1.0) * m_image.width();
    const float imgY = qBound(0.0, n.y(), 1.0) * m_image.height();
    m_panOffset = QPoint(static_cast<int>(width() / 2 - imgX),
                         static_cast<int>(height() / 2 - imgY));
    clampPan();
    update();
    emit viewChanged();
}

void ImageViewport::setCropMode(bool enabled) {
    m_cropMode = enabled;
    if (enabled) {
        m_cropOverlay->setGeometry(rect());
        if (!m_image.isNull())
            m_cropOverlay->setImageSize(m_image.size());
        m_cropOverlay->show();
        m_cropOverlay->raise();
    } else {
        m_cropOverlay->hide();
    }
    update();
}

void ImageViewport::setCropGeometry(const GeometrySettings& geom) {
    m_cropOverlay->setGeometrySettings(geom);
}

void ImageViewport::setBeforeImage(const QImage& image) {
    m_beforeImage = image;
    update();
}

void ImageViewport::setCompareMode(bool enabled) {
    m_compare = enabled;
    update();
    emit compareToggled(enabled);
}

void ImageViewport::setCompareSplit(float ratio) {
    m_split = qBound(0.f, ratio, 1.f);
    update();
}

void ImageViewport::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(45, 45, 45));
    if (m_image.isNull()) {
        p.setPen(QColor(120, 120, 120));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Open a RAW file to begin"));
        return;
    }

    // Reference Photo Mode: reference (locked) on the left, active on the right.
    if (m_referenceMode && !m_referenceImage.isNull()) {
        const int half = width() / 2;
        const QRect leftArea(0, 0, half, height());
        const QRect rightArea(half, 0, width() - half, height());
        auto drawFitted = [&](const QRect& area, const QImage& img, const QString& label) {
            if (img.isNull()) return;
            const float s = qMin(static_cast<float>(area.width()) / img.width(),
                                 static_cast<float>(area.height()) / img.height());
            const int dw = static_cast<int>(img.width() * s);
            const int dh = static_cast<int>(img.height() * s);
            const QRect dest(area.x() + (area.width() - dw) / 2,
                             area.y() + (area.height() - dh) / 2, dw, dh);
            p.drawImage(dest, img);
            p.setPen(QColor(200, 200, 200));
            p.drawText(area.adjusted(6, 4, -6, 0), Qt::AlignTop | Qt::AlignHCenter, label);
        };
        drawFitted(leftArea, m_referenceImage, QStringLiteral("Reference"));
        drawFitted(rightArea, m_image, QStringLiteral("Active"));
        p.setPen(QPen(QColor(80, 80, 80), 1));
        p.drawLine(half, 0, half, height());
        return;
    }

    const QSize imgSize = m_image.size();
    const bool fit = m_fitMode || m_compare || m_cropMode;
    const float scale = fit ? fitScale() : 1.f;
    const int dw = static_cast<int>(imgSize.width() * scale);
    const int dh = static_cast<int>(imgSize.height() * scale);
    const int ox = fit ? (width() - dw) / 2 : m_panOffset.x();
    const int oy = fit ? (height() - dh) / 2 : m_panOffset.y();
    const QRect dest(ox, oy, dw, dh);

    if (m_compare && !m_beforeImage.isNull()) {
        p.drawImage(dest, m_beforeImage);
        p.setClipRect(ox + static_cast<int>(dw * m_split), oy,
                      static_cast<int>(dw * (1.f - m_split)), dh);
        p.drawImage(dest, m_image);
        p.setClipping(false);
        const int sx = ox + static_cast<int>(dw * m_split);
        p.setPen(QPen(Qt::white, 2));
        p.drawLine(sx, oy, sx, oy + dh);
    } else {
        p.drawImage(dest, m_image);
    }
}

void ImageViewport::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (m_cropMode)
        m_cropOverlay->setGeometry(rect());
    update();
}

void ImageViewport::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Backslash) {
        setCompareMode(!m_compare);
        return;
    }
    QWidget::keyPressEvent(e);
}

void ImageViewport::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && zoomable()) {
        m_pressPos = e->pos();
        m_panAtPress = m_panOffset;
        m_dragging = false;
        e->accept();
        return;
    }
    QWidget::mousePressEvent(e);
}

void ImageViewport::mouseMoveEvent(QMouseEvent* e) {
    if ((e->buttons() & Qt::LeftButton) && zoomable() && !m_fitMode) {
        const QPoint delta = e->pos() - m_pressPos;
        if (!m_dragging && delta.manhattanLength() > 3) {
            m_dragging = true;
            setCursor(Qt::ClosedHandCursor);
        }
        if (m_dragging) {
            m_panOffset = m_panAtPress + delta;
            clampPan();
            update();
            emit viewChanged();
            e->accept();
            return;
        }
    }
    QWidget::mouseMoveEvent(e);
}

void ImageViewport::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && zoomable()) {
        if (m_dragging) {
            m_dragging = false;
            setCursor(m_fitMode ? Qt::ArrowCursor : Qt::OpenHandCursor);
            e->accept();
            return;
        }
        // A plain click toggles FIT <-> 1:1, keeping the clicked point anchored.
        if (m_fitMode) {
            const float s = fitScale();
            const int fox = (width() - static_cast<int>(m_image.width() * s)) / 2;
            const int foy = (height() - static_cast<int>(m_image.height() * s)) / 2;
            const float imgX = (e->pos().x() - fox) / s;
            const float imgY = (e->pos().y() - foy) / s;
            m_fitMode = false;
            m_panOffset = QPoint(e->pos().x() - static_cast<int>(imgX),
                                 e->pos().y() - static_cast<int>(imgY));
            clampPan();
            setCursor(Qt::OpenHandCursor);
        } else {
            m_fitMode = true;
            setCursor(Qt::ArrowCursor);
        }
        emit zoomChanged(m_fitMode);
        update();
        emit viewChanged();
        e->accept();
        return;
    }
    QWidget::mouseReleaseEvent(e);
}

} // namespace mylr