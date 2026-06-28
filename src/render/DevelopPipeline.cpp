#include "DevelopPipeline.hpp"
#include "../color/CameraProfile.hpp"
#include <cmath>
#include <cstring>
#include "../lut/LutEngine.hpp"
#include <QDebug>
#include <QTransform>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <thread>
#include <vector>

namespace mylr {

// ---------------------------------------------------------------------------
// Color transfer helpers
// ---------------------------------------------------------------------------
float ColorTransform::srgbToLinear(float c) {
    c = qBound(0.f, c, 1.f);
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float ColorTransform::linearToSrgb(float c) {
    c = qBound(0.f, c, 1.f);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.f / 2.4f) - 0.055f;
}

void ColorTransform::rgbToLab(float r, float g, float blue, float& L, float& a, float& bOut) {
    r = srgbToLinear(r);
    g = srgbToLinear(g);
    blue = srgbToLinear(blue);
    float X = 0.4124564f * r + 0.3575761f * g + 0.1804375f * blue;
    float Y = 0.2126729f * r + 0.7151522f * g + 0.0721750f * blue;
    float Z = 0.0193339f * r + 0.1191920f * g + 0.9503041f * blue;
    auto f = [](float t) {
        const float d = 6.f / 29.f;
        return t > d * d * d ? std::cbrt(t) : t / (3.f * d * d) + 4.f / 29.f;
    };
    X /= 0.95047f;
    Z /= 1.08883f;
    float fx = f(X), fy = f(Y), fz = f(Z);
    L = 116.f * fy - 16.f;
    a = 500.f * (fx - fy);
    bOut = 200.f * (fy - fz);
}

void ColorTransform::applyWhiteBalance(float&, float&, float&, float, float, const float[4]) {
    // Retained as a no-op for ABI compatibility; white balance now lives inside
    // the linear pipeline (see applyWhiteBalanceLinear below).
}

namespace {

inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
inline float linFloor(float v) { return v < 0.f ? 0.f : v; }

// Extended linear range before base-curve / sRGB encode (stops highlight posterization).
constexpr float kHdrLinearMax = 4.0f;

// Split [0,h) into row bands across the hardware threads and run f(y0, y1) on
// each. Used to keep the full-resolution render fast (and the interactive render
// near 60 FPS) without pulling in extra dependencies.
template <class F>
void parallelFor(int h, F&& f) {
    unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 4;
    int nthreads = std::min<int>(static_cast<int>(hw), std::max(1, h / 64));
    if (nthreads <= 1) { f(0, h); return; }
    const int band = (h + nthreads - 1) / nthreads;
    std::vector<std::thread> ts;
    for (int t = 0; t < nthreads; ++t) {
        const int y0 = t * band, y1 = std::min(h, y0 + band);
        if (y0 >= y1) break;
        ts.emplace_back([&f, y0, y1] { f(y0, y1); });
    }
    for (auto& t : ts) t.join();
}
inline float smoothstep(float e0, float e1, float x) {
    if (e1 <= e0) return x < e0 ? 0.f : 1.f;
    float t = clamp01((x - e0) / (e1 - e0));
    return t * t * (3.f - 2.f * t);
}
inline float linToSrgb(float c) {
    c = clamp01(c);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.f / 2.4f) - 0.055f;
}
inline float srgbToLin(float c) {
    c = clamp01(c);
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// Tunable constants (calibrated against Lightroom Classic 8.3.1 / TORBOSHI1).
constexpr float kBaseContrast = 0.26f;   // Adobe Color medium-contrast S strength
constexpr float kHiStrength = 0.45f;
constexpr float kShStrength = 0.44f;
constexpr float kWhStrength = 0.32f;
constexpr float kBlStrength = 0.32f;
constexpr float kParametricStrength = 1.f / 230.f;  // parametric curve divisor

// Kelvin/tint multipliers for absolute white-balance mapping.
void kelvinTintGains(float temp, float tint, float& rMul, float& gMul, float& bMul) {
    const float ref = 6500.f;
    const float t = qBound(2000.f, temp, 12000.f);
    const float ratio = ref / t;
    rMul = std::pow(ratio, -0.52f);
    gMul = 1.f - tint * 0.0015f;
    bMul = std::pow(ratio, 0.52f);
}

// Estimate as-shot color temperature from green-normalized cam_mul coefficients.
float estimateAsShotTemp(const float wb[4]) {
    const float r = wb[0] > 0.f ? wb[0] : 1.f;
    const float b = wb[2] > 0.f ? wb[2] : 1.f;
    const float temp = 6500.f * std::pow(b / r, 0.55f);
    return qBound(2000.f, temp, 12000.f);
}

// --- Stage: scene-referred white balance (linear) --------------------------
// Step A: apply as-shot cam_mul. Step B: at 6500K/0, done. Step C: absolute Temp/Tint
// relative to estimated as-shot illuminant (Lightroom-compatible preset semantics).
void applyWhiteBalanceLinear(float& r, float& g, float& b, const float wb[4], float temp,
                             float tint) {
    const float wr = wb[0] > 0.f ? wb[0] : 1.f;
    const float wg = wb[1] > 0.f ? wb[1] : 1.f;
    const float wbb = wb[2] > 0.f ? wb[2] : 1.f;
    r *= wr;
    g *= wg;
    b *= wbb;

    if (std::fabs(temp - 6500.f) < 0.5f && std::fabs(tint) < 0.05f)
        return;

    float rAs, gAs, bAs, rT, gT, bT;
    const float asShotTemp = estimateAsShotTemp(wb);
    kelvinTintGains(asShotTemp, 0.f, rAs, gAs, bAs);
    kelvinTintGains(temp, tint, rT, gT, bT);
    r *= rT / rAs;
    g *= gT / gAs;
    b *= bT / bAs;
}

// --- Stage: linear contrast (pivot ~18% gray) ------------------------------
void applyContrastLinear(float& r, float& g, float& b, float contrast) {
    if (contrast == 0.f) return;
    const float c = 1.f + contrast / 100.f;
    constexpr float pivot = 0.18f;
    r = pivot + (r - pivot) * c;
    g = pivot + (g - pivot) * c;
    b = pivot + (b - pivot) * c;
}

// Soft-limit perceptual tone position to [0,1] without hard posterization.
inline float softClampPos(float pos) {
    if (pos <= 0.f) return 0.f;
    if (pos >= 1.f) {
        const float t = pos - 1.f;
        return 1.f - 1.f / (1.f + t * 2.f);
    }
    return pos;
}

// --- Stage: PV2012-style tone mapping on luminance -------------------------
// Operates in a perceptual (display) position so the region masks behave like
// Lightroom, then scales RGB by the luminance gain to preserve color ratios.
void applyToneMapping(float& r, float& g, float& b, const BasicSettings& s) {
    if (s.highlights == 0.f && s.shadows == 0.f && s.whites == 0.f && s.blacks == 0.f)
        return;
    const float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    if (lum <= 1e-5f) return;
    // Map linear luminance to perceptual position; allow HDR lum > 1 without hard clip.
    float pos;
    if (lum <= 1.f)
        pos = linToSrgb(lum);
    else {
        const float sdr1 = linToSrgb(1.f);
        const float t = (lum - 1.f) / (kHdrLinearMax - 1.f);
        const float shoulder = t / (1.f + t);
        pos = sdr1 + (1.f - sdr1) * shoulder * 2.f;
    }
    pos = softClampPos(pos);

    const float hiW = smoothstep(0.5f, 0.85f, pos);
    const float shW = 1.f - smoothstep(0.15f, 0.5f, pos);
    const float whW = smoothstep(0.65f, 1.0f, pos);
    const float blW = 1.f - smoothstep(0.0f, 0.32f, pos);

    float np = pos;
    np += (s.highlights / 100.f) * kHiStrength * hiW;
    np += (s.shadows / 100.f) * kShStrength * shW;
    np += (s.whites / 100.f) * kWhStrength * whW;
    np += (s.blacks / 100.f) * kBlStrength * blW;
    np = softClampPos(np);

    const float newLum = srgbToLin(np);
    const float gain = newLum / lum;
    r *= gain;
    g *= gain;
    b *= gain;
}

// --- Stage: Adobe Color base curve (HDR linear -> display 0..1) ------------
inline float baseCurveChannel(float lin) {
    if (lin <= 0.f) return 0.f;
    float sdr;
    if (lin <= 1.f) {
        sdr = lin <= 0.0031308f ? lin * 12.92f
                                : 1.055f * std::pow(lin, 1.f / 2.4f) - 0.055f;
    } else {
        // Soft shoulder compresses [1, kHdrLinearMax] into display space below 1.0.
        const float sdr1 = linToSrgb(1.f);
        const float t = (lin - 1.f) / (kHdrLinearMax - 1.f);
        const float shoulder = t / (1.f + t);
        sdr = sdr1 + (1.f - sdr1) * shoulder * 2.f;
    }
    sdr = clamp01(sdr);
    const float sc = smoothstep(0.f, 1.f, sdr);
    return clamp01(sdr + (sc - sdr) * kBaseContrast);
}

// --- Stage: global contrast (display domain, pivot mid-gray) ---------------
inline float applyContrast(float v, float contrast) {
    if (contrast == 0.f) return v;
    const float c = 1.f + contrast / 100.f;
    return clamp01((v - 0.5f) * c + 0.5f);
}

// --- Stage: tone curve (parametric or point), display domain ---------------
float evalToneCurve(float v, const ToneCurveSettings& curve) {
    v = clamp01(v);
    if (curve.mode == ToneCurveSettings::Mode::Parametric) {
        const float sh = curve.shadowSplit / 100.f;
        const float mid = curve.midtoneSplit / 100.f;
        const float hi = curve.highlightSplit / 100.f;
        if (v > hi)
            v += curve.highlights * kParametricStrength * (v - hi);
        else if (v > mid)
            v += curve.lights * kParametricStrength * (v - mid);
        else if (v > sh)
            v += curve.darks * kParametricStrength * (v - sh);
        else
            v += curve.shadows * kParametricStrength * v;
        return clamp01(v);
    }
    const float x = v * 255.f;
    for (int i = 1; i < curve.points.size(); ++i) {
        if (x <= curve.points[i].x()) {
            const float x0 = curve.points[i - 1].x();
            const float y0 = curve.points[i - 1].y();
            const float x1 = curve.points[i].x();
            const float y1 = curve.points[i].y();
            const float t = x1 > x0 ? (x - x0) / (x1 - x0) : 0.f;
            return clamp01((y0 + t * (y1 - y0)) / 255.f);
        }
    }
    return v;
}

// --- Stage: HSL (8-channel), display domain --------------------------------
inline float hueBandWeight(float hueDeg, float center) {
    float d = std::fabs(hueDeg - center);
    if (d > 180.f) d = 360.f - d;
    const float width = 30.f;
    return qBound(0.f, 1.f - d / width, 1.f);
}

void applyHsl(float& r, float& g, float& b, const std::array<HslBand, 8>& hsl) {
    float maxC = qMax(r, qMax(g, b));
    float minC = qMin(r, qMin(g, b));
    float l = (maxC + minC) * 0.5f;
    float s = maxC == minC ? 0.f : (maxC - minC) / (1.f - std::fabs(2.f * l - 1.f));
    float h = 0.f;
    if (maxC != minC) {
        if (maxC == r) h = 60.f * std::fmod((g - b) / (maxC - minC), 6.f);
        else if (maxC == g) h = 60.f * ((b - r) / (maxC - minC) + 2.f);
        else h = 60.f * ((r - g) / (maxC - minC) + 4.f);
        if (h < 0.f) h += 360.f;
    }
    static const float centers[] = {0, 30, 60, 120, 180, 240, 270, 300};
    float dh = 0, ds = 0, dl = 0;
    for (int i = 0; i < 8; ++i) {
        const float w = hueBandWeight(h, centers[i]);
        dh += hsl[static_cast<size_t>(i)].hue * w;
        ds += hsl[static_cast<size_t>(i)].saturation * w;
        dl += hsl[static_cast<size_t>(i)].luminance * w;
    }
    if (dh == 0.f && ds == 0.f && dl == 0.f) return;
    h = std::fmod(h + dh + 360.f, 360.f);
    s = qBound(0.f, s * (1.f + ds / 100.f), 1.f);
    l = qBound(0.f, l + dl / 100.f, 1.f);
    const float c = (1.f - std::fabs(2.f * l - 1.f)) * s;
    const float x = c * (1.f - std::fabs(std::fmod(h / 60.f, 2.f) - 1.f));
    const float m = l - c * 0.5f;
    if (h < 60) { r = c + m; g = x + m; b = m; }
    else if (h < 120) { r = x + m; g = c + m; b = m; }
    else if (h < 180) { r = m; g = c + m; b = x + m; }
    else if (h < 240) { r = m; g = x + m; b = c + m; }
    else if (h < 300) { r = x + m; g = m; b = c + m; }
    else { r = c + m; g = m; b = x + m; }
}

// --- Stage: 3-way color grading -------------------------------------------
void applyColorGrading(float& r, float& g, float& b, float luma,
                       const ColorGradingSettings& grading) {
    const ColorWheel* wheel = &grading.midtones;
    if (luma < 0.33f) wheel = &grading.shadows;
    else if (luma > 0.66f) wheel = &grading.highlights;
    if (wheel->saturation == 0.f && wheel->luminance == 0.f) return;
    const float angle = wheel->hue * static_cast<float>(M_PI) / 180.f;
    const float amt = wheel->saturation / 200.f;
    r += amt * std::cos(angle);
    g += amt * std::sin(angle * 0.5f);
    b += amt * std::sin(angle);
    const float lift = wheel->luminance / 200.f;
    r = clamp01(r + lift);
    g = clamp01(g + lift);
    b = clamp01(b + lift);
}

// --- Stage: camera calibration --------------------------------------------
void applyCalibration(float& r, float& g, float& b, const CalibrationSettings& c) {
    const float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    if (c.shadowTint != 0.f)
        g = clamp01(g + c.shadowTint / 100.f * 0.1f * (1.f - luma));
    const float mean = (r + g + b) / 3.f;
    const float rp = c.redPrimary.saturation / 100.f;
    const float gp = c.greenPrimary.saturation / 100.f;
    const float bp = c.bluePrimary.saturation / 100.f;
    if (rp != 0.f) r = clamp01(mean + (r - mean) * (1.f + rp));
    if (gp != 0.f) g = clamp01(mean + (g - mean) * (1.f + gp));
    if (bp != 0.f) b = clamp01(mean + (b - mean) * (1.f + bp));
}

// --- Stage: vibrance (skin-protected) + saturation, display domain ---------
void applyVibranceSaturation(float& r, float& g, float& b, const BasicSettings& s) {
    if (s.vibrance == 0.f && s.saturation == 0.f) return;
    const float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    const float mx = qMax(r, qMax(g, b));
    const float mn = qMin(r, qMin(g, b));
    const float sat = mx > 1e-4f ? (mx - mn) / mx : 0.f;

    float f = 1.f + s.saturation / 100.f;
    if (s.vibrance != 0.f) {
        const float vib = s.vibrance / 100.f;
        // Boost low-saturation pixels more than already-saturated ones.
        float vScale = vib * (1.f - sat) * 1.5f;
        // Protect skin tones (hue ~ orange) from over-saturating.
        if (mx != mn) {
            float h;
            if (mx == r) h = 60.f * std::fmod((g - b) / (mx - mn), 6.f);
            else if (mx == g) h = 60.f * ((b - r) / (mx - mn) + 2.f);
            else h = 60.f * ((r - g) / (mx - mn) + 4.f);
            if (h < 0.f) h += 360.f;
            const float skin = hueBandWeight(h, 30.f); // orange band
            vScale *= (1.f - 0.5f * skin);
        }
        f += vScale;
    }
    r = clamp01(luma + (r - luma) * f);
    g = clamp01(luma + (g - luma) * f);
    b = clamp01(luma + (b - luma) * f);
}

// ---------------------------------------------------------------------------
// Float separable box blur over an interleaved RGB float buffer (linear).
// ---------------------------------------------------------------------------
std::vector<float> boxBlurRGB(const std::vector<float>& src, int w, int h, int radius) {
    std::vector<float> tmp(src.size());
    std::vector<float> dst(src.size());
    const int win = radius * 2 + 1;
    for (int y = 0; y < h; ++y) {
        float sr = 0, sg = 0, sb = 0;
        for (int k = -radius; k <= radius; ++k) {
            const int xx = qBound(0, k, w - 1);
            const int i = (y * w + xx) * 3;
            sr += src[i]; sg += src[i + 1]; sb += src[i + 2];
        }
        for (int x = 0; x < w; ++x) {
            const int o = (y * w + x) * 3;
            tmp[o] = sr / win; tmp[o + 1] = sg / win; tmp[o + 2] = sb / win;
            const int xo = qBound(0, x - radius, w - 1);
            const int xn = qBound(0, x + radius + 1, w - 1);
            const int io = (y * w + xo) * 3, in = (y * w + xn) * 3;
            sr += src[in] - src[io];
            sg += src[in + 1] - src[io + 1];
            sb += src[in + 2] - src[io + 2];
        }
    }
    for (int x = 0; x < w; ++x) {
        float sr = 0, sg = 0, sb = 0;
        for (int k = -radius; k <= radius; ++k) {
            const int yy = qBound(0, k, h - 1);
            const int i = (yy * w + x) * 3;
            sr += tmp[i]; sg += tmp[i + 1]; sb += tmp[i + 2];
        }
        for (int y = 0; y < h; ++y) {
            const int o = (y * w + x) * 3;
            dst[o] = sr / win; dst[o + 1] = sg / win; dst[o + 2] = sb / win;
            const int yo = qBound(0, y - radius, h - 1);
            const int yn = qBound(0, y + radius + 1, h - 1);
            const int io = (yo * w + x) * 3, in = (yn * w + x) * 3;
            sr += tmp[in] - tmp[io];
            sg += tmp[in + 1] - tmp[io + 1];
            sb += tmp[in + 2] - tmp[io + 2];
        }
    }
    return dst;
}

// --- Stage: presence (Texture / Clarity / Dehaze) in linear ----------------
void applyPresenceLinear(std::vector<float>& buf, int w, int h, const BasicSettings& s) {
    if (s.texture == 0.f && s.clarity == 0.f && s.dehaze == 0.f) return;
    const int minEdge = qMax(1, qMin(w, h));
    const bool needFine = s.texture != 0.f;
    const bool needCoarse = s.clarity != 0.f || s.dehaze != 0.f;
    const std::vector<float> fine =
        needFine ? boxBlurRGB(buf, w, h, qMax(1, minEdge / 400)) : std::vector<float>();
    const std::vector<float> coarse =
        needCoarse ? boxBlurRGB(buf, w, h, qMax(2, minEdge / 50)) : std::vector<float>();
    const float tAmt = s.texture / 100.f;
    const float cAmt = s.clarity / 100.f;
    const float dAmt = s.dehaze / 100.f;
    const size_t n = static_cast<size_t>(w) * h * 3;
    for (size_t i = 0; i < n; ++i) {
        float v = buf[i];
        if (needFine) v += (v - fine[i]) * tAmt;
        if (needCoarse) {
            if (cAmt != 0.f) v += (v - coarse[i]) * cAmt;
            if (dAmt != 0.f) v += (v - coarse[i]) * dAmt * 1.5f;
        }
        buf[i] = linFloor(v);
    }
}

// --- Stage: lens corrections (distortion + chromatic aberration) -----------
void applyLensLinear(std::vector<float>& buf, int w, int h, const LensSettings& L) {
    if (L.distortion == 0.f && L.caRedCyan == 0.f && L.caBlueYellow == 0.f) return;
    const std::vector<float> src = buf;
    const float cx = (w - 1) * 0.5f, cy = (h - 1) * 0.5f;
    const float norm = 1.f / std::sqrt(cx * cx + cy * cy);
    const float kDist = -L.distortion / 100.f * 0.35f;
    const float kR = L.caRedCyan / 100.f * 0.01f;
    const float kB = L.caBlueYellow / 100.f * 0.01f;
    auto sample = [&](float fx, float fy, int ch) -> float {
        if (fx < 0) fx = 0; if (fx > w - 1) fx = w - 1;
        if (fy < 0) fy = 0; if (fy > h - 1) fy = h - 1;
        const int x0 = static_cast<int>(fx), y0 = static_cast<int>(fy);
        const int x1 = qMin(x0 + 1, w - 1), y1 = qMin(y0 + 1, h - 1);
        const float ax = fx - x0, ay = fy - y0;
        const float v00 = src[(y0 * w + x0) * 3 + ch], v10 = src[(y0 * w + x1) * 3 + ch];
        const float v01 = src[(y1 * w + x0) * 3 + ch], v11 = src[(y1 * w + x1) * 3 + ch];
        return (v00 * (1 - ax) + v10 * ax) * (1 - ay) + (v01 * (1 - ax) + v11 * ax) * ay;
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float dx = (x - cx), dy = (y - cy);
            const float rr = std::sqrt(dx * dx + dy * dy) * norm;
            const float distScale = 1.f + kDist * rr * rr;
            const float bx = cx + dx * distScale, by = cy + dy * distScale;
            const float ddx = (bx - cx), ddy = (by - cy);
            const int o = (y * w + x) * 3;
            buf[o + 0] = sample(cx + ddx * (1.f + kR), cy + ddy * (1.f + kR), 0);
            buf[o + 1] = sample(bx, by, 1);
            buf[o + 2] = sample(cx + ddx * (1.f + kB), cy + ddy * (1.f + kB), 2);
        }
    }
}

// ---------------------------------------------------------------------------
// 8-bit output-stage passes (Effects & Detail), kept on the encoded image.
// ---------------------------------------------------------------------------
QImage boxBlur8(const QImage& src, int radius) {
    const int w = src.width(), h = src.height();
    if (radius < 1 || w == 0 || h == 0) return src;
    const int win = radius * 2 + 1;
    QImage tmp(src.size(), QImage::Format_RGB32);
    QImage dst(src.size(), QImage::Format_RGB32);
    for (int y = 0; y < h; ++y) {
        const QRgb* s = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        QRgb* t = reinterpret_cast<QRgb*>(tmp.scanLine(y));
        long sr = 0, sg = 0, sb = 0;
        for (int k = -radius; k <= radius; ++k) {
            const int xx = qBound(0, k, w - 1);
            sr += qRed(s[xx]); sg += qGreen(s[xx]); sb += qBlue(s[xx]);
        }
        for (int x = 0; x < w; ++x) {
            t[x] = qRgb(int(sr / win), int(sg / win), int(sb / win));
            const int xo = qBound(0, x - radius, w - 1);
            const int xn = qBound(0, x + radius + 1, w - 1);
            sr += qRed(s[xn]) - qRed(s[xo]);
            sg += qGreen(s[xn]) - qGreen(s[xo]);
            sb += qBlue(s[xn]) - qBlue(s[xo]);
        }
    }
    for (int x = 0; x < w; ++x) {
        long sr = 0, sg = 0, sb = 0;
        for (int k = -radius; k <= radius; ++k) {
            const QRgb* s = reinterpret_cast<const QRgb*>(tmp.constScanLine(qBound(0, k, h - 1)));
            sr += qRed(s[x]); sg += qGreen(s[x]); sb += qBlue(s[x]);
        }
        for (int y = 0; y < h; ++y) {
            QRgb* d = reinterpret_cast<QRgb*>(dst.scanLine(y));
            d[x] = qRgb(int(sr / win), int(sg / win), int(sb / win));
            const QRgb* so = reinterpret_cast<const QRgb*>(tmp.constScanLine(qBound(0, y - radius, h - 1)));
            const QRgb* sn = reinterpret_cast<const QRgb*>(tmp.constScanLine(qBound(0, y + radius + 1, h - 1)));
            sr += qRed(sn[x]) - qRed(so[x]);
            sg += qGreen(sn[x]) - qGreen(so[x]);
            sb += qBlue(sn[x]) - qBlue(so[x]);
        }
    }
    return dst;
}

void applyDetail8(QImage& img, const DetailSettings& d) {
    const int w = img.width(), h = img.height();
    const int minEdge = qMax(1, qMin(w, h));
    if (d.noiseLuminance > 0.f || d.noiseColor > 0.f) {
        const QImage blur = boxBlur8(img, qMax(1, minEdge / 300));
        const float amt = qBound(0.f, qMax(d.noiseLuminance, d.noiseColor) / 100.f, 1.f);
        for (int y = 0; y < h; ++y) {
            QRgb* p = reinterpret_cast<QRgb*>(img.scanLine(y));
            const QRgb* bl = reinterpret_cast<const QRgb*>(blur.constScanLine(y));
            for (int x = 0; x < w; ++x) {
                const float r = qRed(p[x]) * (1.f - amt) + qRed(bl[x]) * amt;
                const float g = qGreen(p[x]) * (1.f - amt) + qGreen(bl[x]) * amt;
                const float b = qBlue(p[x]) * (1.f - amt) + qBlue(bl[x]) * amt;
                p[x] = qRgb(qBound(0, int(r), 255), qBound(0, int(g), 255), qBound(0, int(b), 255));
            }
        }
    }
    if (d.sharpenAmount > 0.f) {
        const QImage blur = boxBlur8(img, qMax(1, int(d.sharpenRadius)));
        const float amt = d.sharpenAmount / 100.f;
        for (int y = 0; y < h; ++y) {
            QRgb* p = reinterpret_cast<QRgb*>(img.scanLine(y));
            const QRgb* bl = reinterpret_cast<const QRgb*>(blur.constScanLine(y));
            for (int x = 0; x < w; ++x) {
                const float r = qRed(p[x]) + (qRed(p[x]) - qRed(bl[x])) * amt;
                const float g = qGreen(p[x]) + (qGreen(p[x]) - qGreen(bl[x])) * amt;
                const float b = qBlue(p[x]) + (qBlue(p[x]) - qBlue(bl[x])) * amt;
                p[x] = qRgb(qBound(0, int(r), 255), qBound(0, int(g), 255), qBound(0, int(b), 255));
            }
        }
    }
}

void applyGrain8(QImage& img, const EffectsSettings& e) {
    if (e.grainAmount <= 0.f) return;
    const float amt = e.grainAmount / 100.f * 40.f;
    quint32 seed = 2463534242u;
    auto rnd = [&seed]() {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        return int(seed & 0xff) - 128;
    };
    const int w = img.width(), h = img.height();
    for (int y = 0; y < h; ++y) {
        QRgb* p = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int n = int(rnd() * amt / 128.f);
            p[x] = qRgb(qBound(0, qRed(p[x]) + n, 255), qBound(0, qGreen(p[x]) + n, 255),
                        qBound(0, qBlue(p[x]) + n, 255));
        }
    }
}

