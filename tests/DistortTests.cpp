#include "Document/BrushStroke.h"
#include "Document/Distort.h"
#include "IO/ImageExporter.h"
#include "SessionFixtures.h"
#include <QtTest>

// Free distortion: the warp, the session's edit, masks along.
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

bool near(QPointF a, QPointF b)
{
    return std::abs(a.x() - b.x()) < 1e-6 && std::abs(a.y() - b.y()) < 1e-6;
}
}

class DistortTests : public QObject {
    Q_OBJECT
private slots:
    void cornersRunClockwiseFromTheTopLeft();
    void perspectiveMappingHitsTheCornersAndTwistedShapesAreRefused();
    void distortingWarpsTheLayerIntoTheShapeAsOneUndoStep();
    void distortedLayerIsTrimmedToItsVisiblePixels();
    void foldedShapesWarpAsTwoTrianglesAndFlipsSwapCorners();
    void carriedCornersAndWarpedMasks();
    void nudgesGroupsAndRefusals();
    void mapPathCarriesCurvesFlipsAndTheFillRule();
};

void DistortTests::cornersRunClockwiseFromTheTopLeft()
{
    const LayerTransform upright{.origin = {10, 20}, .size = {40, 30}};
    QCOMPARE(DistortWarp::corners(upright), (Corners{QPointF(10, 20), QPointF(50, 20), QPointF(50, 50), QPointF(10, 50)}));
    // Turned, each corner is where the transform puts it.
    const LayerTransform turned{.origin = {10, 20}, .size = {40, 30}, .rotation = 37, .flipX = true};
    const Corners corners = DistortWarp::corners(turned);
    const std::array<QPointF, 4> unit = {QPointF(0, 0), QPointF(1, 0), QPointF(1, 1), QPointF(0, 1)};
    for (size_t index = 0; index < 4; ++index)
        QVERIFY(near(corners[index], turned.point(unit[index])));
    QVERIFY(!near(corners[1], corners[0]));
}

void DistortTests::perspectiveMappingHitsTheCornersAndTwistedShapesAreRefused()
{
    const QTransform map = DistortWarp::homography(shape);
    const Corners unit = {QPointF(0, 0), QPointF(1, 0), QPointF(1, 1), QPointF(0, 1)};
    for (size_t index = 0; index < 4; ++index)
        QVERIFY2(near(map.map(unit[index]), shape[index]), qPrintable(QString("corner %1").arg(index)));
    // The middle lands where the diagonals cross, a perspective.
    const QPointF middle = map.map(QPointF(0.5, 0.5));
    QVERIFY(std::abs(middle.x() - 170.0 / 7) < 1e-9 && std::abs(middle.y() - 170.0 / 7) < 1e-9);
    // A slanted bottom too: both perspective terms at work.
    const Corners slanted = {QPointF(10, 10), QPointF(60, 10), QPointF(30, 40), QPointF(10, 30)};
    const QTransform slant = DistortWarp::homography(slanted);
    for (size_t index = 0; index < 4; ++index)
        QVERIFY(near(slant.map(unit[index]), slanted[index]));
    const QPointF crossing = slant.map(QPointF(0.5, 0.5));
    QVERIFY(std::abs(crossing.x() - 390.0 / 19) < 1e-9 && std::abs(crossing.y() - 490.0 / 19) < 1e-9);
    QVERIFY(DistortWarp::isUsable(shape) && DistortWarp::isConvex(shape));
    // A bow tie folds: usable as two triangles, no perspective.
    const Corners bowTie = {shape[0], shape[2], shape[1], shape[3]};
    const Corners collapsed = {shape[0], shape[0], shape[2], shape[3]};
    QVERIFY(DistortWarp::isUsable(bowTie) && !DistortWarp::isConvex(bowTie));
    QVERIFY(!DistortWarp::isUsable(collapsed));
    // A flat half is refused; a straight side, no perspective.
    QVERIFY(!DistortWarp::isUsable({QPointF(10, 10), QPointF(60, 10), QPointF(30, 30), QPointF(20, 20)}));
    QVERIFY(DistortWarp::isUsable({QPointF(10, 10), QPointF(60, 10), QPointF(60, 30), QPointF(60, 50)}));
    QVERIFY(!DistortWarp::isConvex({QPointF(10, 10), QPointF(60, 10), QPointF(60, 30), QPointF(60, 50)}));
    QVERIFY(!DistortWarp::isUsable({shape[0], QPointF(std::nan(""), 10), shape[2], shape[3]}));
    QVERIFY(!DistortWarp::isUsable({shape[0], QPointF(2'000'000, 10), shape[2], shape[3]}));
    // A dent is usable but no perspective: two triangles.
    const Corners dented = {QPointF(10, 10), QPointF(60, 10), QPointF(15, 15), QPointF(10, 60)};
    QVERIFY(DistortWarp::isUsable(dented) && !DistortWarp::isConvex(dented));
    // Mirrored winding still counts as convex.
    QVERIFY(DistortWarp::isConvex({shape[1], shape[0], shape[3], shape[2]}));
}

