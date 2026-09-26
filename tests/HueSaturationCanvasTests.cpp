#include "MenuFixtures.h"
#include "SelectionCanvasFixtures.h"
#include "UI/FloatingPanel.h"
#include "UI/HueSaturationSheet.h"
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>

// Hue/Saturation in its panel and on the canvas.
namespace {
// Red left, green right, filling the view.
struct Halves : Canvas {
    Halves()
    {
        QImage image(400, 300, QImage::Format_RGBA8888_Premultiplied);
        image.fill(Qt::red);
        for (int y = 0; y < 300; ++y) {
            for (int x = 200; x < 400; ++x)
                image.setPixelColor(x, y, Qt::green);
        }
        session.insert(ImportedImage(image, image, QStringLiteral("Halves")));
    }
    QColor shown(QPoint at)
    {
        canvas->synchronizeDisplay();
        return canvas->grab().toImage().pixelColor(at);
    }
    const HueSaturationSettings &settings() const { return session.hueSaturation().value().settings; }
    RangeAdjustment adjustment(ColorRange range) const
    {
        return settings().adjustments.contains(range) ? settings().adjustments.at(range) : RangeAdjustment();
    }
};

QWidget *adjustmentPanel()
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() == QStringLiteral("adjustmentPanel") && widget->isVisible())
            return widget;
    }
    return nullptr;
}

// A window with one red layer, shown and active.
struct Editor : Bar {
    Editor()
    {
        window.show();
        if (!QTest::qWaitForWindowActive(&window))
            throw std::runtime_error("the window never became active");
        session().createDocument(4, 4, true);
        QImage red(4, 4, QImage::Format_RGBA8888_Premultiplied);
        red.fill(Qt::red);
        session().insert(ImportedImage(red, red, QStringLiteral("Red")));
    }
};
}

class HueSaturationCanvasTests : public QObject {
    Q_OBJECT
private slots:
    void thePreviewStandsInForTheLayer();
    void anEyedropperClickSetsTheBandAndHandsThePanelTheKeys();
    void colourPickingOutranksTheRangeEyedropperOnlyWhenAsked();
    void targetingDragsSaturationOrWithCtrlTheHue();
    void targetingTakesPressesAndDoubleClicksFirst();
    void thePanelFollowsTheEdit();
    void returnInAFieldClampsAndCommits();
    void returnFromAnyButtonGoesToOK();
};

void HueSaturationCanvasTests::thePreviewStandsInForTheLayer()
{
    Halves halves;
    halves.session.beginHueSaturation();
    halves.session.updateHueSaturation(HueSaturationSettings(120), true);
    QTRY_VERIFY(halves.session.hueSaturation().value().previewImage(halves.session.activeLayerID().value()));
    QCOMPARE(halves.shown(QPoint(100, 150)), QColor(Qt::green));
    QCOMPARE(halves.shown(QPoint(300, 150)), QColor(Qt::blue));
    // Off, the layer's own pixels show again.
    halves.session.updateHueSaturation(HueSaturationSettings(120), false);
    QCOMPARE(halves.shown(QPoint(100, 150)), QColor(Qt::red));
    halves.session.updateHueSaturation(HueSaturationSettings(120), true);
    QTRY_VERIFY(halves.session.hueSaturation().value().previewImage(halves.session.activeLayerID().value()));
    halves.session.cancelHueSaturation();
    QCOMPARE(halves.shown(QPoint(300, 150)), QColor(Qt::green));
}

