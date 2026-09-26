#include "CanvasFixtures.h"
#include "CropFixtures.h"
#include "Document/Crop.h"
#include "IO/ImageExporter.h"
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>
#include <set>

// Swift's CropTests: geometry, snapping and the crop itself.
class CropTests : public QObject {
    Q_OBJECT
private slots:
    void dragGeometrySupportsReverseRatioMoveAndEveryHandle();
    void cropTranslatesWithoutResamplingAndUndoRestoresBounds();
    void sameSizeOffsetCropAndExpansionUseExactBounds();
    void cancellationAndViewportMappingDoNotEditDocument();
    void cropEdgesSnapToNearbyEdges();
    void snapTargetsAreTheCanvasAndLayerBounds();
    void aSymmetricDragKeepsItsMiddle();
    void snappingKeepsToTheDraggedSide();
    void framesStayOnWholePixelsWithinBounds();
    void ratiosReshapeTheFrameAboutItsMiddle();
};


void CropTests::dragGeometrySupportsReverseRatioMoveAndEveryHandle()
{
    const QRectF rect = CropGeometry::create(QPointF(100, 100), QPointF(20, 60), 2);
    QCOMPARE(rect, QRectF(20, 60, 80, 40));
    const CropDrag move{QPointF(40, 70), rect, {CropDrag::Kind::move}};
    QCOMPARE(move.updated(QPointF(30, 40), std::nullopt), rect.translated(-10, -30));
    for (int index = 0; index < 8; ++index) {
        const QPointF unit = LayerTransform::handles[size_t(index)];
        const QPointF start(rect.left() + unit.x() * rect.width(), rect.top() + unit.y() * rect.height());
        const CropDrag drag{start, rect, {CropDrag::Kind::resize, index}};
        const QRectF next = drag.updated(QPointF(start.x() + (unit.x() * 2 - 1) * 20, start.y() + (unit.y() * 2 - 1) * 10), 2);
        QVERIFY(CropGeometry::valid(next));
        QVERIFY(std::abs(next.width() / next.height() - 2) < 0.05);
        QVERIFY(next != rect);
    }
}

void CropTests::cropTranslatesWithoutResamplingAndUndoRestoresBounds()
{
    // Swift's fixture: 64 by 32, its left half red.
    QTemporaryDir folder;
    QImage fixture(64, 32, QImage::Format_RGBA8888_Premultiplied);
    fixture.fill(Qt::transparent);
    QPainter(&fixture).fillRect(0, 0, 32, 32, Qt::red);
    QVERIFY(fixture.save(folder.filePath("fixture.png"), "PNG"));
    EditorSession session;
    bool imported = false;
    session.importImages({QUrl::fromLocalFile(folder.filePath("fixture.png"))}, std::nullopt, [&] { imported = true; });
    QTRY_VERIFY(imported);
    const CanvasDocument before = session.document().value();
    const ImageIdentity image = before.layers.front().asset.value().identity();
    session.selectTool(NavigationTool::crop);
    session.setCropRect(QRectF(8, 4, 32, 16));
    QCOMPARE(committed(session), QRectF(0, 0, 32, 16));
    QCOMPARE(session.cropRect(), std::nullopt);
    QCOMPARE(session.visibleCropRect(), std::optional(QRectF(0, 0, 32, 16)));
    QCOMPARE(session.document().value().layers.front().origin(), QPointF(-8, -4));
    QVERIFY(session.document().value().layers.front().asset.value().identity() == image);
    QCOMPARE(session.history.undoName(), QString("Crop"));
    session.undo();
    QVERIFY(session.document() == before);
    session.redo();
    QCOMPARE(session.document().value().width, 32);
}

