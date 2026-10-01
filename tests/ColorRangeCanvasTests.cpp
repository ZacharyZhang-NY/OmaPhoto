#include "AcceptanceFixtures.h"
#include "SelectionCanvasFixtures.h"
#include "UI/ColorPickerSheet.h"
#include "UI/ColorRangeSheet.h"
#include "UI/SampleButton.h"
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>

// Swift 1.3.4's Color Range: canvas and panel.
namespace {
// Red left, blue right, filling the canvas.
struct Ranged : Canvas {
    Ranged()
    {
        QImage image(400, 300, QImage::Format_RGBA8888_Premultiplied);
        image.fill(QColor(200, 30, 30));
        for (int y = 0; y < 300; ++y)
            for (int x = 200; x < 400; ++x)
                image.setPixelColor(x, y, QColor(30, 40, 210));
        session.insert(ImportedImage(image, image, QStringLiteral("Halves")));
        session.beginColorRange();
        canvas->synchronizeDisplay();
    }
    const ColorRangeEdit &edit() const { return session.colorRange().value(); }
};

QWidget *panel()
{
    for (QWidget *widget : QApplication::topLevelWidgets())
        if (widget->objectName() == QStringLiteral("colorRangePanel") && widget->isVisible())
            return widget;
    return nullptr;
}
}

class ColorRangeCanvasTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void aClickTakesTheColourShiftAddsAltTakesAway();
    void theEyedropperSaysWhatAClickWillDo();
    void theSheetDrivesTheEdit();
    void thePanelFollowsTheEditAndTheMenu();
};

void ColorRangeCanvasTests::aClickTakesTheColourShiftAddsAltTakesAway()
{
    Ranged shown;
    shown.click(QPointF(50, 50));
    QCOMPARE(shown.edit().include, (std::vector<uchar>{200, 30, 30}));
    shown.click(QPointF(300, 50), Qt::ShiftModifier);
    QCOMPARE(shown.edit().include, (std::vector<uchar>{200, 30, 30, 30, 40, 210}));
    shown.click(QPointF(50, 50), Qt::AltModifier);
    QCOMPARE(shown.edit().exclude, (std::vector<uchar>{200, 30, 30}));
    // Alt with Shift takes away too, as Swift's order reads.
    shown.click(QPointF(300, 50), Qt::AltModifier | Qt::ShiftModifier);
    QCOMPARE(shown.edit().exclude, (std::vector<uchar>{200, 30, 30, 30, 40, 210}));
    // With Space held the press pans instead.
    const QSizeF pan = shown.session.viewport.pan;
    QTest::keyPress(shown.canvas, Qt::Key_Space);
    shown.drag(QPointF(100, 100), QPointF(110, 120));
    QTest::keyRelease(shown.canvas, Qt::Key_Space);
    QCOMPARE(shown.session.viewport.pan, pan + QSizeF(10, 20));
    QCOMPARE(shown.edit().exclude.size(), size_t(6));
    // The match lands on the canvas as the selection.
    shown.click(QPointF(300, 50));
    QTRY_VERIFY(shown.session.selection() && shown.coverage(300, 50) == 255);
    QCOMPARE(shown.coverage(50, 50), 0);
    // Under the Eyedropper the click is the range's alone.
    shown.session.cancelColorRange();
    shown.session.selectTool(NavigationTool::eyedropper);
    shown.session.beginColorRange();
    const PaletteColor foreground = shown.session.foregroundColor();
    shown.click(QPointF(50, 50));
    QVERIFY(shown.session.foregroundColor() == foreground && shown.edit().include == (std::vector<uchar>{200, 30, 30}));
}

