#include "AutoAdjust.hpp"

#include <QtMath>
#include <algorithm>
#include <array>
#include <cmath>

namespace mylr {

namespace {

struct ImageStats {
    double meanR = 0, meanG = 0, meanB = 0;
    double medianLuma = 0.5;
    double p01 = 0.0;  // 1st percentile luminance (0..1)
    double p99 = 1.0;  // 99th percentile luminance (0..1)
    bool valid = false;
};

ImageStats computeStats(const QImage& srcIn) {
    ImageStats s;
    if (srcIn.isNull()) return s;
    QImage img = srcIn.convertToFormat(QImage::Format_RGB32);

    std::array<long long, 256> lumaHist{};
    lumaHist.fill(0);
    long long total = 0;
    double sumR = 0, sumG = 0, sumB = 0;

    // Subsample for speed on large images.
    const int step = qMax(1, qMin(img.width(), img.height()) / 512);
    for (int y = 0; y < img.height(); y += step) {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); x += step) {
            const int r = qRed(line[x]);
            const int g = qGreen(line[x]);
            const int b = qBlue(line[x]);
            sumR += r;
            sumG += g;
            sumB += b;
            const int luma = qBound(0, static_cast<int>(0.2126 * r + 0.7152 * g + 0.0722 * b), 255);
            ++lumaHist[luma];
            ++total;
        }
    }
    if (total == 0) return s;

    s.meanR = sumR / total;
    s.meanG = sumG / total;
    s.meanB = sumB / total;

    auto percentile = [&](double frac) {
        const long long target = static_cast<long long>(frac * total);
        long long acc = 0;
        for (int i = 0; i < 256; ++i) {
            acc += lumaHist[i];
            if (acc >= target) return i / 255.0;
        }
        return 1.0;
    };
    s.p01 = percentile(0.01);
    s.medianLuma = percentile(0.5);
    s.p99 = percentile(0.99);
    s.valid = true;
    return s;
}

} // namespace

BasicSettings AutoAdjust::autoExposure(const QImage& source, const BasicSettings& current) {
    BasicSettings out = current;
    const ImageStats s = computeStats(source);
    if (!s.valid) return out;

    // Push the median toward a mid-gray target (~0.45) via exposure stops.
    const double target = 0.45;
    const double median = qBound(0.02, s.medianLuma, 0.98);
    double stops = std::log2(target / median);
    stops = qBound(-3.0, stops, 3.0);
    out.exposure = static_cast<float>(stops);
    return out;
}

BasicSettings AutoAdjust::autoWhiteBalance(const QImage& source, const BasicSettings& current) {
    BasicSettings out = current;
    const ImageStats s = computeStats(source);
    if (!s.valid) return out;

    const double gray = (s.meanR + s.meanG + s.meanB) / 3.0;
    if (gray < 1.0) return out;

    // Gray-world: if red dominates the image is warm -> cool it (lower temp).
    const double rRatio = s.meanR / gray;
    const double bRatio = s.meanB / gray;

    double temp = 6500.0 - (rRatio - bRatio) * 3500.0;
    out.temp = static_cast<float>(qBound(2500.0, temp, 9500.0));

    // Tint compensates the green channel deviation.
    const double gRatio = s.meanG / gray;
    out.tint = static_cast<float>(qBound(-60.0, (gRatio - 1.0) * -300.0, 60.0));
    return out;
}

BasicSettings AutoAdjust::autoContrast(const QImage& source, const BasicSettings& current) {
    BasicSettings out = current;
    const ImageStats s = computeStats(source);
    if (!s.valid) return out;

    const double range = qBound(0.05, s.p99 - s.p01, 1.0);
    // Narrow tonal range -> add contrast; already-wide range -> little/none.
    double contrast = (0.85 - range) * 120.0;
    out.contrast = static_cast<float>(qBound(-50.0, contrast, 60.0));

    // Set black/white points to stretch toward full range.
    out.blacks = static_cast<float>(qBound(-80.0, -s.p01 * 200.0, 0.0));
    out.whites = static_cast<float>(qBound(0.0, (1.0 - s.p99) * 200.0, 80.0));
    return out;
}

BasicSettings AutoAdjust::autoTone(const QImage& source, const BasicSettings& current) {
    BasicSettings out = autoExposure(source, current);
    out = autoContrast(source, out);

    const ImageStats s = computeStats(source);
    if (s.valid) {
        // Recover highlights / open shadows based on clipping at the extremes.
        if (s.p99 > 0.92) out.highlights = qBound(-100.f, out.highlights - 35.f, 100.f);
        if (s.p01 < 0.06) out.shadows = qBound(-100.f, out.shadows + 35.f, 100.f);
    }
    out = autoWhiteBalance(source, out);
    return out;
}

} // namespace mylr
