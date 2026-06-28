#include "../src/lut/LutEngine.hpp"
#include <QTemporaryFile>
#include <QtTest>

class LutEngineTest : public QObject {
    Q_OBJECT
private slots:
    void loadIdentityCube() {
        QTemporaryFile f;
        f.open();
        f.write("TITLE \"identity\"\nLUT_3D_SIZE 2\n0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n");
        f.close();

        mylr::LutEngine engine;
        mylr::Lut3D lut;
        QVERIFY(engine.loadCube(f.fileName(), lut));
        QCOMPARE(lut.size, 2);
        QCOMPARE(lut.data.size(), 2 * 2 * 2 * 3);
    }
};

QTEST_MAIN(LutEngineTest)
#include "test_lut_engine.moc"
