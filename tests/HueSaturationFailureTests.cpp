#include "AddressSpaceLimit.h"
#include "Document/EditorSession.h"
#include "IO/ImageExporter.h"
#include "Rendering/RasterSnapshot.h"
#include <QtTest>
#include <QThreadPool>
#include <malloc.h>

// Hue/Saturation when memory runs out, each on a fresh heap.
namespace {
// A painted asset without full pixels: a two-pixel base.
std::unique_ptr<EditorSession> rasterSession(int width, int height)
{
    QImage base(2, 2, QImage::Format_RGBA8888_Premultiplied);
    base.fill(Qt::red);
    auto session = std::make_unique<EditorSession>();
    session->createDocument(20, 10);
    session->insert(ImportedImage(std::make_shared<const RasterSnapshot>(width, height, base, QRectF(0, 0, 2, 2), std::vector<BrushPatch>{}),
                                  QImage(), QStringLiteral("Painted")));
    return session;
}
}

// Earlier tests leave free heap that big asks would reuse.
class HueSaturationFailureTests : public QObject {
    Q_OBJECT
private slots:
    // First: it leaves no free heap behind for the rest.
    void aFailedPreviewShowsTheErrorAndNothingElse();
    void failuresReachTheSessionsError();
    void theFilterFailsAsAContextWhenMemoryRunsOut();
    void aFailureReportsThoughTheNextPreviewWaits();
};

void HueSaturationFailureTests::aFailedPreviewShowsTheErrorAndNothingElse()
{
    // Flattened first; the preview's copy needs 67 MB.
    const std::unique_ptr<EditorSession> session = rasterSession(8000, 2100);
    QVERIFY(!session->activeLayer().value().asset.value().image().isNull());
    session->beginHueSaturation();
    const int revision = session->brushRevision();
    {
        malloc_trim(0);
        const AddressSpaceLimit limit(16ll * 1024 * 1024);
        session->updateHueSaturation(HueSaturationSettings(120), true);
        QTRY_VERIFY(session->brushError());
    }
    const HueSaturationEdit &edit = session->hueSaturation().value();
    QVERIFY(!edit.previewImage(edit.layerID) && session->brushRevision() == revision);
    session->cancelHueSaturation();
}

void HueSaturationFailureTests::failuresReachTheSessionsError()
{
    // Workers allocate in their own arenas: only large asks fail.
    {
        // 10000 by 9999 in tiles: the scaled copy, 256 MB.
        const std::unique_ptr<EditorSession> session = rasterSession(10000, 9999);
        {
            malloc_trim(0);
            const AddressSpaceLimit limit(16ll * 1024 * 1024);
            session->beginHueSaturation();
        }
        QVERIFY(!session->hueSaturation() && session->brushError());
    }
    // 30000 by 3000: a small copy; the flatten, 360 MB.
    const std::unique_ptr<EditorSession> session = rasterSession(30000, 3000);
    session->beginHueSaturation();
    session->updateHueSaturation(HueSaturationSettings(120), false);
    const ImageIdentity before = session->activeLayer().value().asset.value().identity();
    const int count = session->history.undoCount();
    {
        malloc_trim(0);
        const AddressSpaceLimit limit(16ll * 1024 * 1024);
        bool done = false;
        session->commitHueSaturation([&done] { done = true; });
        QTRY_VERIFY(done);
    }
    QVERIFY(session->brushError() && !session->hueSaturation() && !session->isProjectBusy());
    QCOMPARE(session->activeLayer().value().asset.value().identity(), before);
    QCOMPARE(session->history.undoCount(), count);
}

void HueSaturationFailureTests::theFilterFailsAsAContextWhenMemoryRunsOut()
{
    // One pixel wide: its row list asks as its pixels.
    QImage tall(1, 16'000'000, QImage::Format_RGBA8888_Premultiplied);
    tall.fill(Qt::red);
    const HueSaturationJob job{tall, HueSaturationSettings(120), std::nullopt, QTransform(), false};
    malloc_trim(0);
    const AddressSpaceLimit limit(80ll * 1024 * 1024);
    QVERIFY_THROWS_EXCEPTION(ExportError, HueSaturationFilter::run(job));
}

void HueSaturationFailureTests::aFailureReportsThoughTheNextPreviewWaits()
{
    const std::unique_ptr<EditorSession> session = rasterSession(8000, 2100);
    QVERIFY(!session->activeLayer().value().asset.value().image().isNull());
    session->beginHueSaturation();
    {
        malloc_trim(0);
        const AddressSpaceLimit limit(16ll * 1024 * 1024);
        session->updateHueSaturation(HueSaturationSettings(120), true);
        // Failed but undelivered when the next one asks.
        QThreadPool::globalInstance()->waitForDone();
    }
    session->updateHueSaturation(HueSaturationSettings(240), true);
    QVERIFY(session->hueSaturationPending() && !session->brushError());
    QTRY_VERIFY(session->brushError());
    QTRY_VERIFY(!session->hueSaturationPending() && session->hueSaturation().value().previewImage(session->activeLayerID().value()));
    session->cancelHueSaturation();
}

QTEST_GUILESS_MAIN(HueSaturationFailureTests)
#include "HueSaturationFailureTests.moc"
