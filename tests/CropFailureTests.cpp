#include "AddressSpaceLimit.h"
#include "Document/EditorSession.h"
#include "IO/CanvasResizer.h"
#include "IO/ImageExporter.h"
#include <QSemaphore>
#include <QtConcurrent>
#include <QtTest>
#include <malloc.h>

// Crops when memory runs out, in a binary alone.
class CropFailureTests : public QObject {
    Q_OBJECT
private slots:
    void aResizeThatFindsNoMemoryIsARenderError();
    void aCropThatFindsNoMemorySaysWhyAndKeepsTheProject();
    void aCropThatCannotTakeItsSnapshotSaysWhy();
    void aCropThatCannotLandSaysWhyAndKeepsTheProject();
    void aNewSizeLandsWholeOrNotAtAll();
};

namespace {
QString renderError()
{
    return QString::fromUtf8(ExportError(ExportError::Kind::render).what());
}

// A thousand layers: the resize's records need a megabyte.
ProjectSnapshot manyLayers()
{
    ProjectSnapshot snapshot{.manifest = {.documentID = QUuid::createUuid(), .width = 400, .height = 300, .activeLayerID = std::nullopt, .layers = {}},
                             .images = {}};
    for (int index = 0; index < 1024; ++index)
        snapshot.manifest.layers.push_back({.id = QUuid::createUuid(), .name = QStringLiteral("Blank"), .isVisible = true,
                                            .transform = {.origin = {0, 0}, .size = {400, 300}}, .imageFile = std::nullopt});
    snapshot.manifest.activeLayerID = snapshot.manifest.layers.front().id;
    return snapshot;
}
}

void CropFailureTests::aResizeThatFindsNoMemoryIsARenderError()
{
    const ProjectSnapshot snapshot = manyLayers();
    malloc_trim(0);
    const AddressSpaceLimit limit(512ll * 1024);
    QVERIFY_THROWS_EXCEPTION(ExportError, CanvasResizer::resize(snapshot, {.width = 200, .height = 100}));
}

void CropFailureTests::aCropThatFindsNoMemorySaysWhyAndKeepsTheProject()
{
    EditorSession session;
    session.installProject(manyLayers(), QStringLiteral("Many.comp"));
    session.selectTool(NavigationTool::crop);
    session.setCropRect(QRectF(10, 10, 200, 100));
    const CanvasDocument before = session.document().value();
    const int steps = session.history.undoCount();
    // The worker waits until the limit is in place.
    QThreadPool *pool = QThreadPool::globalInstance();
    pool->setMaxThreadCount(1);
    QSemaphore entered, release;
    const QFuture<void> held = QtConcurrent::run([&] {
        entered.release();
        release.acquire();
    });
    entered.acquire();
    bool done = false;
    session.commitCrop([&] { done = true; });
    {
        malloc_trim(0);
        const AddressSpaceLimit limit(512ll * 1024);
        release.release();
        pool->waitForDone();
    }
    QTRY_VERIFY(done);
    QCOMPARE(session.cropError(), std::optional(renderError()));
    QVERIFY(!session.isProjectBusy());
    QCOMPARE(session.cropRect(), std::optional(QRectF(10, 10, 200, 100)));
    QVERIFY(session.document().value() == before);
    QCOMPARE(session.history.undoCount(), steps);
    // With room again, the same frame crops.
    session.setCropError(std::nullopt);
    done = false;
    session.commitCrop([&] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(session.document().value().width, 200);
    QCOMPARE(session.history.undoCount(), steps + 1);
}

void CropFailureTests::aCropThatCannotTakeItsSnapshotSaysWhy()
{
    EditorSession session;
    session.installProject(manyLayers(), QStringLiteral("Many.comp"));
    session.selectTool(NavigationTool::crop);
    session.setCropRect(QRectF(10, 10, 200, 100));
    const CanvasDocument before = session.document().value();
    bool done = false;
    {
        malloc_trim(0);
        const AddressSpaceLimit limit(512ll * 1024);
        session.commitCrop([&] { done = true; });
    }
    // Refused at once: nothing started, nothing is busy.
    QCOMPARE(session.cropError(), std::optional(renderError()));
    QVERIFY(!session.isProjectBusy());
    QTRY_VERIFY(done);
    QCOMPARE(session.cropRect(), std::optional(QRectF(10, 10, 200, 100)));
    QVERIFY(session.document().value() == before);
}

void CropFailureTests::aCropThatCannotLandSaysWhyAndKeepsTheProject()
{
    EditorSession session;
    session.installProject(manyLayers(), QStringLiteral("Many.comp"));
    // A step before: a failed landing leaves it undoable.
    session.renameLayer(session.activeLayerID().value(), QStringLiteral("Renamed"));
    session.selectTool(NavigationTool::crop);
    session.setCropRect(QRectF(10, 10, 200, 100));
    const CanvasDocument before = session.document().value();
    const int steps = session.history.undoCount();
    bool done = false;
    session.commitCrop([&] { done = true; });
    // The worker finishes with room; its result lands without.
    QThreadPool::globalInstance()->waitForDone();
    {
        malloc_trim(0);
        const AddressSpaceLimit limit(512ll * 1024);
        QTRY_VERIFY(done);
    }
    QCOMPARE(session.cropError(), std::optional(renderError()));
    QVERIFY(!session.isProjectBusy());
    QCOMPARE(session.cropRect(), std::optional(QRectF(10, 10, 200, 100)));
    QVERIFY(session.document().value() == before);
    QCOMPARE(session.history.undoCount(), steps);
    QCOMPARE(session.history.undoName(), QString("Rename Layer"));
    QVERIFY(session.canUndo());
    session.setCropError(std::nullopt);
    done = false;
    session.commitCrop([&] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(session.document().value().width, 200);
    QCOMPARE(session.history.undoName(), QString("Crop"));
    session.undo();
    QVERIFY(session.document().value() == before);
}

void CropFailureTests::aNewSizeLandsWholeOrNotAtAll()
{
    const ProjectSnapshot source = manyLayers();
    const ProjectSnapshot smaller = CanvasResizer::resize(source, {.width = 200, .height = 100});
    // Each room: it lands with its step, or nothing moves.
    bool refused = false;
    for (qint64 room = 0; room <= 16ll * 1024 * 1024; room += 64 * 1024) {
        EditorSession session;
        session.installProject(source, QStringLiteral("Many.comp"));
        session.renameLayer(session.activeLayerID().value(), QStringLiteral("Renamed"));
        const CanvasDocument before = session.document().value();
        try {
            malloc_trim(0);
            const AddressSpaceLimit limit(room);
            session.applyDocumentSize(smaller, QStringLiteral("Canvas Size"));
        } catch (const std::bad_alloc &) {
            refused = true;
            QVERIFY(session.document().value() == before);
            QCOMPARE(session.history.undoName(), QString("Rename Layer"));
            QVERIFY(session.canUndo());
            continue;
        }
        QVERIFY(refused);
        QCOMPARE(session.document().value().width, 200);
        QCOMPARE(session.history.undoName(), QString("Canvas Size"));
        return;
    }
    QFAIL("the size never landed");
}

int main(int argc, char **argv)
{
    // Before any thread: workers share the heap the limit counts.
    mallopt(M_ARENA_MAX, 1);
    // Large asks always map memory.
    mallopt(M_MMAP_THRESHOLD, 64 * 1024);
    QCoreApplication app(argc, argv);
    CropFailureTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "CropFailureTests.moc"
