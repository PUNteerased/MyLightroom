#include "ExportEngine.hpp"
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QtMath>

namespace mylr {

QImage ExportEngine::applyWatermark(const QImage& img, const WatermarkSettings& wm) const {
    if (!wm.enabled || wm.imagePath.isEmpty()) return img;
    QImage wmImg(wm.imagePath);
    if (wmImg.isNull()) return img;

    QImage result = img.copy();
    const int targetW = static_cast<int>(img.width() * wm.scale);
    wmImg = wmImg.scaledToWidth(qMax(1, targetW), Qt::SmoothTransformation);

    int x = wm.marginPx, y = wm.marginPx;
    if (wm.position.contains(QStringLiteral("right")))
        x = img.width() - wmImg.width() - wm.marginPx;
    else if (wm.position.contains(QStringLiteral("center")))
        x = (img.width() - wmImg.width()) / 2;
    if (wm.position.contains(QStringLiteral("bottom")))
        y = img.height() - wmImg.height() - wm.marginPx;
    else if (wm.position.contains(QStringLiteral("center")) && !wm.position.contains(QStringLiteral("left")))
        y = (img.height() - wmImg.height()) / 2;

    QPainter p(&result);
    p.setOpacity(wm.opacity);
    p.drawImage(x, y, wmImg);
    return result;
}

QImage ExportEngine::applySharpen(const QImage& img, const DetailSettings& detail) const {
    if (detail.sharpenAmount <= 0.f) return img;
    QImage out = img.convertToFormat(QImage::Format_RGB32);
    const float amount = detail.sharpenAmount / 100.f;
    QImage blurred = out.scaled(out.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                         .scaled(out.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    for (int y = 1; y < out.height() - 1; ++y) {
        auto* dst = reinterpret_cast<QRgb*>(out.scanLine(y));
        const QRgb* src = reinterpret_cast<const QRgb*>(blurred.constScanLine(y));
        for (int x = 1; x < out.width() - 1; ++x) {
            const int r = qBound(0, static_cast<int>(qRed(src[x]) + (qRed(dst[x]) - qRed(src[x])) * amount), 255);
            const int g = qBound(0, static_cast<int>(qGreen(src[x]) + (qGreen(dst[x]) - qGreen(src[x])) * amount), 255);
            const int b = qBound(0, static_cast<int>(qBlue(src[x]) + (qBlue(dst[x]) - qBlue(src[x])) * amount), 255);
            dst[x] = qRgb(r, g, b);
        }
    }
    return out;
}

bool ExportEngine::exportImage(const QImage& source, const DevelopSettings& develop,
                               const ExportSettings& settings, const QString& outputPath,
                               const float wbCoeffs[4], const float rgbCam[9],
                               bool isCameraLinear) const {
    const float neutralWb[4] = {1.f, 1.f, 1.f, 1.f};
    static const float kIdentityRgbCam[9] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f};
    QImage rendered = source.format() == QImage::Format_RGBX64
                          ? m_pipeline.renderLinear(source, develop,
                                                    wbCoeffs ? wbCoeffs : neutralWb,
                                                    rgbCam ? rgbCam : kIdentityRgbCam,
                                                    settings.maxLongEdge, isCameraLinear)
                          : m_pipeline.render(source, develop, settings.maxLongEdge);
    if (rendered.isNull()) return false;

    // Detail/sharpen is already applied inside the pipeline; only watermark here.
    rendered = applyWatermark(rendered, settings.watermark);

    const QString fmt = settings.format.toLower();
    if (fmt == QStringLiteral("png"))
        return rendered.save(outputPath, "PNG");
    if (fmt == QStringLiteral("tiff") || fmt == QStringLiteral("tif"))
        return rendered.save(outputPath, "TIFF");
    return rendered.save(outputPath, "JPEG", settings.quality);
}

bool ExportEngine::exportBatch(const QVector<QPair<QImage, QString>>& items,
                               const DevelopSettings& develop,
                               const ExportSettings& settings) const {
    QDir().mkpath(settings.outputDir);
    bool allOk = true;
    for (const auto& item : items) {
        const QFileInfo fi(item.second);
        const QString out = QDir(settings.outputDir).filePath(
            fi.completeBaseName() + QStringLiteral(".") + settings.format);
        if (!exportImage(item.first, develop, settings, out))
            allOk = false;
    }
    return allOk;
}

} // namespace mylr
