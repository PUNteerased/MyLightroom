#pragma once

#include "../core/DevelopSettings.hpp"
#include "../core/EditGraph.hpp"

namespace mylr {

class ClassicalMatcher {
public:
    DevelopSettings match(const SceneFingerprint& ref, const SceneFingerprint& target,
                          const DevelopSettings& refDevelop) const;

private:
    float wasserstein1(const QVector<float>& a, const QVector<float>& b) const;
    float solveExposure(const QVector<float>& refCdf, const QVector<float>& tgtCdf) const;
    float solveContrast(const QVector<float>& refCdf, const QVector<float>& tgtCdf) const;
    void solveWhiteBalance(const SceneFingerprint& ref, const SceneFingerprint& target,
                           float& temp, float& tint) const;
    void solveToneSplit(const SceneFingerprint& ref, const SceneFingerprint& target,
                        BasicSettings& basic) const;
};

} // namespace mylr