void DistortTests::distortingWarpsTheLayerIntoTheShapeAsOneUndoStep()
{
    EditorSession session;
    session.createDocument(100, 60);
    const QUuid id = insertAt(session, red(20, 20), QPointF(10, 10));
    session.selectTool(NavigationTool::move);
    session.beginTransform(false);
    session.beginDistort();
    QVERIFY(session.transformEdit().value().corners.has_value() && session.transformEdit().value().persistent);
    // A collapsed corner: ignored.
    session.previewCorners({shape[0], shape[0], shape[2], shape[3]});
    QCOMPARE(session.transformEdit().value().corners.value()[1], QPointF(30, 10));
    session.previewCorners(shape);
    QCOMPARE(session.transformEdit().value().corners.value(), shape);
    const int count = session.history.undoCount();
    session.commitTransform();
    QVERIFY(!session.transformEdit().has_value());
    QCOMPARE(session.history.undoCount(), count + 1);
    QCOMPARE(session.history.undoName(), QString("Distort"));
    const LayerTransform transform = layerWith(session, id).transform;
    QCOMPARE(transform, (LayerTransform{.origin = {10, 10}, .size = {50, 20}}));
    QCOMPARE(layerWith(session, id).asset.value().size(), QSize(50, 20));
    QCOMPARE(alphaAt(session, 50, 12), 255);
    QCOMPARE(alphaAt(session, 15, 25), 255);
    QCOMPARE(alphaAt(session, 50, 28), 0);
    QCOMPARE(alphaAt(session, 80, 12), 0);
    session.undo();
    QCOMPARE(layerWith(session, id).transform.size, QSizeF(20, 20));
    QCOMPARE(layerWith(session, id).asset.value().size(), QSize(20, 20));
}

