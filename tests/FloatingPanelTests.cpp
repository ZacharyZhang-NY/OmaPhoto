#include "CanvasFixtures.h"
#include "UI/ColorPickerSheet.h"
#include "UI/FilterSheet.h"
#include "UI/FloatingPanel.h"
#include "UI/HueSaturationSheet.h"
#include "UI/LevelsSheet.h"
#include <QApplication>
#include <QLabel>
#include <QVBoxLayout>
#include <QWindow>
#include <QtTest>

// Swift's FloatingPanelTests.
namespace {
QWidget *visiblePanel(const QString &name)
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() == name && widget->isVisible())
            return widget;
    }
    return nullptr;
}
}

class FloatingPanelTests : public QObject {
    Q_OBJECT
private slots:
    void colorPickerPanelSurvivesALayoutPass();
    void levelsPanelSurvivesALayoutPass();
    void adjustmentPanelSurvivesALayoutPass();
    void adjustmentEditorsUseMovableNonmodalPanels_data();
    void adjustmentEditorsUseMovableNonmodalPanels();
    void aPanelOpensOverTheCanvasThenWhereItWasLeft();
    void itsCloseButtonAndEscapeCancel();
    void aPanelRemembersWhereItIsMovedAndCentresWithoutACanvas();
    void dockedPlacementLeavesTheSavedFilterPosition();
};

void FloatingPanelTests::colorPickerPanelSurvivesALayoutPass()
{
    Shown shown(QSize(40, 20));
    QImage image(40, 20, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::red);
    shown.session.insert(ImportedImage(image, image, QStringLiteral("Red")));
    shown.session.openColorPicker(false);
    ColorPickerPanelController controller(shown.window);
    controller.show(shown.session.colorPicker().value(), shown.session);
    QTest::qWait(400);
    QWidget *panel = visiblePanel(ColorPickerPanelController::identifier());
    QVERIFY(panel);
    QVERIFY(panel->findChild<ColorPickerSheet *>());
    QVERIFY(!panel->isModal());
    QVERIFY(panel->windowFlags().testFlag(Qt::Tool));
    controller.close();
    shown.session.closeColorPicker(false);
    QVERIFY(!panel->isVisible());
}

void FloatingPanelTests::levelsPanelSurvivesALayoutPass()
{
    Shown shown(QSize(40, 20));
    QImage image(40, 20, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::red);
    shown.session.insert(ImportedImage(image, image, QStringLiteral("Red")));
    shown.session.beginLevels();
    FloatingPanel panel(QStringLiteral("testLevelsPanel"), shown.window);
    panel.show(QStringLiteral("Levels"), new LevelsSheet(shown.session));
    QTest::qWait(400);
    QVERIFY(panel.isVisible());
    // A preview landing while hosted survives a pass too.
    LevelsSettings settings;
    settings.setCurrent(LevelRange{0, 2, 255, 0, 255});
    shown.session.updateLevels(settings, true);
    QTRY_VERIFY(shown.session.levels().value().preparedPreview);
    QTest::qWait(400);
    panel.close();
    shown.session.cancelLevels();
    QVERIFY(!panel.isVisible() && !shown.session.levels());
}

void FloatingPanelTests::adjustmentPanelSurvivesALayoutPass()
{
    Shown shown(QSize(40, 20));
    QImage image(40, 20, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::red);
    shown.session.insert(ImportedImage(image, image, QStringLiteral("Red")));
    shown.session.beginHueSaturation();
    FloatingPanel panel(QStringLiteral("testAdjustmentPanel"), shown.window);
    panel.show(QStringLiteral("Hue/Saturation"), new HueSaturationSheet(shown.session));
    QTest::qWait(400);
    QVERIFY(panel.isVisible());
    const int height = visiblePanel(QStringLiteral("testAdjustmentPanel"))->height();
    // A range's spectrum grows it; a landed preview passes too.
    shown.session.updateHueSaturation(HueSaturationSettings(120, 0, 0, false, ColorRange::reds), true);
    QTRY_VERIFY(shown.session.hueSaturation().value().previewImage(shown.session.activeLayerID().value()));
    QTest::qWait(400);
    QVERIFY(visiblePanel(QStringLiteral("testAdjustmentPanel"))->height() > height);
    panel.close();
    shown.session.cancelHueSaturation();
    QVERIFY(!panel.isVisible() && !shown.session.hueSaturation());
}

