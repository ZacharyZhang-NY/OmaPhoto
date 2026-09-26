#include "Document/LiveLayerMask.h"
#include "Document/PixelAdjust.h"
#include "SessionFixtures.h"
#include <QtTest>

// The baker: a live mask pressed into a layer's pixels.
class LiveMaskBakeTests : public QObject {
    Q_OBJECT
private slots:
    void aBakeMapsTheSourceIntoTheTargetsOwnPixels();
    void aBakeKeepsTheSourcesOwnMaskInAnyShape();
    void aStretchedSourceBakesThroughItsOwnGrid();
    void aSourceWithoutPixelsHidesTheTarget();
    void aSnapshotThatRepeatsAnIdIsRefused();
    void thumbnailsShrinkToNinetySixAndNeverGrow();
};

void LiveMaskBakeTests::aBakeMapsTheSourceIntoTheTargetsOwnPixels()
{
    // The source at the origin; the target one pixel right.
    QImage alpha(2, 2, QImage::Format_RGBA8888_Premultiplied);
    const int alphas[] = {255, 0, 128, 255};
    for (int index = 0; index < 4; ++index)
        alpha.setPixelColor(index % 2, index / 2, QColor(255, 0, 0, alphas[index]));
    QImage opaque(2, 2, QImage::Format_RGBA8888_Premultiplied);
    opaque.fill(Qt::blue);
    EditorSession session;
    session.createDocument(4, 2);
    session.insert(ImportedImage(alpha, QImage(), "Source"), QPointF(1, 1));
    const QUuid source = session.activeLayerID().value();
    session.insert(ImportedImage(opaque, QImage(), "Target"), QPointF(2, 1));
    const QUuid target = session.activeLayerID().value();
    rewrite(session, [&](ProjectSnapshot &snapshot) {
        for (ProjectLayerRecord &layer : snapshot.manifest.layers)
            layer.transform.sampling = LayerSampling::nearest;
        // The target's own opacity and mask stay out.
        record(snapshot, target).opacity = 0.5;
        record(snapshot, source).opacity = 0.5;
        QImage gray(2, 2, QImage::Format_Grayscale8);
        gray.fill(128);
        setMask(snapshot, target, LayerMask::assetFrom(gray));
    });
    QVERIFY(session.linkMask(source, target));
    const ProjectSnapshot snapshot = session.projectSnapshot().value();
    const ImportedImage baked = LiveMaskBaker::bake(snapshot, target).value();
    QCOMPARE(baked.size(), QSize(2, 2));
    QCOMPARE(baked.name, QString("Target"));
    QCOMPARE(baked.thumbnail.size(), QSize(2, 2));
    // Its left column meets the source's right: 0 and 128.
    const QImage pixels = baked.image();
    QCOMPARE(pixels.pixelColor(0, 0).alpha(), 0);
    QVERIFY2(std::abs(pixels.pixelColor(0, 1).alpha() - 128) <= 1, qPrintable(QString::number(pixels.pixelColor(0, 1).alpha())));
    QCOMPARE(pixels.pixelColor(0, 1).blue(), 255);
    QCOMPARE(pixels.pixelColor(1, 0).alpha(), 0);
    QCOMPARE(pixels.pixelColor(1, 1).alpha(), 0);
    // A source clipped in turn: both alphas, each in place.
    QImage half(2, 2, QImage::Format_RGBA8888_Premultiplied);
    half.fill(QColor(0, 255, 0, 128));
    session.selectLayer(std::nullopt);
    session.insert(ImportedImage(half, QImage(), "Second"), QPointF(1, 1));
    const QUuid secondSource = session.activeLayerID().value();
    QVERIFY(session.linkMask(secondSource, source));
    QTest::failOnWarning(QRegularExpression(".*"));
    const QImage chained = LiveMaskBaker::bake(session.projectSnapshot().value(), target).value().image();
    QCOMPARE(chained.pixelColor(0, 0).alpha(), 0);
    QVERIFY2(std::abs(chained.pixelColor(0, 1).alpha() - 64) <= 1, qPrintable(QString::number(chained.pixelColor(0, 1).alpha())));
    QCOMPARE(chained.pixelColor(1, 1).alpha(), 0);
    // No pixels, or no such layer: nothing to bake.
    QVERIFY(!LiveMaskBaker::bake(snapshot, QUuid::createUuid()).has_value());
    ProjectSnapshot bare = snapshot;
    bare.images.erase(target);
    QVERIFY(!LiveMaskBaker::bake(bare, target).has_value());
}

