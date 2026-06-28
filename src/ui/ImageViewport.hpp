#pragma once

#include "../core/DevelopSettings.hpp"
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QWidget>

namespace mylr {

class CropOverlay;

class ImageViewport : public QWidget {
    Q_OBJECT
public:
    explicit ImageViewport(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void setBeforeImage(const QImage& image);
    void setCompareMode(bool enabled);
    bool compareMode() const { return m_compare; }
    void setCompareSplit(float ratio);

    void setCropMode(bool enabled);
    bool cropMode() const { return m_cropMode; }
    void setCropGeometry(const GeometrySettings& geom);

    // Reference Photo Mode: locked reference on the left, live active on the right.
    void setReferenceImage(const QImage& image);
    void setReferenceMode(bool enabled);
    bool referenceMode() const { return m_referenceMode; }

    // FIT (scale image to viewport) vs 1:1 (100%) toggled by clicking the image.
    bool isFitMode() const { return m_fitMode; }
    // Normalized (0..1) region of the image currently visible; whole image in FIT.
    QRectF visibleRegionNormalized() const;
    // Recenter the (zoomed) view so the normalized image point is centered.
    void centerViewOnNormalized(QPointF n);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

signals:
    void compareToggled(bool enabled);
    void cropGeometryChanged(const GeometrySettings& geom);
    void zoomChanged(bool fitMode);
    void viewChanged();   // pan/zoom/image changed; navigator should refresh

private:
    float fitScale() const;     // scale used in FIT mode
    void clampPan();            // keep the zoomed image within sensible bounds
    bool zoomable() const;      // zoom/pan only in plain (non-crop/compare) view

    QImage m_image;
    QImage m_beforeImage;
    bool m_compare = false;
    float m_split = 0.5f;

    bool m_cropMode = false;
    CropOverlay* m_cropOverlay = nullptr;

    QImage m_referenceImage;
    bool m_referenceMode = false;

    bool m_fitMode = true;
    QPoint m_panOffset;   // top-left of the image in widget coords when zoomed (1:1)
    QPoint m_pressPos;
    QPoint m_panAtPress;
    bool m_dragging = false;
};

} // namespace mylr
