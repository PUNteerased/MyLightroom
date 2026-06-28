#pragma once

#include "../core/DevelopSettings.hpp"
#include <QImage>
#include <QVector>

namespace mylr {

struct HistogramData {
    QVector<int> red;
    QVector<int> green;
    QVector<int> blue;
    QVector<int> luminance;
    int clipHighR = 0;
    int clipHighG = 0;
    int clipHighB = 0;
    int clipLow = 0;
    static constexpr int BinCount = 256;
};

class ColorTransform {
public:
    static void rgbToLab(float r, float g, float blue, float& L, float& a, float& bOut);
    static void applyWhiteBalance(float& r, float& g, float& b, float temp, float tint,
                                  const float wbCoeffs[4]);
    static float srgbToLinear(float c);
    static float linearToSrgb(float c);
};

class DevelopPipeline {
public:
    // Primary entry point: process a 16-bit scene-referred linear source
    // (Format_RGBX64) through the full Lightroom-style pipeline and return an
    // 8-bit sRGB display image. wbCoeffs are the as-shot cam_mul (green-normalized).
    QImage renderLinear(const QImage& linear64, const DevelopSettings& settings,
                        const float wbCoeffs[4], int maxEdge = 0) const;

    // Backward-compatible entry: accepts either an RGBX64 linear image (treated as
    // already-white-balanced, neutral coeffs) or an 8-bit sRGB image (linearized
    // first). Used for non-RAW inputs and callers that still pass QImages.
    QImage render(const QImage& source, const DevelopSettings& settings, int maxEdge = 0) const;

    HistogramData computeHistogram(const QImage& image) const;
    QImage applyCropRotate(const QImage& source, const GeometrySettings& geom) const;
};

} // namespace mylr
