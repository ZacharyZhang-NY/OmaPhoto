#include "Document/BrushStroke.h"
#include "Rendering/RasterSnapshot.h"
#include "IO/ImageExporter.h"
#include "SelectionFixtures.h"
#include <QRegularExpression>

// Selected pixels moved by a Ctrl-drag or a Ctrl-arrow.
namespace {
// Swift's twoColorLayer: red on the left half, blue right.
std::unique_ptr<EditorSession> makeSession()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(100, 40, true);
    QImage image = BrushRaster::context(100, 40, false);
    QPainter painter(&image);
    painter.fillRect(QRect(0, 0, 50, 40), Qt::red);
    painter.fillRect(QRect(50, 0, 50, 40), Qt::blue);
    painter.end();
    session->insert(ImportedImage(image, image, "Two"));
    session->deleteLayer(session->document().value().layers[0].id);
    return session;
}

void select(EditorSession &session, const QRectF &rect)
{
    session.applySelection(rectPath(rect), SelectionMode::replace, "Select");
}

std::vector<int> pixel(const QImage &image, int x, int y)
{
    const QImage bytes = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    const uchar *at = bytes.constScanLine(y) + x * 4;
    return {at[0], at[1], at[2], at[3]};
}

QImage render(const EditorSession &session)
{
    return ImageExporter::render(session.projectSnapshot().value()).image;
}

bool finished(EditorSession &session)
{
    bool done = false;
    session.finishPixelMove([&] { done = true; });
    return QTest::qWaitFor([&] { return done; }, 10000);
}
}

class PixelMoveTests : public QObject {
    Q_OBJECT
private slots:
    void cmdDragMovesSelectedPixelsAndOutlineAsOneUndo();
    void duplicatePixelDragPreservesSourceAndUndoesTogether();
    void cmdArrowNudgesPixelsAndMasksRefuse();
    void pixelMoveNeverShowsTheOutlineAtItsOldSpot();
    void aPixelMoveHoldsTheSessionAndAFailedMoveCancels();
    void aMoveAcrossTilesLeavesNoGhost();
    void aScaledLayerMovesInItsOwnPixels();
    void aFractionalLandingIsResampled();
    void aMoveNeedsVisiblePixelsAndEndsAnOpacityDrag();
    void aPaintedLayerMovesWithoutFlattening();
};

void PixelMoveTests::cmdDragMovesSelectedPixelsAndOutlineAsOneUndo()
{
    const auto session = makeSession();
    select(*session, QRectF(10, 10, 10, 10));
    const int count = session->history.undoCount();
    QVERIFY(session->beginPixelMove());
    session->movePixels(QSizeF(30.4, 0));
    session->movePixels(QSizeF(60.2, 5));
    QVERIFY(finished(*session));
    QCOMPARE(session->history.undoCount(), count + 1);
    QCOMPARE(session->history.undoName(), QString("Move Pixels"));
    QCOMPARE(session->selection().value().path.boundingRect(), QRectF(70, 15, 10, 10));
    const QImage result = render(*session);
    QCOMPARE(pixel(result, 15, 15)[3], 0);
    QCOMPARE(pixel(result, 75, 20), (std::vector<int>{255, 0, 0, 255}));
    QCOMPARE(pixel(result, 85, 20), (std::vector<int>{0, 0, 255, 255}));
    QCOMPARE(pixel(result, 5, 5), (std::vector<int>{255, 0, 0, 255}));
    session->undo();
    QCOMPARE(session->selection().value().path.boundingRect(), QRectF(10, 10, 10, 10));
    QCOMPARE(pixel(render(*session), 15, 15), (std::vector<int>{255, 0, 0, 255}));
}