void HueSaturationCanvasTests::anEyedropperClickSetsTheBandAndHandsThePanelTheKeys()
{
    Halves halves;
    halves.session.beginHueSaturation();
    halves.session.updateHueSaturation(HueSaturationSettings(0, 0, 0, false, ColorRange::yellows), false);
    FloatingPanel panel(QStringLiteral("adjustmentPanel"), halves.window);
    panel.show(QStringLiteral("Hue/Saturation"), new HueSaturationSheet(halves.session));
    QTRY_COMPARE(QApplication::activeWindow(), adjustmentPanel());
    const QCursor eyedropper = CanvasView::eyedropperCursor(halves.canvas->devicePixelRatio());
    QVERIFY(!halves.shows(eyedropper));
    halves.session.setHueSampleMode(HueSampleMode::replace);
    halves.canvas->synchronizeDisplay();
    QVERIFY(halves.shows(eyedropper));
    halves.window.activateWindow();
    QTRY_COMPARE(QApplication::activeWindow(), &halves.window);
    // Green: Yellows centred on 120.
    halves.click(QPointF(300, 150));
    QCOMPARE(halves.settings().band(), (HueBand{75, 105, 135, 165}));
    QTRY_COMPARE(QApplication::activeWindow(), adjustmentPanel());
    // With Space held the press pans instead.
    const QSizeF pan = halves.session.viewport.pan;
    QTest::keyPress(halves.canvas, Qt::Key_Space);
    halves.drag(QPointF(100, 150), QPointF(110, 160));
    QTest::keyRelease(halves.canvas, Qt::Key_Space);
    QCOMPARE(halves.session.viewport.pan, pan + QSizeF(10, 10));
    QCOMPARE(halves.settings().band(), (HueBand{75, 105, 135, 165}));
    // Put away, the tool's own cursor returns.
    halves.session.setHueSampleMode(std::nullopt);
    halves.canvas->synchronizeDisplay();
    QVERIFY(!halves.shows(eyedropper));
    halves.session.cancelHueSaturation();
}

void HueSaturationCanvasTests::colourPickingOutranksTheRangeEyedropperOnlyWhenAsked()
{
    Halves halves;
    halves.session.selectTool(NavigationTool::eyedropper);
    halves.session.beginHueSaturation();
    halves.session.updateHueSaturation(HueSaturationSettings(0, 0, 0, false, ColorRange::yellows), false);
    // Unarmed, the Eyedropper takes the colour.
    halves.click(QPointF(300, 150));
    QCOMPARE(halves.session.foregroundColor().hex(), QString("00FF00"));
    QCOMPARE(halves.settings().band(), defaultBand(ColorRange::yellows));
    // Armed, the range's eyedropper takes the click.
    halves.session.setHueSampleMode(HueSampleMode::replace);
    halves.click(QPointF(100, 150));
    QCOMPARE(halves.session.foregroundColor().hex(), QString("00FF00"));
    QCOMPARE(halves.settings().band(), (HueBand{315, 345, 15, 45}));
    // An open colour picker outranks both.
    halves.session.openColorPicker(false);
    halves.click(QPointF(300, 150));
    QCOMPARE(halves.session.colorPicker().value().color().hex(), QString("00FF00"));
    QCOMPARE(halves.settings().band(), (HueBand{315, 345, 15, 45}));
    halves.session.closeColorPicker(false);
    // Armed targeting leaves a colour drag its sampling.
    halves.session.setHueSampleMode(std::nullopt);
    halves.session.setHueTargeting(true);
    halves.canvas->synchronizeDisplay();
    halves.press(QPointF(100, 150));
    halves.move(QPointF(300, 150));
    QCOMPARE(halves.session.foregroundColor().hex(), QString("00FF00"));
    halves.release(QPointF(300, 150));
    halves.session.cancelHueSaturation();
}