void DistortTests::distortedLayerIsTrimmedToItsVisiblePixels()
{
    EditorSession session;
    session.createDocument(100, 60);
    // A 40 by 20 layer, clear except a red square.
    QImage image(40, 20, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.fillRect(QRect(15, 5, 10, 10), Qt::red);
    painter.end();
    const QUuid id = insertAt(session, image, QPointF(10, 10));
    session.selectTool(NavigationTool::move);
    session.beginTransform(false);
    session.beginDistort();
    session.previewCorners({QPointF(10, 10), QPointF(60, 10), QPointF(50, 30), QPointF(10, 30)});
    session.commitTransform();
    const LayerTransform transform = layerWith(session, id).transform;
    // Bounds of 50 by 20; the square's warp is smaller.
    QVERIFY2(transform.size.width() < 20 && transform.size.height() <= 12, qPrintable(QString("%1 x %2").arg(transform.size.width()).arg(transform.size.height())));
    QVERIFY2(transform.origin.x() >= 20 && transform.origin.y() >= 14, qPrintable(QString("%1, %2").arg(transform.origin.x()).arg(transform.origin.y())));
    QCOMPARE(alphaAt(session, 30, 20), 255);
    // The trimmed asset is the layer's own size.
    QCOMPARE(QSizeF(layerWith(session, id).asset.value().size()), transform.size);
}

void DistortTests::foldedShapesWarpAsTwoTrianglesAndFlipsSwapCorners()
{
    const LayerTransform placed{.origin = {10, 10}, .size = {20, 20}};
    QVERIFY_THROWS_EXCEPTION(ProjectError, DistortWarp::warp(red(20, 20), placed, {shape[0], shape[0], shape[2], shape[3]}, false));
    // A shape too large for a warp.
    QVERIFY_THROWS_EXCEPTION(ProjectError, DistortWarp::warp(red(20, 20), placed, {QPointF(0, 0), QPointF(40'000, 0), QPointF(40'000, 10), QPointF(0, 10)}, false));
    const Corners dented = {QPointF(10, 10), QPointF(60, 10), QPointF(15, 15), QPointF(10, 60)};
    const DistortWarp::Warped folded = DistortWarp::warp(red(20, 20), placed, dented, false);
    // Copied without interpolation: a colour boundary stays sharp.
    QImage twoTone = red(20, 20);
    QPainter painter(&twoTone);
    painter.fillRect(QRect(0, 0, 10, 20), Qt::blue);
    painter.end();
    const Corners wide = {QPointF(10, 10), QPointF(110, 10), QPointF(20, 20), QPointF(10, 110)};
    const DistortWarp::Warped sharp = DistortWarp::warp(twoTone, placed, wide, false);
    // The half is sheared: its colour boundary sits at 45.5.
    QCOMPARE(sharp.image.pixelColor(43, 0), QColor(Qt::blue));
    QCOMPARE(sharp.image.pixelColor(47, 0), QColor(Qt::red));
    QCOMPARE(folded.transform, (LayerTransform{.origin = {10, 10}, .size = {50, 50}}));
    QCOMPARE(folded.image.size(), QSize(50, 50));
    QCOMPARE(folded.image.pixelColor(20, 2).alpha(), 255);
    QCOMPARE(folded.image.pixelColor(2, 20).alpha(), 255);
    QCOMPARE(folded.image.pixelColor(40, 4).alpha(), 0);
    QCOMPARE(folded.image.pixelColor(45, 45).alpha(), 0);
    // The halves meet along the diagonal without a seam.
    const DistortWarp::Warped seam = DistortWarp::warp(red(20, 20), {.origin = {0, 0}, .size = {20, 20}}, {QPointF(0, 0), QPointF(100, 0), QPointF(30, 30), QPointF(0, 100)}, false);
    for (const int along : {10, 15, 25})
        QCOMPARE(seam.image.pixelColor(along, along).alpha(), 255);
    QCOMPARE(seam.image.pixelColor(50, 5).alpha(), 255);
    QCOMPARE(seam.image.pixelColor(5, 50).alpha(), 255);
    QCOMPARE(seam.image.pixelColor(60, 60).alpha(), 0);
    // Past a half's edge nothing draws, though its map would.
    const DistortWarp::Warped spill = DistortWarp::warp(red(20, 20), {.origin = {0, 0}, .size = {20, 20}}, {QPointF(0, 0), QPointF(100, 0), QPointF(30, 30), QPointF(10, 5)}, false);
    QCOMPARE(spill.image.pixelColor(40, 10).alpha(), 255);
    QCOMPARE(spill.image.pixelColor(5, 20).alpha(), 0);
    QCOMPARE(spill.image.pixelColor(10, 20).alpha(), 0);
    // A centre a sloping edge holds is kept; fractions too.
    const DistortWarp::Warped sloping = DistortWarp::warp(red(20, 20), {.origin = {0, 0}, .size = {20, 20}}, {QPointF(0, 0), QPointF(100, 1), QPointF(25, 26), QPointF(0, 100)}, false);
    QVERIFY(sloping.image.pixelColor(5, 0).alpha() > 200);
    QVERIFY(sloping.image.pixelColor(2, 40).alpha() > 200);
    // Beside the diagonal each pixel comes from its own half.
    QImage patterned(20, 20, QImage::Format_RGBA8888_Premultiplied);
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 20; ++x)
            patterned.setPixelColor(x, y, QColor(12 * x, 12 * y, 0));
    }
    const DistortWarp::Warped beside = DistortWarp::warp(patterned, {.origin = {0, 0}, .size = {20, 20}}, {QPointF(0, 0), QPointF(100, 1), QPointF(25, 26), QPointF(0, 100)}, false);
    QCOMPARE(beside.image.pixelColor(20, 20), QColor(180, 180, 0));
    // A fractional shared edge, met both ways, has no hole.
    const DistortWarp::Warped shared = DistortWarp::warp(red(20, 20), {.origin = {0, 0}, .size = {20, 20}}, {QPointF(0.1, 0.9), QPointF(100, 0.9), QPointF(20.1, 30.9), QPointF(0.1, 100)}, false);
    for (const QPoint hole : {QPoint(4, 7), QPoint(6, 10), QPoint(8, 13)})
        QCOMPARE(shared.image.pixelColor(hole).alpha(), 255);
    const DistortWarp::Warped fractional = DistortWarp::warp(red(20, 20), {.origin = {0, 0}, .size = {20, 20}}, {QPointF(0.4, 0.4), QPointF(60.6, 0.4), QPointF(20.5, 20.5), QPointF(0.4, 60.6)}, false);
    QCOMPARE(fractional.image.pixelColor(30, 5).alpha(), 255);
    QCOMPARE(fractional.image.pixelColor(5, 30).alpha(), 255);
    QCOMPARE(fractional.image.pixelColor(40, 40).alpha(), 0);
    for (const int along : {8, 12, 16})
        QCOMPARE(fractional.image.pixelColor(along, along).alpha(), 255);
    QVERIFY(fractional.image.pixelColor(30, 0).alpha() > 100);
    // A row centred on a horizontal top edge is drawn.
    const DistortWarp::Warped topEdge = DistortWarp::warp(red(20, 20), {.origin = {0, 0}, .size = {20, 20}}, {QPointF(0.5, 0.5), QPointF(100.5, 0.5), QPointF(30, 30), QPointF(0, 100)}, false);
    QVERIFY(topEdge.image.pixelColor(50, 0).alpha() > 100);
    // Where the halves overlap the second replaces the first.
    QImage faint(20, 20, QImage::Format_RGBA8888_Premultiplied);
    faint.fill(QColor(128, 0, 0, 128));
    const DistortWarp::Warped bowTie = DistortWarp::warp(faint, {.origin = {0, 0}, .size = {20, 20}}, {QPointF(0, 0), QPointF(40, 40), QPointF(40, 0), QPointF(0, 40)}, false);
    QCOMPARE(bowTie.image.pixelColor(20, 10).alpha(), 128);
    QCOMPARE(qAlpha(bowTie.image.pixel(20, 10)), 128);
    // A limit shrinks the warp; a flip sends pixels across.
    const DistortWarp::Warped small = DistortWarp::warp(twoTone, placed, shape, false, 25);
    QCOMPARE(small.image.size(), QSize(25, 10));
    QCOMPARE(small.transform, (LayerTransform{.origin = {10, 10}, .size = {50, 20}}));
    QCOMPARE(small.image.pixelColor(2, 2), QColor(Qt::blue));
    LayerTransform flipped = placed;
    flipped.flipX = true;
    const DistortWarp::Warped mirrored = DistortWarp::warp(twoTone, flipped, shape, false);
    QCOMPARE(mirrored.image.pixelColor(2, 2), QColor(Qt::red));
    QCOMPARE(mirrored.image.pixelColor(40, 2), QColor(Qt::blue));
    // The sampling travels with the placement.
    LayerTransform nearest = placed;
    nearest.sampling = LayerSampling::nearest;
    QCOMPARE(DistortWarp::warp(twoTone, nearest, shape, false).transform.sampling, LayerSampling::nearest);
}

