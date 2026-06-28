#include "DevelopSettings.hpp"
#include <cmath>

namespace mylr {

DevelopSettings DevelopSettings::defaults() {
    return DevelopSettings{};
}

DevelopSettings DevelopSettings::clone() const {
    return *this;
}

static bool feq(float a, float b) {
    return std::fabs(a - b) < 1e-4f;
}

bool DevelopSettings::operator==(const DevelopSettings& o) const {
    return basic.temp == o.basic.temp && basic.tint == o.basic.tint &&
           basic.exposure == o.basic.exposure && basic.contrast == o.basic.contrast &&
           basic.highlights == o.basic.highlights && basic.shadows == o.basic.shadows &&
           basic.whites == o.basic.whites && basic.blacks == o.basic.blacks &&
           geometry.rotation == o.geometry.rotation && lut.path == o.lut.path;
}

} // namespace mylr
