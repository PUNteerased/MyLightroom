#include "FeatureExtractor.hpp"
#include "../raw/RawDecoder.hpp"
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace mylr {

QVector<float> FeatureExtractor::buildCdf(const QVector<int>& hist, int bins) const {
    QVector<float> cdf(bins, 0.f);
    long long total = 0;
    for (int v : hist) total += v;
    if (total == 0) return cdf;
    long long acc = 0;
    for (int i = 0; i < bins; ++i) {
        acc += hist[i];
        cdf[i] = static_cast<float>(acc) / static_cast<float>(total);
    }
    return cdf;
}

void FeatureExtractor::computeLabStats(const QImage& img, SceneFingerprint& fp) const {
    double sumL = 0, sumA = 0, sumB = 0;
    double sumL2 = 0, sumA2 = 0, sumB2 = 0;
    const int n = img.width() * img.height();
    if (n == 0) return;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            float L, a, b;
            ColorTransform::rgbToLab(qRed(line[x]) / 255.f, qGreen(line[x]) / 255.f,
                                     qBlue(line[x]) / 255.f, L, a, b);
            sumL += L; sumA += a; sumB += b;
            sumL2 += L * L; sumA2 += a * a; sumB2 += b * b;
        }
    }
    fp.labLMean = static_cast<float>(sumL / n);
    fp.labAMean = static_cast<float>(sumA / n);
    fp.labBMean = static_cast<float>(sumB / n);
    fp.labLStd = static_cast<float>(std::sqrt(sumL2 / n - fp.labLMean * fp.labLMean));
    fp.labAStd = static_cast<float>(std::sqrt(sumA2 / n - fp.labAMean * fp.labAMean));
    fp.labBStd = static_cast<float>(std::sqrt(sumB2 / n - fp.labBMean * fp.labBMean));
}

void FeatureExtractor::computeZones(const QImage& img, SceneFingerprint& fp) const {
    fp.zoneDistribution.fill(0.f, 9);
    const int n = img.width() * img.height();
    if (n == 0) return;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const int lum = static_cast<int>(0.2126 * qRed(line[x]) + 0.7152 * qGreen(line[x]) +
                                             0.0722 * qBlue(line[x]));
            const int zone = qBound(0, lum * 9 / 256, 8);
            fp.zoneDistribution[zone] += 1.f;
        }
    }
    for (float& z : fp.zoneDistribution) z /= static_cast<float>(n);
}

void FeatureExtractor::computeDominantHues(const QImage& img, SceneFingerprint& fp) const {
    fp.dominantHues.fill(0.f, 8);
    const int n = img.width() * img.height();
    if (n == 0) return;
    static const float centers[] = {0, 30, 60, 120, 180, 240, 270, 300};
    for (int y = 0; y < img.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const float r = qRed(line[x]) / 255.f, g = qGreen(line[x]) / 255.f, b = qBlue(line[x]) / 255.f;
            const float maxC = qMax(r, qMax(g, b)), minC = qMin(r, qMin(g, b));
            if (maxC == minC) continue;
            float h = 0.f;
            if (maxC == r) h = 60.f * std::fmod((g - b) / (maxC - minC), 6.f);
            else if (maxC == g) h = 60.f * ((b - r) / (maxC - minC) + 2.f);
            else h = 60.f * ((r - g) / (maxC - minC) + 4.f);
            if (h < 0.f) h += 360.f;
            for (int i = 0; i < 8; ++i) {
                float d = std::fabs(h - centers[i]);
                if (d > 180.f) d = 360.f - d;
                if (d < 30.f) fp.dominantHues[i] += 1.f;
            }
        }
    }
    for (float& v : fp.dominantHues) v /= static_cast<float>(n);
}

