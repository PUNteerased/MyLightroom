#include "SceneClassifier.hpp"
#include <QtMath>

namespace mylr {

QString SceneClassifier::classify(const SceneFingerprint& fp) {
    if (fp.labLMean < 35.f) return QStringLiteral("night");
    if (fp.labLMean > 60.f && fp.labBMean > 5.f) return QStringLiteral("outdoor");
    if (fp.dominantHues.size() > 1 && fp.dominantHues[1] > 0.15f) return QStringLiteral("portrait");
    if (fp.labLMean < 50.f && fp.shadowClipPct > 5.f) return QStringLiteral("indoor");
    return QStringLiteral("landscape");
}

float SceneClassifier::sceneCompatibility(const QString& a, const QString& b) {
    if (a == b) return 1.f;
    if ((a == QStringLiteral("outdoor") && b == QStringLiteral("landscape")) ||
        (a == QStringLiteral("landscape") && b == QStringLiteral("outdoor")))
        return 0.8f;
    if (a == QStringLiteral("indoor") && b == QStringLiteral("outdoor")) return 0.3f;
    if (a == QStringLiteral("night") && b == QStringLiteral("outdoor")) return 0.4f;
    return 0.6f;
}

} // namespace mylr