void FloatingPanelTests::adjustmentEditorsUseMovableNonmodalPanels_data()
{
    QTest::addColumn<AdjustmentKind>("kind");
    // Invert has no settings, so no editor, as Swift's.
    for (const AdjustmentKind kind : allAdjustmentKinds)
        if (isEditable(kind))
            QTest::newRow(qPrintable(rawValue(kind))) << kind;
}

void FloatingPanelTests::adjustmentEditorsUseMovableNonmodalPanels()
{
    QFETCH(AdjustmentKind, kind);
    Shown shown(QSize(40, 20));
    QImage image(40, 20, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::red);
    shown.session.insert(ImportedImage(image, image, QStringLiteral("Red")));
    shown.session.addAdjustment(kind);
    bool opened = false;
    shown.session.beginAdjustmentEditing(shown.session.adjustmentEditingID().value(), [&opened] { opened = true; });
    QTRY_VERIFY(opened);
    FloatingPanel controller(QStringLiteral("testDynamicAdjustmentPanel"), shown.window);
    controller.onClose = [&shown] { shown.session.finishAdjustmentEditing(false); };
    if (kind == AdjustmentKind::levels)
        controller.show(QStringLiteral("Levels"), new LevelsSheet(shown.session));
    else if (kind == AdjustmentKind::hsv)
        controller.show(QStringLiteral("Hue/Saturation"), new HueSaturationSheet(shown.session));
    else
        controller.show(rawValue(kind), new FilterSheet(shown.session));
    QTest::qWait(400);
    QWidget *panel = visiblePanel(QStringLiteral("testDynamicAdjustmentPanel"));
    QVERIFY(panel);
    // A tool window beside the canvas: movable, never modal.
    QVERIFY(panel->windowFlags().testFlag(Qt::Tool) && !panel->isModal() && !shown.session.showsBusy());
    const QPoint origin = panel->pos();
    panel->move(origin + QPoint(20, 20));
    QCOMPARE(panel->pos(), origin + QPoint(20, 20));
    panel->close();
    QVERIFY(!shown.session.adjustmentEditingID());
    QVERIFY(!shown.session.levels() && !shown.session.hueSaturation() && !shown.session.filterEdit());
}

void FloatingPanelTests::aPanelOpensOverTheCanvasThenWhereItWasLeft()
{
    Shown shown;
    // Wider than the canvas: the canvas's middle, not the window's.
    shown.window.resize(600, 400);
    FloatingPanel panel(QStringLiteral("testPlacedPanel"), shown.window);
    QVERIFY(!panel.isVisible());
    panel.close();
    // First over the canvas's middle.
    panel.show(QStringLiteral("First"), new QLabel(QStringLiteral("One")));
    QWidget *window = visiblePanel(QStringLiteral("testPlacedPanel"));
    QVERIFY(window);
    const QPoint middle = shown.canvas->mapToGlobal(shown.canvas->rect().center());
    QVERIFY(std::abs(window->pos().x() + window->width() / 2 - middle.x()) <= 1);
    QVERIFY(std::abs(window->pos().y() + window->height() / 2 - middle.y()) <= 1);
    // It takes the keys, fixed to its content's size.
    QTRY_COMPARE(QApplication::activeWindow(), window);
    QCOMPARE(window->size(), window->findChild<QLabel *>()->sizeHint());
    QCOMPARE(window->minimumSize(), window->maximumSize());
    // Moved, it keeps its place through content and a close.
    const QPoint spot(30, 20);
    window->move(spot);
    // Shown again, it takes the keys back.
    shown.window.activateWindow();
    QTRY_COMPARE(QApplication::activeWindow(), &shown.window);
    auto *old = new QLabel(QStringLiteral("Old"));
    panel.show(QStringLiteral("Second"), old);
    QTRY_COMPARE(QApplication::activeWindow(), window);
    QCOMPARE(window->pos(), spot);
    QCOMPARE(window->windowTitle(), QString("Second"));
    QTRY_VERIFY(old->isVisible());
    QPointer<QLabel> gone(old);
    auto *wider = new QLabel(QStringLiteral("Wider content than before"));
    panel.show(QStringLiteral("Third"), wider);
    QVERIFY(gone && !gone->isVisible());
    QCOMPARE(window->size(), wider->sizeHint());
    QTRY_VERIFY(!gone);
    panel.close();
    QVERIFY(!panel.isVisible());
    panel.show(QStringLiteral("Fourth"), new QLabel(QStringLiteral("Four")));
    QCOMPARE(window->pos(), spot);
    // Another panel of that name, later, opens there too.
    panel.close();
    FloatingPanel again(QStringLiteral("testPlacedPanel"), shown.window);
    again.show(QStringLiteral("Again"), new QLabel(QStringLiteral("Five")));
    QCOMPARE(visiblePanel(QStringLiteral("testPlacedPanel"))->pos(), spot);
}