void DistortTests::carriedCornersAndWarpedMasks()
{
    const LayerTransform box{.origin = {10, 10}, .size = {20, 20}};
    // The box lands on the shape; one moved aside, aside.
    const Corners same = DistortWarp::carried(box, box, shape);
    for (size_t index = 0; index < 4; ++index)
        QVERIFY(near(same[index], shape[index]));
    const LayerTransform turned{.origin = {10, 10}, .size = {20, 20}, .rotation = 30};
    const Corners turnedSame = DistortWarp::carried(turned, turned, shape);
    for (size_t index = 0; index < 4; ++index)
        QVERIFY(near(turnedSame[index], shape[index]));
    const Corners moved = {QPointF(20, 20), QPointF(40, 20), QPointF(40, 40), QPointF(20, 40)};
    const Corners aside = DistortWarp::carried({.origin = {15, 15}, .size = {20, 20}}, box, moved);
    for (size_t index = 0; index < 4; ++index)
        QVERIFY(near(aside[index], moved[index] + QPointF(5, 5)));
    // A mask's background fills the shape's surround.
    QImage mask(4, 4, QImage::Format_Grayscale8);
    mask.fill(200);
    const Corners half = {QPointF(0, 0), QPointF(8, 0), QPointF(4, 4), QPointF(0, 4)};
    const LayerTransform grid{.origin = {0, 0}, .size = {4, 4}};
    const DistortWarp::Warped lit = DistortWarp::warpMask(mask, grid, half, 1);
    QCOMPARE(lit.image.size(), QSize(8, 4));
    QCOMPARE(lit.image.format(), QImage::Format_Grayscale8);
    QCOMPARE(int(lit.image.constScanLine(3)[7]), 255);
    QVERIFY(std::abs(int(lit.image.constScanLine(1)[1]) - 200) <= 2);
    const DistortWarp::Warped dark = DistortWarp::warpMask(mask, grid, half, 0);
    QCOMPARE(int(dark.image.constScanLine(3)[7]), 0);
    // A uniform mask covers any shape as it is.
    QImage uniform(1, 1, QImage::Format_Grayscale8);
    uniform.fill(255);
    const DistortWarp::Warped kept = DistortWarp::warpMask(uniform, grid, half, 1);
    QCOMPARE(kept.image.cacheKey(), uniform.cacheKey());
    QCOMPARE(kept.transform, (LayerTransform{.origin = {0, 0}, .size = {8, 4}}));
}

