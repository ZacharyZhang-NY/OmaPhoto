#include "Document/EditorSession.h"
#include <QtTest>

namespace {
// A red image as large as the canvas.
void insertPaintedLayer(EditorSession &session)
{
    QImage image(session.document().value().size().toSize(), QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::red);
    session.insert(ImportedImage(image, image, "Painted layer"));
}

bool near(QPointF lhs, QPointF rhs, double tolerance = 0.0001)
{
    return std::abs(lhs.x() - rhs.x()) < tolerance && std::abs(lhs.y() - rhs.y()) < tolerance;
}
}

class TransformTests : public QObject {
    Q_OBJECT
private slots:
    void blankLayersHaveNoTransformHandlesOrEditing();
    void rotatedResizeKeepsOppositeAnchorAtEveryHandle();
    void moveRotateAndShiftConstraints();
    void previewCommitCancelAndUndoPreserveSources();
    void duplicateTransformPreservesOriginalAndSupportsUndoAndCancel();
    void duplicateTransformCopiesEveryPixelLayerButNoFolder();
    void scalePercentSetsBothSidesAboutTheCenter();
    void noOpInvalidValuesAndSwitchingTools();
    void rotatedHitTesting();
    void unitToDocumentAgreesWithPoint();
    void unitToDocumentFlipsAboutTheCenter();
    void placingRecoversATransformFromItsMap();
    void followingCarriesAPlainMoveExactly();
    void followingCarriesAMaskThroughScaleAndRotation();
    void roundedLeavesWholePixelsAndDegrees();
    void samePlacementIgnoresSampling();
    void transformsCompareExactly();
    void validityLimits();
    void resizePastTheAnchorFlipsTheLayer();
    void distortDragMovesCornersEdgesAndBody();
    void snapPullsTheNearestGuideWithinTolerance();
};

void TransformTests::rotatedResizeKeepsOppositeAnchorAtEveryHandle()
{
    const LayerTransform original{.origin = {31, -19}, .size = {200, 100}, .rotation = 37};
    for (int index = 0; index < 8; ++index) {
        const QPointF handle = LayerTransform::handles[index];
        const QPointF opposite(1 - handle.x(), 1 - handle.y());
        const QPointF start = original.point(handle);
        const TransformDrag drag{original, start, {TransformDrag::Kind::resize, index}, std::nullopt};
        for (bool locked : {true, false}) {
            const LayerTransform changed = drag.updated(start + QPointF(34, 17), locked, false);
            QVERIFY(near(original.point(opposite), changed.point(opposite)));
            if (locked)
                QVERIFY(std::abs(changed.size.width() / changed.size.height() - 2) < 0.0001);
            QVERIFY(changed.isValid());
            QVERIFY(!(changed == original));
        }
    }
}

void TransformTests::moveRotateAndShiftConstraints()
{
    const LayerTransform original{.origin = {0, 0}, .size = {100, 50}};
    const TransformDrag move{original, {40, 20}, {TransformDrag::Kind::move}, std::nullopt};
    const LayerTransform moved = move.updated({60, 25}, true, true);
    QCOMPARE(moved.origin, QPointF(20, 0));
    const TransformDrag rotate{original, {100, 25}, {TransformDrag::Kind::rotate}, std::nullopt};
    const LayerTransform rotated = rotate.updated({50, 75}, true, false);
    QVERIFY(std::abs(rotated.rotation - 90) < 0.0001);
    QCOMPARE(rotated.center(), original.center());
    const LayerTransform snapped = rotate.updated({99, 45}, true, true);
    QCOMPARE(snapped.rotation, 15.0);
    const TransformDrag resize{original, {100, 50}, {TransformDrag::Kind::resize, 4}, std::nullopt};
    const LayerTransform free = resize.updated({150, 50}, true, true);
    QCOMPARE(free.size, QSizeF(150, 50));
}

void TransformTests::blankLayersHaveNoTransformHandlesOrEditing()
{
    EditorSession session;
    session.createDocument(100, 100);
    session.addBlankLayer();
    QVERIFY(!session.canTransform());
    session.beginTransform();
    QVERIFY(!session.transformEdit().has_value());
    insertPaintedLayer(session);
    QVERIFY(session.canTransform());
}

