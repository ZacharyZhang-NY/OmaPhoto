#include "HueSaturationSheetFixtures.h"

// Swift's HueSaturationSheet: its controls, bindings and states.
class HueSaturationSheetTests : public QObject {
    Q_OBJECT
private slots:
    void theSheetIsLaidOutAsSwifts();
    void aRangeShowsItsSpectrumAndSamplers();
    void slidersAndFieldsWriteTheirValue();
    void fieldsClampOnReturnAndPrintWhole();
    void samplersAndTargetingToggle();
    void theControlsFollowTheSession();
    void togglesResetAndTheButtons();
};

void HueSaturationSheetTests::theSheetIsLaidOutAsSwifts()
{
    Sheet shown;
    QVERIFY(shown.show());
    QCOMPARE(shown.sheet->width(), 460);
    QCOMPARE(shown.sheet->layout()->contentsMargins(), QMargins(24, 24, 24, 24));
    QCOMPARE(shown.sheet->layout()->spacing(), 16);
    QComboBox &range = shown.child<QComboBox>("hueRange");
    QCOMPARE(range.count(), 7);
    QCOMPARE(range.itemText(0), QString("Master"));
    QCOMPARE(range.itemText(6), QString("Magentas"));
    QCOMPARE(range.width(), 160);
    // Swift hides the picker's label; it still names it.
    QCOMPARE(labelled(range), QString("Range"));
    QVERIFY(shown.titled<QLabel>(QStringLiteral("Range")).isHidden());
    QVERIFY(range.isEnabled());
    // Master: no spectrum, no eyedroppers; targeting stays.
    for (const char *sampler : {"hueSample", "hueAdd", "hueRemove"})
        QVERIFY2(shown.child<QToolButton>(sampler).isHidden(), sampler);
    QVERIFY(!shown.child<QToolButton>("hueTargeting").isHidden());
    QVERIFY(shown.child<QWidget>("hueSpectrum").isHidden());
    QVERIFY(shown.titled<QCheckBox>(QStringLiteral("Apply outside this range instead")).isHidden());
    for (QFrame *frame : shown.sheet->findChildren<QFrame *>()) {
        if (frame->frameShape() == QFrame::VLine || frame->frameShape() == QFrame::HLine)
            QCOMPARE(frame->foregroundRole(), QPalette::Mid);
        if (frame->frameShape() == QFrame::VLine)
            QVERIFY(frame->isHidden());
    }
    const std::pair<const char *, int> rows[] = {{"Hue", 18000}, {"Saturation", 10000}, {"Lightness", 10000}};
    for (const auto &[title, reach] : rows) {
        const QString lower = QString::fromLatin1(title).toLower();
        QSlider &slider = *shown.sheet->findChild<QSlider *>(lower + QStringLiteral("Slider"));
        QVERIFY(slider.minimum() == -reach && slider.maximum() == reach && slider.value() == 0);
        QCOMPARE(labelled(slider), QString(title));
        QLineEdit &field = *shown.sheet->findChild<QLineEdit *>(lower + QStringLiteral("Field"));
        QCOMPARE(field.text(), QString("0"));
        QCOMPARE(field.width(), 48);
        QCOMPARE(field.alignment(), Qt::AlignRight);
        QCOMPARE(name(field), QString(title));
        QCOMPARE(field.placeholderText(), QString(title));
        // Title 76 wide; slider 10 on; unit 2 past.
        QLabel *heading = field.parentWidget()->findChildren<QLabel *>().first();
        QLabel *unit = field.parentWidget()->findChildren<QLabel *>().last();
        QCOMPARE(heading->width(), 76);
        QCOMPARE(slider.x() - heading->geometry().right() - 1, 10);
        QCOMPARE(unit->x() - field.geometry().right() - 1, 2);
        QCOMPARE(unit->text(), QString(lower == QStringLiteral("hue") ? "°" : ""));
    }
    QVERIFY(!shown.titled<QCheckBox>(QStringLiteral("Colorize")).isChecked());
    QVERIFY(shown.titled<QCheckBox>(QStringLiteral("Preview")).isChecked());
    // Toggles 18 apart, the buttons at their own widths.
    QCheckBox &colorize = shown.titled<QCheckBox>(QStringLiteral("Colorize")), &preview = shown.titled<QCheckBox>(QStringLiteral("Preview"));
    QCOMPARE(preview.x() - colorize.geometry().right() - 1, 18);
    QPushButton &ok = shown.titled<QPushButton>(QStringLiteral("OK")), &reset = shown.titled<QPushButton>(QStringLiteral("Reset"));
    QVERIFY(ok.isDefault());
    for (QPushButton *button : {&ok, &reset, &shown.titled<QPushButton>(QStringLiteral("Cancel"))}) {
        QCOMPARE(button->width(), button->sizeHint().width());
        QVERIFY(button == &ok || !button->autoDefault());
    }
    QLabel &limited = shown.child<QLabel>("hueLimited");
    QCOMPARE(limited.text(), QString("Limited to the selection"));
    QCOMPARE(limited.font().pixelSize(), 12);
    QCOMPARE(limited.foregroundRole(), QPalette::PlaceholderText);
    QVERIFY(limited.isHidden());
    // Swift's help and names for each tool.
    QToolButton &targeting = shown.child<QToolButton>("hueTargeting");
    QCOMPARE(targeting.toolTip(), QString("Targeted adjustment: drag on the image to change that color's saturation, or its hue with Ctrl held"));
    QCOMPARE(name(targeting), QString("Targeted adjustment"));
    QCOMPARE(targeting.size(), QSize(24, 20));
    const std::pair<const char *, HueSampleMode> samplers[] = {{"hueSample", HueSampleMode::replace}, {"hueAdd", HueSampleMode::add}, {"hueRemove", HueSampleMode::remove}};
    for (const auto &[object, mode] : samplers) {
        QToolButton &sampler = shown.child<QToolButton>(object);
        QCOMPARE(sampler.toolTip(), help(mode));
        QCOMPARE(name(sampler), rawValue(mode) + QStringLiteral(" color"));
        QCOMPARE(sampler.size(), QSize(24, 20));
    }
    // Over a selection it is limited; the sheet says so.
    shown.session.cancelHueSaturation();
    QPainterPath left;
    left.addRect(0, 0, 2, 1);
    shown.session.applySelection(left, SelectionMode::replace, QStringLiteral("Select"));
    shown.session.beginHueSaturation();
    QVERIFY(!limited.isHidden());
}