void FloatingPanelTests::itsCloseButtonAndEscapeCancel()
{
    Shown shown;
    // Without a listener, its close button just closes.
    FloatingPanel quiet(QStringLiteral("testQuietPanel"), shown.window);
    quiet.show(QStringLiteral("Quiet"), new QLabel(QStringLiteral("Content")));
    visiblePanel(QStringLiteral("testQuietPanel"))->close();
    QVERIFY(!quiet.isVisible());
    // A hidden panel is never handed the keys.
    shown.window.activateWindow();
    QTRY_COMPARE(QApplication::activeWindow(), &shown.window);
    FloatingPanel::refocus(QStringLiteral("testQuietPanel"));
    QTest::qWait(50);
    QCOMPARE(QApplication::activeWindow(), &shown.window);
    FloatingPanel panel(QStringLiteral("testClosedPanel"), shown.window);
    int closes = 0;
    panel.onClose = [&closes] { ++closes; };
    panel.show(QStringLiteral("Panel"), new QLabel(QStringLiteral("Content")));
    QWidget *window = visiblePanel(QStringLiteral("testClosedPanel"));
    QVERIFY(window);
    window->close();
    QCOMPARE(closes, 1);
    QVERIFY(!panel.isVisible());
    panel.show(QStringLiteral("Panel"), new QLabel(QStringLiteral("Content")));
    QTest::keyClick(window, Qt::Key_Escape);
    QCOMPARE(closes, 2);
    QVERIFY(!panel.isVisible());
    // Hidden by its owner, it reports nothing.
    panel.show(QStringLiteral("Panel"), new QLabel(QStringLiteral("Content")));
    panel.close();
    QCOMPARE(closes, 2);
    // A click on the canvas hands it the keys back.
    panel.show(QStringLiteral("Panel"), new QLabel(QStringLiteral("Content")));
    shown.window.activateWindow();
    QTRY_COMPARE(QApplication::activeWindow(), &shown.window);
    FloatingPanel::refocus(QStringLiteral("testClosedPanel"));
    QTRY_COMPARE(QApplication::activeWindow(), window);
    // Only the panel of that name.
    quiet.show(QStringLiteral("Quiet"), new QLabel(QStringLiteral("Content")));
    QWidget *other = visiblePanel(QStringLiteral("testQuietPanel"));
    QTRY_COMPARE(QApplication::activeWindow(), other);
    FloatingPanel::refocus(QStringLiteral("testClosedPanel"));
    QTRY_COMPARE(QApplication::activeWindow(), window);
    // A miss activates no widget; a bare window keeps focus.
    QWindow bare;
    bare.show();
    bare.requestActivate();
    QTRY_COMPARE(QGuiApplication::focusWindow(), &bare);
    FloatingPanel::refocus(QStringLiteral("testMissingPanel"));
    QTest::qWait(50);
    QCOMPARE(QGuiApplication::focusWindow(), &bare);
}