void TransformTests::previewCommitCancelAndUndoPreserveSources()
{
    EditorSession session;
    session.createDocument(200, 100);
    insertPaintedLayer(session);
    const ImageLayer original = session.activeLayer().value();
    const int count = session.history.undoCount();
    session.beginTransform();
    LayerTransform value = original.transform;
    value.origin = {-45, 34};
    value.size = {80, 140};
    value.rotation = 23;
    value.flipX = true;
    for (int index = 0; index < 30; ++index)
        session.previewTransform(value);
    QCOMPARE(session.activeLayer().value().transform, original.transform);
    QCOMPARE(session.history.undoCount(), count);
    session.cancelTransform();
    QCOMPARE(session.activeLayer().value(), original);
    session.beginTransform();
    session.previewTransform(value);
    session.commitTransform();
    QCOMPARE(session.history.undoCount(), count + 1);
    QCOMPARE(session.activeLayer().value().transform, value);
    session.undo();
    QCOMPARE(session.activeLayer().value(), original);
    session.redo();
    QCOMPARE(session.activeLayer().value().transform, value);
    QCOMPARE(session.activeLayerID(), std::optional(original.id));
}

void TransformTests::scalePercentSetsBothSidesAboutTheCenter()
{
    EditorSession session;
    session.createDocument(400, 200);
    insertPaintedLayer(session);
    const QSizeF pixels = session.transformPixelSize().value();
    QCOMPARE(pixels, QSizeF(400, 200));
    LayerTransform stretched = session.activeLayer().value().transform;
    stretched.size = {pixels.width() * 2, pixels.height() * 3};
    stretched.rotation = 30;
    QCOMPARE(stretched.scalePercent(pixels), 200.0);
    const LayerTransform scaled = stretched.scaled(50, pixels);
    QCOMPARE(scaled.size, QSizeF(pixels.width() / 2, pixels.height() / 2));
    QVERIFY(near(scaled.center(), stretched.center(), 0.001));
    QCOMPARE(scaled.rotation, 30.0);
    QCOMPARE(scaled.scalePercent(pixels), 50.0);
}

void TransformTests::noOpInvalidValuesAndSwitchingTools()
{
    EditorSession session;
    session.createDocument(200, 100);
    insertPaintedLayer(session);
    const int count = session.history.undoCount();
    session.beginTransform();
    session.commitTransform();
    QCOMPARE(session.history.undoCount(), count);
    session.beginTransform();
    LayerTransform invalid = session.transformEdit().value().draft;
    invalid.size.setWidth(0);
    session.previewTransform(invalid);
    QCOMPARE(session.transformEdit().value().draft.size.width(), 200.0);
    invalid.size.setWidth(std::numeric_limits<double>::infinity());
    session.previewTransform(invalid);
    QCOMPARE(session.transformEdit().value().draft.size.width(), 200.0);
    LayerTransform moved = session.transformEdit().value().draft;
    moved.origin.setX(10);
    session.previewTransform(moved);
    session.selectTool(NavigationTool::hand);
    QVERIFY(!session.transformEdit().has_value());
    QCOMPARE(session.activeLayer().value().transform.origin.x(), 10.0);
    QCOMPARE(session.history.undoCount(), count + 1);
}

void TransformTests::rotatedHitTesting()
{
    const LayerTransform transform{.origin = {100, 200}, .size = {100, 50}, .rotation = 90};
    QVERIFY(transform.contains(transform.center()));
    QVERIFY(transform.contains(QPointF(150, 265)));
    QVERIFY(!transform.contains(QPointF(190, 225)));
}

void TransformTests::unitToDocumentAgreesWithPoint()
{
    const LayerTransform transform{.origin = {31, -19}, .size = {200, 100}, .rotation = 37};
    const QTransform map = transform.unitToDocument();
    for (const QPointF unit : {QPointF(0, 0), QPointF(1, 0), QPointF(1, 1), QPointF(0.25, 0.8)})
        QVERIFY(near(map.map(unit), transform.point(unit)));
    const LayerTransform quarterTurn{.origin = {0, 0}, .size = {100, 50}, .rotation = 90};
    QVERIFY(near(quarterTurn.unitToDocument().map(QPointF(1, 0.5)), QPointF(50, 75)));
}

