#include "../src/render/DevelopPipeline.hpp"
#include "../src/core/DevelopSettings.hpp"
#include <QImage>
#include <QtTest>

class DevelopPipelineTest : public QObject {
    Q_OBJECT
private slots:
    void histogramCountsPixels() {
        QImage img(100, 100, QImage::Format_RGB32);
        img.fill(qRgb(128, 128, 128));
        mylr::DevelopPipeline pipe;
        const auto h = pipe.computeHistogram(img);
        QCOMPARE(h.luminance.size(), 256);
        QCOMPARE(h.luminance[128], 100 * 100);
    }

    void exposureBrightens() {
        QImage img(10, 10, QImage::Format_RGB32);
        img.fill(qRgb(64, 64, 64));
        mylr::DevelopPipeline pipe;
        mylr::DevelopSettings s = mylr::DevelopSettings::defaults();
        s.basic.exposure = 1.f;
        const QImage out = pipe.render(img, s);
        QVERIFY(qRed(out.pixel(0, 0)) > 64);
    }
};

QTEST_MAIN(DevelopPipelineTest)
#include "test_develop_pipeline.moc"