void PixelMoveTests::duplicatePixelDragPreservesSourceAndUndoesTogether()
{
    const auto session = makeSession();
    select(*session, QRectF(10, 10, 10, 10));
    const int count = session->history.undoCount();
    QVERIFY(session->beginPixelMove(true));
    session->movePixels(QSizeF(60, 5));
    QVERIFY(finished(*session));
    const QImage result = render(*session);
    QVERIFY(pixel(result, 15, 15)[0] > 250);
    QVERIFY(pixel(result, 75, 20)[0] > 250);
    QCOMPARE(session->history.undoCount(), count + 1);
    QCOMPARE(session->history.undoName(), QString("Duplicate Pixels"));
    session->undo();
    const QImage restored = render(*session);
    QVERIFY(pixel(restored, 15, 15)[0] > 250);
    QVERIFY(pixel(restored, 75, 20)[2] > 250);
}

void PixelMoveTests::cmdArrowNudgesPixelsAndMasksRefuse()
{
    const auto session = makeSession();
    // The last red column band moves ten to the right.
    select(*session, QRectF(40, 0, 10, 40));
    bool done = false;
    session->nudgePixels(10, 0, [&] { done = true; });
    QTRY_VERIFY(done);
    const QImage result = render(*session);
    QCOMPARE(pixel(result, 45, 20)[3], 0);
    QCOMPARE(pixel(result, 55, 20), (std::vector<int>{255, 0, 0, 255}));
    session->addLayerMask();
    QVERIFY(session->isMaskSelected() && !session->beginPixelMove());
    session->cancelPixelMove();
    // Refused, a nudge still calls back and records nothing.
    const int count = session->history.undoCount();
    done = false;
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("no selected pixels to nudge")));
    session->nudgePixels(1, 0, [&] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(session->history.undoCount(), count);
}

void PixelMoveTests::pixelMoveNeverShowsTheOutlineAtItsOldSpot()
{
    const auto session = makeSession();
    select(*session, QRectF(10, 10, 10, 10));
    QVERIFY(session->beginPixelMove());
    session->movePixels(QSizeF(30, 0));
    const QRectF moved(40, 10, 10, 10);
    QCOMPARE(session->displayedSelection().value().path.boundingRect(), moved);
    bool done = false;
    session->finishPixelMove([&] { done = true; });
    // Mid-commit the outline stays at its new place.
    QVERIFY(session->isProjectBusy() && session->pixelMove());
    QCOMPARE(session->displayedSelection().value().path.boundingRect(), moved);
    QTRY_VERIFY(done);
    QCOMPARE(session->selection().value().path.boundingRect(), moved);
    QVERIFY(!session->pixelMove());
}

void PixelMoveTests::aPixelMoveHoldsTheSessionAndAFailedMoveCancels()
{
    const auto session = makeSession();
    select(*session, QRectF(10, 10, 10, 10));
    QVERIFY(session->canEditLayers() && session->canInvert() && session->canAdjustColors());
    QVERIFY(session->beginPixelMove());
    // Unmoved, the outline stays where it was.
    QCOMPARE(session->displayedSelection().value().path.boundingRect(), QRectF(10, 10, 10, 10));
    QVERIFY(!session->canEditLayers() && !session->canInvert() && !session->canAdjustColors());
    QVERIFY(!session->beginPixelMove());
    // Unmoved, the finish records nothing and frees the session.
    const int count = session->history.undoCount();
    QVERIFY(finished(*session));
    QVERIFY(!session->pixelMove() && session->canEditLayers());
    QCOMPARE(session->history.undoCount(), count);
    // A move past the budget cancels and shows the error.
    QVERIFY(session->beginPixelMove());
    session->pixelMove()->raster->pixelLimit = 1000;
    const int revision = session->brushRevision();
    session->movePixels(QSizeF(60, 0));
    QVERIFY(!session->pixelMove() && session->brushError().has_value());
    QVERIFY(session->brushRevision() > revision);
    QCOMPARE(session->history.undoCount(), count);
    QCOMPARE(pixel(render(*session), 15, 15), (std::vector<int>{255, 0, 0, 255}));
    // A hard outline stays hard, moving and moved.
    session->setSelectionAntialiased(false);
    select(*session, QRectF(10, 10, 10, 10));
    QVERIFY(session->beginPixelMove());
    session->movePixels(QSizeF(5, 0));
    QVERIFY(!session->displayedSelection().value().antialiased);
    QVERIFY(finished(*session));
    QVERIFY(!session->selection().value().antialiased);
}