void applyVignette8(QImage& img, const EffectsSettings& effects) {
    if (effects.vignetteAmount == 0.f) return;
    const int w = img.width(), h = img.height();
    const float cx = w * 0.5f, cy = h * 0.5f;
    const float mid = qBound(0.f, effects.vignetteMidpoint / 100.f, 1.f);
    const float amt = effects.vignetteAmount / 100.f;
    for (int y = 0; y < h; ++y) {
        QRgb* p = reinterpret_cast<QRgb*>(img.scanLine(y));
        const float dy = (y - cy) / cy;
        for (int x = 0; x < w; ++x) {
            const float dx = (x - cx) / cx;
            const float dist = std::sqrt(dx * dx + dy * dy);
            const float v = 1.f - qBound(0.f, dist - mid, 1.f) * amt;
            p[x] = qRgb(qBound(0, int(qRed(p[x]) * v), 255), qBound(0, int(qGreen(p[x]) * v), 255),
                        qBound(0, int(qBlue(p[x]) * v), 255));
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Pipeline orchestration
// ---------------------------------------------------------------------------
QImage DevelopPipeline::renderLinear(const QImage& linear64, const DevelopSettings& settings,
                                     const float wbCoeffs[4], const float rgbCam[9],
                                     int maxEdge, bool isCameraLinear) const {
    if (linear64.isNull()) return {};

    qDebug() << "DevelopPipeline::renderLinear isCameraLinear=" << isCameraLinear
             << "matrix sum=" << CameraProfile::matrixSum(rgbCam);

    // 1. Geometry: crop / rotate (done on the 16-bit linear buffer).
    QImage work = applyCropRotate(linear64, settings.geometry);
    if (work.format() != QImage::Format_RGBX64)
        work = work.convertToFormat(QImage::Format_RGBX64);
    if (maxEdge > 0 && (work.width() > maxEdge || work.height() > maxEdge)) {
        const float s = static_cast<float>(maxEdge) / static_cast<float>(qMax(work.width(), work.height()));
        work = work.scaled(static_cast<int>(work.width() * s), static_cast<int>(work.height() * s),
                           Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    const int w = work.width(), h = work.height();
    std::vector<float> buf(static_cast<size_t>(w) * h * 3);
    parallelFor(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const auto* src = reinterpret_cast<const quint16*>(work.constScanLine(y));
            for (int x = 0; x < w; ++x) {
                const size_t o = (static_cast<size_t>(y) * w + x) * 3;
                buf[o + 0] = src[x * 4 + 0] / 65535.f;
                buf[o + 1] = src[x * 4 + 1] / 65535.f;
                buf[o + 2] = src[x * 4 + 2] / 65535.f;
            }
        }
    });

    // 2. Lens corrections (geometric, linear).
    applyLensLinear(buf, w, h, settings.lens);

    const BasicSettings& basic = settings.basic;
    const float expGain = std::pow(2.f, basic.exposure);
    const float wb[4] = {wbCoeffs[0], wbCoeffs[1], wbCoeffs[2], wbCoeffs[3]};
    float effectiveRgbCam[9];
    if (isCameraLinear) {
        CameraProfile::resolveForRender(rgbCam, effectiveRgbCam);
    } else {
        static const float kIdentity[9] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f};
        std::memcpy(effectiveRgbCam, kIdentity, sizeof(kIdentity));
    }
    qDebug() << "DevelopPipeline effective matrix sum="
             << CameraProfile::matrixSum(effectiveRgbCam);

    // 3-5. White balance -> camera matrix -> underflow guard -> exposure/contrast/tone.
    parallelFor(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < w; ++x) {
                const size_t o = (static_cast<size_t>(y) * w + x) * 3;
                float r = buf[o], g = buf[o + 1], b = buf[o + 2];
                applyWhiteBalanceLinear(r, g, b, wb, basic.temp, basic.tint);
                CameraProfile::applyLinear(r, g, b, effectiveRgbCam);
                r = std::max(0.f, r);
                g = std::max(0.f, g);
                b = std::max(0.f, b);
                r *= expGain;
                g *= expGain;
                b *= expGain;
                applyContrastLinear(r, g, b, basic.contrast);
                applyToneMapping(r, g, b, basic);
                buf[o] = linFloor(r);
                buf[o + 1] = linFloor(g);
                buf[o + 2] = linFloor(b);
            }
        }
    });

    // 6. Presence (Texture / Clarity / Dehaze) in linear, before the base curve.
    applyPresenceLinear(buf, w, h, basic);

    // 7-end. Base curve -> tone curve -> HSL -> color grading -> calibration ->
    // LUT -> vibrance/saturation, then encode to 8-bit sRGB.
    QImage out(w, h, QImage::Format_RGB32);
    LutEngine lutEngine;
    Lut3D lut;
    const bool useLut = settings.lut.enabled && !settings.lut.path.isEmpty() &&
                        lutEngine.loadFromPath(settings.lut.path, lut) && lut.valid;

    // Precompute 1D LUT: base curve -> tone curve over extended linear range.
    constexpr int kLutN = 8192;
    std::vector<float> chanLut(kLutN);
    for (int i = 0; i < kLutN; ++i) {
        const float lin = (static_cast<float>(i) / (kLutN - 1)) * kHdrLinearMax;
        float v = baseCurveChannel(lin);
        v = evalToneCurve(v, settings.toneCurve);
        chanLut[i] = v;
    }
    auto curveLookup = [&](float lin) -> float {
        if (lin < 0.f) lin = 0.f;
        if (lin >= kHdrLinearMax)
            return evalToneCurve(baseCurveChannel(lin), settings.toneCurve);
        const float t = lin / kHdrLinearMax;
        const float f = t * (kLutN - 1);
        const int i0 = static_cast<int>(f);
        const int i1 = qMin(i0 + 1, kLutN - 1);
        const float frac = f - static_cast<float>(i0);
        return chanLut[i0] * (1.f - frac) + chanLut[i1] * frac;
    };

    auto encodeChannel = [&](float v, int x, int y, int ch) -> int {
        const quint32 seed = 0x12345678u ^ static_cast<quint32>(y * w + x) * 0x9e3779b9u ^
                             static_cast<quint32>(ch) * 0x85ebca6bu;
        const float d = (static_cast<float>((seed >> 8) & 0xff) / 255.f - 0.5f) / 255.f;
        return static_cast<int>(clamp01(v + d) * 255.f + 0.5f);
    };

    parallelFor(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            QRgb* dst = reinterpret_cast<QRgb*>(out.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const size_t o = (static_cast<size_t>(y) * w + x) * 3;
                float r = curveLookup(buf[o]);
                float g = curveLookup(buf[o + 1]);
                float b = curveLookup(buf[o + 2]);

                applyHsl(r, g, b, settings.hsl);
                const float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                applyColorGrading(r, g, b, luma, settings.colorGrading);
                applyCalibration(r, g, b, settings.calibration);

                if (useLut) {
                    const float ir = r * (lut.size - 1);
                    const float ig = g * (lut.size - 1);
                    const float ib = b * (lut.size - 1);
                    const float lr = lutEngine.sample(lut, ir, ig, ib, 0);
                    const float lg = lutEngine.sample(lut, ir, ig, ib, 1);
                    const float lb = lutEngine.sample(lut, ir, ig, ib, 2);
                    const float t = settings.lut.intensity;
                    r = r * (1.f - t) + lr * t;
                    g = g * (1.f - t) + lg * t;
                    b = b * (1.f - t) + lb * t;
                }

                applyVibranceSaturation(r, g, b, basic);
                dst[x] = qRgb(encodeChannel(r, x, y, 0), encodeChannel(g, x, y, 1),
                              encodeChannel(b, x, y, 2));
            }
        }
    });

    // Output-stage Effects & Detail.
    applyVignette8(out, settings.effects);
    applyDetail8(out, settings.detail);
    applyGrain8(out, settings.effects);
    return out;
}

