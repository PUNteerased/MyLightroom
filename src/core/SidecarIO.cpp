#include "SidecarIO.hpp"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace mylr {

QString SidecarIO::sidecarPathForRaw(const QString& rawPath) {
    return rawPath + QStringLiteral(".mylr");
}

static QJsonObject hslBandToJson(const HslBand& b) {
    QJsonObject o;
    o[QStringLiteral("h")] = b.hue;
    o[QStringLiteral("s")] = b.saturation;
    o[QStringLiteral("l")] = b.luminance;
    return o;
}

static HslBand hslBandFromJson(const QJsonObject& o) {
    HslBand b;
    b.hue = static_cast<float>(o.value(QStringLiteral("h")).toDouble());
    b.saturation = static_cast<float>(o.value(QStringLiteral("s")).toDouble());
    b.luminance = static_cast<float>(o.value(QStringLiteral("l")).toDouble());
    return b;
}

static QJsonObject colorWheelToJson(const ColorWheel& w) {
    QJsonObject o;
    o[QStringLiteral("h")] = w.hue;
    o[QStringLiteral("s")] = w.saturation;
    o[QStringLiteral("l")] = w.luminance;
    return o;
}

static ColorWheel colorWheelFromJson(const QJsonObject& o) {
    ColorWheel w;
    w.hue = static_cast<float>(o.value(QStringLiteral("h")).toDouble());
    w.saturation = static_cast<float>(o.value(QStringLiteral("s")).toDouble());
    w.luminance = static_cast<float>(o.value(QStringLiteral("l")).toDouble());
    return w;
}

QJsonObject SidecarIO::settingsToJson(const DevelopSettings& s) {
    QJsonObject root;
    QJsonObject basic;
    basic[QStringLiteral("temp")] = s.basic.temp;
    basic[QStringLiteral("tint")] = s.basic.tint;
    basic[QStringLiteral("exposure")] = s.basic.exposure;
    basic[QStringLiteral("contrast")] = s.basic.contrast;
    basic[QStringLiteral("highlights")] = s.basic.highlights;
    basic[QStringLiteral("shadows")] = s.basic.shadows;
    basic[QStringLiteral("whites")] = s.basic.whites;
    basic[QStringLiteral("blacks")] = s.basic.blacks;
    basic[QStringLiteral("texture")] = s.basic.texture;
    basic[QStringLiteral("clarity")] = s.basic.clarity;
    basic[QStringLiteral("dehaze")] = s.basic.dehaze;
    basic[QStringLiteral("vibrance")] = s.basic.vibrance;
    basic[QStringLiteral("saturation")] = s.basic.saturation;
    root[QStringLiteral("basic")] = basic;

    QJsonObject curve;
    curve[QStringLiteral("mode")] = s.toneCurve.mode == ToneCurveSettings::Mode::Parametric
                                        ? QStringLiteral("parametric")
                                        : QStringLiteral("point");
    curve[QStringLiteral("highlights")] = s.toneCurve.highlights;
    curve[QStringLiteral("lights")] = s.toneCurve.lights;
    curve[QStringLiteral("darks")] = s.toneCurve.darks;
    curve[QStringLiteral("shadows")] = s.toneCurve.shadows;
    QJsonArray pts;
    for (const auto& p : s.toneCurve.points) {
        QJsonArray pt;
        pt.append(p.x());
        pt.append(p.y());
        pts.append(pt);
    }
    curve[QStringLiteral("points")] = pts;
    root[QStringLiteral("tone_curve")] = curve;

    static const char* bandNames[] = {"red", "orange", "yellow", "green", "aqua", "blue", "purple", "magenta"};
    QJsonObject hsl;
    for (int i = 0; i < 8; ++i) {
        hsl[QString::fromUtf8(bandNames[i])] = hslBandToJson(s.hsl[static_cast<size_t>(i)]);
    }
    root[QStringLiteral("hsl")] = hsl;

    QJsonObject grading;
    grading[QStringLiteral("shadows")] = colorWheelToJson(s.colorGrading.shadows);
    grading[QStringLiteral("midtones")] = colorWheelToJson(s.colorGrading.midtones);
    grading[QStringLiteral("highlights")] = colorWheelToJson(s.colorGrading.highlights);
    grading[QStringLiteral("balance")] = s.colorGrading.balance;
    grading[QStringLiteral("blending")] = s.colorGrading.blending;
    root[QStringLiteral("color_grading")] = grading;

    QJsonObject geom;
    geom[QStringLiteral("crop_left")] = s.geometry.cropLeft;
    geom[QStringLiteral("crop_top")] = s.geometry.cropTop;
    geom[QStringLiteral("crop_right")] = s.geometry.cropRight;
    geom[QStringLiteral("crop_bottom")] = s.geometry.cropBottom;
    geom[QStringLiteral("rotation")] = s.geometry.rotation;
    geom[QStringLiteral("straighten")] = s.geometry.straighten;
    geom[QStringLiteral("aspect_ratio")] = s.geometry.aspectRatio;
    root[QStringLiteral("geometry")] = geom;

    QJsonObject lut;
    lut[QStringLiteral("path")] = s.lut.path;
    lut[QStringLiteral("intensity")] = s.lut.intensity;
    lut[QStringLiteral("enabled")] = s.lut.enabled;
    root[QStringLiteral("lut")] = lut;

    QJsonObject effects;
    effects[QStringLiteral("vignette_amount")] = s.effects.vignetteAmount;
    effects[QStringLiteral("grain_amount")] = s.effects.grainAmount;
    root[QStringLiteral("effects")] = effects;

    QJsonObject detail;
    detail[QStringLiteral("sharpen_amount")] = s.detail.sharpenAmount;
    detail[QStringLiteral("noise_luminance")] = s.detail.noiseLuminance;
    root[QStringLiteral("detail")] = detail;

    return root;
}