void PixelMoveTests::aMoveAcrossTilesLeavesNoGhost()
{
    EditorSession session;
    session.createDocument(600, 40, true);
    QImage image = BrushRaster::context(600, 40, false);
    image.fill(Qt::blue);
    QPainter painter(&image);
    painter.fillRect(QRect(0, 0, 100, 40), Qt::red);
    painter.end();
    session.insert(ImportedImage(image, image, "Wide"));
    const std::vector<int> red{255, 0, 0, 255}, blue{0, 0, 255, 255};
    // One tile over: the hole is cut in the first.
    select(session, QRectF(20, 10, 40, 20));
    QVERIFY(session.beginPixelMove());
    session.movePixels(QSizeF(300, 0));
    QVERIFY(finished(session));
    QCOMPARE(pixel(render(session), 330, 15), red);
    QCOMPARE(pixel(render(session), 30, 15)[3], 0);
    // Out to the third tile and back: no ghost stays.
    QVERIFY(session.beginPixelMove());
    session.movePixels(QSizeF(200, 0));
    session.movePixels(QSizeF(-200, 0));
    QVERIFY(finished(session));
    const QImage result = render(session);
    QCOMPARE(pixel(result, 130, 15), red);
    QCOMPARE(pixel(result, 530, 15), blue);
    QCOMPARE(pixel(result, 330, 15)[3], 0);
}

void PixelMoveTests::aScaledLayerMovesInItsOwnPixels()
{
    EditorSession session;
    session.createDocument(200, 100, true);
    QImage image = BrushRaster::context(50, 20, false);
    image.fill(Qt::blue);
    QPainter painter(&image);
    painter.fillRect(QRect(0, 0, 25, 20), Qt::red);
    painter.end();
    session.insert(ImportedImage(image, image, "Scaled"));
    const QUuid id = session.activeLayerID().value();
    // Doubled past the top left, so the grid starts off-canvas.
    rewrite(session, [&](ProjectSnapshot &snapshot) { record(snapshot, id).transform = LayerTransform{.origin = {-10, -10}, .size = {100, 40}}; });
    session.selectLayer(id);
    select(session, QRectF(0, 0, 20, 20));
    QVERIFY(session.beginPixelMove());
    session.movePixels(QSizeF(60, 0));
    QVERIFY(finished(session));
    QImage result = render(session);
    QCOMPARE(pixel(result, 70, 10), (std::vector<int>{255, 0, 0, 255}));
    QCOMPARE(pixel(result, 70, 25), (std::vector<int>{0, 0, 255, 255}));
    QCOMPARE(pixel(result, 10, 10)[3], 0);
}

void PixelMoveTests::aFractionalLandingIsResampled()
{
    EditorSession session;
    session.createDocument(100, 40, true);
    // Four times as large; red pixels 10..15 by 2..6.
    QImage image = BrushRaster::context(25, 10, false);
    image.fill(Qt::blue);
    QPainter painter(&image);
    painter.fillRect(QRect(10, 2, 5, 4), Qt::red);
    painter.end();
    session.insert(ImportedImage(image, image, "Fourfold"));
    const QUuid id = session.activeLayerID().value();
    rewrite(session, [&](ProjectSnapshot &snapshot) { record(snapshot, id).transform = LayerTransform{.origin = {0, 0}, .size = {100, 40}}; });
    session.selectLayer(id);
    select(session, QRectF(40, 8, 20, 16));
    QVERIFY(session.beginPixelMove());
    // A quarter and half a pixel: weights 1/8 and 3/8.
    session.movePixels(QSizeF(-1, -2));
    QVERIFY(finished(session));
    const ImageLayer layer = layerWith(session, id);
    QCOMPARE(layer.transform, (LayerTransform{.origin = {0, 0}, .size = {100, 40}}));
    const QImage moved = layer.asset.value().image();
    QCOMPARE(pixel(moved, 9, 1), (std::vector<int>{32, 0, 223, 255}));
    QCOMPARE(pixel(moved, 10, 1), (std::vector<int>{128, 0, 127, 255}));
    QCOMPARE(pixel(moved, 9, 2), (std::vector<int>{64, 0, 191, 255}));
    QCOMPARE(pixel(moved, 10, 2), (std::vector<int>{255, 0, 0, 255}));
}