void HueSaturationSheetTests::aRangeShowsItsSpectrumAndSamplers()
{
    Sheet shown;
    QVERIFY(shown.show());
    QComboBox &range = shown.child<QComboBox>("hueRange");
    range.setFocus();
    QTest::keyClick(&range, Qt::Key_Down);
    QCOMPARE(shown.settings().range, ColorRange::reds);
    for (const char *sampler : {"hueSample", "hueAdd", "hueRemove"})
        QVERIFY2(shown.child<QToolButton>(sampler).isVisible(), sampler);
    QToolButton &sample = shown.child<QToolButton>("hueSample"), &add = shown.child<QToolButton>("hueAdd");
    QCOMPARE(add.x() - sample.geometry().right() - 1, 6);
    QFrame *divider = nullptr;
    for (QFrame *frame : shown.sheet->findChildren<QFrame *>()) {
        if (frame->frameShape() == QFrame::VLine)
            divider = frame;
    }
    QVERIFY(divider && divider->isVisible() && divider->height() == 16);
    QVERIFY(shown.child<QWidget>("hueSpectrum").isVisible());
    QVERIFY(shown.titled<QCheckBox>(QStringLiteral("Apply outside this range instead")).isVisible());
    QLabel &readout = shown.child<QLabel>("spectrumReadout");
    QCOMPARE(readout.text(), QString("315°   345°   15°   45°"));
    QCOMPARE(readout.font().pixelSize(), 10);
    QCOMPARE(readout.foregroundRole(), QPalette::PlaceholderText);
    // Colorize: hue and saturation set outright, no range.
    shown.titled<QCheckBox>(QStringLiteral("Colorize")).click();
    QVERIFY(shown.settings() == HueSaturationSettings::colorizeStart());
    QVERIFY(!range.isEnabled());
    QVERIFY(!shown.child<QToolButton>("hueTargeting").isVisible() && !shown.child<QWidget>("hueSpectrum").isVisible());
    QSlider &hue = *shown.sheet->findChild<QSlider *>(QStringLiteral("hueSlider"));
    QSlider &saturation = *shown.sheet->findChild<QSlider *>(QStringLiteral("saturationSlider"));
    QVERIFY(hue.minimum() == 0 && hue.maximum() == 36000);
    QVERIFY(saturation.minimum() == 0 && saturation.maximum() == 10000 && saturation.value() == 2500);
    QCOMPARE(shown.child<QLineEdit>("saturationField").text(), QString("25"));
    // Off again: everything back to the start.
    shown.titled<QCheckBox>(QStringLiteral("Colorize")).click();
    QVERIFY(shown.settings() == HueSaturationSettings());
    QVERIFY(range.isEnabled() && shown.child<QToolButton>("hueTargeting").isVisible());
    // A range under Colorize still shows no spectrum.
    shown.set(HueSaturationSettings(0, 25, 0, true, ColorRange::reds));
    QVERIFY(!shown.child<QWidget>("hueSpectrum").isVisible() && !sample.isVisible());
}