void CropTests::sameSizeOffsetCropAndExpansionUseExactBounds()
{
    EditorSession session;
    session.createDocument(100, 50);
    session.addBlankLayer();
    session.selectTool(NavigationTool::crop);
    session.setCropRect(QRectF(-20, 10, 100, 50));
    committed(session);
    QCOMPARE(session.document().value().layers.front().origin(), QPointF(20, -10));
    session.setCropRect(QRectF(-10, -10, 140, 80));
    QCOMPARE(committed(session), QRectF(0, 0, 140, 80));
    QCOMPARE(session.document().value().layers.front().origin(), QPointF(30, 0));
    const QImage output = QImage::fromData(ImageExporter::pngData(session.projectSnapshot().value()), "PNG");
    QCOMPARE(output.size(), QSize(140, 80));
    QCOMPARE(output.pixelColor(0, 0).alpha(), 0);
}

void CropTests::cancellationAndViewportMappingDoNotEditDocument()
{
    EditorSession session;
    session.createDocument(1000, 500);
    const std::optional<CanvasDocument> original = session.document();
    session.selectTool(NavigationTool::crop);
    QCOMPARE(session.cropRect(), std::optional(QRectF(0, 0, 1000, 500)));
    QVERIFY(session.document() == original);
    session.setCropRect(QRectF(25, 20, 200, 100));
    session.cancelCrop();
    QCOMPARE(session.visibleCropRect(), std::optional(QRectF(0, 0, 1000, 500)));
    QVERIFY(session.document() == original);
    session.setCropRect(QRectF(0, 0, 200, 100));
    session.selectTool(NavigationTool::move);
    QVERIFY(!session.cropRect() && session.document() == original);
    QCOMPARE(session.visibleCropRect(), std::nullopt);
    for (const double scale : {1.0, 2.0}) {
        session.viewport.resize(QSizeF(800, 600), scale, original.value().size());
        session.viewport.translate(QSizeF(37, -19));
        const QPointF point(-20, 135);
        const QPointF result = session.viewport.documentPoint(session.viewport.viewPoint(point, original.value().size()), original.value().size());
        QVERIFY(std::abs(result.x() - point.x()) < 0.001 && std::abs(result.y() - point.y()) < 0.001);
    }
}

void CropTests::cropEdgesSnapToNearbyEdges()
{
    const CropSnap snap{{0, 200, 50, 150}, {0, 100, 20, 80}, 6};
    const QRectF rect(10, 10, 60, 40);
    // A move lands each axis's nearest edge; its size stays.
    const CropDrag move{QPointF(30, 30), rect, {CropDrag::Kind::move}};
    const QPointF movedTo(26, 34);
    QCOMPARE(snap.apply(move.updated(movedTo, std::nullopt), move, movedTo, std::nullopt), QRectF(0, 20, 60, 40));
    // The bottom right corner snaps those two edges alone.
    const CropDrag resize{QPointF(70, 50), rect, {CropDrag::Kind::resize, 4}};
    QCOMPARE(LayerTransform::handles[4], QPointF(1, 1));
    const QPointF near(146, 83);
    QCOMPARE(snap.apply(resize.updated(near, std::nullopt), resize, near, std::nullopt), QRectF(10, 10, 140, 70));
    const QPointF far(120, 60);
    QCOMPARE(snap.apply(resize.updated(far, std::nullopt), resize, far, std::nullopt), QRectF(10, 10, 110, 50));
    // A fixed ratio snaps moves alone, so it stays exact.
    const QRectF ratioRect(10, 10, 138, 69);
    QCOMPARE(snap.apply(ratioRect, resize, QPointF(148, 79), 2), ratioRect);
    // A new frame snaps its dragged corner, never its anchor.
    const CropDrag create{QPointF(52, 18), QRectF(), {CropDrag::Kind::create}};
    const QPointF dragged(147, 77);
    QCOMPARE(snap.apply(create.updated(dragged, std::nullopt), create, dragged, std::nullopt), QRectF(52, 18, 98, 62));
}