void FloatingPanelTests::aPanelRemembersWhereItIsMovedAndCentresWithoutACanvas()
{
    Shown shown;
    QPointer<QWidget> dropped;
    {
        FloatingPanel moved(QStringLiteral("testMovedPanel"), shown.window);
        moved.show(QStringLiteral("Moved"), new QLabel(QStringLiteral("Content")));
        dropped = visiblePanel(QStringLiteral("testMovedPanel"));
        dropped->move(QPoint(25, 35));
        QTRY_COMPARE(dropped->pos(), QPoint(25, 35));
    }
    // Its window goes with it.
    QVERIFY(!dropped);
    // Gone without a close, it reopens where it was moved.
    FloatingPanel again(QStringLiteral("testMovedPanel"), shown.window);
    again.show(QStringLiteral("Again"), new QLabel(QStringLiteral("Content")));
    QCOMPARE(visiblePanel(QStringLiteral("testMovedPanel"))->pos(), QPoint(25, 35));
    // Shown, never moved, it keeps that place for the next.
    QPoint first;
    {
        FloatingPanel still(QStringLiteral("testStillPanel"), shown.window);
        still.show(QStringLiteral("Still"), new QLabel(QStringLiteral("Content")));
        first = visiblePanel(QStringLiteral("testStillPanel"))->pos();
    }
    const QPoint away = shown.window.pos() + QPoint(40, 30);
    shown.window.move(away);
    QTRY_COMPARE(shown.window.pos(), away);
    FloatingPanel next(QStringLiteral("testStillPanel"), shown.window);
    next.show(QStringLiteral("Next"), new QLabel(QStringLiteral("Content")));
    QCOMPARE(visiblePanel(QStringLiteral("testStillPanel"))->pos(), first);
    // A window without a canvas centres it on itself.
    QWidget plain;
    plain.setGeometry(100, 80, 500, 300);
    plain.show();
    QVERIFY(QTest::qWaitForWindowExposed(&plain));
    FloatingPanel centred(QStringLiteral("testCentredPanel"), plain);
    centred.show(QStringLiteral("Centred"), new QLabel(QStringLiteral("Content")));
    QWidget *window = visiblePanel(QStringLiteral("testCentredPanel"));
    const QPoint middle = plain.geometry().center();
    QVERIFY(std::abs(window->pos().x() + window->width() / 2 - middle.x()) <= 1);
    QVERIFY(std::abs(window->pos().y() + window->height() / 2 - middle.y()) <= 1);
}

// Swift's test: a docked frame never sets where panels reopen.
void FloatingPanelTests::dockedPlacementLeavesTheSavedFilterPosition()
{
    Shown shown;
    auto *slot = new QWidget(&shown.window);
    auto *layout = new QVBoxLayout(slot);
    slot->setGeometry(250, 0, 150, 300);
    FloatingPanel panel(QStringLiteral("testDockedPanel"), shown.window);
    panel.show(QStringLiteral("Docked"), new QLabel(QStringLiteral("Docked")), slot);
    QVERIFY(!visiblePanel(QStringLiteral("testDockedPanel")) && panel.isVisible());
    // The docked panel moves inside its slot; nothing is kept.
    layout->setContentsMargins(20, 20, 0, 0);
    layout->activate();
    panel.close();
    QVERIFY(!slot->isVisible() && !panel.isVisible());
    panel.show(QStringLiteral("Floating"), new QLabel(QStringLiteral("Floating")));
    QWidget *window = visiblePanel(QStringLiteral("testDockedPanel"));
    const QPoint middle = shown.canvas->mapToGlobal(shown.canvas->rect().center());
    QVERIFY(std::abs(window->pos().x() + window->width() / 2 - middle.x()) <= 1);
}

QTEST_MAIN(FloatingPanelTests)
#include "FloatingPanelTests.moc"
