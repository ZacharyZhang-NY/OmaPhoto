#include "AddressSpaceLimit.h"
#include "Document/BrushStroke.h"
#include "Document/SubjectRemoval.h"
#include "IO/ImageExporter.h"
#include <QPainter>
#include <QtTest>
#include <malloc.h>

// Remove Background's preview when memory runs out.
class SubjectRemovalFailureTests : public QObject {
    Q_OBJECT
private slots:
    // Large requests always map memory the limit counts.
    void initTestCase() { mallopt(M_MMAP_THRESHOLD, 64 * 1024); }
    void aPreviewThatFindsNoMemoryIsARenderError();
    void aModelRunThatFindsNoMemoryIsARenderError();
};

namespace {
QImage disc(int width, int height)
{
    QImage image = BrushRaster::context(width, height, false);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(217, 26, 26));
    painter.drawEllipse(QPointF(width / 2.0, height / 2.0), width / 5.0, height / 5.0);
    return image;
}
}

void SubjectRemovalFailureTests::aPreviewThatFindsNoMemoryIsARenderError()
{
    const QImage large = disc(6000, 4000);
    // The model's mask is kept; the preview copies 96 MB.
    QCOMPARE(SubjectRemoval::subjectMask(large, std::nullopt, FilterSettings()).size(), large.size());
    malloc_trim(0);
    const AddressSpaceLimit limit(8ll * 1024 * 1024);
    QVERIFY_THROWS_EXCEPTION(ExportError, SubjectRemoval::run(large, FilterSettings()));
}

void SubjectRemovalFailureTests::aModelRunThatFindsNoMemoryIsARenderError()
{
    if (!placesStarvation())
        QSKIP("the failure point is placed for Qt 6.4's allocations");
    QCOMPARE(SubjectRemoval::subjectMask(disc(200, 150), std::nullopt, FilterSettings()).size(), QSize(200, 150));
    // A new image, whose model input finds no room.
    const QImage fresh = disc(320, 320);
    malloc_trim(0);
    const AddressSpaceLimit limit(512ll * 1024);
    QVERIFY_THROWS_EXCEPTION(ExportError, SubjectRemoval::run(fresh, FilterSettings()));
}

QTEST_GUILESS_MAIN(SubjectRemovalFailureTests)
#include "SubjectRemovalFailureTests.moc"