QString FeatureExtractor::classifyScene(const SceneFingerprint& fp) const {
    if (fp.labLMean < 35.f) return QStringLiteral("night");
    if (fp.labLMean > 60.f && fp.labBMean > 5.f) return QStringLiteral("outdoor");
    if (fp.dominantHues.size() > 1 && fp.dominantHues[1] > 0.15f) return QStringLiteral("portrait");
    return QStringLiteral("landscape");
}

SceneFeatures FeatureExtractor::extract(const QImage& renderedIn, const RawMetadata& meta) const {
    SceneFeatures sf;
    // Pixel loops below read 32-bit ARGB words; previews may be Format_RGB888,
    // so normalise to a 32-bit buffer to avoid scanline overruns (crashes).
    const QImage rendered = (renderedIn.format() == QImage::Format_RGB32 ||
                             renderedIn.format() == QImage::Format_ARGB32)
                                ? renderedIn
                                : renderedIn.convertToFormat(QImage::Format_RGB32);
    DevelopPipeline pipe;
    const HistogramData h = pipe.computeHistogram(rendered);

    auto upscale = [](const QVector<int>& src, int bins) {
        QVector<int> dst(bins, 0);
        for (int i = 0; i < 256; ++i) {
            const int bin = i * (bins - 1) / 255;
            dst[bin] += src[i];
        }
        return dst;
    };

    FeatureExtractor extractor;
    const QVector<int> lumUp = upscale(h.luminance, HistogramBins);
    const QVector<int> rUp = upscale(h.red, HistogramBins);
    const QVector<int> gUp = upscale(h.green, HistogramBins);
    const QVector<int> bUp = upscale(h.blue, HistogramBins);

    sf.fingerprint.luminanceCdf = buildCdf(lumUp, HistogramBins);
    sf.fingerprint.redCdf = buildCdf(rUp, HistogramBins);
    sf.fingerprint.greenCdf = buildCdf(gUp, HistogramBins);
    sf.fingerprint.blueCdf = buildCdf(bUp, HistogramBins);

    computeLabStats(rendered, sf.fingerprint);
    computeZones(rendered, sf.fingerprint);
    computeDominantHues(rendered, sf.fingerprint);

    const int totalPx = rendered.width() * rendered.height();
    sf.fingerprint.highlightClipPct = totalPx > 0
        ? static_cast<float>(h.clipHighR + h.clipHighG + h.clipHighB) / (3.f * totalPx) * 100.f
        : 0.f;
    sf.fingerprint.shadowClipPct =
        totalPx > 0 ? static_cast<float>(h.clipLow) / totalPx * 100.f : 0.f;

    sf.context.camera = meta.cameraModel;
    sf.context.iso = meta.iso;
    sf.context.evBaseline = meta.evBaseline;
    sf.context.sceneType = classifyScene(sf.fingerprint);
    sf.compressed128 = compress(sf.fingerprint);
    return sf;
}

QVector<float> FeatureExtractor::compress(const SceneFingerprint& fp) const {
    QVector<float> out(CompressedSize, 0.f);
    int idx = 0;
    auto copySub = [&](const QVector<float>& src, int count) {
        for (int i = 0; i < count && idx < CompressedSize; ++i, ++idx) {
            const int si = src.isEmpty() ? 0 : i * src.size() / count;
            out[idx] = si < src.size() ? src[si] : 0.f;
        }
    };
    copySub(fp.luminanceCdf, 32);
    copySub(fp.redCdf, 16);
    copySub(fp.greenCdf, 16);
    copySub(fp.blueCdf, 16);
    out[idx++] = fp.labLMean / 100.f;
    out[idx++] = fp.labAMean / 128.f;
    out[idx++] = fp.labBMean / 128.f;
    copySub(fp.zoneDistribution, 9);
    copySub(fp.dominantHues, 8);
    out[idx++] = fp.highlightClipPct / 100.f;
    out[idx++] = fp.shadowClipPct / 100.f;
    while (idx < CompressedSize) out[idx++] = 0.f;
    return out;
}

} // namespace mylr
