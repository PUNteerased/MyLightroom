#include "CameraProfile.hpp"
#include <QDebug>
#include <cmath>
#include <cstring>

namespace mylr {

namespace {

constexpr float kLumR = 0.2126729f;
constexpr float kLumG = 0.7151522f;
constexpr float kLumB = 0.0721750f;

// Typical Canon EOS rgb_cam (LibRaw/dcraw family), row-major 3x3.
constexpr float kFallbackCanonRaw[9] = {
    1.591339f, -0.541382f, -0.049957f,
   -0.330010f,  1.158831f,  0.171182f,
   -0.079912f,  0.178249f,  0.901663f,
};

void normalizeLuminancePreserving(float m[9]) {
    const float outR = m[0] + m[1] + m[2];
    const float outG = m[3] + m[4] + m[5];
    const float outB = m[6] + m[7] + m[8];
    const float outLum = kLumR * outR + kLumG * outG + kLumB * outB;
    if (outLum <= 1e-5f) return;
    const float scale = 1.f / outLum;
    for (int i = 0; i < 9; ++i)
        m[i] *= scale;
}

} // namespace

CameraProfile CameraProfile::identity() {
    return CameraProfile{};
}

CameraProfile CameraProfile::fallbackCanon() {
    CameraProfile p;
    std::memcpy(p.matrix, kFallbackCanonRaw, sizeof(p.matrix));
    normalizeLuminancePreserving(p.matrix);
    return p;
}

float CameraProfile::matrixSum(const float matrix[9]) {
    float sum = 0.f;
    for (int i = 0; i < 9; ++i)
        sum += matrix[i];
    return sum;
}

bool CameraProfile::isNearZero(const float matrix[9]) {
    float sumAbs = 0.f;
    for (int i = 0; i < 9; ++i)
        sumAbs += std::fabs(matrix[i]);
    return sumAbs < 1e-6f;
}

bool CameraProfile::isIdentity(const float matrix[9]) {
    static const float kId[9] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f};
    for (int i = 0; i < 9; ++i) {
        if (std::fabs(matrix[i] - kId[i]) > 1e-3f)
            return false;
    }
    return true;
}

void CameraProfile::resolveForRender(const float matrix[9], float resolved[9]) {
    const float sum = matrixSum(matrix);
    if (isNearZero(matrix) || std::fabs(sum) < 1e-6f) {
        qWarning() << "DevelopPipeline: camera matrix sum is" << sum
                   << "— forcing Canon EOS fallback (not identity/no-op)";
        const CameraProfile fb = fallbackCanon();
        std::memcpy(resolved, fb.matrix, sizeof(fb.matrix));
        return;
    }
    if (isIdentity(matrix)) {
        qWarning() << "DevelopPipeline: camera matrix is identity (unset?) — forcing Canon EOS fallback";
        const CameraProfile fb = fallbackCanon();
        std::memcpy(resolved, fb.matrix, sizeof(fb.matrix));
        return;
    }
    std::memcpy(resolved, matrix, 9 * sizeof(float));
}

CameraProfile CameraProfile::fromLibRaw(const float rgb_cam[3][4]) {
    CameraProfile p;
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            p.matrix[row * 3 + col] = rgb_cam[row][col];

    const float sum = matrixSum(p.matrix);
    if (isNearZero(p.matrix) || std::fabs(sum) < 1e-6f) {
        qWarning() << "RawDecoder: LibRaw rgb_cam sum is" << sum
                   << "— using Canon EOS fallback matrix";
        return fallbackCanon();
    }

    normalizeLuminancePreserving(p.matrix);
    return p;
}

void CameraProfile::applyLinear(float& r, float& g, float& b, const float matrix[9]) {
    const float nr = matrix[0] * r + matrix[1] * g + matrix[2] * b;
    const float ng = matrix[3] * r + matrix[4] * g + matrix[5] * b;
    const float nb = matrix[6] * r + matrix[7] * g + matrix[8] * b;
    r = nr;
    g = ng;
    b = nb;
}

} // namespace mylr
