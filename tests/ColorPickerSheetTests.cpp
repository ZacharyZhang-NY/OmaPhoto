#include "ColorPaletteFixtures.h"
#include "UI/ColorPickerSheet.h"
#include <QAccessible>
#include <QMenu>

// Swift's ColorPickerSheet: field, strip, preview, entries, keys.
class ColorPickerSheetTests : public QObject {
    Q_OBJECT
private slots:
    void theFieldAndStripSetTheWorkingColour();
    void theFieldStripAndPreviewDrawAsSwifts();
    void theEntriesTypeAndStep();
    void entriesKeepTheirBoundsAndTheirTyping();
    void returnAppliesWhereEscapeAndClosingCancel();
    void everyControlCarriesSwiftsLabel();
};

void ColorPickerSheetTests::theFieldAndStripSetTheWorkingColour()
{
    Palette shown;
    openForeground(shown);
    QWidget &field = inPanel<QWidget>("saturationBrightness"), &strip = inPanel<QWidget>("hueStrip");
    QCOMPARE(field.size(), QSize(256, 256));
    QCOMPARE(strip.size(), QSize(34, 256));
    QTest::mousePress(&field, Qt::LeftButton, Qt::NoModifier, QPoint(300, -10));
    QTest::mouseRelease(&field, Qt::LeftButton, Qt::NoModifier, QPoint(300, -10));
    QCOMPARE(shown.session.colorPicker().value().color().hex(), QString("FF0000"));
    QTest::mousePress(&strip, Qt::LeftButton, Qt::NoModifier, QPoint(17, 128));
    QCOMPARE(shown.session.colorPicker().value().color().hex(), QString("00FFFF"));
    QMouseEvent drag(QEvent::MouseMove, QPointF(17, 400), QPointF(17, 400), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&strip, &drag);
    QCOMPARE(shown.session.colorPicker().value().hsb.hue, 0.0);
    QTest::mouseRelease(&strip, Qt::LeftButton, Qt::NoModifier, QPoint(17, 400));
    // The other buttons pick nothing, as Swift's drag.
    QTest::mousePress(&strip, Qt::RightButton, Qt::NoModifier, QPoint(17, 128));
    QMouseEvent right(QEvent::MouseMove, QPointF(17, 64), QPointF(17, 64), Qt::NoButton, Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(&strip, &right);
    QTest::mouseRelease(&strip, Qt::RightButton, Qt::NoModifier, QPoint(17, 64));
    QTest::mouseClick(&field, Qt::RightButton, Qt::NoModifier, QPoint(0, 255));
    QCOMPARE(shown.session.colorPicker().value().color().hex(), QString("FF0000"));
    // Half down the field: half as bright.
    QTest::mouseClick(&field, Qt::LeftButton, Qt::NoModifier, QPoint(256, 128));
    QCOMPARE(shown.session.colorPicker().value().color().hex(), QString("800000"));
    QCOMPARE(middle(inPanel<QWidget>("newColor")), QColor(128, 0, 0));
    // A pick, field or strip, replaces a channel's typing.
    QLineEdit &red = inPanel<QLineEdit>("r"), &green = inPanel<QLineEdit>("g");
    type(red, QStringLiteral("12"));
    QTest::mouseClick(&field, Qt::LeftButton, Qt::NoModifier, QPoint(256, 0));
    QVERIFY(red.hasFocus() && red.text() == "255" && !red.isModified());
    type(red, QStringLiteral("12"));
    QTest::mouseClick(&strip, Qt::LeftButton, Qt::NoModifier, QPoint(17, 128));
    QVERIFY(red.text() == "0" && !red.isModified());
    green.setFocus();
    QCOMPARE(shown.session.colorPicker().value().color().hex(), QString("00FFFF"));
}

void ColorPickerSheetTests::theFieldStripAndPreviewDrawAsSwifts()
{
    Palette shown;
    openForeground(shown);
    // A colour from elsewhere repaints the field, strip and preview.
    QWidget &sheet = *panel()->findChild<ColorPickerSheet *>(), &stripWidget = inPanel<QWidget>("hueStrip");
    PaintSpy fieldPaints(inPanel<QWidget>("saturationBrightness")), stripPaints(stripWidget), previewPaints(inPanel<QWidget>("newColor")),
        sheetPaints(sheet);
    shown.session.setColorPickerHSB(PickerHSB(0, 0.5, 0.25));
    QTRY_VERIFY(fieldPaints.count > 0 && stripPaints.count > 0 && previewPaints.count > 0);
    // The arrows' room too: five points past either end.
    QTRY_VERIFY(sheetPaints.painted.contains(stripWidget.geometry().adjusted(0, -5, 0, 5)));
    // White to the hue across, to black down, framed.
    const QImage field = inPanel<QWidget>("saturationBrightness").grab().toImage();
    QVERIFY(field.pixelColor(2, 2).lightness() > 245);
    const QColor hue = field.pixelColor(253, 2);
    QVERIFY2(hue.red() > 245 && hue.green() < 12 && hue.blue() < 12, qPrintable(hue.name()));
    QVERIFY(field.pixelColor(128, 253).lightness() < 10);
    QVERIFY(field.pixelColor(128, 0).lightness() < field.pixelColor(128, 2).lightness());
    // The marker: white ring, black ring, at the colour.
    QVERIFY(field.pixelColor(133, 192).lightness() > 200);
    QVERIFY(field.pixelColor(134, 192).lightness() < field.pixelColor(138, 192).lightness());
    // Red, magenta, cyan, red down the strip; arrows mark hues.
    shown.session.setColorPickerHSB(PickerHSB(90, 1, 1));
    const QImage strip = inPanel<QWidget>("hueStrip").grab().toImage();
    for (const auto &[y, turn] : {std::pair(2, 0), std::pair(43, 300), std::pair(128, 180), std::pair(253, 0)}) {
        const QColor shade = strip.pixelColor(17, y);
        const int off = std::abs(shade.hsvHue() - turn) % 360;
        QVERIFY2(std::min(off, 360 - off) <= 8 && shade.hsvSaturation() == 255, qPrintable(shade.name()));
    }
    // The sheet draws the arrows, past the strip's ends too.
    const QColor ink = panel()->palette().color(QPalette::WindowText);
    const auto arrows = [&sheet, &stripWidget](QPoint at) { return sheet.grab().toImage().pixelColor(stripWidget.geometry().topLeft() + at); };
    QCOMPARE(arrows(QPoint(2, 192)), ink);
    QCOMPARE(arrows(QPoint(31, 192)), ink);
    QVERIFY(arrows(QPoint(2, 64)) != ink);
    // Antialiased: an edge pixel a quarter covered is neither.
    const QColor edge = arrows(QPoint(2, 188));
    QVERIFY2(edge != ink && edge != arrows(QPoint(2, 64)), qPrintable(edge.name()));
    QVERIFY(strip.pixelColor(7, 192) != strip.pixelColor(8, 192));
    // The preview is the new colour, rimmed in 60% black.
    const QImage preview = inPanel<QWidget>("newColor").grab().toImage();
    QCOMPARE(preview.pixelColor(32, 32), QColor(128, 255, 0));
    const QColor rim = preview.pixelColor(32, 0);
    QVERIFY2(std::abs(rim.red() - 51) <= 2 && std::abs(rim.green() - 102) <= 2, qPrintable(rim.name()));
    // At either end the arrows reach past the strip.
    for (const auto &[turn, y] : {std::pair(360.0, -3), std::pair(0.0, 258)}) {
        shown.session.setColorPickerHSB(PickerHSB(turn, 1, 1));
        QCOMPARE(arrows(QPoint(1, y)), ink);
        QCOMPARE(arrows(QPoint(32, y)), ink);
    }
}

void ColorPickerSheetTests::theEntriesTypeAndStep()
{
    Palette shown;
    openForeground(shown);
    QLineEdit &red = inPanel<QLineEdit>("r"), &green = inPanel<QLineEdit>("g"), &hex = inPanel<QLineEdit>("hex");
    QCOMPARE(red.text(), QString("0"));
    QCOMPARE(hex.text(), QString("000000"));
    // Past 255 is 255; leaving applies it.
    type(red, QStringLiteral("300"));
    green.setFocus();
    QCOMPARE(shown.session.colorPicker().value().color().hex(), QString("FF0000"));
    QCOMPARE(red.text(), QString("255"));
    QCOMPARE(hex.text(), QString("FF0000"));
    // The arrows step: one, ten with Shift.
    QTest::keyClick(&green, Qt::Key_Up);
    QTest::keyClick(&green, Qt::Key_Up, Qt::ShiftModifier);
    QTest::keyClick(&green, Qt::Key_Down);
    QCOMPARE(green.text(), QString("10"));
    QCOMPARE(shown.session.colorPicker().value().color().hex(), QString("FF0A00"));
    // Empty or wild text puts the colour's own back.
    type(green, QString());
    hex.setFocus();
    QCOMPARE(green.text(), QString("10"));
    type(hex, QStringLiteral("zzz"));
    red.setFocus();
    QCOMPARE(hex.text(), QString("FF0A00"));
    type(hex, QStringLiteral("#00f"));
    red.setFocus();
    QCOMPARE(shown.session.colorPicker().value().color().hex(), QString("0000FF"));
    QCOMPARE(red.text(), QString("0"));
    // Typing stays while the colour changes elsewhere.
    type(red, QStringLiteral("12"));
    shown.session.setColorPickerHSB(PickerHSB(120, 1, 1));
    QCOMPARE(red.text(), QString("12"));
    QCOMPARE(hex.text(), QString("00FF00"));
    type(hex, QStringLiteral("12"));
    shown.session.setColorPickerHSB(PickerHSB(240, 1, 1));
    QCOMPARE(hex.text(), QString("12"));
    // A step after typing shows the stepped number.
    type(red, QStringLiteral("12"));
    QTest::keyClick(&red, Qt::Key_Up);
    QCOMPARE(red.text(), QString("1"));
    // Blue takes its typing too.
    QLineEdit &blue = inPanel<QLineEdit>("b");
    type(blue, QStringLiteral("200"));
    red.setFocus();
    QCOMPARE(shown.session.colorPicker().value().color().hex(), QString("0100C8"));
}

void ColorPickerSheetTests::entriesKeepTheirBoundsAndTheirTyping()
{
    Palette shown;
    openForeground(shown);
    QLineEdit &red = inPanel<QLineEdit>("r"), &green = inPanel<QLineEdit>("g"), &hex = inPanel<QLineEdit>("hex");
    // More digits than a number holds read as the top.
    type(red, QStringLiteral("99999999999999999999"));
    green.setFocus();
    QCOMPARE(red.text(), QString("255"));
    QTest::keyClick(&red, Qt::Key_Up);
    QCOMPARE(red.text(), QString("255"));
    QTest::keyClick(&green, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(green.text(), QString("0"));
    // A menu or popup borrowing the focus keeps the typing.
    type(green, QStringLiteral("200"));
    for (const Qt::FocusReason reason : {Qt::PopupFocusReason, Qt::MenuBarFocusReason}) {
        QFocusEvent borrowed(QEvent::FocusOut, reason);
        QApplication::sendEvent(&green, &borrowed);
        QCOMPARE(green.text(), QString("200"));
        QCOMPARE(shown.session.colorPicker().value().color().hex(), QString("FF0000"));
    }
    red.setFocus();
    QCOMPARE(shown.session.colorPicker().value().color().hex(), QString("FFC800"));
    // A menu taking the focus keeps the typing through changes.
    for (QLineEdit *entry : {&green, &hex}) {
        type(*entry, QStringLiteral("99"));
        QMenu menu;
        menu.addAction(QStringLiteral("Nothing"));
        menu.popup(entry->mapToGlobal(QPoint(0, 0)));
        QTRY_VERIFY(QApplication::activePopupWidget());
        shown.session.setColorPickerHSB(PickerHSB(240, 1, 1));
        QCOMPARE(entry->text(), QString("99"));
        menu.close();
    }
    // Letters never reach a channel.
    type(red, QStringLiteral("ab"));
    QCOMPARE(red.text(), QString());
    green.setFocus();
    QCOMPARE(red.text(), QString("0"));
    // An entry left untouched leaves the working colour exact.
    const PickerHSB exact(10.3, 0.5, 0.5);
    shown.session.setColorPickerHSB(exact);
    green.setFocus();
    red.setFocus();
    QVERIFY(shown.session.colorPicker().value().hsb == exact);
    // The hex commits on each leave, as Swift's: 8 bits.
    hex.setFocus();
    red.setFocus();
    QVERIFY(!(shown.session.colorPicker().value().hsb == exact));
    QCOMPARE(shown.session.colorPicker().value().color(), PickerHSB(exact).rgb().quantized());
}

void ColorPickerSheetTests::returnAppliesWhereEscapeAndClosingCancel()
{
    Palette shown;
    openForeground(shown);
    type(inPanel<QLineEdit>("r"), QStringLiteral("128"));
    QTest::keyClick(&inPanel<QLineEdit>("r"), Qt::Key_Return);
    QVERIFY(!panel());
    QCOMPARE(shown.session.foregroundColor().hex(), QString("800000"));
    openForeground(shown);
    type(inPanel<QLineEdit>("hex"), QStringLiteral("00ff00"));
    QTest::keyClick(&inPanel<QLineEdit>("hex"), Qt::Key_Escape);
    QVERIFY(!panel());
    QVERIFY(!shown.session.colorPicker());
    QCOMPARE(shown.session.foregroundColor().hex(), QString("800000"));
    QTest::mouseClick(&shown.button("backgroundSwatch"), Qt::LeftButton);
    shown.session.setColorPickerHSB(PickerHSB(240, 1, 1));
    panel()->close();
    QVERIFY(!shown.session.colorPicker());
    QCOMPARE(shown.session.backgroundColor(), PaletteColor::white());
    // Return on a focused Cancel still means OK, the default.
    openForeground(shown);
    shown.session.setColorPickerHSB(PickerHSB(120, 1, 1));
    QPushButton &cancel = inPanel<QPushButton>("pickerCancel");
    cancel.setFocus();
    QTest::keyClick(&cancel, Qt::Key_Return);
    QVERIFY(!panel());
    QCOMPARE(shown.session.foregroundColor(), (PaletteColor{0, 1, 0}));
}

void ColorPickerSheetTests::everyControlCarriesSwiftsLabel()
{
    Palette shown;
    openForeground(shown);
    // What a screen reader reads, as Swift labels it.
    const auto text = [](const char *name, QAccessible::Text kind) { return QAccessible::queryAccessibleInterface(&inPanel<QWidget>(name))->text(kind); };
    for (const auto &[name, label] : {std::pair("saturationBrightness", "Saturation and brightness"), std::pair("hueStrip", "Hue"),
                                      std::pair("newColor", "New color"), std::pair("r", "Red"), std::pair("g", "Green"), std::pair("b", "Blue"),
                                      std::pair("hex", "Hex color")})
        QCOMPARE(text(name, QAccessible::Name), QString(label));
    // Swift's titles show while an entry is empty.
    for (const auto &[name, title] : {std::pair("r", "R"), std::pair("g", "G"), std::pair("b", "B"), std::pair("hex", "Hex")})
        QCOMPARE(inPanel<QLineEdit>(name).placeholderText(), QString(title));
    // The hue's whole degrees, rounded, follow the colour.
    QCOMPARE(text("hueStrip", QAccessible::Description), QString("0 degrees"));
    shown.session.setColorPickerHSB(PickerHSB(120.5, 1, 1));
    QCOMPARE(text("hueStrip", QAccessible::Description), QString("121 degrees"));
    shown.session.setColorPickerHSB(PickerHSB(359.4, 1, 1));
    QCOMPARE(text("hueStrip", QAccessible::Description), QString("359 degrees"));
}

QTEST_MAIN(ColorPickerSheetTests)
#include "ColorPickerSheetTests.moc"
