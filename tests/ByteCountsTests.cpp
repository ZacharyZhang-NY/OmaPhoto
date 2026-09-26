#include "UI/ByteCounts.h"
#include <QtTest>

// Foundation's byte counts: units, places and grouping.
class ByteCountsTests : public QObject {
    Q_OBJECT
private slots:
    void memoryCountsInBinaryUnits();
    void filesCountInThousands();
};

void ByteCountsTests::memoryCountsInBinaryUnits()
{
    QCOMPARE(ByteCounts::memory(4), QString("4 bytes"));
    QCOMPARE(ByteCounts::memory(1023), QString("1,023 bytes"));
    QCOMPARE(ByteCounts::memory(1024), QString("1 KB"));
    QCOMPARE(ByteCounts::memory(10 * 1024), QString("10 KB"));
    QCOMPARE(ByteCounts::memory(1000 * 1024), QString("1,000 KB"));
    QCOMPARE(ByteCounts::memory(1024 * 1024), QString("1 MB"));
    QCOMPARE(ByteCounts::memory(3 * 1024 * 1024 + 300 * 1024), QString("3.3 MB"));
    QCOMPARE(ByteCounts::memory(qint64(30'000) * 30'000 * 4), QString("3.35 GB"));
    QCOMPARE(ByteCounts::memory(qint64(2) * 1024 * 1024 * 1024 + 10 * 1024 * 1024), QString("2.01 GB"));
    QCOMPARE(ByteCounts::memory(qint64(2) * 1024 * 1024 * 1024 + 100 * 1024 * 1024), QString("2.1 GB"));
}

void ByteCountsTests::filesCountInThousands()
{
    QCOMPARE(ByteCounts::file(631), QString("631 bytes"));
    QCOMPARE(ByteCounts::file(999), QString("999 bytes"));
    QCOMPARE(ByteCounts::file(1000), QString("1 KB"));
    QCOMPARE(ByteCounts::file(20'400), QString("20 KB"));
    QCOMPARE(ByteCounts::file(999'999), QString("1,000 KB"));
    QCOMPARE(ByteCounts::file(1'000'000), QString("1 MB"));
    QCOMPARE(ByteCounts::file(2'345'678), QString("2.3 MB"));
    QCOMPARE(ByteCounts::file(40'020'000), QString("40 MB"));
}

QTEST_APPLESS_MAIN(ByteCountsTests)
#include "ByteCountsTests.moc"
