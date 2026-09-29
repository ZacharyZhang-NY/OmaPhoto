#include "Document/DocumentLimits.h"
#include <QtTest>
#include <unistd.h>

// Swift 1.2.8's DocumentLimits: one surface, and a document's budget.
class DocumentLimitsTests : public QObject {
    Q_OBJECT
private slots:
    void theCeilingsAreSwifts();
};

void DocumentLimitsTests::theCeilingsAreSwifts()
{
    QCOMPARE(DocumentLimits::maxSide, 30'000);
    QCOMPARE(DocumentLimits::maxSurfacePixels, qint64(200'000'000));
    QCOMPARE(DocumentLimits::maxSurfaceMegapixels(), qint64(200));
    QCOMPARE(DocumentLimits::maxSideText(), QString("30,000"));
    // A quarter of memory, four bytes a pixel, clamped.
    const qint64 memory = qint64(sysconf(_SC_PHYS_PAGES)) * qint64(sysconf(_SC_PAGE_SIZE));
    const qint64 budget = std::clamp(memory / 16, qint64(200'000'000), qint64(800'000'000));
    QCOMPARE(DocumentLimits::documentPixelBudget(), budget);
    QCOMPARE(DocumentLimits::documentBudgetMegapixels(), budget / 1'000'000);
    // Swift's 537 MP on 8 GB; the cap, the floor.
    QCOMPARE(DocumentLimits::budgetFor(8ll << 30), qint64(536'870'912));
    QCOMPARE(DocumentLimits::budgetFor(16ll << 30), qint64(800'000'000));
    QCOMPARE(DocumentLimits::budgetFor(2ll << 30), qint64(200'000'000));
    // Both stay under a square at the side limit.
    QVERIFY(DocumentLimits::documentPixelBudget() < qint64(DocumentLimits::maxSide) * DocumentLimits::maxSide);
}

QTEST_GUILESS_MAIN(DocumentLimitsTests)
#include "DocumentLimitsTests.moc"
