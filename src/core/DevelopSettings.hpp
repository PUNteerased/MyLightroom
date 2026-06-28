#pragma once

#include <QString>
#include <QVector>
#include <QPointF>
#include <array>

namespace mylr {

struct HslBand {
    float hue = 0.f;
    float saturation = 0.f;
    float luminance = 0.f;
};

struct ColorWheel {
    float hue = 0.f;
    float saturation = 0.f;
    float luminance = 0.f;
};

struct BasicSettings {
    float temp = 6500.f;
    float tint = 0.f;
    float exposure = 0.f;
    float contrast = 0.f;
    float highlights = 0.f;
    float shadows = 0.f;
    float whites = 0.f;
    float blacks = 0.f;
    float texture = 0.f;
    float clarity = 0.f;
    float dehaze = 0.f;
    float vibrance = 0.f;
    float saturation = 0.f;
};

struct ToneCurveSettings {
    enum class Mode { Parametric, Point };
    Mode mode = Mode::Parametric;
    float highlights = 0.f;
    float lights = 0.f;
    float darks = 0.f;
    float shadows = 0.f;
    float shadowSplit = 25.f;
    float midtoneSplit = 50.f;
    float highlightSplit = 75.f;
    QVector<QPointF> points = {{0, 0}, {255, 255}};
    QString channel = QStringLiteral("rgb");
};

struct ColorGradingSettings {
    ColorWheel shadows;
    ColorWheel midtones;
    ColorWheel highlights;
    float balance = 0.f;
    float blending = 50.f;
};

struct CalibrationSettings {
    float shadowTint = 0.f;
    HslBand redPrimary;
    HslBand greenPrimary;
    HslBand bluePrimary;
};

struct LutSettings {
    QString path;
    float intensity = 1.f;
    bool enabled = false;
};

struct GeometrySettings {
    float cropLeft = 0.f;
    float cropTop = 0.f;
    float cropRight = 1.f;
    float cropBottom = 1.f;
    float rotation = 0.f;
    float straighten = 0.f;
    QString aspectRatio = QStringLiteral("free");
};

struct EffectsSettings {
    float vignetteAmount = 0.f;
    float vignetteMidpoint = 50.f;
    float grainAmount = 0.f;
    float grainSize = 25.f;
};

// Manual lens corrections (auto profile/Lensfun-style DB is out of scope). All
// default to 0 = no correction.
struct LensSettings {
    float distortion = 0.f;     // +barrel / -pincushion correction, percent-ish
    float caRedCyan = 0.f;      // chromatic aberration red/cyan scale
    float caBlueYellow = 0.f;   // chromatic aberration blue/yellow scale
    float vignette = 0.f;       // lens (corner) vignette correction
};

struct DetailSettings {
    float sharpenAmount = 40.f;
    float sharpenRadius = 1.f;
    float sharpenDetail = 25.f;
    float noiseLuminance = 0.f;
    float noiseColor = 0.f;
};

struct WatermarkSettings {
    QString imagePath;
    QString position = QStringLiteral("bottom-right");
    float scale = 0.15f;
    float opacity = 0.8f;
    int marginPx = 24;
    bool enabled = false;
};

enum class HslBandId {
    Red, Orange, Yellow, Green, Aqua, Blue, Purple, Magenta, Count
};

struct DevelopSettings {
    BasicSettings basic;
    ToneCurveSettings toneCurve;
    std::array<HslBand, static_cast<size_t>(HslBandId::Count)> hsl{};
    ColorGradingSettings colorGrading;
    CalibrationSettings calibration;
    LutSettings lut;
    GeometrySettings geometry;
    LensSettings lens;
    EffectsSettings effects;
    DetailSettings detail;
    WatermarkSettings watermark;

    static DevelopSettings defaults();
    DevelopSettings clone() const;
    bool operator==(const DevelopSettings& o) const;
};

} // namespace mylr
