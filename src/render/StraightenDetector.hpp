#pragma once

#include <QImage>

namespace mylr {

class StraightenDetector {
public:
    float detectAngle(const QImage& image) const;
};

} // namespace mylr
