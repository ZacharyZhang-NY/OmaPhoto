#include "MenuFixtures.h"
#include "SelectionCanvasFixtures.h"
#include "UI/FloatingPanel.h"
#include "UI/LevelsSheet.h"
#include <QLineEdit>
#include <QPushButton>

// Levels in its panel and on the canvas.
namespace {
LevelsSettings inverted()
{
    LevelsSettings settings;
    settings.setCurrent(LevelRange{0, 1, 255, 255, 0});
    return settings;
}

// Gray 100 left, gray 200 right, filling the view.
struct Split : Canvas {
    Split()
    {
        QImage image(400, 300, QImage::Format_RGBA8888_Premultiplied);
        image.fill(QColor(100, 100, 100));
        for (int y = 0; y < 300; ++y) {
            for (int x = 200; x < 400; ++x)
                image.setPixelColor(x, y, QColor(200, 200, 200));
        }
        session.insert(ImportedImage(image, image, QStringLiteral("Split")));
    }
    void tool(NavigationTool tool)
    {
        session.selectTool(tool);
        canvas->synchronizeDisplay();
    }
    QColor shown(QPoint at)
    {
        canvas->synchronizeDisplay();
        return canvas->grab().toImage().pixelColor(at);
    }
};

QWidget *levelsPanel()
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() == QStringLiteral("levelsPanel") && widget->isVisible())
            return widget;
    }
    return nullptr;
}

// A window with one gray 64 layer, shown and active.
struct Editor : Bar {
    Editor()
    {
        window.show();
        if (!QTest::qWaitForWindowActive(&window))
            throw std::runtime_error("the window never became active");
        session().createDocument(4, 4, true);
        QImage gray(4, 4, QImage::Format_RGBA8888_Premultiplied);
        gray.fill(QColor(64, 64, 64));
        session().insert(ImportedImage(gray, gray, QStringLiteral("Gray")));
    }
};
}

class LevelsCanvasTests : public QObject {
    Q_OBJECT
private slots:
    void thePreviewStandsInForTheLayer();
    void aSampleClickSetsItsPointAndHandsThePanelTheKeys();
    void pressesWaitForSpaceTheHandAndTheZoom();
    void theCanvasKeysWhileLevelsIsOpen();
    void bracketsRestWhileLevelsIsOpen();
    void thePanelFollowsTheEdit();
    void returnInAnEntryGoesOnToOK();
    void returnFromAnyButtonGoesToOK();
};

void LevelsCanvasTests::thePreviewStandsInForTheLayer()
{
    Split split;
    split.session.beginLevels();
    split.session.updateLevels(inverted(), true);
    QTRY_VERIFY(split.session.levels().value().preparedPreview);
    QCOMPARE(split.shown(QPoint(100, 150)), QColor(155, 155, 155));
    QCOMPARE(split.shown(QPoint(300, 150)), QColor(55, 55, 55));
    // Off, the layer's own pixels show again.
    split.session.updateLevels(inverted(), false);
    QCOMPARE(split.shown(QPoint(100, 150)), QColor(100, 100, 100));
    split.session.updateLevels(inverted(), true);
    QTRY_VERIFY(split.session.levels().value().preparedPreview);
    split.session.cancelLevels();
    QCOMPARE(split.shown(QPoint(300, 150)), QColor(200, 200, 200));
}

void LevelsCanvasTests::aSampleClickSetsItsPointAndHandsThePanelTheKeys()
{
    Split split;
    split.session.beginLevels();
    FloatingPanel panel(QStringLiteral("levelsPanel"), split.window);
    panel.show(QStringLiteral("Levels"), new LevelsSheet(split.session));
    QTRY_COMPARE(QApplication::activeWindow(), levelsPanel());
    // Choosing an eyedropper puts it on the canvas.
    QVERIFY(!split.shows(CanvasView::eyedropperCursor(split.canvas->devicePixelRatio())));
    split.session.setLevelsSampleMode(LevelsSample::white);
    split.canvas->synchronizeDisplay();
    QVERIFY(split.shows(CanvasView::eyedropperCursor(split.canvas->devicePixelRatio())));
    split.window.activateWindow();
    QTRY_COMPARE(QApplication::activeWindow(), &split.window);
    split.click(QPointF(300, 150));
    const LevelsSettings sampled = split.session.levels().value().settings;
    for (size_t channel = 1; channel < 4; ++channel)
        QCOMPARE(sampled.ranges[channel].white, 200.0);
    QVERIFY(!split.session.lassoDraft() && !split.session.selection());
    QTRY_COMPARE(QApplication::activeWindow(), levelsPanel());
    // With Space held the press pans instead.
    const QSizeF pan = split.session.viewport.pan;
    QTest::keyPress(split.canvas, Qt::Key_Space);
    split.drag(QPointF(100, 150), QPointF(110, 160));
    QTest::keyRelease(split.canvas, Qt::Key_Space);
    QCOMPARE(split.session.viewport.pan, pan + QSizeF(10, 10));
    QCOMPARE(split.session.levels().value().settings, sampled);
    // Put away, the canvas's own cursor returns.
    split.session.setLevelsSampleMode(std::nullopt);
    split.canvas->synchronizeDisplay();
    QVERIFY(!split.shows(CanvasView::eyedropperCursor(split.canvas->devicePixelRatio())));
    split.session.cancelLevels();
}

