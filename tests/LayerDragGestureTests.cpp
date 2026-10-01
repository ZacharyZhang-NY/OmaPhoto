#include "LayerDragFixtures.h"
#include <QDrag>

namespace {
// What a started drag offered: no drag loop runs offscreen.
struct StartedDrag {
    QByteArray rows;
    QByteArray mask;
    QByteArray source;
    Qt::DropActions supported;
    Qt::DropAction preferred;
};
std::vector<StartedDrag> startedDrags;
}

// Takes libQt6Gui's place in this executable, as ProjectReplaceTests do libc's.
Qt::DropAction QDrag::exec(Qt::DropActions supportedActions, Qt::DropAction defaultDropAction)
{
    const QMimeData *data = mimeData();
    startedDrags.push_back({data->data(ProjectWorkspace::layerType), data->data(NativeLayerList::maskType), data->data(NativeLayerList::sourceType),
                            supportedActions, defaultDropAction});
    return Qt::IgnoreAction;
}

Qt::DropAction QDrag::exec(Qt::DropActions supportedActions)
{
    return exec(supportedActions, Qt::IgnoreAction);
}

// Presses and hovers in the list: drags start, cursors follow.
class LayerDragGestureTests : public QObject {
    Q_OBJECT
private slots:
    void altShowsTheClippingAndDuplicateCursors();
    void pressesArmADragThatAMoveStartsFromRowsAndThumbnails();
    void hoverOverRowsAndCtrlKeepTheCursorRight();
    void aSpentPressArmsNothingLater();
    void aMaskPressReleasedInAPopupIsOver();
    void altClicksOnAMaskThroughTheWindowChooseAndToggle();
    void anotherButtonsReleaseLeavesAMaskPress();
};

