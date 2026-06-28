#pragma once

#include <QString>
#include <QVector>

namespace mylr {

struct Lut3D {
    int size = 0;
    QVector<float> data;
    bool valid = false;
    float domainMin[3] = {0.f, 0.f, 0.f};
    float domainMax[3] = {1.f, 1.f, 1.f};
};

class LutEngine {
public:
    bool loadFromPath(const QString& path, Lut3D& out);
    bool loadCube(const QString& path, Lut3D& out);
    bool load3dl(const QString& path, Lut3D& out);
    float sample(const Lut3D& lut, float r, float g, float b, int channel) const;

    static QStringList supportedExtensions();

private:
    static bool parseCubeHeader(const QString& line, int& size);
    void normalizeDomain(Lut3D& lut) const;
    float mapInput(float v, float lo, float hi) const;
};

} // namespace mylr
