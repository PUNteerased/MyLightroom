#include "../src/lut/LutEngine.hpp"
#include <QTemporaryFile>
#include <QtTest>

class Lut3dlTest : public QObject {
    Q_OBJECT
private slots:
    void load3dlMesh() {
        QTemporaryFile f;
        f.open();
        f.write("Mesh 2\n0 0 0\n4095 0 0\n0 4095 0\n4095 4095 0\n0 0 4095\n4095 0 4095\n0 4095 4095\n4095 4095 4095\n");
        f.close();

        mylr::LutEngine engine;
        mylr::Lut3D lut;
        QVERIFY(engine.load3dl(f.fileName(), lut));
        QCOMPARE(lut.size, 2);
    }
};

QTEST_MAIN(Lut3dlTest)
#include "test_lut_3dl.moc"
