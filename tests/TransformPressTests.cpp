#include "MovePressFixtures.h"

// The Move tool on the canvas: drags, handles, snapping.
class TransformPressTests : public QObject {
    Q_OBJECT
private slots:
    void draggingOutsideTheLayerMovesIt();
    void altDraggingOutsideTheLayerDuplicatesIt();
    void handlesResizeAndTheGripRotates();
    void movesSnapToTheCanvasAndOtherLayersUnlessCtrl();
    void autoSelectCanBeDisabledAndCtrlClickOverridesIt();
    void autoSelectPicksTheForegroundOverASelectedBackground();
    void escapeMidDragRestoresAndAPersistentEditWaitsForReturn();
    void losingTheKeysOrTheWheelMidDrag();
    void aSelectionDragsAsOneBox();
    void ctrlFlipsAutoSelectWhileHeld_data();
    void ctrlFlipsAutoSelectWhileHeld();
    void ctrlShiftClickAddsALayerWithAutoSelectOn();
    void aCanvasDragAppliesTheFieldsEditFirst();
};

void TransformPressTests::draggingOutsideTheLayerMovesIt()
{
    Canvas shown;
    QCOMPARE(shown.origin(), QPointF(150, 100));
    // Thirteen down keeps the middle clear of the canvas's snap.
    shown.drag(QPointF(20, 20), QPointF(40, 33));
    QVERIFY(!shown.session.transformEdit().has_value());
    QCOMPARE(shown.origin(), QPointF(170, 113));
    QCOMPARE(shown.session.history.undoName(), QString("Transform Layer"));
    // A press that never moves changes nothing and records nothing.
    const int steps = shown.session.history.undoCount();
    shown.click(QPointF(20, 20));
    QCOMPARE(shown.session.history.undoCount(), steps);
    QCOMPARE(shown.origin(), QPointF(170, 113));
    // At 200% drags land on whole pixels; snapping halves.
    shown.session.zoom(2);
    shown.canvas->synchronizeDisplay();
    shown.drag(QPointF(20, 20), QPointF(66, 20));
    QCOMPARE(shown.origin(), QPointF(193, 113));
    shown.drag(QPointF(20, 20), QPointF(21, 20));
    QCOMPARE(shown.origin(), QPointF(194, 113));
    shown.session.zoom(1);
    shown.canvas->synchronizeDisplay();
    // Nothing drags while the project is busy.
    shown.session.setIsProjectBusy(true);
    shown.drag(QPointF(20, 20), QPointF(60, 20));
    QCOMPARE(shown.origin(), QPointF(194, 113));
}

void TransformPressTests::altDraggingOutsideTheLayerDuplicatesIt()
{
    Canvas shown;
    shown.drag(QPointF(20, 20), QPointF(40, 33), Qt::AltModifier);
    const std::vector<ImageLayer> &layers = shown.session.document().value().layers;
    QCOMPARE(int(layers.size()), 2);
    QCOMPARE(layers[0].transform.origin, QPointF(150, 100));
    QCOMPARE(layers[1].transform.origin, QPointF(170, 113));
    QCOMPARE(layers[1].name, QString("Red copy"));
    QVERIFY(!shown.session.transformEdit().has_value());
    QCOMPARE(shown.session.history.undoName(), QString("Duplicate Layer"));
    shown.session.undo();
    QCOMPARE(int(shown.session.document().value().layers.size()), 1);
    // Alt on a handle resizes about the middle, no copy.
    shown.drag(QPointF(250, 200), QPointF(300, 250), Qt::AltModifier);
    QCOMPARE(int(shown.session.document().value().layers.size()), 1);
    QCOMPARE(layerWith(shown.session, shown.red).transform, (LayerTransform{.origin = {100, 50}, .size = {200, 200}}));
}