DevelopSettings SidecarIO::settingsFromJson(const QJsonObject& root) {
    DevelopSettings s = DevelopSettings::defaults();
    const QJsonObject basic = root.value(QStringLiteral("basic")).toObject();
    s.basic.temp = static_cast<float>(basic.value(QStringLiteral("temp")).toDouble(6500));
    s.basic.tint = static_cast<float>(basic.value(QStringLiteral("tint")).toDouble());
    s.basic.exposure = static_cast<float>(basic.value(QStringLiteral("exposure")).toDouble());
    s.basic.contrast = static_cast<float>(basic.value(QStringLiteral("contrast")).toDouble());
    s.basic.highlights = static_cast<float>(basic.value(QStringLiteral("highlights")).toDouble());
    s.basic.shadows = static_cast<float>(basic.value(QStringLiteral("shadows")).toDouble());
    s.basic.whites = static_cast<float>(basic.value(QStringLiteral("whites")).toDouble());
    s.basic.blacks = static_cast<float>(basic.value(QStringLiteral("blacks")).toDouble());
    s.basic.texture = static_cast<float>(basic.value(QStringLiteral("texture")).toDouble());
    s.basic.clarity = static_cast<float>(basic.value(QStringLiteral("clarity")).toDouble());
    s.basic.dehaze = static_cast<float>(basic.value(QStringLiteral("dehaze")).toDouble());
    s.basic.vibrance = static_cast<float>(basic.value(QStringLiteral("vibrance")).toDouble());
    s.basic.saturation = static_cast<float>(basic.value(QStringLiteral("saturation")).toDouble());

    const QJsonObject curve = root.value(QStringLiteral("tone_curve")).toObject();
    s.toneCurve.mode = curve.value(QStringLiteral("mode")).toString() == QStringLiteral("point")
                           ? ToneCurveSettings::Mode::Point
                           : ToneCurveSettings::Mode::Parametric;
    s.toneCurve.highlights = static_cast<float>(curve.value(QStringLiteral("highlights")).toDouble());
    s.toneCurve.lights = static_cast<float>(curve.value(QStringLiteral("lights")).toDouble());
    s.toneCurve.darks = static_cast<float>(curve.value(QStringLiteral("darks")).toDouble());
    s.toneCurve.shadows = static_cast<float>(curve.value(QStringLiteral("shadows")).toDouble());
    s.toneCurve.points.clear();
    for (const auto& v : curve.value(QStringLiteral("points")).toArray()) {
        const QJsonArray pt = v.toArray();
        if (pt.size() >= 2)
            s.toneCurve.points.append(QPointF(pt.at(0).toDouble(), pt.at(1).toDouble()));
    }
    if (s.toneCurve.points.isEmpty())
        s.toneCurve.points = {{0, 0}, {255, 255}};

    static const char* bandNames[] = {"red", "orange", "yellow", "green", "aqua", "blue", "purple", "magenta"};
    const QJsonObject hsl = root.value(QStringLiteral("hsl")).toObject();
    for (int i = 0; i < 8; ++i) {
        s.hsl[static_cast<size_t>(i)] = hslBandFromJson(hsl.value(QString::fromUtf8(bandNames[i])).toObject());
    }

    const QJsonObject grading = root.value(QStringLiteral("color_grading")).toObject();
    s.colorGrading.shadows = colorWheelFromJson(grading.value(QStringLiteral("shadows")).toObject());
    s.colorGrading.midtones = colorWheelFromJson(grading.value(QStringLiteral("midtones")).toObject());
    s.colorGrading.highlights = colorWheelFromJson(grading.value(QStringLiteral("highlights")).toObject());
    s.colorGrading.balance = static_cast<float>(grading.value(QStringLiteral("balance")).toDouble());
    s.colorGrading.blending = static_cast<float>(grading.value(QStringLiteral("blending")).toDouble(50));

    const QJsonObject geom = root.value(QStringLiteral("geometry")).toObject();
    s.geometry.cropLeft = static_cast<float>(geom.value(QStringLiteral("crop_left")).toDouble());
    s.geometry.cropTop = static_cast<float>(geom.value(QStringLiteral("crop_top")).toDouble());
    s.geometry.cropRight = static_cast<float>(geom.value(QStringLiteral("crop_right")).toDouble(1));
    s.geometry.cropBottom = static_cast<float>(geom.value(QStringLiteral("crop_bottom")).toDouble(1));
    s.geometry.rotation = static_cast<float>(geom.value(QStringLiteral("rotation")).toDouble());
    s.geometry.straighten = static_cast<float>(geom.value(QStringLiteral("straighten")).toDouble());
    s.geometry.aspectRatio = geom.value(QStringLiteral("aspect_ratio")).toString(QStringLiteral("free"));

    const QJsonObject lut = root.value(QStringLiteral("lut")).toObject();
    s.lut.path = lut.value(QStringLiteral("path")).toString();
    s.lut.intensity = static_cast<float>(lut.value(QStringLiteral("intensity")).toDouble(1));
    s.lut.enabled = lut.value(QStringLiteral("enabled")).toBool();

    const QJsonObject effects = root.value(QStringLiteral("effects")).toObject();
    s.effects.vignetteAmount = static_cast<float>(effects.value(QStringLiteral("vignette_amount")).toDouble());
    s.effects.grainAmount = static_cast<float>(effects.value(QStringLiteral("grain_amount")).toDouble());

    const QJsonObject detail = root.value(QStringLiteral("detail")).toObject();
    s.detail.sharpenAmount = static_cast<float>(detail.value(QStringLiteral("sharpen_amount")).toDouble(40));
    s.detail.noiseLuminance = static_cast<float>(detail.value(QStringLiteral("noise_luminance")).toDouble());

    return s;
}

