#include "XmpParser.hpp"
#include <QFile>
#include <QFileInfo>
#include <QPointF>
#include <QRegularExpression>
#include <QXmlStreamReader>

namespace mylr {

QString XmpParser::localName(const QString& key) {
    const int colon = key.lastIndexOf(QLatin1Char(':'));
    return colon >= 0 ? key.mid(colon + 1) : key;
}

float XmpParser::parseNumber(const QString& raw) {
    QString s = raw.trimmed();
    if (s.startsWith(QLatin1Char('+')))
        s = s.mid(1);
    s.replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    const float v = s.toFloat(&ok);
    return ok ? v : 0.f;
}

QVector<QPointF> XmpParser::parseCurvePoints(const QString& raw) {
    QVector<QPointF> pts;
    const QStringList nums = raw.split(QRegularExpression("[\\s,]+"), Qt::SkipEmptyParts);
    for (int i = 0; i + 1 < nums.size(); i += 2) {
        bool okX = false, okY = false;
        const float x = nums[i].toFloat(&okX);
        const float y = nums[i + 1].toFloat(&okY);
        if (okX && okY)
            pts.append(QPointF(x, y));
    }
    if (pts.isEmpty())
        pts = {{0, 0}, {255, 255}};
    return pts;
}

QHash<QString, QString> XmpParser::extractAttributes(const QString& content) {
    QHash<QString, QString> attrs;

    QXmlStreamReader xml(content);
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const auto attributes = xml.attributes();
            for (const auto& attr : attributes) {
                const QString name = localName(attr.name().toString());
                if (!attrs.contains(name))
                    attrs.insert(name, attr.value().toString());
            }
        }
    }

    static const QRegularExpression re(
        QStringLiteral(R"((?:crs:|lr:|xmp:)?([\w]+)\s*=\s*["']([^"']*)["'])"));
    auto it = re.globalMatch(content);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString name = m.captured(1);
        if (!attrs.contains(name))
            attrs.insert(name, m.captured(2));
    }

    return attrs;
}