void LevelsCanvasTests::pressesWaitForSpaceTheHandAndTheZoom()
{
    Split split;
    // Tools are chosen first: an open Levels keeps the tool.
    split.session.beginLevels();
    split.drag(QPointF(50, 50), QPointF(150, 150));
    QVERIFY(!split.session.lassoDraft() && !split.session.selection());
    split.session.cancelLevels();
    split.tool(NavigationTool::brush);
    split.session.beginLevels();
    split.press(QPointF(100, 100));
    QVERIFY(!split.session.brushStroke());
    split.release(QPointF(100, 100));
    split.session.cancelLevels();
    split.tool(NavigationTool::eyedropper);
    split.session.beginLevels();
    const QString foreground = split.session.foregroundColor().hex();
    split.click(QPointF(300, 150));
    QCOMPARE(split.session.foregroundColor().hex(), foreground);
    split.session.cancelLevels();
    // The Hand pans and the Zoom zooms, as ever.
    split.tool(NavigationTool::hand);
    split.session.beginLevels();
    const QSizeF pan = split.session.viewport.pan;
    split.drag(QPointF(100, 100), QPointF(120, 90));
    QCOMPARE(split.session.viewport.pan, pan + QSizeF(20, -10));
    // A sample press samples alone, even with the Hand.
    split.session.setLevelsSampleMode(LevelsSample::black);
    const QString palette = split.session.foregroundColor().hex();
    split.drag(QPointF(300, 150), QPointF(320, 150));
    QCOMPARE(split.session.foregroundColor().hex(), palette);
    QCOMPARE(split.session.viewport.pan, pan + QSizeF(20, -10));
    QCOMPARE(split.session.levels().value().settings.ranges[1].black, 200.0);
    split.session.cancelLevels();
    split.tool(NavigationTool::zoom);
    split.session.beginLevels();
    split.click(QPointF(200, 150));
    QVERIFY(split.session.viewport.zoom() > 1);
    split.session.cancelLevels();
}

void LevelsCanvasTests::theCanvasKeysWhileLevelsIsOpen()
{
    Split split;
    split.canvas->setFocus();
    QTRY_VERIFY(split.canvas->hasFocus());
    split.session.beginLevels();
    QTest::keyClick(split.canvas, Qt::Key_P, Qt::AltModifier);
    QVERIFY(!split.session.levels().value().preview);
    QTest::keyClick(split.canvas, Qt::Key_P, Qt::AltModifier);
    QVERIFY(split.session.levels().value().preview);
    QTest::keyClick(split.canvas, Qt::Key_P);
    QVERIFY(split.session.levels().value().preview);
    // Other keys pass on: B chooses no brush.
    QTest::keyClick(split.canvas, Qt::Key_B);
    QTest::keyClick(split.canvas, Qt::Key_Tab);
    QCOMPARE(split.session.tool(), NavigationTool::marquee);
    QCOMPARE(split.session.marqueeKind(), LassoKind::rectangle);
    QTest::keyClick(split.canvas, Qt::Key_Escape);
    QVERIFY(!split.session.levels());
    // Return and Enter apply.
    for (const Qt::Key key : {Qt::Key_Return, Qt::Key_Enter}) {
        split.session.beginLevels();
        split.session.updateLevels(inverted(), false);
        const int steps = split.session.history.undoCount();
        QTest::keyClick(split.canvas, key);
        QTRY_VERIFY(!split.session.levels());
        QCOMPARE(split.session.history.undoCount(), steps + 1);
        QCOMPARE(split.session.history.undoName(), QString("Levels"));
    }
    QCOMPARE(split.shown(QPoint(100, 150)), QColor(100, 100, 100));
}

