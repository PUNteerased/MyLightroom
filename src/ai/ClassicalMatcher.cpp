#include "ClassicalMatcher.hpp"
#include <QtMath>
#include <cmath>

namespace mylr {

float ClassicalMatcher::wasserstein1(const QVector<float>& a, const QVector<float>& b) const {
    const int n = qMin(a.size(), b.size());
    if (n == 0) return 0.f;
    float sum = 0.f;
    for (int i = 0; i < n; ++i)
        sum += std::fabs(a[i] - b[i]);
    return sum / n;
}

float ClassicalMatcher::solveExposure(const QVector<float>& refCdf,
                                      const QVector<float>& tgtCdf) const {
    float bestEv = 0.f, bestDist = 1e9f;
    for (float ev = -3.f; ev <= 3.f; ev += 0.05f) {
        float dist = 0.f;
        const int n = qMin(refCdf.size(), tgtCdf.size());
        for (int i = 1; i < n; ++i) {
            const int shifted = qBound(0, static_cast<int>(i * std::pow(2.f, ev)), n - 1);
            dist += std::fabs(refCdf[i] - tgtCdf[shifted]);
        }
        dist /= n;
        if (dist < bestDist) {
            bestDist = dist;
            bestEv = ev;
        }
    }
    return bestEv;
}

float ClassicalMatcher::solveContrast(const QVector<float>& refCdf,
                                      const QVector<float>& tgtCdf) const {
    if (refCdf.isEmpty() || tgtCdf.isEmpty()) return 0.f;
    const int mid = refCdf.size() / 2;
    const float refSpread = refCdf[qMin(refCdf.size() - 1, mid + 100)] -
                            refCdf[qMax(0, mid - 100)];
    const float tgtSpread = tgtCdf[qMin(tgtCdf.size() - 1, mid + 100)] -
                            tgtCdf[qMax(0, mid - 100)];
    if (tgtSpread < 1e-4f) return 0.f;
    const float ratio = refSpread / tgtSpread;
    return qBound(-100.f, (ratio - 1.f) * 100.f, 100.f);
}

void ClassicalMatcher::solveWhiteBalance(const SceneFingerprint& ref,
                                         const SceneFingerprint& target, float& temp,
                                         float& tint) const {
    const float da = ref.labAMean - target.labAMean;
    const float db = ref.labBMean - target.labBMean;
    temp += da * 50.f;
    tint += db * 20.f;
    temp = qBound(2000.f, temp, 50000.f);
    tint = qBound(-150.f, tint, 150.f);
}

void ClassicalMatcher::solveToneSplit(const SceneFingerprint& ref,
                                      const SceneFingerprint& target,
                                      BasicSettings& basic) const {
    if (ref.zoneDistribution.size() < 9 || target.zoneDistribution.size() < 9) return;
    float refShadow = 0, tgtShadow = 0, refHigh = 0, tgtHigh = 0;
    for (int i = 0; i < 3; ++i) {
        refShadow += ref.zoneDistribution[i];
        tgtShadow += target.zoneDistribution[i];
    }
    for (int i = 6; i < 9; ++i) {
        refHigh += ref.zoneDistribution[i];
        tgtHigh += target.zoneDistribution[i];
    }
    if (tgtShadow > refShadow + 0.05f)
        basic.shadows += (tgtShadow - refShadow) * 200.f;
    if (tgtHigh > refHigh + 0.05f)
        basic.highlights -= (tgtHigh - refHigh) * 200.f;
    basic.shadows = qBound(-100.f, basic.shadows, 100.f);
    basic.highlights = qBound(-100.f, basic.highlights, 100.f);
}

DevelopSettings ClassicalMatcher::match(const SceneFingerprint& ref,
                                        const SceneFingerprint& target,
                                        const DevelopSettings& refDevelop) const {
    DevelopSettings result = refDevelop.clone();

    // Earth-mover (Wasserstein-1) distance between the luminance distributions:
    // when the two histograms already line up there is nothing to solve, so we
    // skip exposure/contrast to avoid amplifying noise on near-identical frames.
    const float lumDist = wasserstein1(ref.luminanceCdf, target.luminanceCdf);
    if (lumDist > 0.004f) {
        result.basic.exposure = solveExposure(ref.luminanceCdf, target.luminanceCdf);
        result.basic.contrast = solveContrast(ref.luminanceCdf, target.luminanceCdf);
    }
    solveWhiteBalance(ref, target, result.basic.temp, result.basic.tint);
    solveToneSplit(ref, target, result.basic);

    return result;
}

} // namespace mylr