static QJsonArray vecToJson(const QVector<float>& v) {
    QJsonArray a;
    for (float f : v) a.append(static_cast<double>(f));
    return a;
}

static QVector<float> vecFromJson(const QJsonArray& a) {
    QVector<float> v;
    v.reserve(a.size());
    for (const auto& x : a) v.append(static_cast<float>(x.toDouble()));
    return v;
}

QJsonObject SidecarIO::fingerprintToJson(const SceneFingerprint& fp) {
    QJsonObject o;
    o[QStringLiteral("luminance_cdf")] = vecToJson(fp.luminanceCdf);
    QJsonObject rgb;
    rgb[QStringLiteral("r")] = vecToJson(fp.redCdf);
    rgb[QStringLiteral("g")] = vecToJson(fp.greenCdf);
    rgb[QStringLiteral("b")] = vecToJson(fp.blueCdf);
    o[QStringLiteral("rgb_cdf")] = rgb;
    QJsonObject lab;
    lab[QStringLiteral("L_mean")] = fp.labLMean;
    lab[QStringLiteral("a_mean")] = fp.labAMean;
    lab[QStringLiteral("b_mean")] = fp.labBMean;
    o[QStringLiteral("lab_stats")] = lab;
    o[QStringLiteral("zone_distribution")] = vecToJson(fp.zoneDistribution);
    o[QStringLiteral("dominant_hues")] = vecToJson(fp.dominantHues);
    o[QStringLiteral("highlight_clip_pct")] = fp.highlightClipPct;
    o[QStringLiteral("shadow_clip_pct")] = fp.shadowClipPct;
    return o;
}

