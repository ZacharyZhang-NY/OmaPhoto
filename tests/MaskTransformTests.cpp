#include "SessionFixtures.h"
#include <QPainter>
#include <QtTest>

namespace {
const QRect square(100, 50, 100, 100);

// A red layer whose mask is white but for `square`.
std::unique_ptr<EditorSession> maskedSession(bool revealing = true)
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(400, 200);
    QImage pixels(400, 200, QImage::Format_RGBA8888_Premultiplied);
    pixels.fill(Qt::red);
    session->insert(ImportedImage(pixels, pixels, "Layer"));
    QImage mask(400, 200, QImage::Format_Grayscale8);
    mask.fill(revealing ? 255 : 0);
    QPainter(&mask).fillRect(square, revealing ? Qt::black : Qt::white);
    const QUuid id = session->activeLayerID().value();
    rewrite(*session, [&](ProjectSnapshot &snapshot) { setMask(snapshot, id, LayerMask::assetFrom(mask)); });
    return session;
}

int gray(const QImage &image, int x, int y)
{
    return image.convertToFormat(QImage::Format_Grayscale8).constScanLine(y)[x];
}

std::optional<LayerTransform> move(EditorSession &session, double dx)
{
    session.beginTransform();
    if (!session.transformEdit().has_value())
        return std::nullopt;
    LayerTransform moved = session.transformEdit().value().draft;
    moved.origin.rx() += dx;
    session.previewTransform(moved);
    return moved;
}

QImage clip(const ImageLayer &layer)
{
    return layer.mask.value().clipImage(layer.mask.value().placement, layer.transform, 400, 200).value();
}
}

class MaskTransformTests : public QObject {
    Q_OBJECT
private slots:
    void aLinkedMaskMovesWithItsLayerWhicheverThumbnailIsSelected();
    void unlinkedTheLayerMovesAloneAndItsMaskStaysOnTheCanvas();
    void unlinkedTheMaskMovesAloneAndRelinkedTheyMoveTogether();
    void aMovedHideAllMaskKeepsHidingPastItsPixels();
    void placementAndLinkAreSavedAndPaintingFollowsThePlacement();
};

void MaskTransformTests::aLinkedMaskMovesWithItsLayerWhicheverThumbnailIsSelected()
{
    const std::unique_ptr<EditorSession> session = maskedSession();
    const ImageLayer layer = session->activeLayer().value();
    QCOMPARE(layer.transform.origin, QPointF(0, 0));
    QVERIFY(layer.mask.value().isLinked);
    session->selectLayerTarget(layer.id, true);
    QVERIFY(session->isMaskSelected() && !session->transformTargetsMask());
    const LayerTransform moved = move(*session, 100).value();
    QVERIFY(!session->transformEdit().value().mask);
    QCOMPARE(session->displayedMaskPlacement(layer), std::nullopt);
    session->commitTransform();
    QCOMPARE(session->activeLayer().value().transform, moved);
    QCOMPARE(session->activeLayer().value().mask.value().placement, std::nullopt);
    QVERIFY(session->activeLayer().value().mask.value().asset.identity() == layer.mask.value().asset.identity());
}

void MaskTransformTests::unlinkedTheLayerMovesAloneAndItsMaskStaysOnTheCanvas()
{
    const std::unique_ptr<EditorSession> session = maskedSession();
    const ImageLayer layer = session->activeLayer().value();
    session->toggleMaskLink(layer.id);
    QVERIFY(!session->activeLayer().value().mask.value().isLinked);
    session->selectLayerTarget(layer.id, false);
    const LayerTransform moved = move(*session, 100).value();
    // The mask stays while the layer drags.
    QCOMPARE(session->displayedMaskPlacement(session->activeLayer().value()), std::optional(layer.transform));
    session->commitTransform();
    const ImageLayer after = session->activeLayer().value();
    QCOMPARE(after.transform, moved);
    QCOMPARE(after.mask.value().placement, std::optional(layer.transform));
    // The square stayed put: layer x 0 to 100.
    const QImage clipped = clip(after);
    QVERIFY(gray(clipped, 50, 100) < 5);
    QVERIFY(gray(clipped, 150, 100) > 250);
    // Past its pixels the mask reveals, like its white edges.
    QVERIFY(gray(clipped, 350, 100) > 250);
    session->undo();
    QCOMPARE(session->activeLayer().value().transform, layer.transform);
    QCOMPARE(session->activeLayer().value().mask.value().placement, std::nullopt);
    QVERIFY(!session->activeLayer().value().mask.value().isLinked);
}