void LayerDragGestureTests::altShowsTheClippingAndDuplicateCursors()
{
    const auto session = sessionWithLayers(2);
    Shown shown(*session);
    const double ratio = shown.list.devicePixelRatio();
    const QPoint strip = shown.list.cells().at(0)->mapTo(&shown.list, QPoint(200, LayerCell::rowHeight - 4));
    const QPoint body = shown.list.cells().at(0)->mapTo(&shown.list, QPoint(200, 20));
    QCOMPARE(image(shown.list.cursorFor(strip, Qt::AltModifier)), image(NativeLayerList::clippingCursor(false, ratio)));
    // The badge: a plus to create, a minus to release.
    QVERIFY(image(NativeLayerList::clippingCursor(false, 1)).pixelColor(25, 10).lightness() > 128);
    QVERIFY(image(NativeLayerList::clippingCursor(true, 1)).pixelColor(25, 10).lightness() < 128);
    session->toggleClippingMask(shown.id(0));
    QCOMPARE(image(shown.list.cursorFor(strip, Qt::AltModifier)), image(NativeLayerList::clippingCursor(true, ratio)));
    QCOMPARE(image(shown.list.cursorFor(body, Qt::AltModifier)), image(CanvasView::duplicateCursor(ratio)));
    QCOMPARE(shown.list.cursorFor(body, Qt::AltModifier | Qt::ControlModifier).shape(), Qt::ArrowCursor);
    QCOMPARE(shown.list.cursorFor(body, Qt::NoModifier).shape(), Qt::ArrowCursor);
    // Ctrl over a thumbnail loads a selection: the pointing hand.
    LayerCell &first = *shown.list.cells().at(0);
    const QPoint picture = first.mapTo(&shown.list, first.thumbnail().geometry().center());
    QCOMPARE(image(shown.list.cursorFor(picture, Qt::ControlModifier)), image(CanvasView::loadSelectionCursor(ratio)));
    QCOMPARE(image(shown.list.cursorFor(picture, Qt::ControlModifier | Qt::AltModifier)), image(CanvasView::loadSelectionCursor(ratio)));
    QCOMPARE(shown.list.cursorFor(body, Qt::ControlModifier).shape(), Qt::ArrowCursor);
    QVERIFY(image(CanvasView::loadSelectionCursor(1)).pixelColor(11, 8).lightness() > 128);
    QVERIFY(image(CanvasView::loadSelectionCursor(1)).pixelColor(21, 18).alpha() > 0);
    QCOMPARE(image(CanvasView::loadSelectionCursor(1)).pixelColor(21, 21).alpha(), 0);
    // The bottom row clips nothing; a folder has no strip.
    const QPoint lastStrip = shown.list.cells().at(1)->mapTo(&shown.list, QPoint(200, LayerCell::rowHeight - 4));
    QCOMPARE(shown.list.cursorFor(lastStrip, Qt::AltModifier).shape(), Qt::ArrowCursor);
    session->selectLayer(shown.id(1));
    session->addGroup();
    const QPoint folderBody = shown.list.cells().at(1)->mapTo(&shown.list, QPoint(200, 20));
    QCOMPARE(image(shown.list.cursorFor(folderBody, Qt::AltModifier)), image(CanvasView::duplicateCursor(ratio)));
    const QPoint folderStrip = shown.list.cells().at(1)->mapTo(&shown.list, QPoint(200, LayerCell::rowHeight - 4));
    QCOMPARE(image(shown.list.cursorFor(folderStrip, Qt::AltModifier)), image(CanvasView::duplicateCursor(ratio)));
    // Alt over a mask: an eye behind the pointer.
    session->selectLayer(shown.id(0));
    session->addLayerMask();
    LayerCell &top = *shown.list.cells().at(0);
    const QPoint mask = top.mapTo(&shown.list, top.maskThumbnail().geometry().center());
    QCOMPARE(image(shown.list.cursorFor(mask, Qt::AltModifier)), image(NativeLayerList::showMaskCursor(ratio)));
    QCOMPARE(image(shown.list.cursorFor(mask, Qt::ControlModifier)), image(CanvasView::loadSelectionCursor(ratio)));
    // The pointer's hot spot; the eye sits below right.
    const QCursor eye = NativeLayerList::showMaskCursor(1);
    QCOMPARE(eye.hotSpot(), CanvasView::duplicateCursor(1).hotSpot());
    QCOMPARE(eye.pixmap().size(), QSize(29, 32));
    const QImage drawn = image(eye);
    QCOMPARE(drawn.pixelColor(22, 23), QColor(Qt::black));
    QVERIFY(drawn.pixelColor(21, 23).lightness() > 150);
    QCOMPARE(drawn.pixelColor(25, 23).alpha(), 255);
    QCOMPARE(drawn.copy(0, 0, 18, 18), image(CanvasView::duplicateCursor(1)).copy(0, 0, 18, 18));
    QCOMPARE(image(CanvasView::duplicateCursor(1)).pixelColor(23, 23).alpha(), 0);
    session->setIsImporting(true);
    QCOMPARE(image(shown.list.cursorFor(mask, Qt::AltModifier)), image(NativeLayerList::showMaskCursor(ratio)));
    QCOMPARE(shown.list.cursorFor(mask, Qt::ControlModifier).shape(), Qt::ArrowCursor);
    QCOMPARE(shown.list.cursorFor(body, Qt::AltModifier).shape(), Qt::ArrowCursor);
    session->setIsImporting(false);
    // The viewport's cursor follows the pointer with Alt held.
    QTest::keyPress(shown.list.windowHandle(), Qt::Key_Alt, Qt::AltModifier);
    QTest::mouseMove(shown.list.viewport(), shown.list.viewport()->mapFromParent(body));
    QCOMPARE(image(shown.list.viewport()->cursor()), image(CanvasView::duplicateCursor(ratio)));
    QTest::keyRelease(shown.list.windowHandle(), Qt::Key_Alt, Qt::NoModifier);
    QCOMPARE(shown.list.viewport()->cursor().shape(), Qt::ArrowCursor);
}

