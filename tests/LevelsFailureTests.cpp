#include "AddressSpaceLimit.h"
#include "LevelsFixtures.h"
#include "Rendering/RasterSnapshot.h"
#include <malloc.h>

// Levels when memory runs out, each on a fresh heap.
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

LevelsSettings inverted()
{
    LevelsSettings settings;
    settings.setCurrent(LevelRange{0, 1, 255, 255, 0});
    return settings;
}
}

// Earlier tests leave free heap that big asks would reuse.
class LevelsFailureTests : public QObject {
    Q_OBJECT
private slots:
    // First: it leaves no free heap behind for the rest.
    void aFailedCountOrPreviewLeavesNothing();
    void failuresReachTheSessionsError();
};

void LevelsFailureTests::failuresReachTheSessionsError()
{
    // Workers allocate in their own arenas: only large asks fail.
    {
        // 10000 by 9999 in tiles: the scaled copy, 256 MB.
        const std::unique_ptr<EditorSession> session = rasterSession(10000, 9999);
        {
            // Freed heap returns first: the room is real.
            malloc_trim(0);
            const AddressSpaceLimit limit(16ll * 1024 * 1024);
            session->beginLevels();
        }
        QVERIFY(!session->levels() && session->brushError());
    }
    // 30000 by 3000: a small copy; the flatten, 360 MB.
    const std::unique_ptr<EditorSession> session = rasterSession(30000, 3000);
    session->beginLevels();
    QTRY_VERIFY(session->levels().value().histogramReady);
    session->updateLevels(inverted(), false);
    const ImageIdentity before = session->activeLayer().value().asset.value().identity();
    const int count = session->history.undoCount();
    session->setLevelsSampleMode(LevelsSample::gray);
    {
        malloc_trim(0);
        const AddressSpaceLimit limit(16ll * 1024 * 1024);
        // A sample flattens the pixels first.
        session->sampleLevels(QPointF(5, 5));
        QVERIFY(session->brushError());
        session->setBrushError(std::nullopt);
        commit(*session);
    }
    QVERIFY(session->brushError() && !session->levels() && !session->isProjectBusy());
    QCOMPARE(session->activeLayer().value().asset.value().identity(), before);
    QCOMPARE(session->history.undoCount(), count);
}

void LevelsFailureTests::aFailedCountOrPreviewLeavesNothing()
{
    // Flattened first; the count's and preview's copies need 67 MB.
    const std::unique_ptr<EditorSession> session = rasterSession(8000, 2100);
    QVERIFY(!session->activeLayer().value().asset.value().image().isNull());
    {
        malloc_trim(0);
        const AddressSpaceLimit limit(16ll * 1024 * 1024);
        session->beginLevels();
        QTRY_VERIFY(session->levels().value().histogramReady);
        for (const std::array<double, 256> &channel : session->levels().value().histogram)
            QVERIFY(std::all_of(channel.begin(), channel.end(), [](double bin) { return bin == 0; }));
        // A failed preview lands as none, and redraws.
        const int revision = session->brushRevision();
        session->updateLevels(inverted(), true);
        QTRY_COMPARE(session->brushRevision(), revision + 1);
        QVERIFY(!session->levels().value().preparedPreview);
    }
    session->cancelLevels();
}

QTEST_GUILESS_MAIN(LevelsFailureTests)
#include "LevelsFailureTests.moc"
