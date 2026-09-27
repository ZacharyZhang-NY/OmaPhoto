#include "CanvasFixtures.h"
#include <QDialog>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>

// The canvas: redraws, keys and focus.
class EditorCanvasTests : public QObject {
    Q_OBJECT
private slots:
    void selectionAndRenameDoNotInvalidateCanvasButPixelChangesDo();
    void mountingCanvasGivesItKeyboardFocus();
    void focusIsLeftWithASheetOrADialog();
    void geometryReachesTheViewportAfterTheEventLoop();
    void toolKeysSelectToolsAndSpaceHoldsTheHand();
    void transformKeysCommitCancelAndNudge();
    void shiftPlusAndMinusCycleTheBlendModeAcrossTheWindow();
    void altReachesTheZoomCursorFromAnyControl();
    void altHeldWhileMountingOrHiddenIsReadOnShow();
};

void EditorCanvasTests::selectionAndRenameDoNotInvalidateCanvasButPixelChangesDo()
{
    EditorSession session;
    const ImportedImage asset = filled(8, 8, qRgba(255, 0, 0, 255), "Test");
    session.insert(asset);
    session.insert(asset);
    CanvasView view(session);
    QVERIFY(view.synchronizeDisplay());
    const QUuid first = session.document().value().layers.front().id;
    const QUuid second = session.document().value().layers.back().id;
    session.setActiveLayerID(first);
    session.renameLayer(first, "Renamed");
    QVERIFY(!view.synchronizeDisplay());
    session.moveActiveLayer(1);
    QVERIFY(view.synchronizeDisplay());
    session.toggleLayerVisibility(first);
    QVERIFY(view.synchronizeDisplay());
    session.zoom(2);
    QVERIFY(view.synchronizeDisplay());
    // Beyond Swift's: the selection shows nothing, each displayed thing does.
    session.selectLayer(second);
    QVERIFY(!view.synchronizeDisplay());
    session.setShowsPixelGrid(false);
    QVERIFY(view.synchronizeDisplay());
    session.setLayerOpacity(0.5);
    QVERIFY(view.synchronizeDisplay());
    session.previewBlendMode(LayerBlendMode::multiply, second);
    QVERIFY(view.synchronizeDisplay());
    session.previewBlendMode(std::nullopt, std::nullopt);
    QVERIFY(view.synchronizeDisplay());
    session.beginTransform();
    QVERIFY(!view.synchronizeDisplay());
    LayerTransform draft = session.transformEdit().value().draft;
    draft.origin += QPointF(3, 0);
    session.previewTransform(draft);
    QVERIFY(view.synchronizeDisplay());
    session.cancelTransform();
    QVERIFY(view.synchronizeDisplay());
    session.addLayerMask(false);
    QVERIFY(view.synchronizeDisplay());
    session.toggleLayerMask();
    QVERIFY(view.synchronizeDisplay());
    // Hidden layers change unseen until a live mask reads them.
    session.selectLayer(first);
    session.setLayerOpacity(0.25);
    QCOMPARE(layerWith(session, first).opacity, 0.25);
    QVERIFY(!view.synchronizeDisplay());
    QVERIFY(session.linkMask(first, second));
    QVERIFY(view.synchronizeDisplay());
    session.setLayerOpacity(0.75);
    QCOMPARE(layerWith(session, first).opacity, 0.75);
    QVERIFY(view.synchronizeDisplay());
    session.toggleLayerVisibility(first);
    QVERIFY(view.synchronizeDisplay());
    session.toggleLayerVisibility(first);
    QVERIFY(view.synchronizeDisplay());
    session.removeLiveMask(second);
    QVERIFY(view.synchronizeDisplay());
    // A folder changes the clip; its mask counts too.
    session.selectLayers({first, second}, second);
    session.groupSelectedLayers();
    QVERIFY(view.synchronizeDisplay());
    session.addLayerMask(false);
    QVERIFY(session.activeLayer().value().isGroup);
    QVERIFY(view.synchronizeDisplay());
    session.toggleLayerMask();
    QVERIFY(view.synchronizeDisplay());
    session.toggleGroupExpansion(session.activeLayerID().value());
    QVERIFY(!view.synchronizeDisplay());
}