void DistortTests::nudgesGroupsAndRefusals()
{
    EditorSession session;
    session.beginDistort();
    session.previewCorners(shape);
    session.createDocument(100, 60);
    const QUuid a = insertAt(session, red(20, 20), QPointF(10, 10));
    const QUuid b = insertAt(session, red(10, 10), QPointF(50, 30));
    session.selectLayer(a);
    // Without an edit or corners, nothing begins or moves.
    session.beginDistort();
    QVERIFY(!session.transformEdit().has_value());
    session.beginTransform();
    session.previewCorners(shape);
    QVERIFY(!session.transformEdit().value().corners.has_value());
    session.beginDistort();
    session.beginDistort();
    const Corners own = DistortWarp::corners(layerWith(session, a).transform);
    QCOMPARE(session.transformEdit().value().corners.value(), own);
    // A nudge moves the corners; a new begin keeps them.
    session.nudgeLayer(3, -2);
    QCOMPARE(session.transformEdit().value().draft.origin, QPointF(13, 8));
    QCOMPARE(session.transformEdit().value().corners.value()[2], own[2] + QPointF(3, -2));
    session.beginDistort();
    QCOMPARE(session.transformEdit().value().corners.value()[2], own[2] + QPointF(3, -2));
    session.cancelTransform();
    QCOMPARE(layerWith(session, a).transform.origin, QPointF(10, 10));
    // A group takes the box's perspective; an unlinked mask stays.
    rewrite(session, [&](ProjectSnapshot &snapshot) {
        setMask(snapshot, b, coverage());
        record(snapshot, b).maskLinked = false;
    });
    session.selectLayers({a, b}, a);
    session.beginTransform();
    session.beginDistort();
    QCOMPARE(session.displayedMaskPlacement(layerWith(session, b)), std::optional(layerWith(session, b).transform));
    const Corners box = session.transformEdit().value().corners.value();
    QCOMPARE(box[0], QPointF(10, 10));
    QCOMPARE(box[2], QPointF(60, 40));
    session.previewCorners({box[0], box[1] + QPointF(20, 0), box[2], box[3]});
    QVERIFY(session.distortPreview(layerWith(session, b)).has_value());
    session.commitTransform();
    QCOMPARE(session.history.undoName(), QString("Distort Layers"));
    QVERIFY(layerWith(session, a).transform.size.width() > 20);
    QVERIFY(layerWith(session, b).transform.size.width() > 10);
    QCOMPARE(layerWith(session, b).transform.origin.x(), 50.0);
}

