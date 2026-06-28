#include "ColorManager.hpp"

namespace mylr {

bool ColorManager::loadOcioConfig(const QString& path) {
    m_ocioConfig = path;
    return !path.isEmpty();
}

} // namespace mylr
