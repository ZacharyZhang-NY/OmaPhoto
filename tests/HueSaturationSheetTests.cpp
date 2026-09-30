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
    void tracksFollowTheRangeAndColorize();
    void aDoubleClickResetsToNoneOrColorizesStart();
    void aResetReplacesTheFieldsTyping();
    void arrowKeysWalkEverySlider();
    void aResetKeepsPreviewOffInBothModes();
    void aRangeUnderColorizeKeepsItsTracks();
    void aTitleResetsOnALeftDoubleClickThenScrubs();
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
    for (const char *title : {"Hue", "Saturation", "Lightness"}) {
        const QString lower = QString::fromLatin1(title).toLower();
        // Swift's coloured slider: centred, its help naming the reset.
        CameraRawSlider &slider = *shown.sheet->findChild<CameraRawSlider *>(lower + QStringLiteral("Slider"));
        QVERIFY(slider.minimum() == 0 && slider.maximum() == 1000 && slider.value() == 500 && slider.shown() == 0);
        QCOMPARE(slider.toolTip(), QString(title) + QStringLiteral(". Double-click to reset."));
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
    CameraRawSlider &hue = *shown.sheet->findChild<CameraRawSlider *>(QStringLiteral("hueSlider"));
    CameraRawSlider &saturation = *shown.sheet->findChild<CameraRawSlider *>(QStringLiteral("saturationSlider"));
    // Both ranges now start at zero.
    QVERIFY(hue.value() == 0 && hue.shown() == 0);
    QVERIFY(saturation.value() == 250 && saturation.shown() == 25);
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
    CameraRawSlider &hue = *shown.sheet->findChild<CameraRawSlider *>(QStringLiteral("hueSlider"));
    CameraRawSlider &saturationSlider = *shown.sheet->findChild<CameraRawSlider *>(QStringLiteral("saturationSlider"));
    // 30.6 along the track: Swift sets the whole number.
    hue.setValue(585);
    QCOMPARE(shown.settings().hue(), 31.0);
    QLineEdit &hueField = shown.child<QLineEdit>("hueField");
    QCOMPARE(hueField.text(), QString("31"));
    // Visited without typing, the shown whole number stays unread.
    hueField.setFocus();
    QTRY_VERIFY(hueField.hasFocus());
    QLineEdit &saturation = shown.child<QLineEdit>("saturationField");
    saturation.setFocus();
    QTRY_VERIFY(saturation.hasFocus());
    QCOMPARE(shown.settings().hue(), 31.0);
    // Typed and left, a number applies unclamped.
    type(saturation, QStringLiteral("250"));
    hueField.setFocus();
    QCOMPARE(shown.settings().saturation(), 250.0);
    QCOMPARE(saturationSlider.value(), 1000);
    QCOMPARE(saturation.text(), QString("250"));
    // Applied, the field follows the session again.
    saturationSlider.setValue(400);
    QCOMPARE(saturation.text(), QString("-20"));
    // What is no finite number reverts.
    for (const char *text : {"abc", "inf", "nan"}) {
        type(hueField, QString::fromLatin1(text));
        saturation.setFocus();
        QCOMPARE(shown.settings().hue(), 31.0);
        QCOMPARE(hueField.text(), QString("31"));
    }
    // Past any int, the slider rests at its end.
    type(saturation, QStringLiteral("1e300"));
    hueField.setFocus();
    QCOMPARE(saturationSlider.value(), 1000);
    // Sliders show Colorize's range without writing.
    HueSaturationSettings colour = HueSaturationSettings::colorizeStart();
    colour.setHue(-40);
    shown.set(colour);
    QCOMPARE(hue.value(), 0);
    QCOMPARE(shown.settings().hue(), -40.0);
    QCOMPARE(hueField.text(), QString("-40"));
    // Typing waits through other values; its own slider replaces it.
    type(hueField, QStringLiteral("4"));
    saturationSlider.setValue(100);
    QVERIFY(hueField.text() == "4" && hueField.isModified());
    hue.setValue(250);
    QVERIFY(hueField.text() == "90" && !hueField.isModified() && hueField.hasFocus());
    saturation.setFocus();
    QCOMPARE(shown.settings().hue(), 90.0);
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
    shown.sheet->findChild<QSlider *>(QStringLiteral("lightnessSlider"))->setValue(525);
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

void HueSaturationSheetTests::tracksFollowTheRangeAndColorize()
{
    using Kind = CameraRawSliderTrack::Kind;
    Sheet shown;
    const auto track = [&](const char *name) { return shown.child<CameraRawSlider>(name).track(); };
    // Master: circle round red, gray to red, black to white.
    QVERIFY(track("hueSlider").kind == Kind::spectrum && track("hueSlider").degrees == 0);
    QCOMPARE(track("saturationSlider").kind, Kind::chroma);
    QVERIFY(track("lightnessSlider").kind == Kind::opposing && track("lightnessSlider").colors() == (std::vector<QColor>{Qt::black, Qt::white}));
    // A range centres both on its own hue.
    shown.set(HueSaturationSettings(0, 0, 0, false, ColorRange::cyans));
    QVERIFY(track("hueSlider").kind == Kind::spectrum && track("hueSlider").degrees == 180);
    QVERIFY(track("saturationSlider").kind == Kind::saturation && track("saturationSlider").degrees == 180);
    shown.set(HueSaturationSettings(0, 0, 0, false, ColorRange::magentas));
    QCOMPARE(track("hueSlider").degrees, 300.0);
    // Colorize: red to red; saturation towards the chosen hue.
    shown.set(HueSaturationSettings(200, 25, 0, true));
    QVERIFY(track("hueSlider").kind == Kind::spectrum && track("hueSlider").degrees == 180);
    QVERIFY(track("saturationSlider").kind == Kind::saturation && track("saturationSlider").degrees == 200);
    // Its knob at 200 of 0 to 360.
    QCOMPARE(shown.child<CameraRawSlider>("hueSlider").value(), 556);
}

void HueSaturationSheetTests::aDoubleClickResetsToNoneOrColorizesStart()
{
    Sheet shown;
    QVERIFY(shown.show());
    const auto title = [&](const char *text) -> QLabel & { return shown.titled<QLabel>(QString::fromLatin1(text)); };
    shown.set(HueSaturationSettings(30, -40, 20, false, ColorRange::reds));
    // The title: that value alone back to none.
    QTest::mouseDClick(&title("Hue"), Qt::LeftButton);
    QVERIFY(shown.settings().hue() == 0 && shown.settings().saturation() == -40 && shown.settings().lightness() == 20);
    // The knob.
    CameraRawSlider &lightness = shown.child<CameraRawSlider>("lightnessSlider");
    QPoint knob;
    for (int x = 0; x < lightness.width() && knob.isNull(); ++x)
        knob = lightness.isOnKnob(QPoint(x, lightness.height() / 2)) ? QPoint(x, lightness.height() / 2) : knob;
    QTest::mouseDClick(&lightness, Qt::LeftButton, {}, knob);
    QVERIFY(shown.settings().lightness() == 0 && shown.settings().saturation() == -40);
    // Under Colorize, back to its start: saturation 25.
    shown.set(HueSaturationSettings(200, 70, 10, true));
    QTest::mouseDClick(&title("Saturation"), Qt::LeftButton);
    QVERIFY(shown.settings().saturation() == 25 && shown.settings().hue() == 200 && shown.settings().lightness() == 10);
    QCOMPARE(shown.child<QLineEdit>("saturationField").text(), QString("25"));
}

void HueSaturationSheetTests::aResetReplacesTheFieldsTyping()
{
    Sheet shown;
    QVERIFY(shown.show());
    shown.set(HueSaturationSettings(200, 70, 10, true));
    QLineEdit &hueField = shown.child<QLineEdit>("hueField");
    hueField.setFocus();
    QTRY_VERIFY(hueField.hasFocus());
    hueField.selectAll();
    QTest::keyClicks(&hueField, QStringLiteral("77"));
    QTest::mouseDClick(&shown.titled<QLabel>(QStringLiteral("Hue")), Qt::LeftButton);
    QCOMPARE(shown.settings().hue(), 0.0);
    QCOMPARE(hueField.text(), QString("0"));
    QVERIFY(!hueField.isModified());
}

void HueSaturationSheetTests::arrowKeysWalkEverySlider()
{
    // A hundred steps: whole values, the knob keeping its travel.
    const auto walk = [](Sheet &shown, const char *name, Qt::Key key) {
        CameraRawSlider &slider = shown.child<CameraRawSlider>(name);
        slider.setFocus();
        for (int step = 0; step < 100; ++step)
            QTest::keyClick(&slider, key);
    };
    const std::tuple<bool, const char *, Qt::Key, double> cases[] = {
        {false, "hueSlider", Qt::Key_Right, 53},     {false, "hueSlider", Qt::Key_Left, -53},    {false, "saturationSlider", Qt::Key_Right, 33},
        {false, "lightnessSlider", Qt::Key_Left, -33}, {true, "hueSlider", Qt::Key_Right, 53},    {true, "saturationSlider", Qt::Key_Right, 45},
        {true, "saturationSlider", Qt::Key_Left, 9},   {true, "lightnessSlider", Qt::Key_Right, 33}};
    for (const auto &[colorize, name, key, expected] : cases) {
        Sheet shown;
        QVERIFY(shown.show());
        shown.set(colorize ? HueSaturationSettings::colorizeStart() : HueSaturationSettings());
        walk(shown, name, key);
        const HueSaturationSettings &now = shown.settings();
        const double value = QByteArray(name) == "hueSlider" ? now.hue() : QByteArray(name) == "saturationSlider" ? now.saturation() : now.lightness();
        QVERIFY2(value == expected, qPrintable(QStringLiteral("%1 %2: %3").arg(QString::fromLatin1(name)).arg(colorize).arg(value)));
    }
    // An unchanged step leaves the field's typing.
    Sheet shown;
    QVERIFY(shown.show());
    QLineEdit &field = shown.child<QLineEdit>("hueField");
    field.setFocus();
    QTRY_VERIFY(field.hasFocus());
    field.selectAll();
    QTest::keyClicks(&field, QStringLiteral("4"));
    QTest::keyClick(&shown.child<CameraRawSlider>("hueSlider"), Qt::Key_Right);
    QVERIFY(field.isModified() && field.text() == "4" && shown.settings().hue() == 0);
}

void HueSaturationSheetTests::aResetKeepsPreviewOffInBothModes()
{
    for (const bool colorize : {false, true}) {
        Sheet shown;
        QVERIFY(shown.show());
        shown.session.updateHueSaturation(HueSaturationSettings(90, 60, 30, colorize), false);
        QTest::mouseDClick(&shown.titled<QLabel>(QStringLiteral("Saturation")), Qt::LeftButton);
        QCOMPARE(shown.settings().saturation(), colorize ? 25.0 : 0.0);
        QVERIFY(shown.settings().hue() == 90 && shown.settings().lightness() == 30);
        QCOMPARE(shown.child<QLineEdit>("saturationField").text(), colorize ? QString("25") : QString("0"));
        QVERIFY(!shown.session.hueSaturation().value().preview);
        CameraRawSlider &lightness = shown.child<CameraRawSlider>("lightnessSlider");
        QPoint knob;
        for (int x = 0; x < lightness.width() && knob.isNull(); ++x)
            knob = lightness.isOnKnob(QPoint(x, lightness.height() / 2)) ? QPoint(x, lightness.height() / 2) : knob;
        QTest::mouseDClick(&lightness, Qt::LeftButton, {}, knob);
        QVERIFY(shown.settings().lightness() == 0 && shown.settings().hue() == 90);
        QVERIFY(!shown.session.hueSaturation().value().preview);
    }
}

void HueSaturationSheetTests::aRangeUnderColorizeKeepsItsTracks()
{
    using Kind = CameraRawSliderTrack::Kind;
    Sheet shown;
    shown.set(HueSaturationSettings(200, 25, 0, true, ColorRange::greens));
    CameraRawSlider &hue = shown.child<CameraRawSlider>("hueSlider");
    CameraRawSlider &saturation = shown.child<CameraRawSlider>("saturationSlider");
    // Red to red, towards the chosen hue, Colorize's bounds.
    QVERIFY(hue.track().kind == Kind::spectrum && hue.track().degrees == 180);
    QVERIFY(saturation.track().kind == Kind::saturation && saturation.track().degrees == 200);
    QVERIFY(hue.value() == 556 && saturation.value() == 250);
}

void HueSaturationSheetTests::aTitleResetsOnALeftDoubleClickThenScrubs()
{
    Sheet shown;
    QVERIFY(shown.show());
    shown.set(HueSaturationSettings(30, 0, 0, false));
    QLabel &title = shown.titled<QLabel>(QStringLiteral("Hue"));
    const auto send = [&title](QEvent::Type type, double x, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QMouseEvent event(type, QPointF(x, 2), title.mapToGlobal(QPointF(x, 2)), button, buttons, Qt::NoModifier);
        QApplication::sendEvent(&title, &event);
    };
    // Other buttons and a resting sheet reset nothing.
    for (const Qt::MouseButton button : {Qt::RightButton, Qt::MiddleButton})
        send(QEvent::MouseButtonDblClick, 2, button, button);
    shown.sheet->setEnabled(false);
    send(QEvent::MouseButtonDblClick, 2, Qt::LeftButton, Qt::LeftButton);
    shown.sheet->setEnabled(true);
    QCOMPARE(shown.settings().hue(), 30.0);
    // A real double click resets; its second press scrubs.
    send(QEvent::MouseButtonPress, 2, Qt::LeftButton, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, 2, Qt::LeftButton, Qt::NoButton);
    send(QEvent::MouseButtonDblClick, 2, Qt::LeftButton, Qt::LeftButton);
    QCOMPARE(shown.settings().hue(), 0.0);
    send(QEvent::MouseMove, 14, Qt::NoButton, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, 14, Qt::LeftButton, Qt::NoButton);
    QCOMPARE(shown.settings().hue(), 12.0);
}

QTEST_MAIN(HueSaturationSheetTests)
#include "HueSaturationSheetTests.moc"