void CropTests::snapTargetsAreTheCanvasAndLayerBounds()
{
    EditorSession session;
    session.createDocument(400, 300);
    session.addBlankLayer();
    session.insert(filled(100, 60, qRgba(255, 0, 0, 255), "Red"));
    const SnapGuides targets = session.cropSnapTargets();
    QCOMPARE(std::set<double>(targets.xs.begin(), targets.xs.end()), (std::set<double>{0, 400, 150, 250}));
    QCOMPARE(std::set<double>(targets.ys.begin(), targets.ys.end()), (std::set<double>{0, 300, 120, 180}));
    // The canvas's edges first; a layer without pixels gives none.
    QCOMPARE(targets.xs, (std::vector<double>{0, 400, 150, 250}));
    QCOMPARE(targets.ys, (std::vector<double>{0, 300, 120, 180}));
    // A layer between pixels snaps on whole ones.
    LayerTransform shifted = session.activeLayer().value().transform;
    shifted.origin = QPointF(150.4, 120.6);
    session.beginTransform();
    session.previewTransform(shifted);
    session.commitTransform();
    QCOMPARE(session.cropSnapTargets().xs, (std::vector<double>{0, 400, 150, 250}));
    QCOMPARE(session.cropSnapTargets().ys, (std::vector<double>{0, 300, 121, 181}));
    shifted.origin = QPointF(150, 120);
    session.beginTransform();
    session.previewTransform(shifted);
    session.commitTransform();
    // A previewing blur snaps to its grown box, as shown.
    session.beginFilter(FilterKind::gaussianBlur);
    session.updateFilter(FilterSettings{.radius = 10}, true);
    QTRY_VERIFY(session.displayedTransform(session.activeLayer().value()).origin.x() < 150);
    const LayerTransform grown = session.displayedTransform(session.activeLayer().value());
    QCOMPARE(session.cropSnapTargets().xs, (std::vector<double>{0, 400, std::round(grown.origin.x()), std::round(grown.origin.x() + grown.size.width())}));
    session.cancelFilter();
    // A turned layer gives its upright box; hidden ones none.
    LayerTransform turned = session.activeLayer().value().transform;
    turned.rotation = 90;
    session.beginTransform();
    session.previewTransform(turned);
    session.commitTransform();
    const SnapGuides upright = session.cropSnapTargets();
    QCOMPARE(std::set<double>(upright.xs.begin(), upright.xs.end()), (std::set<double>{0, 400, 170, 230}));
    QCOMPARE(std::set<double>(upright.ys.begin(), upright.ys.end()), (std::set<double>{0, 300, 100, 200}));
    session.toggleLayerVisibility(session.activeLayerID().value());
    QCOMPARE(session.cropSnapTargets().xs, (std::vector<double>{0, 400}));
    QCOMPARE(EditorSession().cropSnapTargets().xs, std::vector<double>());
}

void CropTests::aSymmetricDragKeepsItsMiddle()
{
    // Alt grows a new frame out from the press.
    QCOMPARE(CropGeometry::create(QPointF(50, 50), QPointF(60, 45), std::nullopt, true), QRectF(40, 45, 20, 10));
    QCOMPARE(CropGeometry::create(QPointF(50, 50), QPointF(60, 45), 1, true), QRectF(40, 40, 20, 20));
    QCOMPARE((CropDrag{QPointF(50, 50), QRectF(), {CropDrag::Kind::create}}.updated(QPointF(60, 45), std::nullopt, true)), QRectF(40, 45, 20, 10));
    // A resize keeps the frame's middle where it was.
    const QRectF rect(10, 10, 40, 20);
    const CropDrag right{QPointF(50, 20), rect, {CropDrag::Kind::resize, 3}};
    QCOMPARE(right.updated(QPointF(60, 20), std::nullopt, true), QRectF(0, 10, 60, 20));
    // A snapped edge sets the half; the other mirrors it.
    const CropSnap snap{{63}, {}, 4};
    QCOMPARE(snap.apply(QRectF(0, 10, 60, 20), right, QPointF(60, 20), std::nullopt, true), QRectF(-3, 10, 66, 20));
    const CropDrag create{QPointF(50, 50), QRectF(), {CropDrag::Kind::create}};
    QCOMPARE(snap.apply(QRectF(40, 45, 20, 10), create, QPointF(60, 45), std::nullopt, true), QRectF(37, 45, 26, 10));
    // Left of the middle, the left edge sets the half.
    const CropSnap left{{38}, {}, 4};
    QCOMPARE(left.apply(QRectF(40, 45, 20, 10), create, QPointF(40, 45), std::nullopt, true), QRectF(38, 45, 24, 10));
}

