#include "../src/core/SidecarIO.hpp"
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class SidecarIOTest : public QObject {
    Q_OBJECT
private slots:
    void roundTripSettings() {
        QTemporaryDir dir;
        const QString raw = dir.path() + QStringLiteral("/test.cr2");
        QFile f(raw);
        f.open(QIODevice::WriteOnly);
        f.write("dummy");
        f.close();

        mylr::DevelopSettings s = mylr::DevelopSettings::defaults();
        s.basic.exposure = 0.75f;
        s.basic.temp = 5800.f;
        QVERIFY(mylr::SidecarIO::save(raw, s));

        mylr::DevelopSettings loaded;
        QVERIFY(mylr::SidecarIO::load(raw, loaded));
        QCOMPARE(loaded.basic.exposure, 0.75f);
        QCOMPARE(loaded.basic.temp, 5800.f);
    }
};

QTEST_MAIN(SidecarIOTest)
#include "test_sidecar_io.moc"
