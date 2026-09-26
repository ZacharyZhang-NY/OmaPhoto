#include "AddressSpaceLimit.h"
#include "DialogDesk.h"
#include "IO/ImageExporter.h"
#include "IO/ImageResizer.h"
#include "IO/ProjectController.h"
#include "UI/ColorPickerSheet.h"
#include <QDialog>
#include <QSemaphore>
#include <QtConcurrent>
#include <QtTest>
#include <malloc.h>

// Canvas and image sizes out of memory, a binary alone.
class ProjectResizeFailureTests : public QObject {
    Q_OBJECT
private slots:
    void anImageResizeThatFindsNoMemoryIsARenderError();
    void aSnapshotThatFindsNoMemoryIsExplained();
    void aWorkerThatFindsNoMemoryIsExplained();
    void aLandingThatFindsNoMemoryIsExplained();
};

namespace {
// A thousand layers: every copy of them needs a megabyte.
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

struct Desk {
    EditorSession session;
    ProjectController controller{session};
    QWidget window;
    DialogDesk alerts;
    bool done = false;
    Desk()
    {
        session.installProject(manyLayers(), QStringLiteral("Many.comp"));
        window.show();
        controller.window = &window;
        alerts.replies = {"OK"};
        alerts.note = [this] { return QStringLiteral("busy %1").arg(session.isProjectBusy()); };
    }
    void open() { controller.canvasSize([this] { done = true; }); }
    // The sheet's width typed, not yet committed.
    PickerField &width()
    {
        auto *field = window.findChild<QDialog *>()->findChild<PickerField *>(QStringLiteral("canvasWidth"));
        field->setText(QStringLiteral("200"));
        field->setModified(true);
        return *field;
    }
    QStringList explained() const
    {
        return {QStringLiteral("alert|2|Couldn’t change canvas size|%1|OK|busy 1").arg(QString::fromUtf8(ExportError(ExportError::Kind::render).what()))};
    }
};
}

void ProjectResizeFailureTests::anImageResizeThatFindsNoMemoryIsARenderError()
{
    const ProjectSnapshot snapshot = manyLayers();
    malloc_trim(0);
    const AddressSpaceLimit limit(512ll * 1024);
    QVERIFY_THROWS_EXCEPTION(ExportError, ImageResizer::resize(snapshot, {.width = 200, .height = 150, .resolution = 72}));
}

void ProjectResizeFailureTests::aSnapshotThatFindsNoMemoryIsExplained()
{
    Desk desk;
    const CanvasDocument before = desk.session.document().value();
    desk.open();
    PickerField &field = desk.width();
    {
        malloc_trim(0);
        const AddressSpaceLimit limit(512ll * 1024);
        QTest::keyClick(&field, Qt::Key_Return);
    }
    QTRY_VERIFY(desk.done);
    QCOMPARE(desk.alerts.seen, desk.explained());
    QVERIFY(!desk.session.isProjectBusy());
    QVERIFY(desk.session.document().value() == before);
}

void ProjectResizeFailureTests::aWorkerThatFindsNoMemoryIsExplained()
{
    Desk desk;
    const CanvasDocument before = desk.session.document().value();
    // The worker waits until the limit is in place.
    QThreadPool *pool = QThreadPool::globalInstance();
    pool->setMaxThreadCount(1);
    QSemaphore entered, release;
    const QFuture<void> held = QtConcurrent::run([&] {
        entered.release();
        release.acquire();
    });
    entered.acquire();
    desk.open();
    QTest::keyClick(&desk.width(), Qt::Key_Return);
    {
        malloc_trim(0);
        const AddressSpaceLimit limit(512ll * 1024);
        release.release();
        pool->waitForDone();
    }
    QTRY_VERIFY(desk.done);
    QCOMPARE(desk.alerts.seen, desk.explained());
    QVERIFY(desk.session.document().value() == before);
}

void ProjectResizeFailureTests::aLandingThatFindsNoMemoryIsExplained()
{
    Desk desk;
    desk.session.renameLayer(desk.session.activeLayerID().value(), QStringLiteral("Renamed"));
    const CanvasDocument before = desk.session.document().value();
    desk.open();
    QTest::keyClick(&desk.width(), Qt::Key_Return);
    // The worker finishes with room; its result lands without.
    QThreadPool::globalInstance()->waitForDone();
    {
        malloc_trim(0);
        const AddressSpaceLimit limit(512ll * 1024);
        QCoreApplication::processEvents();
    }
    QTRY_VERIFY(desk.done);
    QCOMPARE(desk.alerts.seen, desk.explained());
    QVERIFY(desk.session.document().value() == before);
    QCOMPARE(desk.session.history.undoName(), QString("Rename Layer"));
    QVERIFY(desk.session.canUndo());
}

int main(int argc, char **argv)
{
    // Before any thread: workers share the heap the limit counts.
    mallopt(M_ARENA_MAX, 1);
    // Large asks always map memory.
    mallopt(M_MMAP_THRESHOLD, 64 * 1024);
    QApplication app(argc, argv);
    ProjectResizeFailureTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "ProjectResizeFailureTests.moc"