void EditorCanvasTests::mountingCanvasGivesItKeyboardFocus()
{
    Shown shown;
    QTRY_VERIFY(shown.canvas->hasFocus());
    shown.canvas->clearFocus();
    QVERIFY(!shown.canvas->hasFocus());
    shown.canvas->consumeFocusRequest(1);
    QTRY_VERIFY(shown.canvas->hasFocus());
    // The same request again asks nothing.
    shown.canvas->clearFocus();
    shown.canvas->consumeFocusRequest(1);
    QTest::qWait(50);
    QVERIFY(!shown.canvas->hasFocus());
    shown.canvas->consumeFocusRequest(2);
    QTRY_VERIFY(shown.canvas->hasFocus());
    // Without a document the welcome's field keeps the keys.
    EditorSession bare;
    QWidget window;
    auto *field = new QLineEdit(&window);
    field->setFocus();
    auto *canvas = new CanvasView(bare, &window);
    canvas->setGeometry(0, 30, 200, 170);
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    QTest::qWait(50);
    QVERIFY(field->hasFocus() && !canvas->hasFocus());
}

void EditorCanvasTests::focusIsLeftWithASheetOrADialog()
{
    EditorSession session;
    session.createDocument(10, 10);
    session.setShowsImporter(true);
    QWidget window;
    auto *field = new QLineEdit(&window);
    field->setFocus();
    auto *canvas = new CanvasView(session, &window);
    window.resize(200, 200);
    canvas->setGeometry(0, 30, 200, 170);
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    QTest::qWait(50);
    QVERIFY(field->hasFocus() && !canvas->hasFocus());
    session.setShowsImporter(false);
    // The new-canvas sheet keeps the keys as well.
    session.setShowsNewDocument(true);
    canvas->hide();
    canvas->show();
    QTest::qWait(50);
    QVERIFY(field->hasFocus() && !canvas->hasFocus());
    session.setShowsNewDocument(false);
    canvas->hide();
    canvas->show();
    QTRY_VERIFY(canvas->hasFocus());
    field->setFocus();
    // A dialog over the window keeps a request's focus too.
    QDialog dialog(&window);
    dialog.open();
    QTRY_VERIFY(dialog.isVisible());
    canvas->consumeFocusRequest(1);
    canvas->hide();
    canvas->show();
    QTest::qWait(50);
    QVERIFY(!canvas->hasFocus());
    dialog.close();
    // Back, the field keeps focus; a new request lands.
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    QTest::qWait(50);
    QVERIFY(field->hasFocus() && !canvas->hasFocus());
    canvas->consumeFocusRequest(2);
    QTRY_VERIFY(canvas->hasFocus());
}

void EditorCanvasTests::geometryReachesTheViewportAfterTheEventLoop()
{
    EditorSession session;
    session.createDocument(100, 50);
    QWidget window;
    auto *canvas = new CanvasView(session, &window);
    window.resize(400, 300);
    canvas->setGeometry(0, 0, 400, 300);
    QSignalSpy changes(&session, &EditorSession::changed);
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    QTRY_COMPARE(session.viewport.viewSize, QSizeF(400, 300));
    QCOMPARE(session.viewport.backingScale, 1.0);
    QVERIFY(session.viewport.followsFit());
    QCOMPARE(session.viewport.zoom(), 3.04);
    // Shown and resized: one announcement; the same size, none.
    QTest::qWait(30);
    QCOMPARE(changes.count(), 1);
    canvas->resize(600, 300);
    QTRY_COMPARE(session.viewport.viewSize, QSizeF(600, 300));
    QCOMPARE(session.viewport.zoom(), 4.08);
    QCOMPARE(changes.count(), 2);
    // Zoomed by hand, the view keeps its zoom when resized.
    session.zoom(1);
    canvas->resize(500, 300);
    QTRY_COMPARE(session.viewport.viewSize, QSizeF(500, 300));
    QCOMPARE(session.viewport.zoom(), 1.0);
}

