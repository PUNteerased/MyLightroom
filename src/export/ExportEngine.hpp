#pragma once

#include "../core/DevelopSettings.hpp"
#include "../render/DevelopPipeline.hpp"
#include <QImage>
#include <QString>

namespace mylr {

struct ExportSettings {
    QString format = QStringLiteral("jpeg");
    int quality = 92;
    int maxLongEdge = 0;
    QString outputDir;
    bool embedProfile = true;
    // File naming template for batch export. {name} = original base name,
    // {seq} = 1-based sequence number.
    QString fileNameTemplate = QStringLiteral("{name}");
    WatermarkSettings watermark;
};

class ExportEngine {
public:
    // source may be a 16-bit linear RGBX64 (RAW) image, in which case wbCoeffs
    // (as-shot cam_mul) must be supplied; or an 8-bit sRGB image (wbCoeffs null).
    bool exportImage(const QImage& source, const DevelopSettings& develop,
                     const ExportSettings& settings, const QString& outputPath,
                     const float wbCoeffs[4] = nullptr,
                     const float rgbCam[9] = nullptr,
                     bool isCameraLinear = true) const;
    bool exportBatch(const QVector<QPair<QImage, QString>>& items,
                     const DevelopSettings& develop, const ExportSettings& settings) const;

private:
    QImage applyWatermark(const QImage& img, const WatermarkSettings& wm) const;
    QImage applySharpen(const QImage& img, const DetailSettings& detail) const;
    DevelopPipeline m_pipeline;
};

} // namespace mylr