void LiveMaskBakeTests::aBakeKeepsTheSourcesOwnMaskInAnyShape()
{
    // Three by two: a square would hide swapped sides.
    QImage red(3, 2, QImage::Format_RGBA8888_Premultiplied);
    red.fill(Qt::red);
    QImage blue(3, 2, QImage::Format_RGBA8888_Premultiplied);
    blue.fill(Qt::blue);
    QImage gray(3, 2, QImage::Format_Grayscale8);
    const uchar values[] = {255, 0, 128, 0, 255, 64};
    for (int index = 0; index < 6; ++index)
        gray.scanLine(index / 3)[index % 3] = values[index];
    EditorSession session;
    session.createDocument(3, 2);
    session.insert(ImportedImage(red, QImage(), "Source"));
    const QUuid source = session.activeLayerID().value();
    session.insert(ImportedImage(blue, QImage(), "Target"));
    const QUuid target = session.activeLayerID().value();
    rewrite(session, [&](ProjectSnapshot &snapshot) {
        for (ProjectLayerRecord &layer : snapshot.manifest.layers)
            layer.transform.sampling = LayerSampling::nearest;
        setMask(snapshot, source, LayerMask::assetFrom(gray));
    });
    QVERIFY(session.linkMask(source, target));
    const ImportedImage baked = LiveMaskBaker::bake(session.projectSnapshot().value(), target).value();
    QCOMPARE(baked.size(), QSize(3, 2));
    QCOMPARE(baked.thumbnail.size(), QSize(3, 2));
    // The source's own mask hides what it hides.
    const QImage pixels = baked.image();
    for (int index = 0; index < 6; ++index) {
        const QColor pixel = pixels.pixelColor(index % 3, index / 3);
        QVERIFY2(std::abs(pixel.alpha() - values[index]) <= 1, qPrintable(QString("%1: %2").arg(index).arg(pixel.alpha())));
        if (values[index] == 255)
            QCOMPARE(pixel, QColor(Qt::blue));
    }
    // Moved a pixel right, the mask fills the source's grid.
    rewrite(session, [&](ProjectSnapshot &snapshot) { record(snapshot, source).maskPlacement = LayerTransform{.origin = {1, 0}, .size = {3, 2}}; });
    const QImage moved = LiveMaskBaker::bake(session.projectSnapshot().value(), target).value().image();
    const int shifted[] = {0, 255, 0, 0, 0, 255};
    for (int index = 0; index < 6; ++index) {
        const int alpha = moved.pixelColor(index % 3, index / 3).alpha();
        QVERIFY2(std::abs(alpha - shifted[index]) <= 1, qPrintable(QString("%1: %2").arg(index).arg(alpha)));
    }
}

void LiveMaskBakeTests::aStretchedSourceBakesThroughItsOwnGrid()
{
    // Three by two pixels stretched over six by four.
    QImage red(3, 2, QImage::Format_RGBA8888_Premultiplied);
    red.fill(Qt::red);
    QImage blue(6, 4, QImage::Format_RGBA8888_Premultiplied);
    blue.fill(Qt::blue);
    QImage gray(3, 2, QImage::Format_Grayscale8);
    const uchar values[] = {255, 0, 128, 0, 255, 64};
    for (int index = 0; index < 6; ++index)
        gray.scanLine(index / 3)[index % 3] = values[index];
    EditorSession session;
    session.createDocument(6, 4);
    session.insert(ImportedImage(red, QImage(), "Source"));
    const QUuid source = session.activeLayerID().value();
    session.insert(ImportedImage(blue, QImage(), "Target"));
    const QUuid target = session.activeLayerID().value();
    rewrite(session, [&](ProjectSnapshot &snapshot) {
        for (ProjectLayerRecord &layer : snapshot.manifest.layers)
            layer.transform.sampling = LayerSampling::nearest;
        record(snapshot, source).transform.origin = {0, 0};
        record(snapshot, source).transform.size = {6, 4};
        // The mask sits one source pixel to the right.
        setMask(snapshot, source, LayerMask::assetFrom(gray));
        record(snapshot, source).maskPlacement = LayerTransform{.origin = {2, 0}, .size = {6, 4}};
    });
    QVERIFY(session.linkMask(source, target));
    ProjectSnapshot snapshot = session.projectSnapshot().value();
    // An older project states no opacity: that means one.
    record(snapshot, source).opacity = std::nullopt;
    const ImportedImage baked = LiveMaskBaker::bake(snapshot, target).value();
    QCOMPARE(baked.size(), QSize(6, 4));
    const QImage pixels = baked.image();
    const int grid[] = {0, 255, 0, 0, 0, 255};
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 6; ++x) {
            const int alpha = pixels.pixelColor(x, y).alpha();
            QVERIFY2(alpha == grid[y / 2 * 3 + x / 2], qPrintable(QString("%1,%2: %3").arg(x).arg(y).arg(alpha)));
        }
    }
}