void EditorCanvasTests::toolKeysSelectToolsAndSpaceHoldsTheHand()
{
    Shown shown;
    CanvasView &canvas = *shown.canvas;
    EditorSession &session = shown.session;
    QTRY_VERIFY(canvas.hasFocus());
    const std::pair<Qt::Key, NavigationTool> keys[] = {
        {Qt::Key_I, NavigationTool::eyedropper}, {Qt::Key_B, NavigationTool::brush}, {Qt::Key_V, NavigationTool::move},
        {Qt::Key_E, NavigationTool::brush}, {Qt::Key_J, NavigationTool::spotHealing}, {Qt::Key_S, NavigationTool::cloneStamp},
        {Qt::Key_T, NavigationTool::type}, {Qt::Key_G, NavigationTool::gradient}, {Qt::Key_U, NavigationTool::shape},
        {Qt::Key_M, NavigationTool::marquee}, {Qt::Key_W, NavigationTool::wand}, {Qt::Key_L, NavigationTool::lasso},
        {Qt::Key_A, NavigationTool::idle}, {Qt::Key_R, NavigationTool::blur}, {Qt::Key_C, NavigationTool::crop},
        {Qt::Key_H, NavigationTool::hand}, {Qt::Key_Z, NavigationTool::zoom},
    };
    for (const auto &[key, tool] : keys) {
        QTest::keyClick(&canvas, key);
        QCOMPARE(session.tool(), tool);
    }
    // With Ctrl or Alt the letters are shortcuts, not tools.
    session.selectTool(NavigationTool::move);
    QTest::keyClick(&canvas, Qt::Key_I, Qt::ControlModifier);
    QTest::keyClick(&canvas, Qt::Key_I, Qt::AltModifier);
    QCOMPARE(session.tool(), NavigationTool::move);
    QTest::keyClick(&canvas, Qt::Key_M, Qt::ShiftModifier);
    QCOMPARE(session.tool(), NavigationTool::marquee);
    // Space holds the hand; letting go restores the tool's cursor.
    session.selectTool(NavigationTool::brush);
    canvas.synchronizeDisplay();
    QCOMPARE(canvas.cursor().shape(), Qt::CrossCursor);
    QTest::keyPress(&canvas, Qt::Key_Space);
    QCOMPARE(canvas.cursor().shape(), Qt::OpenHandCursor);
    // A held key repeats: its releases are no release.
    QKeyEvent repeated(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier, QString(), true);
    QApplication::sendEvent(&canvas, &repeated);
    QCOMPARE(canvas.cursor().shape(), Qt::OpenHandCursor);
    QTest::keyRelease(&canvas, Qt::Key_Space);
    QCOMPARE(canvas.cursor().shape(), Qt::CrossCursor);
    // Each tool has its cursor; Alt turns the magnifier's sign.
    session.selectTool(NavigationTool::type);
    canvas.synchronizeDisplay();
    QCOMPARE(canvas.cursor().shape(), Qt::IBeamCursor);
    session.selectTool(NavigationTool::idle);
    canvas.synchronizeDisplay();
    QCOMPARE(canvas.cursor().shape(), Qt::ArrowCursor);
    session.selectTool(NavigationTool::hand);
    canvas.synchronizeDisplay();
    QCOMPARE(canvas.cursor().shape(), Qt::OpenHandCursor);
    session.selectTool(NavigationTool::zoom);
    canvas.synchronizeDisplay();
    QCOMPARE(canvas.cursor().shape(), Qt::BitmapCursor);
    const QImage plus = canvas.cursor().pixmap().toImage();
    QTest::keyPress(&canvas, Qt::Key_Alt, Qt::AltModifier);
    const QImage minus = canvas.cursor().pixmap().toImage();
    QVERIFY(plus != minus);
    QTest::keyRelease(&canvas, Qt::Key_Alt);
    QCOMPARE(canvas.cursor().pixmap().toImage(), plus);
    // Focus lost while Space is held: the hand goes.
    QTest::keyPress(&canvas, Qt::Key_Space);
    QCOMPARE(canvas.cursor().shape(), Qt::OpenHandCursor);
    canvas.clearFocus();
    QCOMPARE(canvas.cursor().shape(), Qt::BitmapCursor);
}