void ColorRangeCanvasTests::theEyedropperSaysWhatAClickWillDo()
{
    Ranged shown;
    const double ratio = shown.canvas->devicePixelRatio();
    const QCursor plain = CanvasView::eyedropperCursor(ratio), add = CanvasView::eyedropperCursor(ratio, HueSampleMode::add),
                  remove = CanvasView::eyedropperCursor(ratio, HueSampleMode::remove);
    QVERIFY(shown.shows(plain));
    // A disc bottom right: white, a black sign across.
    const QImage badged = image(add);
    const auto gray = [ratio](const QCursor &cursor, QPoint at) { return qGray(image(cursor).pixel(at * ratio)); };
    QVERIFY(gray(add, QPoint(20, 16)) > 230 && gray(add, QPoint(18, 18)) < 80 && gray(remove, QPoint(18, 18)) < 80);
    // A plus has its upright; a minus has none.
    QVERIFY(gray(add, QPoint(18, 15)) < 80 && gray(remove, QPoint(18, 15)) > 230);
    // The bar spans the disc; a black rim rings it.
    QVERIFY(gray(add, QPoint(16, 18)) < 80 && gray(remove, QPoint(20, 18)) < 80 && gray(remove, QPoint(18, 23)) < 80);
    QCOMPARE(image(plain).pixelColor(QPoint(20, 16) * ratio).alpha(), 0);
    // Held keys show it; the chosen eyedropper too.
    QTest::keyPress(shown.canvas, Qt::Key_Shift);
    QCOMPARE(shown.edit().held, std::optional(HueSampleMode::add));
    QVERIFY(shown.shows(add));
    QTest::keyPress(shown.canvas, Qt::Key_Alt, Qt::ShiftModifier);
    QCOMPARE(shown.edit().held, std::optional(HueSampleMode::remove));
    QVERIFY(shown.shows(remove));
    // QTest would let go of Shift too: Alt alone goes.
    QKeyEvent alt(QEvent::KeyRelease, Qt::Key_Alt, Qt::ShiftModifier);
    QApplication::sendEvent(shown.canvas, &alt);
    QCOMPARE(shown.edit().held, std::optional(HueSampleMode::add));
    QTest::keyRelease(shown.canvas, Qt::Key_Shift);
    QCOMPARE(shown.edit().held, std::optional<HueSampleMode>());
    QVERIFY(shown.shows(plain));
    shown.session.setColorRangeSampleMode(HueSampleMode::remove);
    shown.canvas->synchronizeDisplay();
    QVERIFY(shown.shows(remove));
    // Closed, the tool's own cursor returns.
    shown.session.cancelColorRange();
    shown.canvas->synchronizeDisplay();
    QVERIFY(!shown.shows(plain) && !shown.shows(remove));
}