void TransformPressTests::handlesResizeAndTheGripRotates()
{
    Canvas shown;
    // Shift frees a locked corner: each side follows the pointer.
    shown.drag(QPointF(250, 200), QPointF(300, 225), Qt::ShiftModifier);
    QCOMPARE(layerWith(shown.session, shown.red).transform, (LayerTransform{.origin = {150, 100}, .size = {150, 125}}));
    shown.session.undo();
    // The bottom right corner dragged out: both sides grow, locked.
    shown.drag(QPointF(250, 200), QPointF(300, 250));
    LayerTransform transform = layerWith(shown.session, shown.red).transform;
    QCOMPARE(transform.size, QSizeF(150, 150));
    QCOMPARE(transform.origin, QPointF(150, 100));
    shown.session.setLocksTransformRatio(false);
    shown.drag(QPointF(300, 250), QPointF(300, 200));
    transform = layerWith(shown.session, shown.red).transform;
    QCOMPARE(transform.size, QSizeF(150, 100));
    // An edge, away from its handle, resizes too.
    shown.drag(QPointF(300, 170), QPointF(320, 170));
    QCOMPARE(layerWith(shown.session, shown.red).transform.size, QSizeF(170, 100));
    // The dragged edge snaps: five past the middle lands there.
    shown.drag(QPointF(320, 170), QPointF(205, 170));
    QCOMPARE(layerWith(shown.session, shown.red).transform, (LayerTransform{.origin = {150, 100}, .size = {50, 100}}));
    // The grip: a quarter turn about the middle.
    const QPointF center = layerWith(shown.session, shown.red).transform.center();
    const QPointF grip = center + QPointF(0, -78);
    QVERIFY(center == QPointF(175, 150));
    shown.drag(grip, center + QPointF(78, 0));
    transform = layerWith(shown.session, shown.red).transform;
    QCOMPARE(transform.rotation, 90.0);
    QCOMPARE(transform.center(), center);
    QCOMPARE(shown.session.history.undoName(), QString("Transform Layer"));
}

void TransformPressTests::movesSnapToTheCanvasAndOtherLayersUnlessCtrl()
{
    Canvas shown;
    EditorSession &session = shown.session;
    // Three short of the middle, the left edge snaps.
    shown.press(QPointF(20, 20));
    shown.move(QPointF(73, 20));
    QCOMPARE(session.transformEdit().value().draft.origin, QPointF(200, 100));
    QCOMPARE(session.snapGuides, (SnapGuides{{200}, {150}}));
    shown.release(QPointF(73, 20));
    QCOMPARE(shown.origin(), QPointF(200, 100));
    QCOMPARE(session.snapGuides, SnapGuides{});
    // Ctrl drags freely.
    shown.press(QPointF(20, 20));
    shown.move(QPointF(23, 20), Qt::ControlModifier);
    QCOMPARE(session.transformEdit().value().draft.origin, QPointF(203, 100));
    QCOMPARE(session.snapGuides, SnapGuides{});
    shown.release(QPointF(23, 20), Qt::ControlModifier);
    QCOMPARE(shown.origin(), QPointF(203, 100));
    // Another layer's edge is the nearest target now.
    session.insert(filled(50, 50, qRgba(0, 0, 255, 255), "Blue"), QPointF(330, 50));
    session.selectLayer(shown.red);
    shown.press(QPointF(20, 20));
    shown.move(QPointF(21, 20));
    QCOMPARE(session.transformEdit().value().draft.origin, QPointF(205, 100));
    QCOMPARE(session.snapGuides, (SnapGuides{{305}, {150}}));
    shown.release(QPointF(21, 20));
    QCOMPARE(shown.origin(), QPointF(205, 100));
}

