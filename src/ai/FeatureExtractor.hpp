#pragma once

#include "../core/EditGraph.hpp"
#include "../raw/RawDecoder.hpp"
#include "../render/DevelopPipeline.hpp"
#include <QImage>

namespace mylr {

struct SceneFeatures {
    QVector<float> compressed128;
    SceneFingerprint fingerprint;
    SceneContext context;
};

class FeatureExtractor {
public:
    static constexpr int HistogramBins = 1024;
    static constexpr int CompressedSize = 128;

    SceneFeatures extract(const QImage& rendered, const RawMetadata& meta) const;
    QVector<float> compress(const SceneFingerprint& fp) const;

private:
    QVector<float> buildCdf(const QVector<int>& hist, int bins) const;
    void computeLabStats(const QImage& img, SceneFingerprint& fp) const;
    void computeZones(const QImage& img, SceneFingerprint& fp) const;
    void computeDominantHues(const QImage& img, SceneFingerprint& fp) const;
    QString classifyScene(const SceneFingerprint& fp) const;
};

} // namespace mylr
