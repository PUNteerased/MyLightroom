#pragma once

namespace mylr {

// Bump when decode/pipeline color science changes (invalidates RAM + disk caches).
inline constexpr const char* kPipelineCacheGeneration = "cache_v99_forced";

struct CameraProfile {
    float matrix[9] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f};

    static CameraProfile identity();
    static CameraProfile fromLibRaw(const float rgb_cam[3][4]);
    static CameraProfile fallbackCanon();

    static float matrixSum(const float matrix[9]);
    static bool isNearZero(const float matrix[9]);
    static bool isIdentity(const float matrix[9]);
    static void resolveForRender(const float matrix[9], float resolved[9]);

    static void applyLinear(float& r, float& g, float& b, const float matrix[9]);
};

} // namespace mylr