void CropTests::snappingKeepsToTheDraggedSide()
{
    const CropSnap snap{{0, 100}, {0, 100}, 5};
    const QRectF rect(20, 20, 40, 40);
    // An edge's grip snaps its own axis alone.
    const CropDrag top{QPointF(40, 20), rect, {CropDrag::Kind::resize, 1}};
    QCOMPARE(snap.apply(QRectF(3, 3, 40, 57), top, QPointF(10, 3), std::nullopt), QRectF(3, 0, 40, 60));
    const CropDrag left{QPointF(20, 40), rect, {CropDrag::Kind::resize, 7}};
    QCOMPARE(snap.apply(QRectF(3, 3, 57, 40), left, QPointF(3, 10), std::nullopt), QRectF(0, 3, 60, 40));
    // The pointer's edge snaps, but never past the far edge.
    const CropDrag create{QPointF(98, 20), QRectF(), {CropDrag::Kind::create}};
    QCOMPARE(CropSnap({{0}, {}, 200}).apply(QRectF(90, 20, 8, 8), create, QPointF(90, 28), std::nullopt), QRectF(0, 20, 98, 8));
    QCOMPARE(CropSnap({{0, 100}, {}, 200}).apply(QRectF(90, 20, 8, 8), create, QPointF(90, 28), std::nullopt), QRectF(90, 20, 8, 8));
    QCOMPARE(CropSnap({{0, 100}, {}, 200}).apply(QRectF(90, 20, 8, 8), create, QPointF(98, 28), std::nullopt), QRectF(90, 20, 10, 8));
    QCOMPARE(CropSnap({{80}, {}, 200}).apply(QRectF(90, 20, 8, 8), create, QPointF(98, 28), std::nullopt), QRectF(90, 20, 8, 8));
    QCOMPARE(CropSnap({{}, {0}, 200}).apply(QRectF(90, 20, 8, 8), create, QPointF(98, 20), std::nullopt), QRectF(90, 0, 8, 28));
    QCOMPARE(CropSnap({{}, {0, 30}, 200}).apply(QRectF(90, 20, 8, 8), create, QPointF(98, 20), std::nullopt), QRectF(90, 20, 8, 8));
    QCOMPARE(CropSnap({{}, {100}, 200}).apply(QRectF(90, 20, 8, 8), create, QPointF(98, 28), std::nullopt), QRectF(90, 20, 8, 80));
    QCOMPARE(CropSnap({{}, {10}, 200}).apply(QRectF(90, 20, 8, 8), create, QPointF(98, 28), std::nullopt), QRectF(90, 20, 8, 8));
    // Two targets as near: the first holds. Zero tolerance: none.
    const CropSnap tie{{8, 12}, {}, 5};
    QCOMPARE(tie.apply(QRectF(10, 0, 20, 20), create, QPointF(10, 0), std::nullopt), QRectF(8, 0, 22, 20));
    const CropSnap none{{0, 100}, {0, 100}, 0};
    QCOMPARE(none.apply(QRectF(3, 3, 40, 40), create, QPointF(3, 3), std::nullopt), QRectF(3, 3, 40, 40));
    const CropDrag centred{QPointF(50, 50), QRectF(), {CropDrag::Kind::create}};
    QCOMPARE(CropSnap({{0}, {}, 0}).apply(QRectF(40, 45, 23, 10), centred, QPointF(63, 50), std::nullopt, true), QRectF(40, 45, 23, 10));
    // Far-edge targets never snap; halfway, the near side wins.
    QCOMPARE(CropSnap({{98}, {}, 200}).apply(QRectF(90, 20, 8, 8), create, QPointF(90, 28), std::nullopt), QRectF(90, 20, 8, 8));
    QCOMPARE(CropSnap({{90}, {}, 200}).apply(QRectF(90, 20, 8, 8), create, QPointF(98, 28), std::nullopt), QRectF(90, 20, 8, 8));
    QCOMPARE(CropSnap({{}, {28}, 200}).apply(QRectF(90, 20, 8, 8), create, QPointF(98, 20), std::nullopt), QRectF(90, 20, 8, 8));
    QCOMPARE(CropSnap({{}, {20}, 200}).apply(QRectF(90, 20, 8, 8), create, QPointF(98, 28), std::nullopt), QRectF(90, 20, 8, 8));
    QCOMPARE(CropSnap({{-1, 12}, {}, 3}).apply(QRectF(0, 0, 10, 10), create, QPointF(5, 20), std::nullopt), QRectF(-1, 0, 11, 10));
    QCOMPARE(CropSnap({{}, {-1, 12}, 3}).apply(QRectF(0, 0, 10, 10), create, QPointF(20, 5), std::nullopt), QRectF(0, -1, 10, 11));
    // A new frame with a ratio stays exact too.
    QCOMPARE(CropSnap({{150}, {}, 6}).apply(QRectF(52, 18, 95, 95), create, QPointF(147, 113), 1), QRectF(52, 18, 95, 95));
    // Mirrored on the pointer's side; at the middle, the right.
    QCOMPARE(CropSnap({{}, {}, 1}).apply(QRectF(50, 40, 1, 20), centred, QPointF(50, 60), std::nullopt, true), QRectF(49, 40, 2, 20));
    QCOMPARE(CropSnap({{}, {}, 1}).apply(QRectF(50, 40, 1, 20), centred, QPointF(49, 60), std::nullopt, true), QRectF(50, 40, 1, 20));
    QCOMPARE(CropSnap({{}, {}, 1}).apply(QRectF(40, 50, 20, 1), centred, QPointF(60, 50), std::nullopt, true), QRectF(40, 49, 20, 2));
    QCOMPARE(CropSnap({{}, {}, 1}).apply(QRectF(40, 50, 20, 1), centred, QPointF(60, 49), std::nullopt, true), QRectF(40, 50, 20, 1));
    // A move takes the smaller shift of its two edges.
    const CropDrag move{QPointF(0, 0), rect, {CropDrag::Kind::move}};
    QCOMPARE(CropSnap({{0, 100}, {}, 10}).apply(QRectF(4, 0, 94, 10), move, QPointF(), std::nullopt), QRectF(6, 0, 94, 10));
    QCOMPARE(CropSnap({{}, {0, 100}, 10}).apply(QRectF(0, 4, 10, 94), move, QPointF(), std::nullopt), QRectF(0, 6, 10, 94));
    // Two edges as near: the first edge's shift holds.
    QCOMPARE(CropSnap({{0, 100}, {}, 5}).apply(QRectF(2, 0, 96, 10), move, QPointF(), std::nullopt), QRectF(0, 0, 96, 10));
}