SceneFingerprint SidecarIO::fingerprintFromJson(const QJsonObject& o) {
    SceneFingerprint fp;
    fp.luminanceCdf = vecFromJson(o.value(QStringLiteral("luminance_cdf")).toArray());
    const QJsonObject rgb = o.value(QStringLiteral("rgb_cdf")).toObject();
    fp.redCdf = vecFromJson(rgb.value(QStringLiteral("r")).toArray());
    fp.greenCdf = vecFromJson(rgb.value(QStringLiteral("g")).toArray());
    fp.blueCdf = vecFromJson(rgb.value(QStringLiteral("b")).toArray());
    const QJsonObject lab = o.value(QStringLiteral("lab_stats")).toObject();
    fp.labLMean = static_cast<float>(lab.value(QStringLiteral("L_mean")).toDouble());
    fp.labAMean = static_cast<float>(lab.value(QStringLiteral("a_mean")).toDouble());
    fp.labBMean = static_cast<float>(lab.value(QStringLiteral("b_mean")).toDouble());
    fp.zoneDistribution = vecFromJson(o.value(QStringLiteral("zone_distribution")).toArray());
    fp.dominantHues = vecFromJson(o.value(QStringLiteral("dominant_hues")).toArray());
    fp.highlightClipPct = static_cast<float>(o.value(QStringLiteral("highlight_clip_pct")).toDouble());
    fp.shadowClipPct = static_cast<float>(o.value(QStringLiteral("shadow_clip_pct")).toDouble());
    return fp;
}

QJsonObject SidecarIO::matchProfileToJson(const MatchProfile& p) {
    QJsonObject o;
    o[QStringLiteral("match_profile_version")] = p.version;
    o[QStringLiteral("reference_image_id")] = p.referenceImageId;
    o[QStringLiteral("fingerprint")] = fingerprintToJson(p.fingerprint);
    o[QStringLiteral("develop_state")] = settingsToJson(p.developState);
    QJsonObject ctx;
    ctx[QStringLiteral("camera")] = p.sceneContext.camera;
    ctx[QStringLiteral("iso")] = p.sceneContext.iso;
    ctx[QStringLiteral("ev_baseline")] = p.sceneContext.evBaseline;
    ctx[QStringLiteral("scene_type")] = p.sceneContext.sceneType;
    o[QStringLiteral("scene_context")] = ctx;
    return o;
}

MatchProfile SidecarIO::matchProfileFromJson(const QJsonObject& o) {
    MatchProfile p;
    p.version = o.value(QStringLiteral("match_profile_version")).toInt(1);
    p.referenceImageId = o.value(QStringLiteral("reference_image_id")).toString();
    p.fingerprint = fingerprintFromJson(o.value(QStringLiteral("fingerprint")).toObject());
    p.developState = settingsFromJson(o.value(QStringLiteral("develop_state")).toObject());
    const QJsonObject ctx = o.value(QStringLiteral("scene_context")).toObject();
    p.sceneContext.camera = ctx.value(QStringLiteral("camera")).toString();
    p.sceneContext.iso = ctx.value(QStringLiteral("iso")).toInt();
    p.sceneContext.evBaseline = static_cast<float>(ctx.value(QStringLiteral("ev_baseline")).toDouble());
    p.sceneContext.sceneType = ctx.value(QStringLiteral("scene_type")).toString();
    return p;
}

bool SidecarIO::save(const QString& rawPath, const DevelopSettings& settings,
                     const MatchProfile* matchProfile) {
    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("develop")] = settingsToJson(settings);
    if (matchProfile)
        root[QStringLiteral("match_profile")] = matchProfileToJson(*matchProfile);

    QFile f(sidecarPathForRaw(rawPath));
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool SidecarIO::load(const QString& rawPath, DevelopSettings& settings, MatchProfile* matchProfile) {
    QFile f(sidecarPathForRaw(rawPath));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    settings = settingsFromJson(root.value(QStringLiteral("develop")).toObject());
    if (matchProfile && root.contains(QStringLiteral("match_profile")))
        *matchProfile = matchProfileFromJson(root.value(QStringLiteral("match_profile")).toObject());
    return true;
}

} // namespace mylr