void EditorCanvasTests::transformKeysCommitCancelAndNudge()
{
    Shown shown;
    CanvasView &canvas = *shown.canvas;
    EditorSession &session = shown.session;
    session.insert(filled(10, 10, qRgba(0, 0, 255, 255), "Blue"));
    const QUuid id = session.activeLayerID().value();
    const QPointF origin = layerWith(session, id).transform.origin;
    session.selectTool(NavigationTool::move);
    QTRY_VERIFY(canvas.hasFocus());
    QTest::keyClick(&canvas, Qt::Key_Right);
    QCOMPARE(layerWith(session, id).transform.origin, origin + QPointF(1, 0));
    QTest::keyClick(&canvas, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(layerWith(session, id).transform.origin, origin + QPointF(1, 10));
    QTest::keyClick(&canvas, Qt::Key_Left, Qt::ControlModifier);
    QCOMPARE(layerWith(session, id).transform.origin, origin + QPointF(1, 10));
    // Arrows nudge in the Move tool only.
    session.selectTool(NavigationTool::brush);
    QTest::keyClick(&canvas, Qt::Key_Up);
    QCOMPARE(layerWith(session, id).transform.origin, origin + QPointF(1, 10));
    session.selectTool(NavigationTool::move);
    // Escape cancels a pending transform, Return and Enter commit it.
    const auto previewMoved = [&](double by) {
        session.beginTransform();
        LayerTransform draft = session.transformEdit().value().draft;
        draft.origin += QPointF(by, 0);
        session.previewTransform(draft);
    };
    previewMoved(5);
    QTest::keyClick(&canvas, Qt::Key_Escape);
    QVERIFY(!session.transformEdit());
    QCOMPARE(layerWith(session, id).transform.origin, origin + QPointF(1, 10));
    previewMoved(5);
    QTest::keyClick(&canvas, Qt::Key_Return);
    QVERIFY(!session.transformEdit());
    QCOMPARE(layerWith(session, id).transform.origin, origin + QPointF(6, 10));
    previewMoved(2);
    QTest::keyClick(&canvas, Qt::Key_Enter);
    QVERIFY(!session.transformEdit());
    QCOMPARE(layerWith(session, id).transform.origin, origin + QPointF(8, 10));
    session.undo();
    QCOMPARE(layerWith(session, id).transform.origin, origin + QPointF(6, 10));
}

void EditorCanvasTests::shiftPlusAndMinusCycleTheBlendModeAcrossTheWindow()
{
    EditorSession session;
    session.createDocument(10, 10);
    session.insert(filled(4, 4, qRgba(0, 0, 255, 255), "Blue"));
    QWidget window;
    auto *button = new QPushButton(&window);
    auto *field = new QLineEdit(&window);
    field->move(120, 0);
    auto *canvas = new CanvasView(session, &window);
    window.resize(300, 200);
    canvas->setGeometry(0, 40, 300, 160);
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    QTRY_VERIFY(canvas->hasFocus());
    const auto mode = [&] { return session.activeLayer().value().blendMode; };
    QCOMPARE(mode(), LayerBlendMode::normal);
    QTest::keyClick(canvas, Qt::Key_Plus, Qt::ShiftModifier);
    QCOMPARE(mode(), LayerBlendMode::darken);
    QTest::keyClick(canvas, Qt::Key_Underscore, Qt::ShiftModifier);
    QCOMPARE(mode(), LayerBlendMode::normal);
    QTest::keyClick(canvas, Qt::Key_Underscore, Qt::ShiftModifier);
    QCOMPARE(mode(), LayerBlendMode::luminosity);
    // From any control of the window, unshifted keys too.
    button->setFocus();
    QTRY_VERIFY(button->hasFocus());
    QTest::keyClick(button, Qt::Key_Equal, Qt::ShiftModifier);
    QCOMPARE(mode(), LayerBlendMode::normal);
    QTest::keyClick(button, Qt::Key_Minus, Qt::ShiftModifier);
    QCOMPARE(mode(), LayerBlendMode::luminosity);
    // Not while typing, without Shift, or with Ctrl or Alt.
    field->setFocus();
    QTRY_VERIFY(field->hasFocus());
    QTest::keyClick(field, Qt::Key_Plus, Qt::ShiftModifier);
    QCOMPARE(mode(), LayerBlendMode::luminosity);
    QCOMPARE(field->text(), QString("+"));
    canvas->setFocus();
    QTest::keyClick(canvas, Qt::Key_Plus);
    QTest::keyClick(canvas, Qt::Key_Plus, Qt::ShiftModifier | Qt::ControlModifier);
    QTest::keyClick(canvas, Qt::Key_Plus, Qt::ShiftModifier | Qt::AltModifier);
    QCOMPARE(mode(), LayerBlendMode::luminosity);
    // Another window's keys are its own.
    QWidget other;
    auto *elsewhere = new QPushButton(&other);
    other.show();
    QVERIFY(QTest::qWaitForWindowActive(&other));
    QTest::keyClick(elsewhere, Qt::Key_Plus, Qt::ShiftModifier);
    QCOMPARE(mode(), LayerBlendMode::luminosity);
    other.hide();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    // Hidden, the canvas listens no more; shown, again.
    canvas->hide();
    button->setFocus();
    QTest::keyClick(button, Qt::Key_Plus, Qt::ShiftModifier);
    QCOMPARE(mode(), LayerBlendMode::luminosity);
    canvas->show();
    QTest::keyClick(button, Qt::Key_Plus, Qt::ShiftModifier);
    QCOMPARE(mode(), LayerBlendMode::normal);
}

void EditorCanvasTests::altReachesTheZoomCursorFromAnyControl()
{
    EditorSession session;
    session.createDocument(10, 10);
    session.selectTool(NavigationTool::zoom);
    QWidget window;
    auto *field = new QLineEdit(&window);
    auto *canvas = new CanvasView(session, &window);
    window.resize(300, 200);
    canvas->setGeometry(0, 40, 300, 160);
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    canvas->synchronizeDisplay();
    const QImage plus = canvas->cursor().pixmap().toImage();
    field->setFocus();
    QTRY_VERIFY(field->hasFocus());
    QTest::keyPress(field, Qt::Key_Alt, Qt::AltModifier);
    const QImage minus = canvas->cursor().pixmap().toImage();
    QVERIFY(minus != plus);
    // Alt repeats while held; only its release restores the plus.
    QKeyEvent repeated(QEvent::KeyRelease, Qt::Key_Alt, Qt::AltModifier, QString(), true);
    QApplication::sendEvent(field, &repeated);
    QCOMPARE(canvas->cursor().pixmap().toImage(), minus);
    QTest::keyRelease(field, Qt::Key_Alt);
    QCOMPARE(canvas->cursor().pixmap().toImage(), plus);
}

void EditorCanvasTests::altHeldWhileMountingOrHiddenIsReadOnShow()
{
    EditorSession session;
    session.createDocument(10, 10);
    session.selectTool(NavigationTool::zoom);
    QWidget window;
    auto *field = new QLineEdit(&window);
    window.resize(300, 200);
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    field->setFocus();
    QTRY_VERIFY(field->hasFocus());
    // Alt held before the canvas mounts: the minus shows.
    QTest::keyPress(window.windowHandle(), Qt::Key_Alt, Qt::AltModifier);
    QVERIFY(QApplication::keyboardModifiers().testFlag(Qt::AltModifier));
    auto *canvas = new CanvasView(session, &window);
    canvas->setGeometry(0, 40, 300, 160);
    canvas->show();
    canvas->synchronizeDisplay();
    const QImage minus = canvas->cursor().pixmap().toImage();
    QTest::keyRelease(window.windowHandle(), Qt::Key_Alt);
    const QImage plus = canvas->cursor().pixmap().toImage();
    QVERIFY(minus != plus);
    // Released while hidden, Alt is read again on show.
    QTest::keyPress(window.windowHandle(), Qt::Key_Alt, Qt::AltModifier);
    QCOMPARE(canvas->cursor().pixmap().toImage(), minus);
    canvas->hide();
    QTest::keyRelease(window.windowHandle(), Qt::Key_Alt);
    canvas->show();
    QCOMPARE(canvas->cursor().pixmap().toImage(), plus);
    // A menu is its own window; Alt's release reaches it.
    QMenu menu;
    menu.addAction(QStringLiteral("Undo"));
    QTest::keyPress(window.windowHandle(), Qt::Key_Alt, Qt::AltModifier);
    QCOMPARE(canvas->cursor().pixmap().toImage(), minus);
    menu.popup(window.mapToGlobal(QPoint(20, 20)));
    QTRY_COMPARE(QApplication::activePopupWidget(), static_cast<QWidget *>(&menu));
    QTest::keyRelease(&menu, Qt::Key_Alt);
    QCOMPARE(canvas->cursor().pixmap().toImage(), plus);
    QTest::keyClick(&menu, Qt::Key_Escape);
    QTRY_VERIFY(!menu.isVisible());
    QCOMPARE(canvas->cursor().pixmap().toImage(), plus);
}

QTEST_MAIN(EditorCanvasTests)
#include "EditorCanvasTests.moc"
