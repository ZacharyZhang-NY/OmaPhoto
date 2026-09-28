#include "AddressSpaceLimit.h"
#include "DialogDesk.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectController.h"
#include "UI/JPEGExportSheet.h"
#include <QApplication>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QtConcurrent>
#include <QtTest>
#include <malloc.h>

// Exports that run out of memory: a render error, explained.
class ProjectExportFailureTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void aRenderThatFindsNoMemoryIsARenderError();
    void aSnapshotThatFindsNoMemoryIsExplained();
    void aJPEGRenderThatFindsNoMemoryIsExplained();
    void aPanelsHandOffThatFindsNoMemoryIsExplained();
    void thePanelOpensWithoutCopyingTheSnapshot();
    void aRefusedNameCopiesNoSnapshot();
    void theJPEGRenderTakesTheSnapshotWithoutACopy();
};

namespace {
// A thousand layers on four pixels: copies need a megabyte.
ProjectSnapshot manyLayers()
{
    ProjectSnapshot snapshot{.manifest = {.documentID = QUuid::createUuid(), .width = 2, .height = 2, .activeLayerID = std::nullopt, .layers = {}},
                             .images = {}};
    for (int index = 0; index < 1024; ++index)
        snapshot.manifest.layers.push_back({.id = QUuid::createUuid(), .name = QStringLiteral("Blank"), .isVisible = true,
                                            .transform = {.origin = {0, 0}, .size = {2, 2}}, .imageFile = std::nullopt});
    snapshot.manifest.activeLayerID = snapshot.manifest.layers.front().id;
    return snapshot;
}

// The accept and what it starts run limited; painting not.
std::function<void(const std::function<void()> &)> limited(long long bytes)
{
    return [bytes](const std::function<void()> &accept) {
        malloc_trim(0);
        const AddressSpaceLimit limit(bytes);
        accept();
    };
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
    QStringList explained(const QString &title) const
    {
        return {QStringLiteral("alert|2|%1|%2|OK|busy 1").arg(title, QString::fromUtf8(ExportError(ExportError::Kind::render).what()))};
    }
};
}

// The app opened panels and alerts before any export.
void ProjectExportFailureTests::initTestCase()
{
    Desk desk;
    QTemporaryDir folder;
    QFile taken(folder.filePath("taken.jpg.png"));
    QVERIFY(taken.open(QIODevice::WriteOnly));
    taken.close();
    desk.alerts.replies = {folder.filePath("taken.jpg"), "OK"};
    desk.controller.exportPNG([&desk] { desk.done = true; });
    QTRY_VERIFY(desk.done);
    QCOMPARE(desk.alerts.seen.size(), 2);
}

void ProjectExportFailureTests::aRenderThatFindsNoMemoryIsARenderError()
{
    const ProjectSnapshot snapshot = manyLayers();
    malloc_trim(0);
    const AddressSpaceLimit limit(512ll * 1024);
    try {
        ImageExporter::render(snapshot);
        QFAIL("the render found room");
    } catch (const ExportError &error) {
        QCOMPARE(error.kind, ExportError::Kind::render);
    }
}

void ProjectExportFailureTests::aSnapshotThatFindsNoMemoryIsExplained()
{
    for (const bool jpeg : {false, true}) {
        Desk desk;
        {
            malloc_trim(0);
            const AddressSpaceLimit limit(512ll * 1024);
            if (jpeg)
                desk.controller.exportJPEG([&desk] { desk.done = true; });
            else
                desk.controller.exportPNG([&desk] { desk.done = true; });
        }
        QTRY_VERIFY(desk.done);
        QCOMPARE(desk.alerts.seen, desk.explained(jpeg ? QStringLiteral("Couldn’t export JPEG") : QStringLiteral("Couldn’t export PNG")));
        QVERIFY(!desk.session.isProjectBusy());
    }
}

