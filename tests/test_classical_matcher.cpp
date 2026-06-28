#include "../src/ai/ClassicalMatcher.hpp"
#include "../src/core/DevelopSettings.hpp"
#include <QtTest>

class ClassicalMatcherTest : public QObject {
    Q_OBJECT
private slots:
    void exposureSolveShiftsCdf() {
        mylr::SceneFingerprint ref, tgt;
        ref.luminanceCdf.resize(1024);
        tgt.luminanceCdf.resize(1024);
        for (int i = 0; i < 1024; ++i) {
            ref.luminanceCdf[i] = float(i) / 1023.f;
            tgt.luminanceCdf[i] = float(qMax(0, i - 100)) / 1023.f;
        }
        mylr::ClassicalMatcher m;
        const auto result = m.match(ref, tgt, mylr::DevelopSettings::defaults());
        QVERIFY(result.basic.exposure > 0.f);
    }
};

QTEST_MAIN(ClassicalMatcherTest)
#include "test_classical_matcher.moc"