void HueSaturationSheetTests::slidersAndFieldsWriteTheirValue()
{
    Sheet shown;
    QVERIFY(shown.show());
    QSlider &hue = *shown.sheet->findChild<QSlider *>(QStringLiteral("hueSlider"));
    QSlider &saturationSlider = *shown.sheet->findChild<QSlider *>(QStringLiteral("saturationSlider"));
    hue.setValue(3060);
    QCOMPARE(shown.settings().hue(), 30.6);
    QLineEdit &hueField = shown.child<QLineEdit>("hueField");
    QCOMPARE(hueField.text(), QString("31"));
    // Visited without typing, the shown whole number stays unread.
    hueField.setFocus();
    QTRY_VERIFY(hueField.hasFocus());
    QLineEdit &saturation = shown.child<QLineEdit>("saturationField");
    saturation.setFocus();
    QTRY_VERIFY(saturation.hasFocus());
    QCOMPARE(shown.settings().hue(), 30.6);
    // Typed and left, a number applies unclamped.
    type(saturation, QStringLiteral("250"));
    hueField.setFocus();
    QCOMPARE(shown.settings().saturation(), 250.0);
    QCOMPARE(saturationSlider.value(), 10000);
    QCOMPARE(saturation.text(), QString("250"));
    // Applied, the field follows the session again.
    saturationSlider.setValue(-2000);
    QCOMPARE(saturation.text(), QString("-20"));
    // What is no finite number reverts.
    for (const char *text : {"abc", "inf", "nan"}) {
        type(hueField, QString::fromLatin1(text));
        saturation.setFocus();
        QCOMPARE(shown.settings().hue(), 30.6);
        QCOMPARE(hueField.text(), QString("31"));
    }
    // Past any int, the slider rests at its end.
    type(saturation, QStringLiteral("1e300"));
    hueField.setFocus();
    QCOMPARE(saturationSlider.value(), 10000);
    // Sliders show Colorize's range without writing.
    HueSaturationSettings colour = HueSaturationSettings::colorizeStart();
    colour.setHue(-40);
    shown.set(colour);
    QCOMPARE(hue.value(), 0);
    QCOMPARE(shown.settings().hue(), -40.0);
    QCOMPARE(hueField.text(), QString("-40"));
    // Typing waits through other values; its own slider replaces it.
    type(hueField, QStringLiteral("4"));
    saturationSlider.setValue(1000);
    QVERIFY(hueField.text() == "4" && hueField.isModified());
    hue.setValue(1000);
    QVERIFY(hueField.text() == "10" && !hueField.isModified() && hueField.hasFocus());
    saturation.setFocus();
    QCOMPARE(shown.settings().hue(), 10.0);
}

void HueSaturationSheetTests::fieldsClampOnReturnAndPrintWhole()
{
    Sheet shown;
    QVERIFY(shown.show());
    // Return clamps into the slider's range.
    QLineEdit &lightness = shown.child<QLineEdit>("lightnessField");
    type(lightness, QStringLiteral("-150"));
    QTest::keyClick(&lightness, Qt::Key_Return);
    QCOMPARE(shown.settings().lightness(), -100.0);
    QCOMPARE(lightness.text(), QString("-100"));
    // Under Colorize, Colorize's range.
    shown.set(HueSaturationSettings::colorizeStart());
    QLineEdit &hue = shown.child<QLineEdit>("hueField");
    type(hue, QStringLiteral("400"));
    QTest::keyClick(&hue, Qt::Key_Return);
    QCOMPARE(shown.settings().hue(), 360.0);
    // The user's decimal mark reads; digits print ungrouped.
    lightness.setLocale(QLocale(QLocale::German));
    type(lightness, QStringLiteral("12,4"));
    hue.setFocus();
    QCOMPARE(shown.settings().lightness(), 12.4);
    QCOMPARE(lightness.text(), QString("12"));
    type(lightness, QStringLiteral("12345"));
    hue.setFocus();
    QCOMPARE(shown.settings().lightness(), 12345.0);
    QCOMPARE(lightness.text(), QString("12345"));
}

