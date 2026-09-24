#include <QtTest/QtTest>

class SmokeTest : public QObject
{
    Q_OBJECT

private slots:
    void bootstrap();
};

void SmokeTest::bootstrap()
{
    QVERIFY(true);
}

QTEST_GUILESS_MAIN(SmokeTest)
#include "tst_smoke.moc"