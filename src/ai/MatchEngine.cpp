#include "MatchEngine.hpp"
#include "SceneClassifier.hpp"
#include <QtMath>

namespace mylr {

MatchEngine::MatchEngine(QObject* parent) : QObject(parent) {
    m_regressor.loadModel(QStringLiteral("models/parameter_regressor.onnx"));
}

void MatchEngine::setReferenceProfile(const MatchProfile& profile) {
    m_reference = profile;
    m_hasReference = true;
}

MatchProfile MatchEngine::captureReference(const QImage& rendered, const RawMetadata& meta,
                                           const DevelopSettings& settings,
                                           const QString& imageId) {
    MatchProfile p;
    p.version = 1;
    p.referenceImageId = imageId;
    const SceneFeatures sf = m_extractor.extract(rendered, meta);
    p.fingerprint = sf.fingerprint;
    p.developState = settings.clone();
    p.sceneContext = sf.context;
    m_reference = p;
    m_hasReference = true;
    return p;
}

float MatchEngine::computeLoss(const SceneFingerprint& ref,
                               const SceneFingerprint& current) const {
    float loss = 0.f;
    const int n = qMin(ref.luminanceCdf.size(), current.luminanceCdf.size());
    for (int i = 0; i < n; ++i)
        loss += std::fabs(ref.luminanceCdf[i] - current.luminanceCdf[i]);
    loss /= qMax(1, n);
    loss += std::fabs(ref.labLMean - current.labLMean) / 100.f;
    loss += std::fabs(ref.labAMean - current.labAMean) / 50.f;
    loss += std::fabs(ref.labBMean - current.labBMean) / 50.f;
    return loss;
}

QVector<float> MatchEngine::buildRegressorInput(const SceneFeatures& target,
                                                const SceneFeatures& ref) const {
    QVector<float> input(OnnxRegressor::InputSize, 0.f);
    int idx = 0;
    for (float v : target.compressed128) {
        if (idx >= 128) break;
        input[idx++] = v;
    }
    for (float v : ref.compressed128) {
        if (idx >= 256) break;
        input[idx++] = v;
    }
    const int n = qMin(target.fingerprint.luminanceCdf.size(), ref.fingerprint.luminanceCdf.size());
    for (int i = 0; i < 128 && i < n; ++i)
        input[256 + i] = ref.fingerprint.luminanceCdf[i] - target.fingerprint.luminanceCdf[i];
    idx = 384;
    input[idx++] = (target.context.iso - ref.context.iso) / 1000.f;
    input[idx++] = target.context.evBaseline - ref.context.evBaseline;
    return input;
}

DevelopSettings MatchEngine::refine(const QImage& source, DevelopSettings initial,
                                    const SceneFingerprint& refFp) const {
    DevelopSettings best = initial;
    float bestLoss = 1e9f;

    const float ranges[8] = {500.f, 30.f, 0.5f, 20.f, 30.f, 30.f, 30.f, 30.f};
    float* params[8] = {&best.basic.temp, &best.basic.tint, &best.basic.exposure,
                        &best.basic.contrast, &best.basic.highlights, &best.basic.shadows,
                        &best.basic.whites, &best.basic.blacks};

    for (int iter = 0; iter < 15; ++iter) {
        const QImage rendered = m_pipeline.render(source, best, 512);
        const SceneFeatures sf = m_extractor.extract(rendered, RawMetadata{});
        const float loss = computeLoss(refFp, sf.fingerprint);
        if (loss < bestLoss) bestLoss = loss;
        if (loss < 0.02f) break;

        for (int p = 0; p < 8; ++p) {
            const float orig = *params[p];
            for (float sign : {1.f, -1.f}) {
                *params[p] = orig + sign * ranges[p] * 0.1f;
                const QImage trial = m_pipeline.render(source, best, 512);
                const SceneFeatures tsf = m_extractor.extract(trial, RawMetadata{});
                if (computeLoss(refFp, tsf.fingerprint) < loss)
                    break;
                *params[p] = orig;
            }
        }
    }
    return best;
}

MatchResult MatchEngine::matchImage(const QImage& source, const RawMetadata& meta) const {
    MatchResult r;
    if (!m_hasReference) {
        r.warning = QStringLiteral("No reference profile set");
        return r;
    }

    const DevelopSettings neutral = DevelopSettings::defaults();
    const QImage neutralRender = m_pipeline.render(source, neutral, 512);
    const SceneFeatures targetFeatures = m_extractor.extract(neutralRender, meta);

    const float compat = SceneClassifier::sceneCompatibility(m_reference.sceneContext.sceneType,
                                                             targetFeatures.context.sceneType);
    if (compat < 0.5f)
        r.warning = QStringLiteral("Scene type differs significantly — color transfer reduced");

    // --- Stage 1: Classical CDF/Lab matcher -------------------------------
    // Closed-form estimate of exposure/contrast/WB/tone-split from the
    // reference vs. target distributions (Wasserstein-gated, CPU only).
    DevelopSettings theta0 =
        m_classical.match(m_reference.fingerprint, targetFeatures.fingerprint, m_reference.developState);

    // --- Stage 2: Lightweight CPU regressor -------------------------------
    // Predicts a small correction delta on top of the classical estimate.
    // Stays on the CPU via OnnxRegressor::heuristicPredict (no GPU/ONNX dep).
    SceneFeatures refFeat;
    refFeat.compressed128 = m_extractor.compress(m_reference.fingerprint);
    refFeat.fingerprint = m_reference.fingerprint;
    refFeat.context = m_reference.sceneContext;
    const QVector<float> regInput = buildRegressorInput(targetFeatures, refFeat);
    const QVector<float> delta = m_regressor.predict(regInput);
    m_regressor.applyDelta(theta0, delta);

    // --- Stage 3: Iterative refinement ------------------------------------
    // Coordinate-descent loop that minimises the fingerprint loss.
    r.settings = refine(source, theta0, m_reference.fingerprint);
    const QImage finalRender = m_pipeline.render(source, r.settings, 512);
    const SceneFeatures finalFeat = m_extractor.extract(finalRender, meta);
    r.loss = computeLoss(m_reference.fingerprint, finalFeat.fingerprint);
    r.confidence = qBound(0.f, 1.f - r.loss, 1.f);

    if (std::abs(meta.iso - m_reference.sceneContext.iso) > 800)
        r.warning = QStringLiteral("ISO differs by >2 stops — shadow noise not matched");

    return r;
}

QVector<MatchResult> MatchEngine::batchMatch(
    const QVector<QPair<QImage, RawMetadata>>& targets) {
    QVector<MatchResult> results;
    results.reserve(targets.size());
    for (int i = 0; i < targets.size(); ++i) {
        results.append(matchImage(targets[i].first, targets[i].second));
        emit matchProgress(i + 1, targets.size());
    }
    return results;
}

} // namespace mylr