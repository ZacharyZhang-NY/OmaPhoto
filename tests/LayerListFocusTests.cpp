#include "UI/LayerAppearanceControls.h"
#include "UI/LayerIcons.h"
#include "UI/LayersPanel.h"
#include "UI/NativeLayerList.h"
#include "SessionFixtures.h"
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QScrollBar>
#include <QSlider>
#include <QtTest>

// The list under menus, themes and a short viewport.
namespace {
std::unique_ptr<EditorSession> sessionWithLayers(int count)
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(800, 600);
    for (int each = 0; each < count; ++each)
        session->addBlankLayer();
    return session;
}

// A window with an Edit menu over the panel.
struct Framed {
    QMainWindow window;
    explicit Framed(QWidget *central)
    {
        window.menuBar()->addMenu(QStringLiteral("&Edit"))->addAction(QStringLiteral("Nothing"));
        window.setCentralWidget(central);
        window.resize(400, 300);
        window.show();
        if (!QTest::qWaitForWindowActive(&window))
            throw std::runtime_error("the window never became active");
    }
    void openEditMenuAndClose()
    {
        QTest::keyClick(&window, Qt::Key_E, Qt::AltModifier);
        QTRY_VERIFY(QApplication::activePopupWidget() != nullptr);
        QTest::keyClick(QApplication::activePopupWidget(), Qt::Key_Escape);
        QTest::keyClick(window.menuBar(), Qt::Key_Escape);
    }
};

QImage iconImage(const QIcon &icon, int side)
{
    return icon.pixmap(QSize(side, side)).toImage();
}
}

class LayerListFocusTests : public QObject {
    Q_OBJECT
private slots:
    void aMenuBorrowingFocusKeepsTheRenameAndTheField();
    void aSwipeInterruptedByARebuildStillEndsItsStep();
    void aSwipeScrollsAShortListAndTheKeysKeepTheRowInView();
    void aNewPaletteRedrawsEveryIcon();
    void aSwipeReleasedOverAPopupStillEnds();
    void shiftArrowsGrowAndShrinkARangeFromTheAnchor();
    void altAndShiftOnAMaskThumbnailChooseAndToggleIt();
    void tabAndTheBrushKeysStayWithTheList();
    void aSliderDragReleasedOverAPopupStillEnds();
    void anotherButtonsReleaseLeavesTheSwipe();
    void altDoubleClickOnTheStripClipsTwiceAndAltShiftTogglesAMask();
};

void LayerListFocusTests::aMenuBorrowingFocusKeepsTheRenameAndTheField()
{
    const auto session = sessionWithLayers(2);
    auto *panel = new LayersPanel(*session);
    Framed framed(panel);
    LayerCell &row = *panel->list().cells().at(0);
    QTest::mouseDClick(&row, Qt::LeftButton, Qt::NoModifier, QPoint(row.width() - 20, 20));
    auto *editor = row.findChild<QLineEdit *>("layerNameEditor");
    QTRY_VERIFY(editor->hasFocus());
    QTest::keyClicks(editor, "Sky");
    framed.openEditMenuAndClose();
    QTRY_VERIFY(editor->hasFocus());
    QVERIFY(editor->isVisible() && session->renamingLayerID().has_value());
    QCOMPARE(layerWith(*session, row.layerID()).name, QString("Layer 2"));
    QTest::keyClick(editor, Qt::Key_Return);
    QCOMPARE(layerWith(*session, row.layerID()).name, QString("Sky"));
    // The opacity field keeps its text and undo too.
    auto *field = panel->findChild<QLineEdit *>("opacityPercent");
    field->setFocus();
    field->selectAll();
    QTest::keyClicks(field, "40");
    framed.openEditMenuAndClose();
    QTRY_VERIFY(field->hasFocus());
    QCOMPARE(field->text(), QString("40"));
    QCOMPARE(session->activeLayer().value().opacity, 1.0);
    QVERIFY(field->isUndoAvailable());
    QTest::keyClick(field, Qt::Key_Return);
    QCOMPARE(session->activeLayer().value().opacity, 0.4);
}