void TransformPressTests::autoSelectCanBeDisabledAndCtrlClickOverridesIt()
{
    Canvas shown;
    EditorSession &session = shown.session;
    session.insert(filled(100, 100, qRgba(0, 0, 255, 255), "Blue"), QPointF(300, 50));
    const QUuid blue = session.activeLayerID().value();
    session.selectLayer(shown.red);
    QVERIFY(!session.transformAutoSelect());
    shown.click(QPointF(300, 50));
    QCOMPARE(session.activeLayerID(), std::optional(shown.red));
    session.setTransformAutoSelect(true);
    shown.click(QPointF(300, 50));
    QCOMPARE(session.activeLayerID(), std::optional(blue));
    session.selectLayer(shown.red);
    session.setTransformAutoSelect(false);
    shown.click(QPointF(300, 50));
    QCOMPARE(session.activeLayerID(), std::optional(shown.red));
    shown.click(QPointF(300, 50), Qt::ControlModifier);
    QCOMPARE(session.activeLayerID(), std::optional(blue));
    QVERIFY(!session.transformAutoSelect());
    // Ctrl+Shift adds the layer under the pointer to the selection.
    shown.click(QPointF(200, 150), Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(session.selectedLayerIDs(), (QSet<QUuid>{shown.red, blue}));
    // With auto-select, empty canvas still drags the active layer.
    session.selectLayer(shown.red);
    session.setTransformAutoSelect(true);
    shown.drag(QPointF(20, 250), QPointF(33, 250));
    QCOMPARE(shown.origin(), QPointF(163, 100));
    // Inside the active layer's box a layer above wins, auto-selecting.
    session.insert(filled(100, 100, qRgba(0, 255, 0, 255), "Green"), QPointF(220, 120));
    const QUuid green = session.activeLayerID().value();
    session.selectLayer(shown.red);
    shown.click(QPointF(213, 150));
    QCOMPARE(session.activeLayerID(), std::optional(green));
    session.selectLayer(shown.red);
    session.setTransformAutoSelect(false);
    shown.click(QPointF(213, 150));
    QCOMPARE(session.activeLayerID(), std::optional(shown.red));
    // Ctrl picks what lies under the pointer, active included.
    shown.click(QPointF(213, 150), Qt::ControlModifier);
    QCOMPARE(session.activeLayerID(), std::optional(green));
    session.selectLayer(shown.red);
    session.setTransformAutoSelect(true);
    // A selection's box drags them all; outside it auto-select picks.
    session.insert(filled(100, 100, qRgba(255, 255, 0, 255), "Yellow"), QPointF(60, 260));
    const QUuid yellow = session.activeLayerID().value();
    session.selectLayers({shown.red, blue}, shown.red);
    shown.click(QPointF(300, 50));
    QCOMPARE(session.selectedLayerIDs(), (QSet<QUuid>{shown.red, blue}));
    shown.click(QPointF(60, 260));
    QCOMPARE(session.activeLayerID(), std::optional(yellow));
    QCOMPARE(session.selectedLayerIDs(), QSet<QUuid>{yellow});
}

void TransformPressTests::autoSelectPicksTheForegroundOverASelectedBackground()
{
    Shown shown(QSize(400, 300), QSize(400, 300));
    shown.settle();
    EditorSession &session = shown.session;
    session.zoom(1);
    session.insert(filled(400, 300, qRgba(0, 0, 255, 255), "Sky"));
    const QUuid background = session.activeLayerID().value();
    session.insert(filled(80, 60, qRgba(255, 0, 0, 255), "Kite"), QPointF(190, 80));
    const QUuid foreground = session.activeLayerID().value();
    session.selectTool(NavigationTool::move);
    shown.canvas->synchronizeDisplay();
    const auto click = [&](QPoint at) {
        QTest::mouseClick(shown.canvas, Qt::LeftButton, Qt::NoModifier, at);
        session.commitTransform();
    };
    // Swift's test: the background holds every press; the kite wins.
    session.selectLayer(background);
    session.setTransformAutoSelect(true);
    click(QPoint(180, 70));
    QCOMPARE(session.activeLayerID(), std::optional(foreground));
    click(QPoint(20, 20));
    QCOMPARE(session.activeLayerID(), std::optional(background));
    click(QPoint(180, 70));
    QCOMPARE(session.activeLayerID(), std::optional(foreground));
    session.selectLayer(background);
    session.setTransformAutoSelect(false);
    click(QPoint(180, 70));
    QCOMPARE(session.activeLayerID(), std::optional(background));
    // A Ctrl+T edit keeps its layer; a plain press picks.
    session.setTransformAutoSelect(true);
    session.beginTransform();
    QTest::mouseClick(shown.canvas, Qt::LeftButton, Qt::NoModifier, QPoint(180, 70));
    QCOMPARE(session.activeLayerID(), std::optional(background));
    session.cancelTransform();
    session.setTransformAutoSelect(false);
    // A wide mask box keeps its layer over lower ones.
    session.selectLayer(foreground);
    session.addMask();
    session.toggleMaskLink(foreground);
    session.selectLayerTarget(foreground, true);
    QVERIFY(session.transformTargetsMask());
    session.beginTransform();
    session.previewTransform(LayerTransform(QPointF(40, 20), QSizeF(300, 200)));
    session.commitTransform();
    session.setTransformAutoSelect(true);
    click(QPoint(60, 40));
    QCOMPARE(session.activeLayerID(), std::optional(foreground));
    // Over empty canvas nothing lies under the pointer.
    session.toggleLayerVisibility(background);
    click(QPoint(60, 40));
    QCOMPARE(session.activeLayerID(), std::optional(foreground));
}

void TransformPressTests::escapeMidDragRestoresAndAPersistentEditWaitsForReturn()
{
    Canvas shown;
    EditorSession &session = shown.session;
    QTRY_VERIFY(shown.canvas->hasFocus());
    shown.press(QPointF(20, 20));
    shown.move(QPointF(55, 40));
    QCOMPARE(session.transformEdit().value().draft.origin, QPointF(185, 120));
    QVERIFY(!session.transformEdit().value().persistent);
    QTest::keyClick(shown.canvas, Qt::Key_Escape);
    QVERIFY(!session.transformEdit().has_value());
    shown.release(QPointF(55, 40));
    QCOMPARE(shown.origin(), QPointF(150, 100));
    // Ctrl+T's edit outlives the drag until Return applies it.
    session.beginTransform();
    QVERIFY(session.transformEdit().value().persistent);
    shown.drag(QPointF(20, 20), QPointF(40, 33));
    QVERIFY(session.transformEdit().has_value());
    QCOMPARE(session.transformEdit().value().draft.origin, QPointF(170, 113));
    QCOMPARE(shown.origin(), QPointF(150, 100));
    QTest::keyClick(shown.canvas, Qt::Key_Return);
    QVERIFY(!session.transformEdit().has_value());
    QCOMPARE(shown.origin(), QPointF(170, 113));
    // A snapped drag under Ctrl+T: guides go with the release.
    session.beginTransform();
    shown.press(QPointF(20, 20));
    shown.move(QPointF(53, 20));
    QCOMPARE(session.transformEdit().value().draft.origin, QPointF(200, 113));
    QCOMPARE(session.snapGuides, (SnapGuides{{200}, {}}));
    // The release repaints the guide away, though nothing commits.
    struct PaintSpy : QObject {
        std::vector<QRect> rects;
        bool eventFilter(QObject *, QEvent *event) override
        {
            if (event->type() == QEvent::Paint)
                rects.push_back(static_cast<QPaintEvent *>(event)->region().boundingRect());
            return false;
        }
    } spy;
    QCoreApplication::processEvents();
    shown.canvas->installEventFilter(&spy);
    shown.release(QPointF(53, 20));
    QVERIFY(session.transformEdit().has_value());
    QCOMPARE(session.snapGuides, SnapGuides{});
    QCoreApplication::processEvents();
    shown.canvas->removeEventFilter(&spy);
    QVERIFY(std::any_of(spy.rects.begin(), spy.rects.end(), [&](const QRect &rect) { return rect == shown.canvas->rect(); }));
    // Escape mid-resize puts the layer and the cursor back.
    shown.press(QPointF(300, 213));
    QCOMPARE(shown.canvas->cursor().shape(), Qt::SizeFDiagCursor);
    shown.move(QPointF(350, 263));
    QCOMPARE(session.transformEdit().value().draft.size, QSizeF(150, 150));
    QTest::keyClick(shown.canvas, Qt::Key_Escape);
    QVERIFY(!session.transformEdit().has_value());
    QVERIFY(shown.canvas->cursor().shape() != Qt::SizeFDiagCursor);
    shown.release(QPointF(350, 263));
    QCOMPARE(layerWith(session, shown.red).transform.size, QSizeF(100, 100));
}

void TransformPressTests::losingTheKeysOrTheWheelMidDrag()
{
    Canvas shown;
    EditorSession &session = shown.session;
    shown.press(QPointF(20, 20));
    shown.move(QPointF(60, 40));
    // The wheel neither pans nor zooms mid-drag.
    const CanvasViewport before = session.viewport;
    wheel(*shown.canvas, QPointF(100, 100), QPoint(0, 30), QPoint(0, 120), Qt::NoModifier);
    wheel(*shown.canvas, QPointF(100, 100), QPoint(0, 30), QPoint(0, 120), Qt::ControlModifier);
    QVERIFY(session.viewport == before);
    // Nor does a pinch.
    QNativeGestureEvent pinch(Qt::ZoomNativeGesture, QPointingDevice::primaryPointingDevice(), 2, QPointF(100, 100), QPointF(100, 100),
                              shown.canvas->mapToGlobal(QPoint(100, 100)), 0.5, QPointF());
    QApplication::sendEvent(shown.canvas, &pinch);
    QVERIFY(session.viewport == before);
    // The keys lost mid-drag: the layer goes back.
    shown.canvas->clearFocus();
    QVERIFY(!session.transformEdit().has_value());
    QCOMPARE(shown.origin(), QPointF(150, 100));
    shown.release(QPointF(60, 40));
    QCOMPARE(shown.origin(), QPointF(150, 100));
    wheel(*shown.canvas, QPointF(100, 100), QPoint(0, 30), QPoint(0, 120), Qt::NoModifier);
    QVERIFY(!(session.viewport == before));
    // Under Ctrl+T the edit stays, its draft put back.
    session.viewport = before;
    session.beginTransform();
    shown.press(QPointF(20, 20));
    shown.move(QPointF(55, 40));
    QCOMPARE(session.transformEdit().value().draft.origin, QPointF(185, 120));
    shown.canvas->clearFocus();
    QVERIFY(session.transformEdit().value().persistent);
    QCOMPARE(session.transformEdit().value().draft.origin, QPointF(150, 100));
    shown.release(QPointF(55, 40));
    QVERIFY(session.transformEdit().has_value());
}

void TransformPressTests::aSelectionDragsAsOneBox()
{
    Canvas shown;
    EditorSession &session = shown.session;
    session.insert(filled(50, 50, qRgba(0, 0, 255, 255), "Blue"), QPointF(325, 275));
    const QUuid blue = session.activeLayerID().value();
    session.selectLayers({shown.red, blue}, shown.red);
    shown.drag(QPointF(20, 20), QPointF(30, 32));
    QCOMPARE(shown.origin(), QPointF(160, 112));
    QCOMPARE(layerWith(session, blue).transform.origin, QPointF(310, 262));
    QCOMPARE(session.history.undoName(), QString("Transform Layers"));
    QCOMPARE(session.selectedLayerIDs(), (QSet<QUuid>{shown.red, blue}));
}

// A full-canvas blue layer under the red, and active.
static QUuid blueBelow(Canvas &shown)
{
    shown.session.insert(filled(400, 300, qRgba(0, 0, 255, 255), "Blue"));
    const QUuid blue = shown.session.activeLayerID().value();
    shown.session.reorderLayers({1}, 0);
    if (shown.session.document().value().layers[0].id != blue || shown.session.activeLayerID() != blue)
        throw std::runtime_error("the blue layer is not active at the bottom");
    shown.canvas->synchronizeDisplay();
    return blue;
}

void TransformPressTests::ctrlFlipsAutoSelectWhileHeld_data()
{
    QTest::addColumn<bool>("autoSelect");
    QTest::newRow("off") << false;
    QTest::newRow("on") << true;
}

// Swift's commandFlipsAutoSelectWhileHeld: Ctrl turns it the other way.
void TransformPressTests::ctrlFlipsAutoSelectWhileHeld()
{
    QFETCH(bool, autoSelect);
    Canvas shown;
    const QUuid blue = blueBelow(shown);
    shown.session.setTransformAutoSelect(autoSelect);
    shown.drag(QPointF(210, 170), QPointF(230, 180), Qt::ControlModifier);
    const QUuid moved = autoSelect ? blue : shown.red;
    QCOMPARE(shown.session.activeLayerID(), std::optional(moved));
    QCOMPARE(layerWith(shown.session, moved).transform.origin, autoSelect ? QPointF(20, 10) : QPointF(170, 110));
    QCOMPARE(layerWith(shown.session, autoSelect ? shown.red : blue).transform.origin, autoSelect ? QPointF(150, 100) : QPointF(0, 0));
    // Without Ctrl it picks as set.
    shown.session.undo();
    shown.session.selectLayer(blue);
    shown.drag(QPointF(210, 170), QPointF(230, 180));
    QCOMPARE(shown.session.activeLayerID(), std::optional(autoSelect ? shown.red : blue));
}

void TransformPressTests::ctrlShiftClickAddsALayerWithAutoSelectOn()
{
    Canvas shown;
    const QUuid blue = blueBelow(shown);
    shown.session.setTransformAutoSelect(true);
    shown.click(QPointF(210, 170), Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(shown.session.selectedLayerIDs(), (QSet<QUuid>{shown.red, blue}));
    // Past the active layer's box, Ctrl still keeps it.
    shown.session.insert(filled(50, 50, qRgba(0, 255, 0, 255), "Green"), QPointF(330, 20));
    shown.session.selectLayer(shown.red);
    shown.drag(QPointF(340, 30), QPointF(353, 30), Qt::ControlModifier);
    QCOMPARE(shown.session.activeLayerID(), std::optional(shown.red));
    QCOMPARE(shown.origin(), QPointF(163, 100));
}

// The bar's pending value applies first, its own step.
void TransformPressTests::aCanvasDragAppliesTheFieldsEditFirst()
{
    Canvas shown;
    EditorSession &session = shown.session;
    const int steps = session.history.undoCount();
    session.beginTransform(false, true);
    LayerTransform flipped = session.transformEdit().value().draft;
    flipped.flipX = true;
    session.previewTransform(flipped);
    shown.drag(QPointF(20, 20), QPointF(40, 33));
    QVERIFY(!session.transformEdit().has_value());
    QCOMPARE(session.history.undoCount(), steps + 2);
    QCOMPARE(shown.origin(), QPointF(170, 113));
    QVERIFY(layerWith(session, shown.red).transform.flipX);
    session.undo();
    QCOMPARE(shown.origin(), QPointF(150, 100));
    QVERIFY(layerWith(session, shown.red).transform.flipX);
    // A Ctrl+T edit takes the drag into itself.
    session.undo();
    session.beginTransform();
    shown.drag(QPointF(20, 20), QPointF(40, 33));
    QCOMPARE(session.transformEdit().value().draft.origin, QPointF(170, 113));
    QCOMPARE(session.history.undoCount(), steps);
}

QTEST_MAIN(TransformPressTests)
#include "TransformPressTests.moc"
