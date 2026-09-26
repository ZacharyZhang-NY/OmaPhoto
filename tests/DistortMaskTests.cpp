#include "Document/Distort.h"
#include "IO/ImageExporter.h"
#include "SessionFixtures.h"
#include <QtTest>

// Masks under a distortion: along with the layer, or alone.
namespace {
// The next thumbnail fails, as an allocation might.
bool failNextScale = false;
}

// Takes libQt6Gui's place, as ProjectReplaceTests takes libc's.
QImage QImage::scaled(const QSize &size, Qt::AspectRatioMode aspectMode, Qt::TransformationMode mode) const
{
    if (failNextScale) {
        failNextScale = false;
        return QImage();
    }
    if (size == this->size() && aspectMode == Qt::IgnoreAspectRatio && mode == Qt::SmoothTransformation)
        return *this;
    // Every scale these tests make is the image's own size.
    throw std::logic_error("DistortTests scale nothing");
}

namespace {
const Corners shape = {QPointF(10, 10), QPointF(60, 10), QPointF(30, 30), QPointF(10, 30)};

QImage red(int width, int height)
{
    QImage image(width, height, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::red);
    return image;
}

QUuid insertAt(EditorSession &session, const QImage &image, QPointF origin)
{
    session.insert(ImportedImage(image, image, "Image"));
    const QUuid id = session.activeLayerID().value();
    rewrite(session, [&](ProjectSnapshot &snapshot) { record(snapshot, id).transform.origin = origin; });
    return id;
}

int alphaAt(const EditorSession &session, int x, int y)
{
    return ImageExporter::render(session.projectSnapshot().value()).image.pixelColor(x, y).alpha();
}

}

class DistortMaskTests : public QObject {
    Q_OBJECT
private slots:
    void masksFollowADistortionOrStayPut();
    void anUnlinkedMaskDistortsOnItsOwn();
};