void DistortTests::mapPathCarriesCurvesFlipsAndTheFillRule()
{
    // A 10-pixel layer at 10,10, its box the shape.
    const LayerTransform transform{.origin = {10, 10}, .size = {10, 10}};
    const QTransform placement = BrushRaster::pixelToDocument(transform, 10, 10);
    const Corners box = DistortWarp::corners(transform);
    QPainterPath ellipse;
    ellipse.addEllipse(QRectF(11, 11, 4, 4));
    ellipse.setFillRule(Qt::WindingFill);
    const QPainterPath carried = DistortWarp::mapPath(ellipse, placement, QSizeF(10, 10), transform, box).value();
    QCOMPARE(carried.fillRule(), Qt::WindingFill);
    QCOMPARE(carried.elementCount(), ellipse.elementCount());
    for (int i = 0; i < ellipse.elementCount(); ++i) {
        QCOMPARE(carried.elementAt(i).type, ellipse.elementAt(i).type);
        QVERIFY(near(carried.elementAt(i), ellipse.elementAt(i)));
    }
    // A flipped draft mirrors it inside the shape.
    LayerTransform flipped = transform;
    flipped.flipX = true;
    QRectF bounds = DistortWarp::mapPath(ellipse, placement, QSizeF(10, 10), flipped, box).value().boundingRect();
    QVERIFY(near(bounds.topLeft(), QPointF(15, 11)) && near(bounds.bottomRight(), QPointF(19, 15)));
    flipped = transform;
    flipped.flipY = true;
    bounds = DistortWarp::mapPath(ellipse, placement, QSizeF(10, 10), flipped, box).value().boundingRect();
    QVERIFY(near(bounds.topLeft(), QPointF(11, 15)) && near(bounds.bottomRight(), QPointF(15, 19)));
    // A folded shape or an empty grid carries nothing.
    const Corners folded = {QPointF(10, 10), QPointF(20, 20), QPointF(20, 10), QPointF(10, 20)};
    QVERIFY(!DistortWarp::mapPath(ellipse, placement, QSizeF(10, 10), transform, folded).has_value());
    QVERIFY(!DistortWarp::mapPath(ellipse, placement, QSizeF(0, 10), transform, box).has_value());
}

QTEST_GUILESS_MAIN(DistortTests)
#include "DistortTests.moc"