void LayerListFocusTests::aSwipeInterruptedByARebuildStillEndsItsStep()
{
    const auto session = sessionWithLayers(2);
    NativeLayerList list(*session);
    list.resize(252, 300);
    list.show();
    QVERIFY(QTest::qWaitForWindowActive(&list));
    auto *eye = list.cells().at(0)->findChild<EyeSwipeButton *>("layerEye");
    const int steps = session->history.undoCount();
    QTest::mousePress(eye, Qt::LeftButton, Qt::NoModifier, QPoint(10, 16));
    QVERIFY(!session->canUndo());
    // The shortcut lands mid-swipe: the rows are made anew.
    session->addBlankLayer();
    QCOMPARE(list.cells().size(), size_t(3));
    QTRY_VERIFY(session->canUndo());
    QCOMPARE(session->history.undoCount(), steps + 1);
    QCOMPARE(session->document().value().layers.size(), size_t(3));
    session->undo();
    QCOMPARE(session->document().value().layers.size(), size_t(2));
    QVERIFY(session->document().value().layers.back().isVisible);
}

void LayerListFocusTests::aSwipeScrollsAShortListAndTheKeysKeepTheRowInView()
{
    const auto session = sessionWithLayers(15);
    NativeLayerList list(*session);
    list.resize(252, 200);
    list.show();
    QVERIFY(QTest::qWaitForWindowActive(&list));
    QCOMPARE(list.verticalScrollBar()->value(), 0);
    auto *eye = list.cells().at(0)->findChild<EyeSwipeButton *>("layerEye");
    QTest::mousePress(eye, Qt::LeftButton, Qt::NoModifier, QPoint(10, 16));
    // Held below the viewport, the list scrolls by the overshoot.
    const QPoint below(10, 16 + 250);
    QMouseEvent move(QEvent::MouseMove, QPointF(below), eye->mapToGlobal(below), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(eye, &move);
    QTest::mouseRelease(eye, Qt::LeftButton, Qt::NoModifier, below);
    QCOMPARE(list.verticalScrollBar()->value(), 76);
    // The pointer sat 276 points down the list, now scrolled.
    const int reached = list.rowAt(QPoint(10, 276));
    QCOMPARE(reached, 6);
    QVERIFY(!layerWith(*session, list.cells().at(reached)->layerID()).isVisible);
    QVERIFY(layerWith(*session, list.cells().at(reached + 1)->layerID()).isVisible);
    session->undo();
    list.verticalScrollBar()->setValue(0);
    // Down walks past the view and scrolls; Shift extends.
    session->selectLayers({list.cells().at(0)->layerID()}, list.cells().at(0)->layerID());
    session->selectTool(NavigationTool::brush);
    list.setFocus();
    for (int each = 0; each < 6; ++each)
        QTest::keyClick(&list, Qt::Key_Down);
    QCOMPARE(session->activeLayerID(), std::optional(list.cells().at(6)->layerID()));
    QVERIFY(list.verticalScrollBar()->value() > 0);
    const QRect shown = list.cells().at(6)->geometry().translated(0, -list.verticalScrollBar()->value());
    QVERIFY(list.viewport()->rect().contains(shown));
    QTest::keyClick(&list, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(session->selectedLayerIDs(), (QSet<QUuid>{list.cells().at(6)->layerID(), list.cells().at(7)->layerID()}));
    QCOMPARE(session->activeLayerID(), std::optional(list.cells().at(7)->layerID()));
}

void LayerListFocusTests::aNewPaletteRedrawsEveryIcon()
{
    const auto session = sessionWithLayers(1);
    session->addGroup();
    LayersPanel panel(*session);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    const QPalette before = QApplication::palette();
    QPalette changed = before;
    changed.setColor(QPalette::PlaceholderText, QColor(10, 200, 30));
    changed.setColor(QPalette::WindowText, QColor(200, 10, 30));
    QApplication::setPalette(changed);
    const double ratio = panel.devicePixelRatio();
    auto *add = panel.findChild<QToolButton *>("addBlankLayer");
    QTRY_COMPARE(iconImage(add->icon(), 16), iconImage(LayerIcons::pixmap(LayerIcon::newLayer, 16, QColor(10, 200, 30), ratio), 16));
    QCOMPARE(iconImage(panel.findChild<QToolButton *>("addLayerMask")->icon(), 16), iconImage(LayerIcons::pixmap(LayerIcon::addMask, 16, QColor(10, 200, 30), ratio), 16));
    LayerCell &folder = *panel.list().cells().at(0);
    QTRY_COMPARE(iconImage(folder.findChild<QToolButton *>("layerEye")->icon(), 16), iconImage(LayerIcons::pixmap(LayerIcon::eye, 16, QColor(200, 10, 30), ratio), 16));
    QCOMPARE(iconImage(folder.thumbnail().icon(), 36), iconImage(LayerIcons::pixmap(LayerIcon::folder, 36 * 0.8, QColor(200, 10, 30), ratio), 36));
    QApplication::setPalette(before);
    QTRY_COMPARE(iconImage(add->icon(), 16), iconImage(LayerIcons::pixmap(LayerIcon::newLayer, 16, before.color(QPalette::PlaceholderText), ratio), 16));
}

void LayerListFocusTests::aSwipeReleasedOverAPopupStillEnds()
{
    const auto session = sessionWithLayers(2);
    NativeLayerList list(*session);
    list.resize(252, 300);
    list.show();
    QVERIFY(QTest::qWaitForWindowActive(&list));
    auto *eye = list.cells().at(0)->findChild<EyeSwipeButton *>("layerEye");
    const int steps = session->history.undoCount();
    QTest::mousePress(eye, Qt::LeftButton, Qt::NoModifier, QPoint(10, 16));
    QVERIFY(!session->canUndo());
    // A menu opened mid-press takes the release.
    QMenu menu;
    menu.addAction(QStringLiteral("Nothing"));
    menu.popup(list.mapToGlobal(QPoint(100, 100)));
    QTRY_VERIFY(menu.isVisible());
    QTest::mouseRelease(&menu, Qt::LeftButton, Qt::NoModifier, QPoint(4, 4));
    menu.close();
    QVERIFY(session->canUndo());
    QCOMPARE(session->history.undoCount(), steps + 1);
    QVERIFY(!layerWith(*session, list.cells().at(0)->layerID()).isVisible);
    // The next press begins a swipe of its own.
    QTest::mouseClick(eye, Qt::LeftButton, Qt::NoModifier, QPoint(10, 16));
    QCOMPARE(session->history.undoCount(), steps + 2);
    QVERIFY(layerWith(*session, list.cells().at(0)->layerID()).isVisible);
}

void LayerListFocusTests::shiftArrowsGrowAndShrinkARangeFromTheAnchor()
{
    const auto session = sessionWithLayers(4);
    NativeLayerList list(*session);
    list.resize(252, 300);
    list.show();
    QVERIFY(QTest::qWaitForWindowActive(&list));
    session->selectTool(NavigationTool::hand);
    const auto id = [&](int row) { return list.cells().at(size_t(row))->layerID(); };
    const auto ids = [&](std::initializer_list<int> rows) {
        QSet<QUuid> set;
        for (const int row : rows)
            set.insert(id(row));
        return set;
    };
    session->selectLayers({id(0)}, id(0));
    list.setFocus();
    QTest::keyClick(&list, Qt::Key_Down, Qt::ShiftModifier);
    QTest::keyClick(&list, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(session->selectedLayerIDs(), ids({0, 1, 2}));
    QCOMPARE(session->activeLayerID(), std::optional(id(2)));
    // Back up: the range shrinks to its anchor, then past.
    QTest::keyClick(&list, Qt::Key_Up, Qt::ShiftModifier);
    QCOMPARE(session->selectedLayerIDs(), ids({0, 1}));
    QCOMPARE(session->activeLayerID(), std::optional(id(1)));
    QTest::keyClick(&list, Qt::Key_Up, Qt::ShiftModifier);
    QCOMPARE(session->selectedLayerIDs(), ids({0}));
    QTest::keyClick(&list, Qt::Key_Up, Qt::ShiftModifier);
    QCOMPARE(session->selectedLayerIDs(), ids({0}));
    session->selectLayers({id(2)}, id(2));
    QTest::keyClick(&list, Qt::Key_Up, Qt::ShiftModifier);
    QCOMPARE(session->selectedLayerIDs(), ids({1, 2}));
    QTest::keyClick(&list, Qt::Key_Down, Qt::ShiftModifier);
    QTest::keyClick(&list, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(session->selectedLayerIDs(), ids({2, 3}));
    QCOMPARE(session->activeLayerID(), std::optional(id(3)));
}

void LayerListFocusTests::altAndShiftOnAMaskThumbnailChooseAndToggleIt()
{
    const auto session = sessionWithLayers(1);
    session->addLayerMask();
    session->selectLayerTarget(session->activeLayerID().value(), false);
    NativeLayerList list(*session);
    list.resize(252, 300);
    list.show();
    QVERIFY(QTest::qWaitForWindowActive(&list));
    LayerThumbnailButton &mask = list.cells().at(0)->maskThumbnail();
    // An Alt click chooses the mask; a drag away, nothing.
    QTest::mousePress(&mask, Qt::LeftButton, Qt::AltModifier, QPoint(5, 5));
    QVERIFY(!session->isMaskSelected());
    QTest::mouseRelease(&mask, Qt::LeftButton, Qt::AltModifier, QPoint(200, 5));
    QVERIFY(!session->isMaskSelected());
    QTest::mouseClick(&mask, Qt::LeftButton, Qt::AltModifier, QPoint(5, 5));
    QVERIFY(session->isMaskSelected());
    // A Shift double click toggles twice and renames nothing.
    QTest::mouseDClick(list.windowHandle(), Qt::LeftButton, Qt::ShiftModifier, mask.mapTo(&list, QPoint(5, 5)));
    QVERIFY(session->activeLayer().value().mask.value().isEnabled);
    QVERIFY(!session->renamingLayerID().has_value());
    QTest::mouseClick(&mask, Qt::LeftButton, Qt::ShiftModifier, QPoint(5, 5));
    QVERIFY(!session->activeLayer().value().mask.value().isEnabled);
}

void LayerListFocusTests::aSliderDragReleasedOverAPopupStillEnds()
{
    const auto session = sessionWithLayers(1);
    LayerAppearanceControls controls(*session);
    controls.show();
    QVERIFY(QTest::qWaitForWindowActive(&controls));
    auto *slider = controls.findChild<QSlider *>("opacitySlider");
    const int steps = session->history.undoCount();
    // The handle sits at the right end, at full opacity.
    QTest::mousePress(slider, Qt::LeftButton, Qt::NoModifier, QPoint(slider->width() - 4, slider->height() / 2));
    QVERIFY(slider->isSliderDown() && !session->canUndo());
    QMenu menu;
    menu.addAction(QStringLiteral("Nothing"));
    menu.popup(controls.mapToGlobal(QPoint(50, 50)));
    QTRY_VERIFY(menu.isVisible());
    QTest::mouseRelease(&menu, Qt::LeftButton, Qt::NoModifier, QPoint(4, 4));
    menu.close();
    QVERIFY(!slider->isSliderDown());
    QVERIFY(session->canUndo());
    QCOMPARE(session->history.undoCount(), steps);
    // The next drag is its own step.
    QTest::mousePress(slider, Qt::LeftButton, Qt::NoModifier, QPoint(slider->width() - 4, slider->height() / 2));
    QVERIFY(!session->canUndo());
    const QPoint middle(slider->width() / 2, slider->height() / 2);
    QMouseEvent drag(QEvent::MouseMove, QPointF(middle), slider->mapToGlobal(middle), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(slider, &drag);
    QTest::mouseRelease(slider, Qt::LeftButton, Qt::NoModifier, middle);
    QVERIFY(session->canUndo());
    QCOMPARE(session->history.undoCount(), steps + 1);
    QVERIFY(session->activeLayer().value().opacity < 0.6);
}

void LayerListFocusTests::anotherButtonsReleaseLeavesTheSwipe()
{
    const auto session = sessionWithLayers(3);
    NativeLayerList list(*session);
    list.resize(252, 300);
    list.show();
    QVERIFY(QTest::qWaitForWindowActive(&list));
    auto *eye = list.cells().at(0)->findChild<EyeSwipeButton *>("layerEye");
    QTest::mousePress(eye, Qt::LeftButton, Qt::NoModifier, QPoint(10, 16));
    QTest::mousePress(eye, Qt::MiddleButton, Qt::NoModifier, QPoint(10, 16));
    QTest::mouseRelease(eye, Qt::MiddleButton, Qt::NoModifier, QPoint(10, 16));
    QVERIFY(!session->canUndo());
    // Still swiping: the next row takes the state.
    const QPoint next(10, 16 + LayerCell::rowHeight);
    QMouseEvent move(QEvent::MouseMove, QPointF(next), eye->mapToGlobal(next), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(eye, &move);
    QVERIFY(!layerWith(*session, list.cells().at(1)->layerID()).isVisible);
    QTest::mouseRelease(eye, Qt::LeftButton, Qt::NoModifier, next);
    QVERIFY(session->canUndo());
}

void LayerListFocusTests::altDoubleClickOnTheStripClipsTwiceAndAltShiftTogglesAMask()
{
    const auto session = sessionWithLayers(2);
    NativeLayerList list(*session);
    list.resize(252, 300);
    list.show();
    QVERIFY(QTest::qWaitForWindowActive(&list));
    LayerCell &top = *list.cells().at(0);
    const QPoint strip = top.mapTo(&list, QPoint(top.width() - 20, LayerCell::rowHeight - 4));
    const int steps = session->history.undoCount();
    QTest::mouseDClick(list.windowHandle(), Qt::LeftButton, Qt::AltModifier, strip);
    QVERIFY(!layerWith(*session, top.layerID()).maskSourceID.has_value());
    QVERIFY(!session->renamingLayerID().has_value());
    // Clipped and released again: two steps, no rename.
    QCOMPARE(session->history.undoCount(), steps + 2);
    session->undo();
    QCOMPARE(layerWith(*session, top.layerID()).maskSourceID, std::optional(list.cells().at(1)->layerID()));
    session->redo();
    // Alt+Shift on a mask chooses it and toggles it.
    session->selectLayers({top.layerID()}, top.layerID());
    session->addLayerMask();
    session->selectLayerTarget(top.layerID(), false);
    QTest::mouseClick(&top.maskThumbnail(), Qt::LeftButton, Qt::AltModifier | Qt::ShiftModifier, QPoint(5, 5));
    QVERIFY(session->isMaskSelected());
    QVERIFY(!session->activeLayer().value().mask.value().isEnabled);
}

void LayerListFocusTests::tabAndTheBrushKeysStayWithTheList()
{
    const auto session = sessionWithLayers(2);
    auto *panel = new QWidget;
    auto *list = new NativeLayerList(*session, panel);
    list->setGeometry(0, 0, 252, 200);
    // A field beside it could take the focus from Tab.
    (new QLineEdit(panel))->setGeometry(0, 210, 100, 20);
    Framed framed(panel);
    list->setFocus();
    QTRY_VERIFY(list->hasFocus());
    session->selectTool(NavigationTool::marquee);
    // Tab and Shift-Tab both switch the shape, as Swift's table.
    QTest::keyClick(list, Qt::Key_Tab);
    QCOMPARE(session->marqueeKind(), LassoKind::ellipse);
    QTest::keyClick(list, Qt::Key_Backtab, Qt::ShiftModifier);
    QCOMPARE(session->marqueeKind(), LassoKind::rectangle);
    QTest::keyClick(list, Qt::Key_Tab, Qt::ControlModifier);
    QCOMPARE(session->marqueeKind(), LassoKind::rectangle);
    QVERIFY(list->hasFocus());
    // E erases, B paints; Ctrl leaves the letter alone.
    QTest::keyClick(list, Qt::Key_E);
    QVERIFY(session->tool() == NavigationTool::brush && session->brushMode() == BrushToolMode::erase);
    QTest::keyClick(list, Qt::Key_B);
    QCOMPARE(session->brushMode(), BrushToolMode::paint);
    QTest::keyClick(list, Qt::Key_E, Qt::ControlModifier);
    QCOMPARE(session->brushMode(), BrushToolMode::paint);
    QTest::keyClick(list, Qt::Key_4);
    QCOMPARE(session->brushSettings().opacity, 0.4);
    // Where digits set nothing, the key goes to the parent.
    session->selectTool(NavigationTool::hand);
    QKeyEvent digit(QEvent::KeyPress, Qt::Key_5, Qt::NoModifier, QStringLiteral("5"));
    QApplication::sendEvent(list, &digit);
    QVERIFY(!digit.isAccepted());
}

QTEST_MAIN(LayerListFocusTests)
#include "LayerListFocusTests.moc"
