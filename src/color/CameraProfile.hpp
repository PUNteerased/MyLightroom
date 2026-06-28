#pragma once

namespace mylr {

inline constexpr const char* kPipelineCacheGeneration = "cache_v101_linear_fix";

struct CameraProfile {
    float matrix[9] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f};

    static CameraProfile identity();
    // LibRaw rgb_cam: camera RGB -> output RGB (same convention as dcraw_process).
    static CameraProfile fromLibRaw(const float rgb_cam[3][4]);

    static float matrixSum(const float matrix[9]);
    static bool isNearZero(const float matrix[9]);
    static void resolveForRender(const float matrix[9], float resolved[9]);

    static void applyLinear(float& r, float& g, float& b, const float matrix[9]);
    // Matrix multiply with per-pixel luminance preserved (prevents exposure blow-out).
    static void applyLinearPreservingLuminance(float& r, float& g, float& b, const float matrix[9]);
};

} // namespace mylr