void CropTests::framesStayOnWholePixelsWithinBounds()
{
    QCOMPARE(CropGeometry::snapped(QRectF(10.6, 10.6, -4.8, 0.1)), QRectF(6, 11, 5, 1));
    QCOMPARE(CropGeometry::snapped(QRectF(5.6, 5, 0.2, 5)), QRectF(6, 5, 1, 5));
    const CropDrag corner{QPointF(10, 10), QRectF(0, 0, 10, 10), {CropDrag::Kind::resize, 4}};
    QCOMPARE(corner.updated(QPointF(15.4, 15.6), std::nullopt), QRectF(0, 0, 15, 16));
    QVERIFY(CropGeometry::valid(QRectF(-1'000'000, 1'000'000, 30'000, 1)));
    QVERIFY(CropGeometry::valid(QRectF(0, 0, 1, 30'000)));
    QVERIFY(!CropGeometry::valid(QRectF(0, 0, 30'001, 1)));
    QVERIFY(!CropGeometry::valid(QRectF(0, 0, 1, 30'001)));
    QVERIFY(!CropGeometry::valid(QRectF(0, 0, 0.5, 1)));
    QVERIFY(!CropGeometry::valid(QRectF(0, 0, 1, 0.5)));
    QVERIFY(!CropGeometry::valid(QRectF(-1'000'001, 0, 1, 1)));
    QVERIFY(!CropGeometry::valid(QRectF(0, 1'000'001, 1, 1)));
    QVERIFY(!CropGeometry::valid(QRectF(qQNaN(), 0, 1, 1)));
    QVERIFY(!CropGeometry::valid(QRectF(0, 0, qInf(), 1)));
    QVERIFY(CropGeometry::valid(QRectF(10, 10, -5, -5)));
    // Either way round, a ratio keeps the drag's direction.
    QCOMPARE(CropGeometry::create(QPointF(100, 100), QPointF(20, 90), 2), QRectF(20, 60, 80, 40));
    QCOMPARE(CropGeometry::create(QPointF(100, 100), QPointF(110, 20), 2), QRectF(100, 20, 160, 80));
    QCOMPARE(CropGeometry::create(QPointF(0, 0), QPointF(30, 20), 2), QRectF(0, 0, 40, 20));
}

void CropTests::ratiosReshapeTheFrameAboutItsMiddle()
{
    EditorSession session;
    session.createDocument(300, 200);
    QCOMPARE(session.cropRatio(), std::nullopt);
    session.selectTool(NavigationTool::crop);
    session.setCropRect(QRectF(10, 20, 120, 60));
    const std::pair<const char *, std::optional<double>> ratios[] = {
        {"Original", 1.5}, {"1:1", 1.0}, {"4:3", 4.0 / 3}, {"16:9", 16.0 / 9}, {"Free", std::nullopt}};
    for (const auto &[choice, ratio] : ratios) {
        session.setCropRatioChoice(QString::fromLatin1(choice));
        QCOMPARE(session.cropRatio(), ratio);
    }
    session.setCropRatioChoice("1:1");
    session.changeCropRatio();
    QCOMPARE(session.cropRect(), std::optional(QRectF(10, -10, 120, 120)));
    session.setCropRatioChoice("16:9");
    session.changeCropRatio();
    QCOMPARE(session.cropRect(), std::optional(QRectF(10, 16, 120, 68)));
    // Free changes nothing; a frame past the bounds is refused.
    session.setCropRatioChoice("Free");
    session.changeCropRatio();
    QCOMPARE(session.cropRect(), std::optional(QRectF(10, 16, 120, 68)));
    EditorSession tall;
    tall.createDocument(1, 30'000);
    tall.selectTool(NavigationTool::crop);
    tall.setCropRect(QRectF(0, 0, 2, 2));
    tall.setCropRatioChoice("Original");
    tall.changeCropRatio();
    QCOMPARE(tall.cropRect(), std::optional(QRectF(0, 0, 2, 2)));
    // Without the tool there is no frame to reshape.
    EditorSession idle;
    idle.createDocument(10, 10);
    idle.setCropRatioChoice("1:1");
    idle.changeCropRatio();
    QCOMPARE(idle.cropRect(), std::nullopt);
    QCOMPARE(EditorSession().cropRatio(), std::nullopt);
    EditorSession empty;
    empty.setCropRatioChoice("Original");
    QCOMPARE(empty.cropRatio(), std::nullopt);
    empty.selectTool(NavigationTool::crop);
    QCOMPARE(empty.cropRect(), std::nullopt);
    QCOMPARE(empty.visibleCropRect(), std::nullopt);
}

QTEST_GUILESS_MAIN(CropTests)
#include "CropTests.moc"