void DistortMaskTests::masksFollowADistortionOrStayPut()
{
    EditorSession session;
    session.createDocument(100, 60);
    const QUuid id = insertAt(session, red(20, 20), QPointF(10, 10));
    QImage half(20, 20, QImage::Format_Grayscale8);
    half.fill(0);
    for (int y = 0; y < 20; ++y)
        std::fill_n(half.scanLine(y), 10, uchar(255));
    rewrite(session, [&](ProjectSnapshot &snapshot) { setMask(snapshot, id, LayerMask::assetFrom(half)); });
    session.selectTool(NavigationTool::move);
    session.beginTransform(false);
    session.beginDistort();
    session.previewCorners(shape);
    // The preview carries the linked mask, cached while unchanged.
    const std::optional<DistortPreview> preview = session.distortPreview(layerWith(session, id));
    QCOMPARE(preview.value().transform, (LayerTransform{.origin = {10, 10}, .size = {50, 20}}));
    QCOMPARE(preview.value().image.size(), QSize(50, 20));
    QCOMPARE(preview.value().mask.value().size(), QSize(50, 20));
    QCOMPARE(int(preview.value().mask.value().constScanLine(2)[5]), 255);
    QCOMPARE(int(preview.value().mask.value().constScanLine(2)[45]), 0);
    QCOMPARE(session.distortPreview(layerWith(session, id)).value().image.cacheKey(), preview.value().image.cacheKey());
    QVERIFY(!session.displayedMaskPlacement(layerWith(session, id)).has_value());
    QVERIFY(!session.maskDistortPreview(layerWith(session, id)).has_value());
    session.previewCorners({shape[0], QPointF(70, 10), shape[2], shape[3]});
    QVERIFY(session.distortPreview(layerWith(session, id)).value().image.cacheKey() != preview.value().image.cacheKey());
    session.previewCorners(shape);
    session.commitTransform();
    const ImageLayer distorted = layerWith(session, id);
    QCOMPARE(distorted.mask.value().asset.size(), QSize(50, 20));
    QVERIFY(!distorted.mask.value().placement.has_value());
    QCOMPARE(alphaAt(session, 15, 15), 255);
    QCOMPARE(alphaAt(session, 55, 12), 0);
    session.undo();
    // An unlinked mask keeps its place on the document.
    rewrite(session, [&](ProjectSnapshot &snapshot) { record(snapshot, id).maskLinked = false; });
    session.beginTransform(false);
    session.beginDistort();
    QCOMPARE(session.displayedMaskPlacement(layerWith(session, id)), std::optional(layerWith(session, id).transform));
    session.previewCorners(shape);
    QCOMPARE(session.distortPreview(layerWith(session, id)).value().mask.value().size(), QSize(50, 20));
    session.commitTransform();
    QCOMPARE(layerWith(session, id).mask.value().placement, std::optional(LayerTransform{.origin = {10, 10}, .size = {20, 20}}));
    QCOMPARE(layerWith(session, id).mask.value().asset.size(), QSize(20, 20));
    session.undo();
    // A linked mask apart under a fold keeps its place.
    const LayerTransform apart{.origin = {12, 12}, .size = {10, 10}};
    rewrite(session, [&](ProjectSnapshot &snapshot) {
        record(snapshot, id).maskLinked = true;
        record(snapshot, id).maskPlacement = apart;
    });
    session.beginTransform();
    session.previewTransform({.origin = {20, 10}, .size = {20, 20}});
    session.beginDistort();
    session.previewCorners({QPointF(20, 10), QPointF(40, 10), QPointF(22, 12), QPointF(20, 30)});
    const DistortPreview folded = session.distortPreview(layerWith(session, id)).value();
    const LayerMask owned = layerWith(session, id).mask.value();
    QCOMPARE(folded.mask.value(), owned.clipImage(apart, folded.transform, folded.image.width(), folded.image.height(), 2048).value());
    session.cancelTransform();
    // A convex shape carries a placed linked mask along, resampled.
    rewrite(session, [&](ProjectSnapshot &snapshot) { record(snapshot, id).maskPlacement = apart; });
    session.beginTransform();
    session.beginDistort();
    session.previewCorners(shape);
    const ImageLayer before = layerWith(session, id);
    const Corners carried = DistortWarp::carried(apart, before.transform, shape);
    QVERIFY(DistortWarp::isConvex(carried));
    const DistortWarp::Warped expected = DistortWarp::warpMask(before.mask.value().asset.image(), apart, carried, LayerMask::background(before.mask.value().asset.thumbnail));
    const DistortPreview carriedPreview = session.distortPreview(before).value();
    QCOMPARE(carriedPreview.mask.value(), LayerMask(ImportedImage(expected.image, before.mask.value().asset.thumbnail, "m")).clipImage(expected.transform, carriedPreview.transform, 50, 20, 2048).value());
    QVERIFY(carriedPreview.mask.value() != before.mask.value().clipImage(apart, carriedPreview.transform, 50, 20, 2048).value());
    session.commitTransform();
    const LayerMask moved = layerWith(session, id).mask.value();
    QCOMPARE(moved.placement, std::optional(expected.transform));
    QCOMPARE(moved.asset.image(), expected.image);
    QVERIFY(moved.isLinked);
    QVERIFY(expected.transform.origin != apart.origin);
    session.undo();
    // A placement too wide to warp: the layer still previews.
    const LayerTransform wide{.origin = {12, 12}, .size = {40'000, 10}};
    rewrite(session, [&](ProjectSnapshot &snapshot) { record(snapshot, id).maskPlacement = wide; });
    session.beginTransform();
    session.beginDistort();
    session.previewCorners(shape);
    const DistortPreview kept = session.distortPreview(layerWith(session, id)).value();
    QCOMPARE(kept.image.size(), QSize(50, 20));
    QCOMPARE(kept.mask.value(), layerWith(session, id).mask.value().clipImage(wide, kept.transform, 50, 20, 2048).value());
}

void DistortMaskTests::anUnlinkedMaskDistortsOnItsOwn()
{
    EditorSession session;
    session.createDocument(100, 60);
    const QUuid id = insertAt(session, red(20, 20), QPointF(10, 10));
    // A white block in a black border: black background.
    QImage block(20, 20, QImage::Format_Grayscale8);
    block.fill(0);
    for (int y = 2; y < 18; ++y)
        std::fill_n(block.scanLine(y) + 2, 8, uchar(255));
    rewrite(session, [&](ProjectSnapshot &snapshot) {
        setMask(snapshot, id, LayerMask::assetFrom(block));
        record(snapshot, id).maskLinked = false;
    });
    session.selectLayerTarget(id, true);
    QVERIFY(session.transformTargetsMask());
    session.beginTransform();
    session.beginDistort();
    QVERIFY(session.transformEdit().value().mask && session.transformEdit().value().corners.has_value());
    // The mask shrunk into the layer's top left quarter.
    const Corners quarter = {QPointF(10, 10), QPointF(20, 10), QPointF(20, 20), QPointF(10, 20)};
    session.previewCorners(quarter);
    // The preview is the warped mask in the layer's grid.
    const std::optional<QImage> preview = session.maskDistortPreview(layerWith(session, id));
    QCOMPARE(preview.value().size(), QSize(20, 20));
    QCOMPARE(int(preview.value().constScanLine(3)[3]), 255);
    QCOMPARE(int(preview.value().constScanLine(15)[15]), 0);
    QCOMPARE(session.maskDistortPreview(layerWith(session, id)).value().cacheKey(), preview.value().cacheKey());
    QVERIFY(!session.distortPreview(layerWith(session, id)).has_value());
    session.previewCorners({QPointF(10, 10), QPointF(22, 10), QPointF(20, 20), QPointF(10, 20)});
    QVERIFY(session.maskDistortPreview(layerWith(session, id)).value().cacheKey() != preview.value().cacheKey());
    session.previewCorners(quarter);
    const int count = session.history.undoCount();
    session.commitTransform();
    QCOMPARE(session.history.undoName(), QString("Distort Layer Mask"));
    QCOMPARE(session.history.undoCount(), count + 1);
    const LayerMask mask = layerWith(session, id).mask.value();
    QCOMPARE(mask.asset.size(), QSize(10, 10));
    QCOMPARE(mask.placement, std::optional(LayerTransform{.origin = {10, 10}, .size = {10, 10}}));
    QVERIFY(!mask.isLinked);
    QCOMPARE(layerWith(session, id).transform.size, QSizeF(20, 20));
    // Past the mask, its black background hides the layer.
    QCOMPARE(alphaAt(session, 13, 13), 255);
    QCOMPARE(alphaAt(session, 25, 25), 0);
    // A mask apart keeps its place when its corners stay.
    session.beginTransform();
    session.beginDistort();
    session.commitTransform();
    QCOMPARE(session.history.undoName(), QString("Distort Layer Mask"));
    QCOMPARE(layerWith(session, id).mask.value().placement, std::optional(LayerTransform{.origin = {10, 10}, .size = {10, 10}}));
    // A warp that fails opens no transaction: undo stays usable.
    const int steps = session.history.undoCount();
    session.beginTransform();
    session.beginDistort();
    session.previewCorners({QPointF(10, 10), QPointF(40'010, 10), QPointF(40'010, 20), QPointF(10, 20)});
    session.commitTransform();
    QVERIFY(session.brushError().has_value());
    QVERIFY(!session.transformEdit().has_value() && session.canUndo());
    QCOMPARE(session.history.undoCount(), steps);
    session.setBrushError(std::nullopt);
    // A thumbnail failing after the warp opens no transaction.
    session.beginTransform();
    session.beginDistort();
    session.previewCorners({QPointF(10, 10), QPointF(22, 10), QPointF(20, 20), QPointF(10, 20)});
    failNextScale = true;
    session.commitTransform();
    QVERIFY(!failNextScale);
    QVERIFY(session.brushError().has_value());
    QVERIFY(!session.transformEdit().has_value() && session.canUndo());
    QCOMPARE(session.history.undoCount(), steps);
    QCOMPARE(layerWith(session, id).mask.value().asset.size(), QSize(10, 10));
    session.setBrushError(std::nullopt);
    session.selectLayer(id);
    session.toggleLayerVisibility(id);
    QCOMPARE(session.history.undoCount(), steps + 1);
    session.toggleLayerVisibility(id);
    // One covering its layer stays covering it, resampled.
    const QUuid other = insertAt(session, red(20, 20), QPointF(50, 30));
    rewrite(session, [&](ProjectSnapshot &snapshot) {
        setMask(snapshot, other, LayerMask::assetFrom(block));
        record(snapshot, other).maskLinked = false;
    });
    session.selectLayerTarget(other, true);
    session.beginTransform();
    session.beginDistort();
    session.commitTransform();
    QCOMPARE(session.history.undoName(), QString("Distort Layer Mask"));
    QCOMPARE(layerWith(session, other).mask.value().placement, std::nullopt);
    QCOMPARE(layerWith(session, other).mask.value().asset.size(), QSize(20, 20));
    QVERIFY(layerWith(session, other).mask.value().asset.identity() != LayerMask::assetFrom(block).identity());
}

QTEST_GUILESS_MAIN(DistortMaskTests)
#include "DistortMaskTests.moc"