void LiveMaskBakeTests::aSourceWithoutPixelsHidesTheTarget()
{
    EditorSession session;
    session.createDocument(2, 2);
    session.addBlankLayer();
    const QUuid blank = session.activeLayerID().value();
    QImage blue(2, 2, QImage::Format_RGBA8888_Premultiplied);
    blue.fill(Qt::blue);
    session.insert(ImportedImage(blue, QImage(), "Target"));
    const QUuid target = session.activeLayerID().value();
    QVERIFY(session.linkMask(blank, target));
    QTest::failOnWarning(QRegularExpression(".*"));
    // A blank source covers nothing: all of the target goes.
    ProjectSnapshot snapshot = session.projectSnapshot().value();
    QVERIFY(!snapshot.images.contains(blank));
    const ImportedImage baked = LiveMaskBaker::bake(snapshot, target).value();
    QCOMPARE(baked.size(), QSize(2, 2));
    for (int index = 0; index < 4; ++index)
        QCOMPARE(baked.image().pixelColor(index % 2, index / 2).alpha(), 0);
    // So does a link to a layer the snapshot lacks,
    const QUuid stranger = QUuid::createUuid();
    record(snapshot, target).maskSourceID = stranger;
    const QImage orphan = LiveMaskBaker::bake(snapshot, target).value().image();
    for (int index = 0; index < 4; ++index)
        QCOMPARE(orphan.pixelColor(index % 2, index / 2).alpha(), 0);
    // even when stray pixels carry that layer's id.
    snapshot.images.insert({stranger, ImportedImage(blue, QImage(), "Stray")});
    const QImage stray = LiveMaskBaker::bake(snapshot, target).value().image();
    for (int index = 0; index < 4; ++index)
        QCOMPARE(stray.pixelColor(index % 2, index / 2).alpha(), 0);
    // Pixels without a record are no layer to bake.
    QVERIFY(!LiveMaskBaker::bake(snapshot, stranger).has_value());
}

void LiveMaskBakeTests::aSnapshotThatRepeatsAnIdIsRefused()
{
    EditorSession session;
    session.createDocument(2, 2);
    session.insert(ImportedImage(QImage(2, 2, QImage::Format_RGBA8888_Premultiplied), QImage(), "Only"));
    const QUuid only = session.activeLayerID().value();
    ProjectSnapshot twice = session.projectSnapshot().value();
    twice.manifest.layers.push_back(twice.manifest.layers.front());
    // Swift's dictionary of unique keys traps here.
    QVERIFY_EXCEPTION_THROWN(LiveMaskBaker::bake(twice, only), std::logic_error);
    // Nothing to bake is settled first, as in Swift.
    QVERIFY(!LiveMaskBaker::bake(twice, QUuid::createUuid()).has_value());
    ProjectSnapshot bare = twice;
    bare.images.erase(only);
    QVERIFY(!LiveMaskBaker::bake(bare, only).has_value());
}

void LiveMaskBakeTests::thumbnailsShrinkToNinetySixAndNeverGrow()
{
    const auto size = [](int width, int height) { return PixelAdjust::thumbnail(QImage(width, height, QImage::Format_RGBA8888_Premultiplied)).size(); };
    QCOMPARE(size(960, 480), QSize(96, 48));
    QCOMPARE(size(480, 960), QSize(48, 96));
    // Truncated, never under one, never enlarged.
    QCOMPARE(size(1000, 15), QSize(96, 1));
    QCOMPARE(size(4000, 10), QSize(96, 1));
    QCOMPARE(size(97, 50), QSize(96, 49));
    QCOMPARE(size(100, 58), QSize(96, 55));
    QCOMPARE(size(96, 96), QSize(96, 96));
    QCOMPARE(size(8, 4), QSize(8, 4));
    QImage red(200, 100, QImage::Format_RGBA8888_Premultiplied);
    red.fill(Qt::red);
    const QImage small = PixelAdjust::thumbnail(red);
    QCOMPARE(small.format(), QImage::Format_RGBA8888_Premultiplied);
    QCOMPARE(small.pixelColor(95, 47), QColor(Qt::red));
}

QTEST_GUILESS_MAIN(LiveMaskBakeTests)
#include "LiveMaskBakeTests.moc"