void HueSaturationCanvasTests::targetingDragsSaturationOrWithCtrlTheHue()
{
    Halves halves;
    halves.session.beginHueSaturation();
    halves.session.setHueTargeting(true);
    halves.canvas->synchronizeDisplay();
    QCOMPARE(halves.canvas->cursor().shape(), Qt::SizeHorCursor);
    // Red picks Reds; right raises a unit per two points.
    halves.press(QPointF(100, 150));
    QCOMPARE(halves.settings().range, ColorRange::reds);
    halves.move(QPointF(140, 150));
    QCOMPARE(halves.adjustment(ColorRange::reds), (RangeAdjustment{0, 20, 0}));
    // Ctrl drags the hue, from the drag's start.
    halves.move(QPointF(160, 150), Qt::ControlModifier);
    QCOMPARE(halves.adjustment(ColorRange::reds), (RangeAdjustment{30, 20, 0}));
    halves.release(QPointF(160, 150));
    QCOMPARE(halves.canvas->cursor().shape(), Qt::SizeHorCursor);
    // Alt mid-drag shows the Brush's eyedropper till the next drag.
    halves.session.selectTool(NavigationTool::brush);
    halves.canvas->synchronizeDisplay();
    halves.press(QPointF(100, 150));
    QTest::keyPress(halves.canvas, Qt::Key_Alt);
    QVERIFY(halves.shows(CanvasView::eyedropperCursor(halves.canvas->devicePixelRatio())));
    halves.move(QPointF(120, 150), Qt::AltModifier);
    QCOMPARE(halves.canvas->cursor().shape(), Qt::SizeHorCursor);
    QCOMPARE(halves.adjustment(ColorRange::reds), (RangeAdjustment{30, 30, 0}));
    halves.release(QPointF(120, 150), Qt::AltModifier);
    QTest::keyRelease(halves.canvas, Qt::Key_Alt);
    halves.session.selectTool(NavigationTool::marquee);
    // Ended, moves change nothing.
    halves.move(QPointF(300, 150));
    QCOMPARE(halves.adjustment(ColorRange::reds), (RangeAdjustment{30, 30, 0}));
    // A buttonless move ends a drag whose release was lost.
    halves.press(QPointF(300, 150));
    QCOMPARE(halves.settings().range, ColorRange::greens);
    halves.hover(QPointF(320, 150));
    halves.move(QPointF(360, 150));
    QCOMPARE(halves.adjustment(ColorRange::greens), RangeAdjustment());
    halves.release(QPointF(360, 150));
    // With Space held the press pans; Colorize refuses.
    const QSizeF pan = halves.session.viewport.pan;
    QTest::keyPress(halves.canvas, Qt::Key_Space);
    halves.drag(QPointF(100, 150), QPointF(90, 150));
    QTest::keyRelease(halves.canvas, Qt::Key_Space);
    QCOMPARE(halves.session.viewport.pan, pan + QSizeF(-10, 0));
    QCOMPARE(halves.settings().range, ColorRange::greens);
    halves.session.updateHueSaturation(HueSaturationSettings::colorizeStart(), false);
    halves.drag(QPointF(100, 150), QPointF(140, 150));
    QVERIFY(halves.settings() == HueSaturationSettings::colorizeStart());
    // Put away, the tool's cursor returns.
    halves.session.setHueTargeting(false);
    halves.canvas->synchronizeDisplay();
    QVERIFY(halves.canvas->cursor().shape() != Qt::SizeHorCursor);
    halves.session.cancelHueSaturation();
}

void HueSaturationCanvasTests::targetingTakesPressesAndDoubleClicksFirst()
{
    Halves halves;
    // Refused under Colorize, a press still reaches no tool.
    halves.session.selectTool(NavigationTool::zoom);
    halves.session.beginHueSaturation();
    halves.session.updateHueSaturation(HueSaturationSettings::colorizeStart(), false);
    halves.session.setHueTargeting(true);
    halves.canvas->synchronizeDisplay();
    halves.click(QPointF(100, 150));
    QCOMPARE(halves.session.viewport.zoom(), 1.0);
    halves.session.cancelHueSaturation();
    // A polygonal outline begun before stays under a double click.
    halves.session.selectTool(NavigationTool::lasso);
    halves.session.setLassoKind(LassoKind::polygonal);
    halves.canvas->synchronizeDisplay();
    halves.click(QPointF(20, 20));
    halves.click(QPointF(120, 20));
    QVERIFY(halves.session.lassoDraft());
    halves.session.beginHueSaturation();
    halves.session.setHueTargeting(true);
    QTest::mouseDClick(halves.canvas, Qt::LeftButton, Qt::NoModifier, QPoint(100, 150));
    QCOMPARE(halves.settings().range, ColorRange::reds);
    QVERIFY(halves.session.lassoDraft());
    halves.release(QPointF(100, 150));
    halves.session.cancelHueSaturation();
    halves.session.cancelLasso();
    // Open text: the double click targets, not its words.
    halves.session.selectTool(NavigationTool::type);
    halves.session.beginText(QPointF(300, 50), true);
    QVERIFY(halves.session.textDraft());
    halves.session.beginHueSaturation();
    halves.session.setHueTargeting(true);
    halves.canvas->synchronizeDisplay();
    QTest::mouseDClick(halves.canvas, Qt::LeftButton, Qt::NoModifier, QPoint(300, 150));
    QCOMPARE(halves.settings().range, ColorRange::greens);
    halves.release(QPointF(300, 150));
    halves.session.cancelHueSaturation();
}