void LevelsCanvasTests::bracketsRestWhileLevelsIsOpen()
{
    Split split;
    split.tool(NavigationTool::brush);
    auto *other = new QPushButton(QStringLiteral("Other"), &split.window);
    other->show();
    other->setFocus();
    QTRY_VERIFY(other->hasFocus());
    const double diameter = split.session.brushSettings().diameter;
    split.session.beginLevels();
    QTest::keyClick(other, Qt::Key_BracketRight);
    split.canvas->setFocus();
    QTest::keyClick(split.canvas, Qt::Key_BracketRight);
    QCOMPARE(split.session.brushSettings().diameter, diameter);
    split.session.cancelLevels();
    other->setFocus();
    QTest::keyClick(other, Qt::Key_BracketRight);
    QVERIFY(split.session.brushSettings().diameter > diameter);
}

void LevelsCanvasTests::thePanelFollowsTheEdit()
{
    Editor editor;
    QVERIFY(!levelsPanel());
    editor.session().beginLevels();
    QWidget *panel = levelsPanel();
    QVERIFY(panel);
    QCOMPARE(panel->windowTitle(), QString("Levels"));
    LevelsSheet *sheet = panel->findChild<LevelsSheet *>();
    QVERIFY(sheet->isVisible());
    // A change keeps the sheet; a new edit makes one.
    editor.session().updateLevels(inverted(), false);
    QVERIFY(sheet->isVisible());
    editor.session().cancelLevels();
    QVERIFY(!panel->isVisible());
    // Again with a fresh sheet; the old one goes.
    editor.session().beginLevels();
    QVERIFY(panel->isVisible());
    QTRY_COMPARE(panel->findChildren<LevelsSheet *>().size(), 1);
    // Its close button and Escape cancel.
    panel->close();
    QVERIFY(!editor.session().levels());
    editor.session().beginLevels();
    QTRY_COMPARE(QApplication::activeWindow(), panel);
    QTest::keyClick(panel, Qt::Key_Escape);
    QVERIFY(!editor.session().levels() && !panel->isVisible());
}

void LevelsCanvasTests::returnInAnEntryGoesOnToOK()
{
    Editor editor;
    editor.session().beginLevels();
    QWidget *panel = levelsPanel();
    QTRY_COMPARE(QApplication::activeWindow(), panel);
    QLineEdit &black = *panel->findChild<QLineEdit *>(QStringLiteral("levelsInputblack"));
    black.setFocus();
    QTRY_VERIFY(black.hasFocus());
    black.selectAll();
    QTest::keyClicks(&black, QStringLiteral("128"));
    QTest::keyClick(&black, Qt::Key_Return);
    QTRY_VERIFY(!editor.session().levels());
    QCOMPARE(editor.session().history.undoName(), QString("Levels"));
    QCOMPARE(editor.session().activeLayer().value().asset.value().image().pixelColor(0, 0), QColor(Qt::black));
    QVERIFY(!panel->isVisible());
}

void LevelsCanvasTests::returnFromAnyButtonGoesToOK()
{
    Editor editor;
    // Swift's default action: no other button takes Return.
    for (const char *text : {"Black", "Gray", "White", "Contrast", "Color", "Color + neutral midtones", "Reset", "Cancel"}) {
        // Window managers hand the keys back once a panel closes.
        editor.window.activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &editor.window);
        editor.session().beginLevels();
        editor.session().updateLevels(inverted(), false);
        QWidget *panel = levelsPanel();
        QTRY_COMPARE(QApplication::activeWindow(), panel);
        QPushButton *button = nullptr;
        for (QPushButton *found : panel->findChildren<QPushButton *>()) {
            if (found->text() == QString::fromUtf8(text) && found->isVisible())
                button = found;
        }
        QTRY_VERIFY(button && button->isEnabled());
        button->setFocus();
        QTRY_VERIFY(button->hasFocus());
        const int steps = editor.session().history.undoCount();
        QTest::keyClick(button, Qt::Key_Return);
        QTRY_VERIFY2(!editor.session().levels(), text);
        QCOMPARE(editor.session().history.undoCount(), steps + 1);
    }
}

QTEST_MAIN(LevelsCanvasTests)
#include "LevelsCanvasTests.moc"
