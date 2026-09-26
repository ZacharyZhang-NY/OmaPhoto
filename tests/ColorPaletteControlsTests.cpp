#include "ColorPaletteFixtures.h"
#include "ContentView.h"
#include <QScrollArea>

// Swift's ColorPaletteControls under the rail.
class ColorPaletteControlsTests : public QObject {
    Q_OBJECT
private slots:
    void swatchesShowThePaletteAndOpenThePicker();
    void swapAndDefaultsFollowTheirButtons();
    void aMaskChoosesBlackOrWhite();
    void aMaskEndsTheChoiceAndThePicker();
    void aBusyProjectRestsThePalette();
    void theRailHoldsThePaletteBelowItsTools();
};

void ColorPaletteControlsTests::swatchesShowThePaletteAndOpenThePicker()
{
    Palette shown;
    QAbstractButton &foreground = shown.button("foregroundSwatch"), &background = shown.button("backgroundSwatch");
    QCOMPARE(shown.controls->size(), QSize(42, 42));
    QCOMPARE(foreground.geometry(), QRect(3, 3, 24, 24));
    QCOMPARE(background.geometry(), QRect(15, 15, 24, 24));
    QCOMPARE(shown.button("swapColors").geometry(), QRect(30, 0, 12, 12));
    QCOMPARE(shown.button("defaultColors").geometry(), QRect(2, 30, 12, 12));
    QCOMPARE(middle(foreground), QColor(Qt::black));
    QCOMPARE(middle(background), QColor(Qt::white));
    // A white ring inside a black rim.
    QVERIFY(foreground.grab().toImage().pixelColor(12, 1).lightness() > 200);
    QCOMPARE(background.grab().toImage().pixelColor(12, 0).lightness(), 0);
    QCOMPARE(foreground.toolTip(), QString("Foreground color"));
    QCOMPARE(background.toolTip(), QString("Background color"));
    // Each colour repaints its whole swatch; the two overlap.
    QCoreApplication::processEvents();
    PaintSpy front(foreground), back(background);
    shown.session.setForegroundColor(PaletteColor{0, 0, 1});
    QTRY_COMPARE(front.painted, foreground.rect());
    shown.session.setBackgroundColor(PaletteColor{0, 0, 1});
    QTRY_COMPARE(back.painted, background.rect());
    shown.session.resetPaletteColors();
    // A swatch opens the picker; the other takes it over.
    QTest::mouseClick(&foreground, Qt::LeftButton);
    QWidget *window = panel();
    QVERIFY(window);
    QCOMPARE(window->windowTitle(), QString("Color Picker (Foreground Color)"));
    QTest::mouseClick(&background, Qt::LeftButton);
    QCOMPARE(panel(), window);
    QCOMPARE(window->windowTitle(), QString("Color Picker (Background Color)"));
    QCOMPARE(shown.session.colorPicker().value().original, PaletteColor::white());
    shown.session.setColorPickerHSB(PickerHSB(0, 1, 1));
    QTest::mouseClick(&inPanel<QPushButton>("pickerCancel"), Qt::LeftButton);
    QVERIFY(!shown.session.colorPicker());
    QVERIFY(!panel());
    QCOMPARE(shown.session.backgroundColor(), PaletteColor::white());
    QTest::mouseClick(&foreground, Qt::LeftButton);
    shown.session.setColorPickerHSB(PickerHSB(120, 1, 1));
    QTest::mouseClick(&inPanel<QPushButton>("pickerOK"), Qt::LeftButton);
    QVERIFY(!panel());
    QCOMPARE(shown.session.foregroundColor(), (PaletteColor{0, 1, 0}));
    QCOMPARE(middle(foreground), QColor(Qt::green));
}

void ColorPaletteControlsTests::swapAndDefaultsFollowTheirButtons()
{
    Palette shown;
    QAbstractButton &swap = shown.button("swapColors"), &defaults = shown.button("defaultColors");
    QCOMPARE(swap.toolTip(), QString("Swap foreground and background (X)"));
    QCOMPARE(swap.accessibleName(), QString("Swap colors"));
    QCOMPARE(defaults.toolTip(), QString("Default colors (D)"));
    QCOMPARE(defaults.accessibleName(), QString("Default colors"));
    shown.session.setForegroundColor(PaletteColor{1, 0, 0});
    QTest::mouseClick(&swap, Qt::LeftButton);
    QCOMPARE(shown.session.foregroundColor(), PaletteColor::white());
    QCOMPARE(shown.session.backgroundColor(), (PaletteColor{1, 0, 0}));
    QCOMPARE(middle(shown.button("backgroundSwatch")), QColor(Qt::red));
    QTest::mouseClick(&defaults, Qt::LeftButton);
    QCOMPARE(shown.session.foregroundColor(), PaletteColor::black());
    QCOMPARE(shown.session.backgroundColor(), PaletteColor::white());
    // The glyphs take the secondary ink, dimmed when resting.
    QPalette inks = shown.controls->palette();
    inks.setColor(QPalette::Active, QPalette::PlaceholderText, Qt::red);
    inks.setColor(QPalette::Inactive, QPalette::PlaceholderText, Qt::red);
    inks.setColor(QPalette::Disabled, QPalette::PlaceholderText, Qt::blue);
    shown.controls->setPalette(inks);
    const auto ink = [](QWidget &glyph, QPoint at) { return glyph.grab().toImage().pixelColor(at).hsvHue(); };
    // The swap's diagonal, the ring and the ring's head.
    for (const auto &[glyph, at] : {std::pair(&swap, QPoint(4, 4)), std::pair(&defaults, QPoint(9, 6)), std::pair(&defaults, QPoint(7, 1))})
        QCOMPARE(ink(*glyph, at), 0);
    // Turned: the head's corner inked; a flat tip's spot bare.
    QCOMPARE(ink(swap, QPoint(4, 2)), 0);
    QCOMPARE(ink(swap, QPoint(1, 6)), -1);
    shown.session.setIsProjectBusy(true);
    QCOMPARE(ink(swap, QPoint(4, 4)), 240);
    QCOMPARE(ink(defaults, QPoint(9, 6)), 240);
}