void ColorRangeCanvasTests::theSheetDrivesTheEdit()
{
    Ranged shown;
    ColorRangeSheet sheet(shown.session);
    sheet.show();
    QVERIFY(QTest::qWaitForWindowExposed(&sheet));
    const auto dropper = [&sheet](const char *mode) { return sheet.findChild<SampleButton *>(QStringLiteral("colorRange") + QString::fromLatin1(mode)); };
    QVERIFY(dropper("Sample")->isChecked() && !dropper("Add")->isChecked());
    QCOMPARE(dropper("Add")->toolTip(), QStringLiteral("Click the image to add that color to the selection"));
    QCOMPARE(dropper("Remove")->accessibleName(), QStringLiteral("Remove color"));
    dropper("Add")->click();
    QVERIFY(shown.edit().sampleMode == HueSampleMode::add && dropper("Add")->isChecked() && !dropper("Sample")->isChecked());
    // Held keys light the eyedropper a click will use.
    shown.session.setColorRangeHeld(HueSampleMode::remove);
    QVERIFY(dropper("Remove")->isChecked() && !dropper("Add")->isChecked());
    shown.session.setColorRangeHeld(std::nullopt);
    QLabel *caption = sheet.findChild<QLabel *>(QStringLiteral("colorRangeCaption"));
    QCOMPARE(caption->text(), QStringLiteral("Click the image to pick the color to select."));
    shown.session.sampleColorRange(QPointF(50, 50), false, false);
    QCOMPARE(caption->text(), QStringLiteral("Shift-click adds a color, Alt-click takes one away."));
    // Fuzziness: the slider, the field, the title's scrub.
    QSlider *slider = sheet.findChild<QSlider *>(QStringLiteral("fuzzinessSlider"));
    QVERIFY(slider->minimum() == 0 && slider->maximum() == 200 && slider->value() == 40);
    slider->setValue(120);
    QCOMPARE(shown.edit().fuzziness, 120.0);
    PickerField *field = sheet.findChild<PickerField *>(QStringLiteral("fuzzinessField"));
    QCOMPARE(field->text(), QStringLiteral("120"));
    field->setFocus();
    field->selectAll();
    QTest::keyClicks(field, QStringLiteral("33.6"));
    QTest::keyClick(field, Qt::Key_Return);
    QVERIFY(shown.edit().fuzziness == 34 && slider->value() == 34);
    QLabel *title = nullptr;
    for (QLabel *label : sheet.findChildren<QLabel *>())
        title = label->text() == QStringLiteral("Fuzziness") ? label : title;
    QVERIFY(title && title->buddy() == slider);
    QTest::mousePress(title, Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));
    QMouseEvent move(QEvent::MouseMove, QPointF(22, 2), title->mapToGlobal(QPointF(22, 2)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(title, &move);
    QTest::mouseRelease(title, Qt::LeftButton, Qt::NoModifier, QPoint(22, 2));
    QCOMPARE(shown.edit().fuzziness, 54.0);
    // Invert, and an error shown in the warning colour.
    QCheckBox *invert = sheet.findChild<QCheckBox *>(QStringLiteral("colorRangeInvert"));
    invert->click();
    QVERIFY(shown.edit().invert);
    QLabel *error = sheet.findChild<QLabel *>(QStringLiteral("colorRangeError"));
    QVERIFY(error->isHidden() && error->foregroundRole() == QPalette::BrightText);
    // OK keeps the match as a step; the edit closes.
    QTRY_VERIFY(!shown.edit().preview.isNull());
    QPushButton *ok = nullptr;
    for (QPushButton *button : sheet.findChildren<QPushButton *>())
        ok = button->text() == QStringLiteral("OK") ? button : ok;
    QVERIFY(ok && ok->isDefault());
    ok->click();
    QVERIFY(!shown.session.colorRange());
    QCOMPARE(shown.session.history.undoName(), QStringLiteral("Color Range"));
}

void ColorRangeCanvasTests::thePanelFollowsTheEditAndTheMenu()
{
    App app;
    QVERIFY(scene(400, 300).save(app.path("scene.png")));
    app.session().createDocument(400, 300);
    app.importFile(app.path("scene.png"));
    QAction &entry = app.action("colorRange");
    QCOMPARE(entry.text(), QStringLiteral("Color Range…"));
    QVERIFY(entry.isEnabled());
    entry.trigger();
    QVERIFY(app.session().colorRange());
    QTRY_VERIFY(panel());
    QCOMPARE(panel()->windowTitle(), QStringLiteral("Color Range"));
    QVERIFY(panel()->findChild<ColorRangeSheet *>());
    QVERIFY(!entry.isEnabled());
    // Escape is Cancel: the panel and the edit go.
    QTRY_COMPARE(QApplication::activeWindow(), panel());
    QTest::keyClick(panel(), Qt::Key_Escape);
    QTRY_VERIFY(!panel());
    QVERIFY(!app.session().colorRange() && entry.isEnabled());
    // Reopened: a canvas click samples; the panel keeps the keys.
    entry.trigger();
    QTRY_VERIFY(panel());
    app.window.activateWindow();
    QTRY_COMPARE(QApplication::activeWindow(), &app.window);
    app.press(QPointF(200, 150));
    app.release(QPointF(200, 150));
    QVERIFY(app.session().colorRange().value().hasColors());
    QTRY_COMPARE(QApplication::activeWindow(), panel());
    app.session().commitColorRange();
    QTRY_VERIFY(!panel());
}

QTEST_MAIN(ColorRangeCanvasTests)
#include "ColorRangeCanvasTests.moc"
