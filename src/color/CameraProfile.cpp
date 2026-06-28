#include "CameraProfile.hpp"
#include <QDebug>
#include <cmath>
#include <cstring>

namespace mylr {

namespace {

constexpr float kLumR = 0.2126729f;
constexpr float kLumG = 0.7151522f;
constexpr float kLumB = 0.0721750f;

void normalizeLuminancePreserving(float m[9]) {
    // Neutral camera input (1,1,1) should map to unit Rec.709 luminance.
    const float outR = m[0] + m[1] + m[2];
    const float outG = m[3] + m[4] + m[5];
    const float outB = m[6] + m[7] + m[8];
    const float outLum = kLumR * outR + kLumG * outG + kLumB * outB;
    if (outLum <= 1e-5f) return;
    const float scale = 1.f / outLum;
    for (int i = 0; i < 9; ++i)
        m[i] *= scale;
}

void normalizeRowWeights(float m[9]) {
    // Prevent any output row from acting as a massive gain multiplier.
    for (int row = 0; row < 3; ++row) {
        float rowSum = 0.f;
        for (int col = 0; col < 3; ++col)
            rowSum += std::fabs(m[row * 3 + col]);
        if (rowSum > 3.f) {
            const float s = 3.f / rowSum;
            for (int col = 0; col < 3; ++col)
                m[row * 3 + col] *= s;
        }
    }
}

} // namespace

CameraProfile CameraProfile::identity() {
    return CameraProfile{};
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

void CameraProfile::resolveForRender(const float matrix[9], float resolved[9]) {
    if (isNearZero(matrix)) {
        qWarning() << "DevelopPipeline: camera matrix unset — identity";
        const CameraProfile id = identity();
        std::memcpy(resolved, id.matrix, sizeof(id.matrix));
        return;
    }
    std::memcpy(resolved, matrix, 9 * sizeof(float));
}

CameraProfile CameraProfile::fromLibRaw(const float rgb_cam[3][4]) {
    CameraProfile profile;
    float sumAbs = 0.f;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            profile.matrix[row * 3 + col] = rgb_cam[row][col];
            sumAbs += std::fabs(profile.matrix[row * 3 + col]);
        }
    }
    if (sumAbs < 1e-6f) {
        qWarning() << "RawDecoder: rgb_cam is zero — identity matrix";
        return identity();
    }

    normalizeRowWeights(profile.matrix);
    normalizeLuminancePreserving(profile.matrix);
    qDebug() << "CameraProfile: forward rgb_cam camera->sRGB, row sums:"
             << (profile.matrix[0] + profile.matrix[1] + profile.matrix[2])
             << (profile.matrix[3] + profile.matrix[4] + profile.matrix[5])
             << (profile.matrix[6] + profile.matrix[7] + profile.matrix[8]);
    return profile;
}

void CameraProfile::applyLinear(float& r, float& g, float& b, const float matrix[9]) {
    const float nr = matrix[0] * r + matrix[1] * g + matrix[2] * b;
    const float ng = matrix[3] * r + matrix[4] * g + matrix[5] * b;
    const float nb = matrix[6] * r + matrix[7] * g + matrix[8] * b;
    r = nr;
    g = ng;
    b = nb;
}

void CameraProfile::applyLinearPreservingLuminance(float& r, float& g, float& b,
                                                   const float matrix[9]) {
    const float oldLum = kLumR * r + kLumG * g + kLumB * b;
    applyLinear(r, g, b, matrix);
    const float newLum = kLumR * r + kLumG * g + kLumB * b;
    if (oldLum > 1e-5f && newLum > 1e-5f) {
        const float s = oldLum / newLum;
        r *= s;
        g *= s;
        b *= s;
    }
}

} // namespace mylr