void HueSaturationSheetTests::samplersAndTargetingToggle()
{
    Sheet shown;
    shown.set(HueSaturationSettings(0, 0, 0, false, ColorRange::reds));
    QVERIFY(shown.show());
    QToolButton &sample = shown.child<QToolButton>("hueSample"), &add = shown.child<QToolButton>("hueAdd"),
                &remove = shown.child<QToolButton>("hueRemove"), &targeting = shown.child<QToolButton>("hueTargeting");
    shown.session.setHueTargeting(true);
    sample.click();
    QVERIFY(shown.session.hueSampleMode() == HueSampleMode::replace && !shown.session.hueTargeting());
    QVERIFY(sample.isChecked() && !add.isChecked() && !targeting.isChecked());
    // A second click puts the eyedropper away.
    sample.click();
    QVERIFY(!shown.session.hueSampleMode() && !sample.isChecked());
    add.click();
    QVERIFY(shown.session.hueSampleMode() == HueSampleMode::add && add.isChecked());
    targeting.click();
    QVERIFY(!shown.session.hueSampleMode() && shown.session.hueTargeting());
    QVERIFY(targeting.isChecked() && !add.isChecked());
    remove.click();
    QVERIFY(shown.session.hueSampleMode() == HueSampleMode::remove && !shown.session.hueTargeting());
    targeting.click();
    targeting.click();
    QVERIFY(!shown.session.hueSampleMode() && !shown.session.hueTargeting() && !targeting.isChecked());
}

void HueSaturationSheetTests::theControlsFollowTheSession()
{
    Sheet shown;
    QComboBox &range = shown.child<QComboBox>("hueRange");
    HueSaturationSettings yellows(0, 0, 0, false, ColorRange::yellows);
    yellows.invertRange = true;
    shown.session.updateHueSaturation(yellows, false);
    QCOMPARE(range.currentText(), QString("Yellows"));
    QVERIFY(shown.titled<QCheckBox>(QStringLiteral("Apply outside this range instead")).isChecked());
    QVERIFY(!shown.titled<QCheckBox>(QStringLiteral("Preview")).isChecked());
    shown.session.updateHueSaturation(HueSaturationSettings::colorizeStart(), true);
    QVERIFY(shown.titled<QCheckBox>(QStringLiteral("Colorize")).isChecked());
    QVERIFY(shown.titled<QCheckBox>(QStringLiteral("Preview")).isChecked());
    QCOMPARE(range.currentText(), QString("Master"));
}

void HueSaturationSheetTests::togglesResetAndTheButtons()
{
    Sheet shown;
    shown.set(HueSaturationSettings(30, 0, 0, false, ColorRange::reds));
    QCheckBox &preview = shown.titled<QCheckBox>(QStringLiteral("Preview"));
    preview.click();
    QVERIFY(!shown.session.hueSaturation().value().preview);
    // Off, the controls keep Preview off.
    shown.sheet->findChild<QSlider *>(QStringLiteral("lightnessSlider"))->setValue(500);
    QVERIFY(!shown.session.hueSaturation().value().preview && shown.settings().lightness() == 5);
    preview.click();
    QVERIFY(shown.session.hueSaturation().value().preview);
    QCheckBox &invert = shown.titled<QCheckBox>(QStringLiteral("Apply outside this range instead"));
    invert.click();
    QVERIFY(shown.settings().invertRange);
    invert.click();
    QVERIFY(!shown.settings().invertRange);
    // Reset: back to the start, or Colorize's start.
    shown.titled<QPushButton>(QStringLiteral("Reset")).click();
    QVERIFY(shown.settings() == HueSaturationSettings());
    HueSaturationSettings colour = HueSaturationSettings::colorizeStart();
    colour.setHue(200);
    shown.set(colour);
    shown.titled<QPushButton>(QStringLiteral("Reset")).click();
    QVERIFY(shown.settings() == HueSaturationSettings::colorizeStart());
    // OK commits one step; Cancel leaves the pixels.
    shown.titled<QPushButton>(QStringLiteral("OK")).click();
    QTRY_VERIFY(!shown.session.hueSaturation());
    QCOMPARE(shown.session.history.undoName(), QString("Hue/Saturation"));
    const int steps = shown.session.history.undoCount();
    shown.session.beginHueSaturation();
    shown.set(HueSaturationSettings(90));
    shown.titled<QPushButton>(QStringLiteral("Cancel")).click();
    QVERIFY(!shown.session.hueSaturation());
    QCOMPARE(shown.session.history.undoCount(), steps);
}

QTEST_MAIN(HueSaturationSheetTests)
#include "HueSaturationSheetTests.moc"