void LayerDragGestureTests::pressesArmADragThatAMoveStartsFromRowsAndThumbnails()
{
    const auto session = sessionWithLayers(3);
    Shown shown(*session);
    startedDrags.clear();
    LayerCell &row = *shown.list.cells().at(1);
    const int far = QApplication::startDragDistance() + 2;
    const QPoint name(row.width() - 20, 20);
    // Short of the distance, no drag; past it, the row.
    QTest::mousePress(&row, Qt::LeftButton, Qt::NoModifier, name);
    QTest::mouseMove(&row, name + QPoint(2, 0));
    QCOMPARE(startedDrags.size(), size_t(0));
    QTest::mouseMove(&row, name + QPoint(far, 0));
    QCOMPARE(startedDrags.size(), size_t(1));
    QCOMPARE(startedDrags.back().rows, uuidString(shown.id(1)).toUtf8());
    QCOMPARE(startedDrags.back().source, shown.list.dragToken().toUtf8());
    QCOMPARE(startedDrags.back().supported, Qt::MoveAction | Qt::CopyAction);
    QCOMPARE(startedDrags.back().preferred, Qt::MoveAction);
    QTest::mouseRelease(&row, Qt::LeftButton, Qt::NoModifier, name + QPoint(far, 0));
    // A released press is spent: the middle button drags nothing.
    QTest::mousePress(&row, Qt::MiddleButton, Qt::NoModifier, name);
    QTest::mouseMove(&row, name + QPoint(far, 0));
    QTest::mouseRelease(&row, Qt::MiddleButton, Qt::NoModifier, name + QPoint(far, 0));
    QCOMPARE(startedDrags.size(), size_t(1));
    // Alt at the press offers a copy alone.
    QTest::mousePress(&row, Qt::LeftButton, Qt::AltModifier, name);
    QTest::mouseMove(&row, name + QPoint(far, 0));
    QCOMPARE(startedDrags.size(), size_t(2));
    QCOMPARE(startedDrags.back().supported, Qt::DropActions(Qt::CopyAction));
    QTest::mouseRelease(&row, Qt::LeftButton, Qt::AltModifier, name + QPoint(far, 0));
    // Alt on the strip clips and drags nothing.
    const QPoint strip(row.width() - 20, LayerCell::rowHeight - 4);
    QTest::mousePress(&row, Qt::LeftButton, Qt::AltModifier, strip);
    QTest::mouseMove(&row, strip + QPoint(far, 0));
    QTest::mouseRelease(&row, Qt::LeftButton, Qt::AltModifier, strip + QPoint(far, 0));
    QCOMPARE(startedDrags.size(), size_t(2));
    QVERIFY(layerWith(*session, shown.id(1)).maskSourceID.has_value());
    // Thumbnails drag their row too, plain and with Alt.
    LayerThumbnailButton &picture = shown.list.cells().at(0)->thumbnail();
    QTest::mousePress(&picture, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    QTest::mouseMove(&picture, QPoint(5 + far, 5));
    QTest::mouseRelease(&picture, Qt::LeftButton, Qt::NoModifier, QPoint(5 + far, 5));
    QCOMPARE(startedDrags.size(), size_t(3));
    QCOMPARE(startedDrags.back().rows, uuidString(shown.id(0)).toUtf8());
    QTest::mousePress(&picture, Qt::LeftButton, Qt::AltModifier, QPoint(5, 5));
    QTest::mouseMove(&picture, QPoint(5 + far, 5));
    QTest::mouseRelease(&picture, Qt::LeftButton, Qt::AltModifier, QPoint(5 + far, 5));
    QCOMPARE(startedDrags.size(), size_t(4));
    QCOMPARE(startedDrags.back().supported, Qt::DropActions(Qt::CopyAction));
    // A thumbnail click spent, the middle button drags nothing either.
    QTest::mouseClick(&picture, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    QTest::mousePress(&picture, Qt::MiddleButton, Qt::NoModifier, QPoint(5, 5));
    QTest::mouseMove(&picture, QPoint(5 + far, 5));
    QTest::mouseRelease(&picture, Qt::MiddleButton, Qt::NoModifier, QPoint(5 + far, 5));
    QCOMPARE(startedDrags.size(), size_t(4));
    // A mask thumbnail under Alt drags its mask.
    session->selectLayer(shown.id(0));
    session->addLayerMask();
    LayerThumbnailButton &mask = shown.list.cells().at(0)->maskThumbnail();
    QTest::mousePress(&mask, Qt::LeftButton, Qt::AltModifier, QPoint(5, 5));
    QTest::mouseMove(&mask, QPoint(7, 5));
    QCOMPARE(startedDrags.size(), size_t(4));
    QTest::mouseMove(&mask, QPoint(5 + far, 5));
    QTest::mouseRelease(&mask, Qt::LeftButton, Qt::AltModifier, QPoint(5 + far, 5));
    QCOMPARE(startedDrags.size(), size_t(5));
    QCOMPARE(startedDrags.back().mask, uuidString(shown.id(0)).toUtf8());
    QCOMPARE(startedDrags.back().supported, Qt::DropActions(Qt::CopyAction));
    // Busy, a press drags nothing.
    session->setIsImporting(true);
    QTest::mousePress(&row, Qt::LeftButton, Qt::NoModifier, name);
    QTest::mouseMove(&row, name + QPoint(far, 0));
    QTest::mouseRelease(&row, Qt::LeftButton, Qt::NoModifier, name + QPoint(far, 0));
    QCOMPARE(startedDrags.size(), size_t(5));
}

void LayerDragGestureTests::hoverOverRowsAndCtrlKeepTheCursorRight()
{
    const auto session = sessionWithLayers(2);
    Shown shown(*session);
    const double ratio = shown.list.devicePixelRatio();
    LayerCell &row = *shown.list.cells().at(0);
    QWindow *window = shown.list.windowHandle();
    // A hover with Alt over a row's control, as reported.
    const auto hover = [](QWidget &under, QPoint at) {
        QMouseEvent move(QEvent::MouseMove, QPointF(at), under.mapToGlobal(at), Qt::NoButton, Qt::NoButton, Qt::AltModifier);
        QApplication::sendEvent(&under, &move);
    };
    // Every control of a row reports its hover.
    for (const QWidget *child : row.findChildren<QWidget *>())
        QVERIFY2(child->hasMouseTracking(), qPrintable(child->objectName()));
    QVERIFY(row.hasMouseTracking());
    QTest::keyPress(window, Qt::Key_Alt, Qt::AltModifier);
    hover(row, QPoint(200, 20));
    QCOMPARE(image(shown.list.viewport()->cursor()), image(CanvasView::duplicateCursor(ratio)));
    // Onto the strip, over the row itself: the clipping cursor.
    hover(row, QPoint(200, LayerCell::rowHeight - 4));
    QCOMPARE(image(shown.list.viewport()->cursor()), image(NativeLayerList::clippingCursor(false, ratio)));
    hover(row.thumbnail(), QPoint(5, 5));
    QCOMPARE(image(shown.list.viewport()->cursor()), image(CanvasView::duplicateCursor(ratio)));
    // Ctrl joining Alt over a thumbnail: the load cursor.
    qt_handleKeyEvent(window, QEvent::KeyPress, Qt::Key_Control, Qt::AltModifier | Qt::ControlModifier);
    QCOMPARE(image(shown.list.viewport()->cursor()), image(CanvasView::loadSelectionCursor(ratio)));
    qt_handleKeyEvent(window, QEvent::KeyRelease, Qt::Key_Control, Qt::AltModifier);
    QCOMPARE(image(shown.list.viewport()->cursor()), image(CanvasView::duplicateCursor(ratio)));
    QTest::keyRelease(shown.list.windowHandle(), Qt::Key_Alt, Qt::NoModifier);
    QCOMPARE(shown.list.viewport()->cursor().shape(), Qt::ArrowCursor);
}

void LayerDragGestureTests::aSpentPressArmsNothingLater()
{
    const auto session = sessionWithLayers(3);
    Shown shown(*session);
    startedDrags.clear();
    LayerCell &row = *shown.list.cells().at(0);
    const int far = QApplication::startDragDistance() + 2;
    const QPoint name(row.width() - 20, 20), strip(row.width() - 20, LayerCell::rowHeight - 4);
    QTest::mouseClick(&row, Qt::LeftButton, Qt::NoModifier, name);
    // Presses that arm nothing: the strip, Ctrl, Shift.
    QTest::mousePress(&row, Qt::LeftButton, Qt::AltModifier, strip);
    QTest::mouseMove(&row, strip + QPoint(far, 0));
    QTest::mouseRelease(&row, Qt::LeftButton, Qt::AltModifier, strip + QPoint(far, 0));
    QCOMPARE(startedDrags.size(), size_t(0));
    QTest::mouseClick(&row, Qt::LeftButton, Qt::NoModifier, name);
    LayerThumbnailButton &picture = row.thumbnail();
    QTest::mousePress(&picture, Qt::LeftButton, Qt::ControlModifier, QPoint(5, 5));
    QTest::mouseMove(&picture, QPoint(5 + far, 5));
    QTest::mouseRelease(&picture, Qt::LeftButton, Qt::ControlModifier, QPoint(5 + far, 5));
    QCOMPARE(startedDrags.size(), size_t(0));
    session->selectLayer(shown.id(0));
    session->addLayerMask();
    QTest::mouseClick(&row, Qt::LeftButton, Qt::NoModifier, name);
    LayerThumbnailButton &mask = row.maskThumbnail();
    QTest::mousePress(&mask, Qt::LeftButton, Qt::ShiftModifier, QPoint(5, 5));
    QTest::mouseMove(&mask, QPoint(5 + far, 5));
    QTest::mouseRelease(&mask, Qt::LeftButton, Qt::ShiftModifier, QPoint(5 + far, 5));
    QCOMPARE(startedDrags.size(), size_t(0));
}

void LayerDragGestureTests::aMaskPressReleasedInAPopupIsOver()
{
    const auto session = sessionWithLayers(2);
    Shown shown(*session);
    startedDrags.clear();
    session->selectLayer(shown.id(0));
    session->addLayerMask();
    LayerThumbnailButton &mask = shown.list.cells().at(0)->maskThumbnail();
    const int far = QApplication::startDragDistance() + 2;
    QTest::mousePress(&mask, Qt::LeftButton, Qt::AltModifier, QPoint(5, 5));
    QMenu menu;
    menu.addAction(QStringLiteral("Nothing"));
    menu.popup(shown.list.mapToGlobal(QPoint(100, 100)));
    QTRY_VERIFY(menu.isVisible());
    QTest::mouseRelease(&menu, Qt::LeftButton, Qt::NoModifier, QPoint(4, 4));
    menu.close();
    // Neither a hover nor a plain press drags the mask.
    QMouseEvent hover(QEvent::MouseMove, QPointF(5 + far, 5), mask.mapToGlobal(QPoint(5 + far, 5)), Qt::NoButton, Qt::NoButton, Qt::AltModifier);
    QApplication::sendEvent(&mask, &hover);
    QCOMPARE(startedDrags.size(), size_t(0));
    QTest::mousePress(&mask, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    QTest::mouseMove(&mask, QPoint(5 + far, 5));
    QTest::mouseRelease(&mask, Qt::LeftButton, Qt::NoModifier, QPoint(5 + far, 5));
    QCOMPARE(startedDrags.size(), size_t(1));
    QVERIFY(startedDrags.back().mask.isEmpty());
    QCOMPARE(startedDrags.back().rows, uuidString(shown.id(0)).toUtf8());
}

void LayerDragGestureTests::altClicksOnAMaskThroughTheWindowChooseAndToggle()
{
    const auto session = sessionWithLayers(2);
    Shown shown(*session);
    startedDrags.clear();
    session->selectLayer(shown.id(0));
    session->addLayerMask();
    session->selectLayerTarget(shown.id(0), false);
    LayerThumbnailButton &mask = shown.list.cells().at(0)->maskThumbnail();
    const QPoint at = mask.mapTo(&shown.list, QPoint(5, 5));
    // The window sees the release first; the click still lands.
    QTest::mouseClick(shown.list.windowHandle(), Qt::LeftButton, Qt::AltModifier, at);
    QVERIFY(session->isMaskSelected());
    QVERIFY(session->activeLayer().value().mask.value().isEnabled);
    QTest::mouseClick(shown.list.windowHandle(), Qt::LeftButton, Qt::AltModifier | Qt::ShiftModifier, at);
    QVERIFY(session->isMaskSelected());
    QVERIFY(!session->activeLayer().value().mask.value().isEnabled);
    QCOMPARE(startedDrags.size(), size_t(0));
}

void LayerDragGestureTests::anotherButtonsReleaseLeavesAMaskPress()
{
    const auto session = sessionWithLayers(2);
    Shown shown(*session);
    startedDrags.clear();
    session->selectLayer(shown.id(0));
    session->addLayerMask();
    session->selectLayerTarget(shown.id(0), false);
    LayerThumbnailButton &mask = shown.list.cells().at(0)->maskThumbnail();
    const int far = QApplication::startDragDistance() + 2;
    QTest::mousePress(&mask, Qt::LeftButton, Qt::AltModifier, QPoint(5, 5));
    // The middle button pressed and released chooses nothing.
    QTest::mousePress(&mask, Qt::MiddleButton, Qt::AltModifier, QPoint(5, 5));
    QTest::mouseRelease(&mask, Qt::MiddleButton, Qt::AltModifier, QPoint(5, 5));
    QVERIFY(!session->isMaskSelected());
    QTest::mouseMove(&mask, QPoint(5 + far, 5));
    QCOMPARE(startedDrags.size(), size_t(1));
    QCOMPARE(startedDrags.back().mask, uuidString(shown.id(0)).toUtf8());
    QTest::mouseRelease(&mask, Qt::LeftButton, Qt::AltModifier, QPoint(5 + far, 5));
}

QTEST_MAIN(LayerDragGestureTests)
#include "LayerDragGestureTests.moc"