void ColorPaletteControlsTests::aMaskChoosesBlackOrWhite()
{
    Palette shown;
    shown.session.addMask();
    QVERIFY(shown.session.isMaskSelected());
    for (const auto &[name, heading, choice] : {std::tuple("foregroundSwatch", "Mask foreground", "White · Reveal"),
                                                std::tuple("backgroundSwatch", "Mask background", "Black · Hide")}) {
        QTest::mouseClick(&shown.button(name), Qt::LeftButton);
        QWidget *popup = QApplication::activePopupWidget();
        QVERIFY(popup);
        QCOMPARE(popup->objectName(), QString("maskColorChoice"));
        // Under the swatches; offscreen adds a frame of two.
        QVERIFY((popup->pos() - shown.controls->mapToGlobal(QPoint(0, shown.controls->height()))).manhattanLength() <= 4);
        QCOMPARE(popup->findChild<QLabel *>()->text(), QString::fromUtf8(heading));
        QPushButton *button = nullptr;
        for (QPushButton *each : popup->findChildren<QPushButton *>()) {
            if (each->text() == QString::fromUtf8(choice))
                button = each;
        }
        QVERIFY(button);
        QTest::mouseClick(button, Qt::LeftButton);
        QTRY_VERIFY(!QApplication::activePopupWidget());
        QVERIFY(shown.session.maskPaintWhite());
        QVERIFY(!shown.session.colorPicker());
    }
    QCOMPARE(shown.session.paletteColor(false), PaletteColor::white());
    QCOMPARE(shown.session.paletteColor(true), PaletteColor::black());
    QCOMPARE(middle(shown.button("foregroundSwatch")), QColor(Qt::white));
}

void ColorPaletteControlsTests::aMaskEndsTheChoiceAndThePicker()
{
    Palette shown;
    shown.session.addMask();
    const QUuid id = shown.session.activeLayerID().value();
    shown.session.selectLayerTarget(id, false);
    // A mask chosen cancels the open picker.
    QTest::mouseClick(&shown.button("foregroundSwatch"), Qt::LeftButton);
    shown.session.setColorPickerHSB(PickerHSB(0, 1, 1));
    shown.session.selectLayerTarget(id, true);
    QVERIFY(!shown.session.colorPicker());
    QVERIFY(!panel());
    QCOMPARE(shown.session.foregroundColor(), PaletteColor::black());
    // The pixels chosen close the mask's choice.
    QTest::mouseClick(&shown.button("foregroundSwatch"), Qt::LeftButton);
    QVERIFY(QApplication::activePopupWidget());
    shown.session.selectLayerTarget(id, false);
    QTRY_VERIFY(!QApplication::activePopupWidget());
}

void ColorPaletteControlsTests::aBusyProjectRestsThePalette()
{
    Palette shown;
    shown.session.setIsProjectBusy(true);
    QVERIFY(!shown.controls->isEnabled());
    shown.session.setIsProjectBusy(false);
    QVERIFY(shown.controls->isEnabled());
}

void ColorPaletteControlsTests::theRailHoldsThePaletteBelowItsTools()
{
    EditorSession session;
    session.createDocument(400, 300, true);
    ContentView view(session);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    auto *palette = view.findChild<ColorPaletteControls *>();
    QVERIFY(palette);
    QWidget *rail = view.findChild<QScrollArea *>(QStringLiteral("toolRail"))->widget();
    QRect last;
    for (const QToolButton *tool : rail->findChildren<QToolButton *>()) {
        if (tool->parentWidget() == rail && tool->geometry().bottom() > last.bottom())
            last = tool->geometry();
    }
    // Swift's column: 36 across, the swatches 18 below the tools.
    const QRect swatch = palette->findChild<QAbstractButton *>(QStringLiteral("foregroundSwatch"))->geometry().translated(palette->pos());
    QCOMPARE(last.left(), 10);
    QCOMPARE(swatch.left(), 10);
    QCOMPARE(swatch.top() - (last.bottom() + 1), 18);
}

QTEST_MAIN(ColorPaletteControlsTests)
#include "ColorPaletteControlsTests.moc"
