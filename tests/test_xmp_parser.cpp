#include "../src/core/XmpParser.hpp"
#include <QtTest>

class XmpParserTest : public QObject {
    Q_OBJECT
private slots:
    void parsesFullLightroomPreset() {
        const QString xmp = R"(<?xml version="1.0"?>
<x:xmpmeta>
 <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
  <rdf:Description crs:Temperature="5800" crs:Tint="+12"
    crs:Exposure2012="+0.45" crs:Contrast2012="+15"
    crs:Highlights2012="-40" crs:Shadows2012="+35"
    crs:Texture="+20" crs:Clarity2012="+10" crs:Dehaze="+5"
    crs:Vibrance="+15" crs:ParametricShadows="+10"
    crs:HueAdjustmentOrange="+5" crs:ColorGradeShadowHue="220"
    crs:PostCropVignetteAmount="-15" crs:Sharpness="40"
    crs:LookAmount="80" crs:LookName="WarmFilm"
    xmlns:crs="http://ns.adobe.com/camera-raw-settings/1.0/"/>
 </rdf:RDF>
</x:xmpmeta>)";

        mylr::DevelopSettings s;
        const mylr::XmpImportReport report = mylr::XmpParser::parseContent(xmp, s, QStringLiteral("TestPreset"));
        QVERIFY(report.success);
        QCOMPARE(s.basic.temp, 5800.f);
        QCOMPARE(s.basic.exposure, 0.45f);
        QCOMPARE(s.basic.texture, 20.f);
        QCOMPARE(s.hsl[1].hue, 5.f);
        QCOMPARE(s.effects.vignetteAmount, -15.f);
        QCOMPARE(s.detail.sharpenAmount, 40.f);
        QCOMPARE(s.lut.intensity, 0.8f);
        QCOMPARE(report.externalLutHint, QStringLiteral("WarmFilm"));
    }
};

QTEST_MAIN(XmpParserTest)
#include "test_xmp_parser.moc"
