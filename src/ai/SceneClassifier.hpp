#pragma once

#include "../core/EditGraph.hpp"
#include <QString>

namespace mylr {

class SceneClassifier {
public:
    static QString classify(const SceneFingerprint& fp);
    static float sceneCompatibility(const QString& a, const QString& b);
};

} // namespace mylr