void HueSaturationCanvasTests::thePanelFollowsTheEdit()
{
    Editor editor;
    QVERIFY(!adjustmentPanel());
    editor.session().beginHueSaturation();
    QWidget *panel = adjustmentPanel();
    QVERIFY(panel);
    QCOMPARE(panel->windowTitle(), QString("Hue/Saturation"));
    HueSaturationSheet *sheet = panel->findChild<HueSaturationSheet *>();
    QVERIFY(sheet->isVisible());
    // A change keeps the sheet; a new edit makes one.
    editor.session().updateHueSaturation(HueSaturationSettings(90), false);
    QVERIFY(sheet->isVisible());
    editor.session().cancelHueSaturation();
    QVERIFY(!panel->isVisible());
    editor.session().beginHueSaturation();
    QVERIFY(panel->isVisible());
    QTRY_COMPARE(panel->findChildren<HueSaturationSheet *>().size(), 1);
    // Its close button and Escape cancel.
    panel->close();
    QVERIFY(!editor.session().hueSaturation());
    editor.session().beginHueSaturation();
    QTRY_COMPARE(QApplication::activeWindow(), panel);
    QTest::keyClick(panel, Qt::Key_Escape);
    QVERIFY(!editor.session().hueSaturation() && !panel->isVisible());
    // Escape over typing: the field lets go after the edit.
    editor.session().beginHueSaturation();
    QTRY_COMPARE(QApplication::activeWindow(), panel);
    QLineEdit &hue = *panel->findChild<QLineEdit *>(QStringLiteral("hueField"));
    hue.setFocus();
    QTRY_VERIFY(hue.hasFocus());
    QTest::keyClicks(&hue, QStringLiteral("5"));
    QTest::keyClick(&hue, Qt::Key_Escape);
    QTRY_VERIFY(!panel->isVisible());
    QVERIFY(!editor.session().hueSaturation());
}

void HueSaturationCanvasTests::returnInAFieldClampsAndCommits()
{
    Editor editor;
    editor.session().beginHueSaturation();
    QWidget *panel = adjustmentPanel();
    QTRY_COMPARE(QApplication::activeWindow(), panel);
    QLineEdit &lightness = *panel->findChild<QLineEdit *>(QStringLiteral("lightnessField"));
    lightness.setFocus();
    QTRY_VERIFY(lightness.hasFocus());
    lightness.selectAll();
    QTest::keyClicks(&lightness, QStringLiteral("-500"));
    QTest::keyClick(&lightness, Qt::Key_Return);
    QTRY_VERIFY(!editor.session().hueSaturation());
    QCOMPARE(editor.session().history.undoName(), QString("Hue/Saturation"));
    QCOMPARE(editor.session().activeLayer().value().asset.value().image().pixelColor(0, 0), QColor(Qt::black));
    QVERIFY(!panel->isVisible());
}

void HueSaturationCanvasTests::returnFromAnyButtonGoesToOK()
{
    Editor editor;
    // Swift's default action: no other control takes Return.
    for (const char *text : {"Reset", "Cancel", "Colorize", "Preview"}) {
        // Window managers hand the keys back once a panel closes.
        editor.window.activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &editor.window);
        editor.session().beginHueSaturation();
        editor.session().updateHueSaturation(HueSaturationSettings(120), false);
        QWidget *panel = adjustmentPanel();
        QTRY_COMPARE(QApplication::activeWindow(), panel);
        QAbstractButton *button = nullptr;
        for (QAbstractButton *found : panel->findChildren<QAbstractButton *>()) {
            if (found->text() == QString::fromUtf8(text) && found->isVisible())
                button = found;
        }
        QTRY_VERIFY(button && button->isEnabled());
        button->setFocus();
        QTRY_VERIFY(button->hasFocus());
        const int steps = editor.session().history.undoCount();
        QTest::keyClick(button, Qt::Key_Return);
        QTRY_VERIFY2(!editor.session().hueSaturation(), text);
        QCOMPARE(editor.session().history.undoCount(), steps + 1);
    }
}

QTEST_MAIN(HueSaturationCanvasTests)
#include "HueSaturationCanvasTests.moc"