void XmpParser::applyAttribute(const QString& keyRaw, const QString& value, DevelopSettings& s,
                               XmpImportReport& report) {
    const QString key = localName(keyRaw);
    const float v = parseNumber(value);

    auto mapBasic = [&](const QString& k, float BasicSettings::* member) {
        if (key == k) {
            s.basic.*member = v;
            report.mappedFields.append(k);
        }
    };

    mapBasic(QStringLiteral("Temperature"), &BasicSettings::temp);
    mapBasic(QStringLiteral("Tint"), &BasicSettings::tint);
    mapBasic(QStringLiteral("Exposure2012"), &BasicSettings::exposure);
    mapBasic(QStringLiteral("Exposure"), &BasicSettings::exposure);
    mapBasic(QStringLiteral("Contrast2012"), &BasicSettings::contrast);
    mapBasic(QStringLiteral("Contrast"), &BasicSettings::contrast);
    mapBasic(QStringLiteral("Highlights2012"), &BasicSettings::highlights);
    mapBasic(QStringLiteral("Highlights"), &BasicSettings::highlights);
    mapBasic(QStringLiteral("Shadows2012"), &BasicSettings::shadows);
    mapBasic(QStringLiteral("Shadows"), &BasicSettings::shadows);
    mapBasic(QStringLiteral("Whites2012"), &BasicSettings::whites);
    mapBasic(QStringLiteral("Whites"), &BasicSettings::whites);
    mapBasic(QStringLiteral("Blacks2012"), &BasicSettings::blacks);
    mapBasic(QStringLiteral("Blacks"), &BasicSettings::blacks);
    mapBasic(QStringLiteral("Texture"), &BasicSettings::texture);
    mapBasic(QStringLiteral("Clarity2012"), &BasicSettings::clarity);
    mapBasic(QStringLiteral("Clarity"), &BasicSettings::clarity);
    mapBasic(QStringLiteral("Dehaze"), &BasicSettings::dehaze);
    mapBasic(QStringLiteral("Vibrance"), &BasicSettings::vibrance);
    mapBasic(QStringLiteral("Saturation"), &BasicSettings::saturation);

    if (key == QStringLiteral("ParametricHighlights")) {
        s.toneCurve.highlights = v;
        report.mappedFields.append(key);
    } else if (key == QStringLiteral("ParametricLights")) {
        s.toneCurve.lights = v;
        report.mappedFields.append(key);
    } else if (key == QStringLiteral("ParametricDarks")) {
        s.toneCurve.darks = v;
        report.mappedFields.append(key);
    } else if (key == QStringLiteral("ParametricShadows")) {
        s.toneCurve.shadows = v;
        report.mappedFields.append(key);
    } else if (key == QStringLiteral("ParametricShadowSplit")) {
        s.toneCurve.shadowSplit = v;
        report.mappedFields.append(key);
    } else if (key == QStringLiteral("ParametricMidtoneSplit")) {
        s.toneCurve.midtoneSplit = v;
        report.mappedFields.append(key);
    } else if (key == QStringLiteral("ParametricHighlightSplit")) {
        s.toneCurve.highlightSplit = v;
        report.mappedFields.append(key);
    } else if (key.startsWith(QStringLiteral("ToneCurvePV2012")) ||
               key.startsWith(QStringLiteral("ToneCurve"))) {
        if (!value.isEmpty() && value != QStringLiteral("0,0,255,255")) {
            s.toneCurve.mode = ToneCurveSettings::Mode::Point;
            s.toneCurve.points = parseCurvePoints(value);
            report.mappedFields.append(key);
        }
    }

    struct HslMap {
        QString prefix;
        size_t band;
    };
    static const HslMap hslMaps[] = {
        {QStringLiteral("Red"), 0},       {QStringLiteral("Orange"), 1},
        {QStringLiteral("Yellow"), 2},    {QStringLiteral("Green"), 3},
        {QStringLiteral("Aqua"), 4},      {QStringLiteral("Blue"), 5},
        {QStringLiteral("Purple"), 6},    {QStringLiteral("Magenta"), 7},
    };
    for (const auto& hm : hslMaps) {
        if (key == QStringLiteral("HueAdjustment") + hm.prefix) {
            s.hsl[hm.band].hue = v;
            report.mappedFields.append(key);
        } else if (key == QStringLiteral("SaturationAdjustment") + hm.prefix) {
            s.hsl[hm.band].saturation = v;
            report.mappedFields.append(key);
        } else if (key == QStringLiteral("LuminanceAdjustment") + hm.prefix) {
            s.hsl[hm.band].luminance = v;
            report.mappedFields.append(key);
        }
    }

    if (key == QStringLiteral("ColorGradeShadowHue") ||
        key == QStringLiteral("SplitToningShadowHue")) {
        s.colorGrading.shadows.hue = v;
        report.mappedFields.append(key);
    } else if (key == QStringLiteral("ColorGradeShadowSat") ||
               key == QStringLiteral("SplitToningShadowSaturation")) {
        s.colorGrading.shadows.saturation = v;
        report.mappedFields.append(key);
    } else if (key == QStringLiteral("ColorGradeShadowLum")) {
        s.colorGrading.shadows.luminance = v;
        report.mappedFields.append(key);
    } else if (key == QStringLiteral("ColorGradeMidtoneHue")) {
        s.colorGrading.midtones.hue = v;
        report.mappedFields.append(key);
    } else if (key == QStringLiteral("ColorGradeMidtoneSat")) {
        s.colorGrading.midtones.saturation = v;
        report.mappedFields.append(key);
    } else if (key == QStringLiteral("ColorGradeMidtoneLum")) {
        s.colorGrading.midtones.luminance = v;
        report.mappedFields.append(key);
    } else if (key == QStringLiteral("ColorGradeHighlightHue") ||
               key == QStringLiteral("SplitToningHighlightHue")) {
        s.colorGrading.highlights.hue = v;
        report.mappedFields.append(key);
    } else if (key == QStringLiteral("ColorGradeHighlightSat") ||
               key == QStringLiteral("SplitToningHighlightSaturation")) {
        s.colorGrading.highlights.saturation = v;
        report.mappedFields.append(key);
    } else if (key == QStringLiteral("ColorGradeHighlightLum")) {
        s.colorGrading.highlights.luminance = v;
        report.mappedFields.append(key);
    } else if (key == QStringLiteral("ColorGradeBlending")) {
        s.colorGrading.blending = v;
        report.mappedFields.append(key);
    } else if (key == QStringLiteral("SplitToningBalance") ||
               key == QStringLiteral("ColorGradeGlobalHue")) {
        s.colorGrading.balance = v;
        report.mappedFields.append(key);
    }

    if (key == QStringLiteral("ShadowTint")) {
        s.calibration.shadowTint = v;
        report.mappedFields.append(key);
    } else if (key == QStringLiteral("RedHue")) {
        s.calibration.redPrimary.hue = v;
        report.mappedFields.append(key);
    } else if (key == QStringLiteral("RedSaturation")) {
        s.calibration.redPrimary.saturation = v;
        report.mappedFields.append(key);
    } else if (key == QStringLiteral("GreenHue")) {
        s.calibration.greenPrimary.hue = v;
        report.mappedFields.append(key);
    } else if (key == QStringLiteral("GreenSaturation")) {
        s.calibration.greenPrimary.saturation = v;
        report.mappedFields.append(key);
    } else if (key == QStringLiteral("BlueHue")) {
        s.calibration.bluePrimary.hue = v;
        report.mappedFields.append(key);
    } else if (key == QStringLiteral("BlueSaturation")) {
        s.calibration.bluePrimary.saturation = v;
        report.mappedFields.append(key);
    }

    if (key == QStringLiteral("PostCropVignetteAmount")) {
        s.effects.vignetteAmount = v;
        report.mappedFields.append(key);
    } else if (key == QStringLiteral("PostCropVignetteMidpoint")) {
        s.effects.vignetteMidpoint = v;
        report.mappedFields.append(key);
    } else if (key == QStringLiteral("GrainAmount")) {
        s.effects.grainAmount = v;
        report.mappedFields.append(key);
    } else if (key == QStringLiteral("GrainSize")) {
        s.effects.grainSize = v;
        report.mappedFields.append(key);
    }

    if (key == QStringLiteral("Sharpness")) {
        s.detail.sharpenAmount = v;
        report.mappedFields.append(key);
    } else if (key == QStringLiteral("SharpenRadius")) {
        s.detail.sharpenRadius = v;
        report.mappedFields.append(key);
    } else if (key == QStringLiteral("SharpenDetail")) {
        s.detail.sharpenDetail = v;
        report.mappedFields.append(key);
    } else if (key == QStringLiteral("LuminanceSmoothing")) {
        s.detail.noiseLuminance = v;
        report.mappedFields.append(key);
    } else if (key == QStringLiteral("ColorNoiseReduction")) {
        s.detail.noiseColor = v;
        report.mappedFields.append(key);
    }

    if (key == QStringLiteral("LookAmount") || key == QStringLiteral("LookTableAmount")) {
        report.lookAmount = v > 1.f ? v / 100.f : v;
        report.mappedFields.append(key);
    } else if (key == QStringLiteral("LookName") || key == QStringLiteral("ProfileName") ||
               key == QStringLiteral("CameraProfile")) {
        report.externalLutHint = value;
        report.warnings.append(
            QStringLiteral("Profile/Look '%1' referenced — import matching .cube/.3dl separately if needed")
                .arg(value));
    } else if (key == QStringLiteral("LookTable") || key == QStringLiteral("RGBTable")) {
        report.skippedFields.append(key);
        report.warnings.append(
            QStringLiteral("Embedded LookTable/RGBTable not converted — use external .cube/.3dl LUT"));
    } else if (key == QStringLiteral("ProcessVersion")) {
        report.warnings.append(
            QStringLiteral("ProcessVersion %1 noted — MyLightroom uses internal PV2012-style pipeline")
                .arg(value));
    }
}