void ProjectExportFailureTests::aJPEGRenderThatFindsNoMemoryIsExplained()
{
    Desk desk;
    // The worker waits until the limit is in place.
    QThreadPool *pool = QThreadPool::globalInstance();
    pool->setMaxThreadCount(1);
    QSemaphore entered, release;
    const QFuture<void> held = QtConcurrent::run([&] {
        entered.release();
        release.acquire();
    });
    entered.acquire();
    desk.controller.exportJPEG([&desk] { desk.done = true; });
    {
        malloc_trim(0);
        const AddressSpaceLimit limit(512ll * 1024);
        release.release();
        pool->waitForDone();
    }
    QTRY_VERIFY(desk.done);
    QCOMPARE(desk.alerts.seen, desk.explained(QStringLiteral("Couldn’t export JPEG")));
    QVERIFY(!desk.session.isProjectBusy());
}

void ProjectExportFailureTests::aPanelsHandOffThatFindsNoMemoryIsExplained()
{
    Desk desk;
    QTemporaryDir folder;
    desk.alerts.replies = {folder.filePath("many.png"), "OK"};
    desk.alerts.aroundAccept = limited(512ll * 1024);
    desk.controller.exportPNG([&desk] { desk.done = true; });
    QTRY_COMPARE(desk.alerts.seen.size(), 2);
    QTRY_VERIFY(desk.done);
    QCOMPARE(desk.alerts.seen.value(1), desk.explained(QStringLiteral("Couldn’t export PNG")).value(0));
    QVERIFY(!desk.session.isProjectBusy() && !QFileInfo::exists(folder.filePath("many.png")));
}

// A copy needs a megabyte; the panel far less.
void ProjectExportFailureTests::thePanelOpensWithoutCopyingTheSnapshot()
{
    Desk desk;
    desk.alerts.replies = {"<cancel>"};
    {
        malloc_trim(0);
        const AddressSpaceLimit limit(2560ll * 1024);
        desk.controller.exportPNG([&desk] { desk.done = true; });
    }
    QTRY_VERIFY(desk.done);
    QCOMPARE(desk.alerts.seen.size(), 1);
    QVERIFY(desk.alerts.seen.value(0).startsWith("panel|Export PNG|save|file|Many.png|png|"));
    QVERIFY(!desk.session.isProjectBusy());
}

// The refusal's alert fits where a copy would not.
void ProjectExportFailureTests::aRefusedNameCopiesNoSnapshot()
{
    Desk desk;
    QTemporaryDir folder;
    QFile taken(folder.filePath("taken.jpg.png"));
    QVERIFY(taken.open(QIODevice::WriteOnly));
    taken.close();
    desk.alerts.replies = {folder.filePath("taken.jpg"), "OK"};
    desk.alerts.aroundAccept = limited(512ll * 1024);
    desk.controller.exportPNG([&desk] { desk.done = true; });
    QTRY_COMPARE(desk.alerts.seen.size(), 2);
    QTRY_VERIFY(desk.done);
    QVERIFY(desk.alerts.seen.value(1).contains("“taken.jpg.png” already exists."));
    QVERIFY(!desk.session.isProjectBusy());
}

// The worker takes the snapshot; a copy would not fit.
void ProjectExportFailureTests::theJPEGRenderTakesTheSnapshotWithoutACopy()
{
    Desk desk;
    QThreadPool *pool = QThreadPool::globalInstance();
    pool->setMaxThreadCount(1);
    QSemaphore entered, release;
    const QFuture<void> held = QtConcurrent::run([&] {
        entered.release();
        release.acquire();
    });
    entered.acquire();
    {
        malloc_trim(0);
        const AddressSpaceLimit limit(2560ll * 1024);
        desk.controller.exportJPEG([&desk] { desk.done = true; });
    }
    release.release();
    QTRY_VERIFY(desk.window.findChild<JPEGExportSheet *>());
    QTest::keyClick(desk.window.findChild<JPEGExportSheet *>(), Qt::Key_Escape);
    QTRY_VERIFY(desk.done);
    QVERIFY(desk.alerts.seen.isEmpty() && !desk.session.isProjectBusy());
}

int main(int argc, char **argv)
{
    // Before any thread: workers share the heap the limit counts.
    mallopt(M_ARENA_MAX, 1);
    // Large asks always map memory.
    mallopt(M_MMAP_THRESHOLD, 64 * 1024);
    QApplication app(argc, argv);
    ProjectExportFailureTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "ProjectExportFailureTests.moc"