void TransformTests::unitToDocumentFlipsAboutTheCenter()
{
    const LayerTransform flipped{.origin = {10, 20}, .size = {100, 50}, .flipX = true};
    QVERIFY(near(flipped.unitToDocument().map(QPointF(0, 0)), QPointF(110, 20)));
    const LayerTransform turnedOver{.origin = {10, 20}, .size = {100, 50}, .flipY = true};
    QVERIFY(near(turnedOver.unitToDocument().map(QPointF(0, 0)), QPointF(10, 70)));
}

void TransformTests::placingRecoversATransformFromItsMap()
{
    for (bool flipX : {false, true}) {
        for (bool flipY : {false, true}) {
            const LayerTransform source{.origin = {31, -19}, .size = {200, 100}, .rotation = 397,
                                        .flipX = flipX, .flipY = flipY, .sampling = LayerSampling::smooth};
            const LayerTransform placed = source.placing(source.unitToDocument());
            QVERIFY(near(placed.origin, source.origin));
            QVERIFY(std::abs(placed.size.width() - 200) < 0.0001);
            QVERIFY(std::abs(placed.size.height() - 100) < 0.0001);
            QVERIFY(std::abs(placed.rotation - 397) < 0.0001);
            QCOMPARE(placed.flipX, flipX);
            QCOMPARE(placed.flipY, flipY);
            QCOMPARE(placed.sampling, LayerSampling::smooth);
        }
    }
}

void TransformTests::followingCarriesAPlainMoveExactly()
{
    const LayerTransform mask{.origin = {5.5, 7.25}, .size = {40, 30}, .rotation = 12};
    const LayerTransform old{.origin = {0, 0}, .size = {100, 50}, .rotation = 12};
    LayerTransform moved = old;
    moved.origin = {13, -4};
    const LayerTransform carried = mask.following(old, moved);
    QCOMPARE(carried.origin, QPointF(18.5, 3.25));
    QCOMPARE(carried.size, mask.size);
    QCOMPARE(carried.rotation, 12.0);
    QCOMPARE(mask.following(old, old), mask);
}

void TransformTests::followingCarriesAMaskThroughScaleAndRotation()
{
    const LayerTransform old{.origin = {0, 0}, .size = {100, 50}};
    const LayerTransform inner{.origin = {25, 12.5}, .size = {50, 25}};
    const LayerTransform updated{.origin = {100, 100}, .size = {200, 100}, .rotation = 90};
    const LayerTransform carried = inner.following(old, updated);
    QVERIFY(near(carried.center(), updated.center()));
    QVERIFY(std::abs(carried.size.width() - 100) < 0.0001);
    QVERIFY(std::abs(carried.size.height() - 50) < 0.0001);
    QVERIFY(std::abs(carried.rotation - 90) < 0.0001);
    for (const QPointF unit : {QPointF(0, 0), QPointF(1, 1)})
        QVERIFY(near(carried.point(unit), updated.point(old.unitToDocument().inverted().map(inner.point(unit)))));
}

void TransformTests::roundedLeavesWholePixelsAndDegrees()
{
    const LayerTransform rough{.origin = {10.5, -3.5}, .size = {0.2, 99.5}, .rotation = 44.5};
    const LayerTransform whole = rough.rounded();
    QCOMPARE(whole.origin, QPointF(11, -4));
    QCOMPARE(whole.size, QSizeF(1, 100));
    QCOMPARE(whole.rotation, 45.0);
}

void TransformTests::samePlacementIgnoresSampling()
{
    const LayerTransform high{.origin = {1, 2}, .size = {30, 40}, .rotation = 5};
    LayerTransform nearest = high;
    nearest.sampling = LayerSampling::nearest;
    QVERIFY(!(high == nearest));
    QVERIFY(high.samePlacement(nearest));
    nearest.origin = {2, 2};
    QVERIFY(!high.samePlacement(nearest));
}