void MaskTransformTests::unlinkedTheMaskMovesAloneAndRelinkedTheyMoveTogether()
{
    const std::unique_ptr<EditorSession> session = maskedSession();
    const ImageLayer layer = session->activeLayer().value();
    session->toggleMaskLink(layer.id);
    session->selectLayerTarget(layer.id, true);
    QVERIFY(session->transformTargetsMask());
    const LayerTransform moved = move(*session, 100).value();
    QVERIFY(session->transformEdit().value().mask);
    // The layer stays put; the handles follow the mask.
    QCOMPARE(session->displayedTransform(layer), layer.transform);
    QCOMPARE(session->editedTransform(layer), moved);
    QCOMPARE(session->displayedMaskPlacement(layer), std::optional(moved));
    session->commitTransform();
    QCOMPARE(session->activeLayer().value().transform, layer.transform);
    QCOMPARE(session->activeLayer().value().mask.value().placement, std::optional(moved));
    // Moving a mask never resamples it.
    QVERIFY(session->activeLayer().value().mask.value().asset.identity() == layer.mask.value().asset.identity());
    QCOMPARE(session->history.undoName(), QString("Transform Layer Mask"));
    session->toggleMaskLink(layer.id);
    session->selectLayerTarget(layer.id, false);
    QVERIFY(move(*session, 50).has_value());
    session->commitTransform();
    const LayerTransform placement = session->activeLayer().value().mask.value().placement.value();
    QVERIFY(std::abs(placement.origin.x() - (moved.origin.x() + 50)) < 0.001 && std::abs(placement.origin.y() - moved.origin.y()) < 0.001);
    QVERIFY(std::abs(placement.size.width() - moved.size.width()) < 0.001 && std::abs(placement.size.height() - moved.size.height()) < 0.001);
}

void MaskTransformTests::aMovedHideAllMaskKeepsHidingPastItsPixels()
{
    const std::unique_ptr<EditorSession> session = maskedSession(false);
    const ImageLayer layer = session->activeLayer().value();
    session->toggleMaskLink(layer.id);
    session->selectLayerTarget(layer.id, true);
    QVERIFY(move(*session, 100).has_value());
    session->commitTransform();
    const QImage clipped = clip(session->activeLayer().value());
    // The uncovered edge stays hidden; the revealed square moved.
    QVERIFY(gray(clipped, 20, 100) < 5);
    QVERIFY(gray(clipped, 250, 100) > 250);
}

void MaskTransformTests::placementAndLinkAreSavedAndPaintingFollowsThePlacement()
{
    const std::unique_ptr<EditorSession> session = maskedSession();
    const ImageLayer layer = session->activeLayer().value();
    session->toggleMaskLink(layer.id);
    session->selectLayerTarget(layer.id, true);
    const LayerTransform moved = move(*session, 100).value();
    session->commitTransform();
    const ProjectSnapshot snapshot = session->projectSnapshot().value();
    // Through the manifest's JSON and back.
    const ProjectManifest decoded = ProjectManifest::decoded(snapshot.manifest.encoded());
    const LayerMask restored = snapshot.mask(decoded.layers.front()).value();
    QCOMPARE(restored.placement, std::optional(moved));
    QVERIFY(!restored.isLinked);
    // Mask pixels map through the placement.
    const std::unique_ptr<BrushStroke> stroke = session->makeRasterEdit(session->activeLayer().value());
    const QPointF corner = stroke->pixelToDocument.map(QPointF(0, 0));
    QVERIFY(std::abs(corner.x() - moved.origin.x()) < 0.001 && std::abs(corner.y() - moved.origin.y()) < 0.001);
}

QTEST_GUILESS_MAIN(MaskTransformTests)
#include "MaskTransformTests.moc"