XmpImportReport XmpParser::parseContent(const QString& content, DevelopSettings& out,
                                        const QString& suggestedName) {
    XmpImportReport report;
    out = DevelopSettings::defaults();

    const QHash<QString, QString> attrs = extractAttributes(content);
    if (attrs.isEmpty()) {
        report.warnings.append(QStringLiteral("No XMP attributes found"));
        return report;
    }

    for (auto it = attrs.constBegin(); it != attrs.constEnd(); ++it)
        applyAttribute(it.key(), it.value(), out, report);

    // Prefer parametric curve when XMP carries Parametric* fields but only an
    // identity point curve (common in Lightroom exports).
    const bool hasParametric = out.toneCurve.darks != 0.f || out.toneCurve.lights != 0.f ||
                               out.toneCurve.highlights != 0.f || out.toneCurve.shadows != 0.f;
    const bool identityPoint =
        out.toneCurve.points.size() == 2 && out.toneCurve.points[0] == QPointF(0, 0) &&
        out.toneCurve.points[1] == QPointF(255, 255);
    if (hasParametric && identityPoint)
        out.toneCurve.mode = ToneCurveSettings::Mode::Parametric;

    if (report.lookAmount > 0.f && report.lookAmount <= 1.f) {
        out.lut.intensity = report.lookAmount;
    }

    report.presetName = suggestedName;
    if (report.presetName.isEmpty()) {
        if (attrs.contains(QStringLiteral("PresetName")))
            report.presetName = attrs.value(QStringLiteral("PresetName"));
        else if (attrs.contains(QStringLiteral("Name")))
            report.presetName = attrs.value(QStringLiteral("Name"));
    }

    report.success = !report.mappedFields.isEmpty();
    if (!report.success)
        report.warnings.append(QStringLiteral("No compatible develop fields found in XMP"));

    return report;
}

XmpImportReport XmpParser::parseFile(const QString& xmpPath, DevelopSettings& out) {
    QFile f(xmpPath);
    if (!f.open(QIODevice::ReadOnly)) {
        XmpImportReport r;
        r.warnings.append(QStringLiteral("Cannot open file: %1").arg(xmpPath));
        return r;
    }
    return parseContent(QString::fromUtf8(f.readAll()), out, QFileInfo(xmpPath).baseName());
}

} // namespace mylr
