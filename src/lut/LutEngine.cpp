#include "LutEngine.hpp"
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QtMath>
#include <cmath>

namespace mylr {

QStringList LutEngine::supportedExtensions() {
    return {QStringLiteral("cube"), QStringLiteral("3dl")};
}

bool LutEngine::parseCubeHeader(const QString& line, int& size) {
    static const QRegularExpression re(QStringLiteral(R"(LUT_3D_SIZE\s+(\d+))"));
    const auto m = re.match(line.trimmed());
    if (m.hasMatch()) {
        size = m.captured(1).toInt();
        return true;
    }
    return false;
}

float LutEngine::mapInput(float v, float lo, float hi) const {
    if (hi <= lo) return v;
    return (v - lo) / (hi - lo);
}

void LutEngine::normalizeDomain(Lut3D& lut) const {
    const bool needsNorm = lut.domainMin[0] != 0.f || lut.domainMin[1] != 0.f ||
                           lut.domainMin[2] != 0.f || lut.domainMax[0] != 1.f ||
                           lut.domainMax[1] != 1.f || lut.domainMax[2] != 1.f;
    if (!needsNorm) return;

    for (int i = 0; i < lut.data.size(); i += 3) {
        lut.data[i + 0] = mapInput(lut.data[i + 0], lut.domainMin[0], lut.domainMax[0]);
        lut.data[i + 1] = mapInput(lut.data[i + 1], lut.domainMin[1], lut.domainMax[1]);
        lut.data[i + 2] = mapInput(lut.data[i + 2], lut.domainMin[2], lut.domainMax[2]);
    }
    lut.domainMin[0] = lut.domainMin[1] = lut.domainMin[2] = 0.f;
    lut.domainMax[0] = lut.domainMax[1] = lut.domainMax[2] = 1.f;
}

bool LutEngine::loadFromPath(const QString& path, Lut3D& out) {
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == QStringLiteral("3dl"))
        return load3dl(path, out);
    return loadCube(path, out);
}

bool LutEngine::loadCube(const QString& path, Lut3D& out) {
    out = Lut3D{};
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    int size = 0;
    QVector<float> values;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        if (parseCubeHeader(line, size))
            continue;

        static const QRegularExpression domainRe(
            QStringLiteral(R"(DOMAIN_(MIN|MAX)\s+([\d.+-eE]+)\s+([\d.+-eE]+)\s+([\d.+-eE]+))"));
        const auto dm = domainRe.match(line);
        if (dm.hasMatch()) {
            const bool isMin = dm.captured(1) == QStringLiteral("MIN");
            const int base = isMin ? 0 : 0;
            Q_UNUSED(base);
            float* target = isMin ? out.domainMin : out.domainMax;
            target[0] = dm.captured(2).toFloat();
            target[1] = dm.captured(3).toFloat();
            target[2] = dm.captured(4).toFloat();
            continue;
        }

        if (line.startsWith(QStringLiteral("TITLE")) || line.startsWith(QStringLiteral("LUT")))
            continue;

        const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() >= 3) {
            values.append(parts[0].toFloat());
            values.append(parts[1].toFloat());
            values.append(parts[2].toFloat());
        }
    }

    if (size <= 0) {
        const int n = values.size() / 3;
        size = static_cast<int>(std::cbrt(n));
    }
    if (size <= 0 || values.size() < size * size * size * 3)
        return false;

    out.size = size;
    out.data = values;
    normalizeDomain(out);
    out.valid = true;
    return true;
}

bool LutEngine::load3dl(const QString& path, Lut3D& out) {
    out = Lut3D{};
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    int size = 0;
    QVector<float> values;
    while (!f.atEnd()) {
        QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;

        static const QRegularExpression meshRe(QStringLiteral(R"(Mesh\s+(\d+))"), QRegularExpression::CaseInsensitiveOption);
        const auto mm = meshRe.match(line);
        if (mm.hasMatch()) {
            size = mm.captured(1).toInt();
            continue;
        }

        line.replace(QLatin1Char(','), QLatin1Char(' '));
        const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() >= 3) {
            float r = parts[0].toFloat();
            float g = parts[1].toFloat();
            float b = parts[2].toFloat();
            if (r > 1.f || g > 1.f || b > 1.f) {
                r /= 4095.f;
                g /= 4095.f;
                b /= 4095.f;
            }
            values.append(r);
            values.append(g);
            values.append(b);
        }
    }

    if (size <= 0) {
        const int n = values.size() / 3;
        size = static_cast<int>(std::cbrt(n));
    }
    if (size <= 0 || values.size() < size * size * size * 3)
        return false;

    out.size = size;
    out.data = values;
    out.valid = true;
    return true;
}

float LutEngine::sample(const Lut3D& lut, float r, float g, float b, int channel) const {
    if (!lut.valid || lut.size < 2) return 0.f;

    r = mapInput(r, lut.domainMin[0], lut.domainMax[0]);
    g = mapInput(g, lut.domainMin[1], lut.domainMax[1]);
    b = mapInput(b, lut.domainMin[2], lut.domainMax[2]);

    const int n = lut.size - 1;
    const float sr = qBound(0.f, r * n, static_cast<float>(n));
    const float sg = qBound(0.f, g * n, static_cast<float>(n));
    const float sb = qBound(0.f, b * n, static_cast<float>(n));

    const int r0 = static_cast<int>(std::floor(sr));
    const int g0 = static_cast<int>(std::floor(sg));
    const int b0 = static_cast<int>(std::floor(sb));
    const int r1 = qMin(r0 + 1, n);
    const int g1 = qMin(g0 + 1, n);
    const int b1 = qMin(b0 + 1, n);
    const float fr = sr - r0, fg = sg - g0, fb = sb - b0;

    auto at = [&](int ri, int gi, int bi) {
        const int idx = (bi * lut.size * lut.size + gi * lut.size + ri) * 3 + channel;
        return idx >= 0 && idx < lut.data.size() ? lut.data[idx] : 0.f;
    };

    const float c000 = at(r0, g0, b0), c100 = at(r1, g0, b0);
    const float c010 = at(r0, g1, b0), c110 = at(r1, g1, b0);
    const float c001 = at(r0, g0, b1), c101 = at(r1, g0, b1);
    const float c011 = at(r0, g1, b1), c111 = at(r1, g1, b1);

    const float c00 = c000 * (1 - fr) + c100 * fr;
    const float c01 = c001 * (1 - fr) + c101 * fr;
    const float c10 = c010 * (1 - fr) + c110 * fr;
    const float c11 = c011 * (1 - fr) + c111 * fr;
    const float c0 = c00 * (1 - fg) + c10 * fg;
    const float c1 = c01 * (1 - fg) + c11 * fg;
    return c0 * (1 - fb) + c1 * fb;
}

} // namespace mylr