void TransformTests::transformsCompareExactly()
{
    const LayerTransform transform{.origin = {1, 2}, .size = {30, 40}, .rotation = 5};
    QVERIFY(transform == LayerTransform(transform));
    const auto differs = [&](auto change) {
        LayerTransform other = transform;
        change(other);
        return !(transform == other);
    };
    QVERIFY(differs([](LayerTransform &other) { other.origin.rx() += 1e-13; }));
    QVERIFY(differs([](LayerTransform &other) { other.origin.ry() += 1e-13; }));
    QVERIFY(differs([](LayerTransform &other) { other.size.rwidth() += 1e-12; }));
    QVERIFY(differs([](LayerTransform &other) { other.size.rheight() += 1e-12; }));
    QVERIFY(differs([](LayerTransform &other) { other.rotation += 1e-13; }));
    QVERIFY(differs([](LayerTransform &other) { other.flipX = true; }));
    QVERIFY(differs([](LayerTransform &other) { other.flipY = true; }));
    QVERIFY(differs([](LayerTransform &other) { other.sampling = LayerSampling::smooth; }));
}

void TransformTests::validityLimits()
{
    const LayerTransform valid{.origin = {-1'000'000, 1'000'000}, .size = {1, 300'000}};
    QVERIFY(valid.isValid());
    LayerTransform tooSmall = valid;
    tooSmall.size = {0.5, 10};
    QVERIFY(!tooSmall.isValid());
    LayerTransform tooLarge = valid;
    tooLarge.size = {300'001, 10};
    QVERIFY(!tooLarge.isValid());
    LayerTransform tooFar = valid;
    tooFar.origin = {1'000'001, 0};
    QVERIFY(!tooFar.isValid());
    LayerTransform notFinite = valid;
    notFinite.rotation = std::nan("");
    QVERIFY(!notFinite.isValid());
}

void TransformTests::resizePastTheAnchorFlipsTheLayer()
{
    const LayerTransform original{.origin = {0, 0}, .size = {100, 50}};
    const TransformDrag resize{original, {100, 25}, {TransformDrag::Kind::resize, 3}, std::nullopt};
    const LayerTransform turned = resize.updated({-60, 25}, false, false);
    QVERIFY(turned.flipX);
    QVERIFY(!turned.flipY);
    QCOMPARE(turned.size, QSizeF(60, 50));
    QCOMPARE(turned.origin, QPointF(-60, 0));
}

void TransformTests::distortDragMovesCornersEdgesAndBody()
{
    const LayerTransform original{.origin = {0, 0}, .size = {100, 50}};
    const Corners corners{QPointF(0, 0), QPointF(100, 0), QPointF(100, 50), QPointF(0, 50)};
    const TransformDrag corner{original, {100, 0}, {TransformDrag::Kind::distort, 2}, corners};
    QCOMPARE(corner.corners({110, 5}).value(), (Corners{QPointF(0, 0), QPointF(110, 5), QPointF(100, 50), QPointF(0, 50)}));
    QCOMPARE(corner.corners({110, 5}, true).value(), (Corners{QPointF(0, 0), QPointF(110, 0), QPointF(100, 50), QPointF(0, 50)}));
    const TransformDrag edge{original, {100, 25}, {TransformDrag::Kind::distort, 3}, corners};
    QCOMPARE(edge.corners({120, 25}).value(), (Corners{QPointF(0, 0), QPointF(120, 0), QPointF(120, 50), QPointF(0, 50)}));
    const TransformDrag body{original, {50, 25}, {TransformDrag::Kind::move}, corners};
    QCOMPARE(body.corners({53, 29}).value(), (Corners{QPointF(3, 4), QPointF(103, 4), QPointF(103, 54), QPointF(3, 54)}));
    const TransformDrag plain{original, {50, 25}, {TransformDrag::Kind::move}, std::nullopt};
    QVERIFY(!plain.corners({53, 29}).has_value());
    const TransformDrag rotating{original, {50, 25}, {TransformDrag::Kind::rotate}, corners};
    QVERIFY(!rotating.corners({53, 29}).has_value());
}

void TransformTests::snapPullsTheNearestGuideWithinTolerance()
{
    const QRectF box(12, 40, 100, 60);
    const TransformSnap::Offset snapped = TransformSnap::offset(box, {0, 60, 500}, {300}, 5);
    QCOMPARE(snapped.offset, QSizeF(-2, 0));
    QCOMPARE(snapped.x, std::optional<double>(60));
    QCOMPARE(snapped.y, std::nullopt);
    const TransformSnap::Offset edge = TransformSnap::offset(box, {9, 116}, {97}, 5);
    QCOMPARE(edge.offset, QSizeF(-3, -3));
    QCOMPARE(edge.x, std::optional<double>(9));
    QCOMPARE(edge.y, std::optional<double>(97));
}

void TransformTests::duplicateTransformPreservesOriginalAndSupportsUndoAndCancel()
{
    EditorSession session;
    session.createDocument(400, 200);
    insertPaintedLayer(session);
    const ImageLayer original = session.activeLayer().value();
    const int count = session.history.undoCount();
    session.beginDuplicateTransform();
    QVERIFY(session.transformEdit().has_value());
    QVERIFY(!session.transformEdit().value().persistent);
    LayerTransform moved = original.transform;
    moved.origin.setX(moved.origin.x() + 50);
    session.previewTransform(moved);
    session.commitTransform();
    QCOMPARE(int(session.document().value().layers.size()), 2);
    QCOMPARE(session.document().value().layers[0].transform, original.transform);
    QCOMPARE(session.activeLayer().value().transform, moved);
    QCOMPARE(session.activeLayer().value().name, QString("Painted layer copy"));
    QCOMPARE(session.history.undoCount(), count + 1);
    QCOMPARE(session.history.undoName(), QString("Duplicate Layer"));
    session.undo();
    QCOMPARE(int(session.document().value().layers.size()), 1);
    QCOMPARE(session.activeLayerID(), std::optional(original.id));
    session.beginDuplicateTransform();
    session.previewTransform(moved);
    session.cancelTransform();
    QCOMPARE(int(session.document().value().layers.size()), 1);
    QCOMPARE(session.activeLayerID(), std::optional(original.id));
    QCOMPARE(session.selectedLayerIDs(), QSet<QUuid>{original.id});
    QCOMPARE(session.history.undoCount(), count);
    QVERIFY(session.canUndo());
    // A second begin while one is open changes nothing.
    session.beginDuplicateTransform();
    session.beginDuplicateTransform();
    QCOMPARE(int(session.document().value().layers.size()), 2);
    session.cancelTransform();
    QCOMPARE(int(session.document().value().layers.size()), 1);
}

void TransformTests::duplicateTransformCopiesEveryPixelLayerButNoFolder()
{
    EditorSession session;
    session.createDocument(400, 200);
    insertPaintedLayer(session);
    const QUuid first = session.activeLayerID().value();
    insertPaintedLayer(session);
    const QUuid second = session.activeLayerID().value();
    session.addGroup();
    const QUuid folder = session.activeLayerID().value();
    // A folder has no pixels to copy; layers do.
    session.selectLayers({first, second, folder}, first);
    session.beginDuplicateTransform();
    QCOMPARE(int(session.document().value().layers.size()), 5);
    QCOMPARE(session.selectedLayerIDs().size(), 2);
    QVERIFY(!session.selectedLayerIDs().contains(first) && !session.selectedLayerIDs().contains(second));
    // The topmost copy leads; copies sit above their sources.
    QCOMPARE(session.document().value().layers[1].name, QString("Painted layer copy"));
    QCOMPARE(session.activeLayerID(), std::optional(session.document().value().layers[3].id));
    QVERIFY(session.transformsAsGroup());
    session.cancelTransform();
    QCOMPARE(int(session.document().value().layers.size()), 3);
    QCOMPARE(session.selectedLayerIDs(), (QSet<QUuid>{first, second, folder}));
    QCOMPARE(session.activeLayerID(), std::optional(first));
    session.selectLayers({first, second}, second);
    session.beginDuplicateTransform();
    session.commitTransform();
    QCOMPARE(session.history.undoName(), QString("Duplicate Layers"));
    QCOMPARE(int(session.document().value().layers.size()), 5);
    // A folder alone has no pixels: nothing begins.
    QVERIFY(session.placeLayer(second, folder));
    session.selectLayer(folder);
    QVERIFY(session.canTransform());
    session.beginDuplicateTransform();
    QVERIFY(!session.transformEdit().has_value());
    QCOMPARE(int(session.document().value().layers.size()), 5);
    QVERIFY(session.canUndo());
    session.setIsProjectBusy(true);
    session.selectLayer(first);
    session.beginDuplicateTransform();
    QVERIFY(!session.transformEdit().has_value());
    session.setIsProjectBusy(false);
    QVERIFY(session.canUndo());
    QCOMPARE(int(session.document().value().layers.size()), 5);
}

QTEST_GUILESS_MAIN(TransformTests)
#include "TransformTests.moc"