void PixelMoveTests::aMoveNeedsVisiblePixelsAndEndsAnOpacityDrag()
{
    const auto session = makeSession();
    const QUuid id = session->activeLayerID().value();
    select(*session, QRectF(10, 10, 10, 10));
    // Hidden, the layer takes no move.
    session->toggleLayerVisibility(id);
    QVERIFY(!session->beginPixelMove());
    session->toggleLayerVisibility(id);
    // A small layer the selection misses lifts nothing.
    QImage dot = BrushRaster::context(10, 10, false);
    dot.fill(Qt::red);
    session->insert(ImportedImage(dot, dot, "Dot"), QPointF(85, 5));
    select(*session, QRectF(10, 20, 10, 10));
    QVERIFY(!session->beginPixelMove() && !session->pixelMove());
    QVERIFY(!session->brushError().has_value());
    // An opacity drag closes as its own step first.
    select(*session, QRectF(82, 2, 4, 4));
    const int count = session->history.undoCount();
    session->beginOpacityEdit();
    session->setLayerOpacity(0.5);
    QVERIFY(session->beginPixelMove());
    QCOMPARE(session->history.undoCount(), count + 1);
    QCOMPARE(session->history.undoName(), QString("Layer Opacity"));
    // Every step moves the canvas's counter.
    const int revision = session->brushRevision();
    session->movePixels(QSizeF(1, 0));
    QCOMPARE(session->brushRevision(), revision + 1);
    // A second finish while the first commits returns at once.
    bool first = false, second = false;
    session->finishPixelMove([&] { first = true; });
    QVERIFY(session->isProjectBusy());
    session->finishPixelMove([&] { second = true; });
    QTRY_VERIFY(first && second);
    QCOMPARE(session->history.undoCount(), count + 2);
    QCOMPARE(session->history.undoName(), QString("Move Pixels"));
}

void PixelMoveTests::aPaintedLayerMovesWithoutFlattening()
{
    const auto session = makeSession();
    const QUuid id = session->activeLayerID().value();
    // A white dab makes the layer a painted raster.
    session->selectTool(NavigationTool::brush);
    session->setBrushSettings(BrushSettings{.diameter = 6, .hardness = 1, .red = 1, .green = 1, .blue = 1});
    session->beginBrush(QPointF(80, 30));
    QVERIFY(session->finishBrushImmediately());
    const ImportedImage painted = layerWith(*session, id).asset.value();
    QVERIFY(painted.raster && !painted.raster->hasMaterializedPixels());
    // Lift, move and commit read tiles, never the whole raster.
    select(*session, QRectF(10, 10, 10, 10));
    QVERIFY(session->beginPixelMove());
    QVERIFY(!painted.raster->hasMaterializedPixels());
    session->movePixels(QSizeF(60, 0));
    QVERIFY(finished(*session));
    QVERIFY(!painted.raster->hasMaterializedPixels());
    QCOMPARE(session->history.undoName(), QString("Move Pixels"));
    // Red lands in the blue half: only moved pixels show.
    const QImage result = render(*session);
    QCOMPARE(pixel(result, 75, 15), (std::vector<int>{255, 0, 0, 255}));
    QCOMPARE(pixel(result, 15, 15)[3], 0);
    QCOMPARE(pixel(result, 80, 30), (std::vector<int>{255, 255, 255, 255}));
}

QTEST_GUILESS_MAIN(PixelMoveTests)
#include "PixelMoveTests.moc"