QImage DevelopPipeline::render(const QImage& source, const DevelopSettings& settings,
                               int maxEdge) const {
    if (source.isNull()) return {};
    const float neutral[4] = {1.f, 1.f, 1.f, 1.f};
    static const float kIdentityRgbCam[9] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f};
    if (source.format() == QImage::Format_RGBX64) {
        return renderLinear(source, settings, neutral, kIdentityRgbCam, maxEdge, false);
    }
    // 8-bit sRGB input: linearize into RGBX64 first.
    const QImage srgb = source.convertToFormat(QImage::Format_RGB888);
    QImage lin(srgb.width(), srgb.height(), QImage::Format_RGBX64);
    for (int y = 0; y < srgb.height(); ++y) {
        const auto* s = srgb.constScanLine(y);
        auto* d = reinterpret_cast<quint16*>(lin.scanLine(y));
        for (int x = 0; x < srgb.width(); ++x) {
            d[x * 4 + 0] = static_cast<quint16>(srgbToLin(s[x * 3 + 0] / 255.f) * 65535.f + 0.5f);
            d[x * 4 + 1] = static_cast<quint16>(srgbToLin(s[x * 3 + 1] / 255.f) * 65535.f + 0.5f);
            d[x * 4 + 2] = static_cast<quint16>(srgbToLin(s[x * 3 + 2] / 255.f) * 65535.f + 0.5f);
            d[x * 4 + 3] = 0xffff;
        }
    }
    return renderLinear(lin, settings, neutral, kIdentityRgbCam, maxEdge, false);
}

