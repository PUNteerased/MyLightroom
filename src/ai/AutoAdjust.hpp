#pragma once

#include "../core/DevelopSettings.hpp"
#include "../raw/RawDecoder.hpp"
#include <QImage>

namespace mylr {

// Heuristic auto-adjustments computed directly from image statistics.
// These mirror Lightroom's "Auto" buttons without requiring a trained model.
class AutoAdjust {
public:
    // Full auto: exposure + contrast + white balance + black/white points.
    static BasicSettings autoTone(const QImage& source, const BasicSettings& current);

    // Individual adjustments (operate on a copy of current and return updated).
    static BasicSettings autoExposure(const QImage& source, const BasicSettings& current);
    static BasicSettings autoWhiteBalance(const QImage& source, const BasicSettings& current);
    static BasicSettings autoContrast(const QImage& source, const BasicSettings& current);
};

} // namespace mylr
