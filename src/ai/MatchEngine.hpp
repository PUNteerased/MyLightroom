#pragma once

#include "../core/DevelopSettings.hpp"
#include "../core/EditGraph.hpp"
#include "../raw/RawDecoder.hpp"
#include "../render/DevelopPipeline.hpp"
#include "ClassicalMatcher.hpp"
#include "FeatureExtractor.hpp"
#include "OnnxRegressor.hpp"
#include <QObject>

namespace mylr {

struct MatchResult {
    DevelopSettings settings;
    float loss = 0.f;
    float confidence = 0.f;
    QString warning;
};

class MatchEngine : public QObject {
    Q_OBJECT
public:
    explicit MatchEngine(QObject* parent = nullptr);

    void setReferenceProfile(const MatchProfile& profile);
    MatchProfile captureReference(const QImage& rendered, const RawMetadata& meta,
                                  const DevelopSettings& settings, const QString& imageId);

    bool hasReference() const { return m_hasReference; }
    const MatchProfile& referenceProfile() const { return m_reference; }

    MatchResult matchImage(const QImage& source, const RawMetadata& meta) const;
    QVector<MatchResult> batchMatch(const QVector<QPair<QImage, RawMetadata>>& targets);

    float computeLoss(const SceneFingerprint& ref, const SceneFingerprint& current) const;

signals:
    void matchProgress(int current, int total);

private:
    DevelopSettings refine(const QImage& source, DevelopSettings initial,
                           const SceneFingerprint& refFp) const;
    QVector<float> buildRegressorInput(const SceneFeatures& target,
                                       const SceneFeatures& ref) const;

    MatchProfile m_reference;
    ClassicalMatcher m_classical;
    FeatureExtractor m_extractor;
    OnnxRegressor m_regressor;
    DevelopPipeline m_pipeline;
    bool m_hasReference = false;
};

} // namespace mylr