HistogramData DevelopPipeline::computeHistogram(const QImage& imageIn) const {
    HistogramData h;
    h.red.fill(0, HistogramData::BinCount);
    h.green.fill(0, HistogramData::BinCount);
    h.blue.fill(0, HistogramData::BinCount);
    h.luminance.fill(0, HistogramData::BinCount);

    const QImage image = (imageIn.format() == QImage::Format_RGB32 ||
                          imageIn.format() == QImage::Format_ARGB32)
                             ? imageIn
                             : imageIn.convertToFormat(QImage::Format_RGB32);

    for (int y = 0; y < image.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const int r = qRed(line[x]), g = qGreen(line[x]), b = qBlue(line[x]);
            ++h.red[r]; ++h.green[g]; ++h.blue[b];
            const int lum = static_cast<int>(0.2126 * r + 0.7152 * g + 0.0722 * b);
            ++h.luminance[qBound(0, lum, 255)];
            if (r >= 250 || g >= 250 || b >= 250) {
                if (r >= 250) ++h.clipHighR;
                if (g >= 250) ++h.clipHighG;
                if (b >= 250) ++h.clipHighB;
            }
            if (lum <= 5) ++h.clipLow;
        }
    }
    return h;
}

QImage DevelopPipeline::applyCropRotate(const QImage& source, const GeometrySettings& geom) const {
    const int w = source.width();
    const int h = source.height();
    const int x0 = static_cast<int>(geom.cropLeft * w);
    const int y0 = static_cast<int>(geom.cropTop * h);
    const int x1 = static_cast<int>(geom.cropRight * w);
    const int y1 = static_cast<int>(geom.cropBottom * h);
    QImage cropped = source.copy(x0, y0, qMax(1, x1 - x0), qMax(1, y1 - y0));
    if (std::fabs(geom.rotation + geom.straighten) > 0.01f) {
        QTransform t;
        t.rotate(geom.rotation + geom.straighten);
        cropped = cropped.transformed(t, Qt::SmoothTransformation);
    }
    return cropped;
}

} // namespace mylr
