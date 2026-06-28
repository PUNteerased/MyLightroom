#pragma once

#include <QString>

namespace mylr {

class ColorManager {
public:
    bool loadOcioConfig(const QString& path);
    QString displayProfilePath() const { return m_displayProfile; }

private:
    QString m_ocioConfig;
    QString m_displayProfile;
};

} // namespace mylr
