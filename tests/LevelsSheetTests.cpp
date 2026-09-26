#include "LevelsSheetFixtures.h"
#include "Rendering/EyedropperIcon.h"
#include <QFrame>

// Swift's LevelsSheet: its controls, entries and states.
class LevelsSheetTests : public QObject {
    Q_OBJECT
private slots:
    void theSheetShowsTheEditAsSwifts();
    void theSheetIsLaidOutAsSwifts();
    void theHistogramDrawsTheChannelsBins();
    void entriesApplyAsSwiftsBindings();
    void entriesReadAndWriteTheLocale();
    void eyedroppersToggleAndShowTheirNote();
    void autoResetAndPreview();
    void cancelOkAndTheCommitsRest();
    void aNewThemeRecoloursTheEyedroppers();
};

void LevelsSheetTests::theSheetShowsTheEditAsSwifts()
{
    Sheet shown;
    // Until the bins land: the note, and Auto rests.
    QLabel &loading = shown.label(QStringLiteral("Loading histogram…"));
    QVERIFY(!loading.isHidden());
    QCOMPARE(loading.size(), loading.sizeHint());
    QCOMPARE(name(shown.child<QWidget>("levelsHistogram")), QString("Original RGB histogram"));
    QCOMPARE(shown.child<QLineEdit>("levelsGamma").text(), QString("1.00"));
    QCOMPARE(loading.pos(), QPoint(8, 8));
    QCOMPARE(loading.foregroundRole(), QPalette::WindowText);
    QCOMPARE(loading.font().pixelSize(), 10);
    QVERIFY(!shown.button("Contrast").isEnabled());
    QVERIFY(shown.show());
    QVERIFY(loading.isHidden());
    for (const char *text : {"Contrast", "Color", "Color + neutral midtones"})
        QVERIFY(shown.button(text).isEnabled());
    QWidget &histogram = shown.child<QWidget>("levelsHistogram");
    QCOMPARE(histogram.toolTip(), QString("Linear histogram with automatic vertical scaling. Tall spikes may extend beyond the graph; all tones "
                                          "from 0 to 255 remain included."));
    QComboBox &channel = *shown.sheet->findChild<QComboBox *>();
    QCOMPARE(channel.count(), 4);
    QCOMPARE(channel.itemText(3), QString("Blue"));
    // Swift's names, identifiers, decimals and trailing text.
    const std::pair<const char *, const char *> entries[] = {{"levelsInputblack", "0"},   {"levelsGamma", "1.00"},    {"levelsInputwhite", "255"},
                                                             {"levelsOutputblack", "0"}, {"levelsOutputwhite", "255"}};
    const QStringList names{"Input black", "Gamma", "Input white", "Output black", "Output white"};
    for (qsizetype index = 0; index < names.size(); ++index) {
        QLineEdit &entry = shown.child<QLineEdit>(entries[index].first);
        QCOMPARE(entry.text(), QString(entries[index].second));
        QCOMPARE(entry.placeholderText(), names[index]);
        QCOMPARE(name(entry), names[index]);
        QCOMPARE(entry.alignment(), Qt::AlignRight);
        QCOMPARE(name(shown.handle(names[index])), names[index]);
        QCOMPARE(shown.handle(names[index]).size(), QSize(22, 20));
        // Each entry's caption above it, five points apart.
        auto *title = entry.parentWidget()->findChild<QLabel *>();
        QCOMPARE(title->text(), names[index]);
        QCOMPARE(title->foregroundRole(), QPalette::PlaceholderText);
        QCOMPARE(title->font().pixelSize(), 10);
        QCOMPARE(entry.y() - title->geometry().bottom() - 1, 5);
        QCOMPARE(title->x(), 0);
    }
    // Linux names a combo box by its label's relation.
    const auto labels = QAccessible::queryAccessibleInterface(&channel)->relations(QAccessible::Label);
    QCOMPARE(labels.size(), 1);
    QCOMPARE(labels.first().first->text(QAccessible::Name), QString("Channel"));
    QCOMPARE(name(histogram), QString("Original RGB histogram"));
    QCOMPARE(shown.sheet->findChild<QCheckBox *>()->text(), QString("Preview"));
    QVERIFY(shown.sheet->findChild<QCheckBox *>()->isChecked());
    QVERIFY(shown.button("OK").isDefault() && !shown.button("Cancel").isDefault() && !shown.button("Reset").isDefault());
    QLabel &caption = shown.label(QStringLiteral("Original pixels · alpha-weighted histogram"));
    QCOMPARE(caption.foregroundRole(), QPalette::PlaceholderText);
    QCOMPARE(caption.font().pixelSize(), 10);
    // Levels over a selection says so; closed, no bars.
    shown.session.cancelLevels();
    QCOMPARE(histogram.grab().toImage().pixelColor(98, 0), shaded(shown.sheet->palette().color(QPalette::Window)));
    QPainterPath first;
    first.addRect(0, 0, 1, 1);
    shown.session.applySelection(first, SelectionMode::replace, QStringLiteral("Select"));
    shown.session.beginLevels();
    QCOMPARE(caption.text(), QString("Original pixels · selection and alpha-weighted histogram"));
}

void LevelsSheetTests::theSheetIsLaidOutAsSwifts()
{
    Sheet shown;
    QVERIFY(shown.show());
    // Padding 24, width 440, rows 16 apart.
    QCOMPARE(shown.sheet->width(), 440);
    const QWidget &picker = *shown.sheet->findChild<QComboBox *>()->parentWidget();
    QCOMPARE(picker.geometry().topLeft(), QPoint(24, 24));
    QCOMPARE(picker.width(), 180);
    QCOMPARE(shown.sheet->findChild<QComboBox *>()->geometry().right(), 179);
    QWidget &histogram = shown.child<QWidget>("levelsHistogram");
    QCOMPARE(histogram.geometry(), QRect(24, picker.geometry().bottom() + 17, 392, 150));
    const QWidget &input = shown.child<QWidget>("levelsInputHandles"), &output = shown.child<QWidget>("levelsOutputHandles");
    QCOMPARE(input.geometry(), QRect(24, histogram.y() + 150, 392, 20));
    const QWidget &black = *shown.child<QLineEdit>("levelsInputblack").parentWidget();
    QCOMPARE(black.geometry().topLeft(), QPoint(24, input.geometry().bottom() + 17));
    QCOMPARE(shown.child<QLineEdit>("levelsInputblack").width(), 80);
    // A row's last entry ends at the right padding.
    QCOMPARE(shown.child<QLineEdit>("levelsInputwhite").parentWidget()->geometry().right(), 415);
    QCOMPARE(shown.child<QLineEdit>("levelsOutputwhite").parentWidget()->geometry().right(), 415);
    // Samples and Auto pack to the left; Reset sits right.
    QVERIFY(shown.button("White").geometry().right() < 400 && shown.button("Color + neutral midtones").geometry().right() < 400);
    QCOMPARE(shown.button("Black").width(), shown.button("Black").sizeHint().width());
    QCOMPARE(shown.button("Reset").geometry().right(), 415);
    QCOMPARE(shown.button("Cancel").x(), 24);
    // Black to white across, fourteen points tall.
    QWidget *gradient = shown.sheet->childAt(220, output.y() - 7);
    QCOMPARE(gradient->geometry(), QRect(24, black.geometry().bottom() + 17, 392, 14));
    QCOMPARE(output.geometry(), QRect(24, gradient->y() + 14, 392, 20));
    const QImage ramp = gradient->grab().toImage();
    QCOMPARE(ramp.pixelColor(0, 7), QColor(Qt::black));
    QVERIFY(ramp.pixelColor(391, 7).red() >= 254 && std::abs(ramp.pixelColor(196, 7).red() - 128) <= 2);
    // Auto's caption sits six points over its buttons.
    const QLabel &autoTitle = shown.label(QStringLiteral("Auto"));
    QCOMPARE(shown.button("Contrast").y() - autoTitle.geometry().bottom() - 1, 6);
    QCOMPARE(shown.label(QStringLiteral("Sample")).font().pixelSize(), 10);
    // Swift's divider, and the small spinner beside OK.
    QFrame *divider = nullptr;
    for (QFrame *frame : shown.sheet->findChildren<QFrame *>()) {
        if (frame->frameShape() == QFrame::HLine)
            divider = frame;
    }
    QVERIFY(divider && divider->foregroundRole() == QPalette::Mid);
    const QProgressBar &spinner = *shown.sheet->findChild<QProgressBar *>();
    QVERIFY(spinner.isHidden() && spinner.minimum() == 0 && spinner.maximum() == 0 && !spinner.isTextVisible());
    QCOMPARE(spinner.width(), 40);
    QCOMPARE(shown.button("OK").geometry().right(), 415);
}

void LevelsSheetTests::theHistogramDrawsTheChannelsBins()
{
    Sheet shown;
    QVERIFY(shown.show());
    QWidget &histogram = shown.child<QWidget>("levelsHistogram");
    const QColor background = shaded(shown.sheet->palette().color(QPalette::Window)), gray(142, 142, 147);
    // RGB takes a third of each channel: 96 a twelfth.
    QImage drawn = histogram.grab().toImage();
    QCOMPARE(drawn.pixelColor(98, 0), gray);
    QCOMPARE(drawn.pixelColor(147, 126), gray);
    QCOMPARE(drawn.pixelColor(147, 124), background);
    QCOMPARE(drawn.pixelColor(200, 149), background);
    // Neighbouring bars join; their edges are smooth.
    QCOMPARE(drawn.pixelColor(99, 149), gray);
    QVERIFY(drawn.pixelColor(99, 0) != gray && drawn.pixelColor(99, 0) != background);
    // Red: 64 twice, 192 once, in SwiftUI's red.
    QComboBox &channel = *shown.sheet->findChild<QComboBox *>();
    channel.setFocus();
    QTest::keyClick(&channel, Qt::Key_Down);
    QCOMPARE(shown.session.levels().value().settings.channel, LevelsChannel::red);
    QCOMPARE(name(histogram), QString("Original Red histogram"));
    drawn = histogram.grab().toImage();
    QCOMPARE(drawn.pixelColor(98, 0), QColor(255, 66, 69));
    QCOMPARE(drawn.pixelColor(294, 75), QColor(255, 66, 69));
    QCOMPARE(drawn.pixelColor(294, 74), background);
    // A new channel repaints the whole graph.
    Repaints repaints(histogram);
    QTest::keyClick(&channel, Qt::Key_Down);
    QTRY_VERIFY(repaints.painted.contains(histogram.rect()));
    QCOMPARE(histogram.grab().toImage().pixelColor(147, 75), QColor(48, 209, 88));
    QTest::keyClick(&channel, Qt::Key_Down);
    QCOMPARE(histogram.grab().toImage().pixelColor(98, 0), QColor(0, 145, 255));
    // The session's channel shows in the picker.
    LevelsSettings settings = shown.session.levels().value().settings;
    settings.channel = LevelsChannel::green;
    shown.session.updateLevels(settings, true);
    QCOMPARE(channel.currentText(), QString("Green"));
}

void LevelsSheetTests::entriesApplyAsSwiftsBindings()
{
    Sheet shown;
    QVERIFY(shown.show());
    QLineEdit &black = shown.child<QLineEdit>("levelsInputblack"), &gamma = shown.child<QLineEdit>("levelsGamma");
    QLineEdit &white = shown.child<QLineEdit>("levelsInputwhite"), &outputWhite = shown.child<QLineEdit>("levelsOutputwhite");
    type(black, "30");
    QCOMPARE(shown.range().black, 0.0);
    QTest::keyClick(&black, Qt::Key_Return);
    QCOMPARE(shown.range().black, 30.0);
    // Clamped as the binding stores it, and shown so.
    type(white, "300");
    QTest::keyClick(&white, Qt::Key_Return);
    QCOMPARE(shown.range().white, 255.0);
    QCOMPARE(white.text(), QString("255"));
    type(gamma, "2.5");
    gamma.clearFocus();
    QCOMPARE(shown.range().gamma, 2.5);
    QCOMPARE(gamma.text(), QString("2.50"));
    // Text that is no number goes back to the level.
    type(white, "abc");
    white.clearFocus();
    QCOMPARE(white.text(), QString("255"));
    QCOMPARE(shown.range().white, 255.0);
    // Leaving untouched writes nothing back, not even rounded.
    shown.set(LevelRange{0, std::log(0.75) / std::log(0.5), 255, 0, 255});
    QCOMPARE(gamma.text(), QString("0.42"));
    gamma.setFocus();
    gamma.setCursorPosition(0);
    shown.set(LevelRange{0, std::log(0.75) / std::log(0.5), 255, 0, 200});
    QCOMPARE(gamma.cursorPosition(), 0);
    gamma.clearFocus();
    QCOMPARE(shown.range().gamma, std::log(0.75) / std::log(0.5));
    // Typing waits out a change from elsewhere, then applies.
    type(outputWhite, "12");
    shown.set(LevelRange{40, 2.5, 255, 0, 255});
    QCOMPARE(outputWhite.text(), QString("12"));
    QCOMPARE(black.text(), QString("40"));
    QTest::keyClick(&outputWhite, Qt::Key_Return);
    QCOMPARE(shown.range(), (LevelRange{40, 2.5, 255, 0, 12}));
    // White stays above black.
    type(black, "250");
    QTest::keyClick(&black, Qt::Key_Return);
    type(white, "100");
    QTest::keyClick(&white, Qt::Key_Return);
    QCOMPARE(shown.range().white, 251.0);
    QCOMPARE(white.text(), QString("251"));
    // Typing left as Levels closes elsewhere applies nowhere.
    type(black, "77");
    shown.session.cancelLevels();
    black.clearFocus();
    QVERIFY(!shown.session.levels());
}

void LevelsSheetTests::entriesReadAndWriteTheLocale()
{
    // Swift's number format follows the user's locale.
    Sheet shown;
    shown.sheet->setLocale(QLocale(QLocale::German, QLocale::Germany));
    QVERIFY(shown.show());
    shown.set(LevelRange{0, 2.5, 255, 0, 255});
    QLineEdit &gamma = shown.child<QLineEdit>("levelsGamma");
    QCOMPARE(gamma.text(), QString("2,50"));
    type(gamma, "1,25");
    QTest::keyClick(&gamma, Qt::Key_Return);
    QCOMPARE(shown.range().gamma, 1.25);
    QCOMPARE(gamma.text(), QString("1,25"));
}

void LevelsSheetTests::eyedroppersToggleAndShowTheirNote()
{
    Sheet shown;
    QVERIFY(shown.show());
    QPushButton &black = shown.button("Black"), &gray = shown.button("Gray"), &white = shown.button("White");
    QLabel *note = nullptr;
    for (QLabel *label : shown.sheet->findChildren<QLabel *>()) {
        if (label->wordWrap())
            note = label;
    }
    QVERIFY(note && note->isHidden());
    const QColor accent = shown.sheet->palette().color(QPalette::Highlight), secondary = shown.sheet->palette().color(QPalette::PlaceholderText);
    QCOMPARE(ink(black.icon()).rgb(), secondary.rgb());
    const int revision = shown.session.brushRevision();
    black.click();
    QCOMPARE(shown.session.levels().value().sampleMode, std::optional(LevelsSample::black));
    QCOMPARE(shown.session.brushRevision(), revision + 1);
    QVERIFY(black.isChecked() && !gray.isChecked() && !white.isChecked());
    QVERIFY(!note->isHidden());
    QCOMPARE(note->text(), QString("Click the original layer to set black. Click the eyedropper again to stop."));
    QCOMPARE(note->foregroundRole(), QPalette::PlaceholderText);
    // The rail's glyph at 14 points, in the accent.
    QCOMPARE(black.icon().availableSizes(), QList<QSize>{QSize(14, 14)});
    QImage glyph(14, 14, QImage::Format_ARGB32_Premultiplied);
    glyph.fill(Qt::transparent);
    {
        QPainter painter(&glyph);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(14.0 / 18, 14.0 / 18);
        painter.setPen(QPen(accent, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        EyedropperIcon::paint(painter);
    }
    QCOMPARE(black.icon().pixmap(QSize(14, 14)).toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied), glyph);
    QCOMPARE(ink(gray.icon()).rgb(), secondary.rgb());
    // Another takes over; a second click stops.
    gray.click();
    white.click();
    QCOMPARE(shown.session.levels().value().sampleMode, std::optional(LevelsSample::white));
    QVERIFY(!black.isChecked() && !gray.isChecked() && white.isChecked());
    QCOMPARE(note->text(), QString("Click the original layer to set white. Click the eyedropper again to stop."));
    white.click();
    QVERIFY(!shown.session.levels().value().sampleMode);
    QVERIFY(!white.isChecked() && note->isHidden());
}

void LevelsSheetTests::autoResetAndPreview()
{
    Sheet shown;
    QVERIFY(shown.show());
    const LevelsHistogram histogram = shown.session.levels().value().histogram;
    // Each button asks its own mode and drops the eyedropper.
    shown.session.setLevelsSampleMode(LevelsSample::gray);
    const std::pair<const char *, LevelsAuto> autos[] = {
        {"Contrast", LevelsAuto::contrast}, {"Color", LevelsAuto::color}, {"Color + neutral midtones", LevelsAuto::neutral}};
    std::vector<LevelsSettings> made;
    for (const auto &[text, mode] : autos) {
        shown.button(text).click();
        QCOMPARE(shown.session.levels().value().settings, settings(mode, histogram));
        QVERIFY(!shown.session.levels().value().sampleMode);
        made.push_back(shown.session.levels().value().settings);
    }
    QVERIFY(made[0] != made[1] && made[1] != made[2] && made[0] != made[2]);
    // Preview by the box, and Alt+P, keeping the settings.
    QCheckBox &preview = *shown.sheet->findChild<QCheckBox *>();
    preview.click();
    QVERIFY(!shown.session.levels().value().preview);
    QCOMPARE(shown.session.levels().value().settings, made[2]);
    QTest::keyClick(shown.sheet.get(), Qt::Key_P, Qt::AltModifier);
    QTRY_VERIFY(shown.session.levels().value().preview);
    QVERIFY(preview.isChecked());
    // Reset: no eyedropper, identity, the preview kept.
    shown.session.setLevelsSampleMode(LevelsSample::black);
    shown.button("Reset").click();
    QVERIFY(!shown.session.levels().value().sampleMode);
    QCOMPARE(shown.session.levels().value().settings, LevelsSettings());
    QVERIFY(shown.session.levels().value().preview);
}

void LevelsSheetTests::cancelOkAndTheCommitsRest()
{
    {
        Sheet shown;
        QVERIFY(shown.show());
        shown.button("Cancel").click();
        QVERIFY(!shown.session.levels());
    }
    Sheet shown;
    QVERIFY(shown.show());
    shown.set(LevelRange{0, 1, 128, 0, 255});
    const int steps = shown.session.history.undoCount();
    shown.button("OK").click();
    // Committing: the spinner shows and every control rests.
    QVERIFY(shown.session.levels().value().committing);
    QVERIFY(!shown.sheet->isEnabled());
    QProgressBar &spinner = *shown.sheet->findChild<QProgressBar *>();
    QVERIFY(!spinner.isHidden());
    QVERIFY(spinner.x() > shown.button("Cancel").geometry().right() + 150 && spinner.geometry().right() < shown.button("OK").x());
    QTRY_VERIFY(!shown.session.levels());
    QCOMPARE(shown.session.history.undoCount(), steps + 1);
    QCOMPARE(shown.session.history.undoName(), QString("Levels"));
    QCOMPARE(bytes(shown.session.activeLayer().value().asset.value().image()),
             (std::vector<uchar>{128, 128, 128, 255, 128, 128, 128, 255, 255, 191, 0, 255, 129, 129, 129, 255}));
}

void LevelsSheetTests::aNewThemeRecoloursTheEyedroppers()
{
    Sheet shown;
    QVERIFY(shown.show());
    shown.session.setLevelsSampleMode(LevelsSample::gray);
    QPalette theme = shown.sheet->palette();
    theme.setColor(QPalette::Highlight, QColor(200, 30, 40));
    theme.setColor(QPalette::PlaceholderText, QColor(20, 160, 60));
    shown.sheet->setPalette(theme);
    QCOMPARE(ink(shown.button("Gray").icon()).rgb(), QColor(200, 30, 40).rgb());
    QCOMPARE(ink(shown.button("White").icon()).rgb(), QColor(20, 160, 60).rgb());
}

QTEST_MAIN(LevelsSheetTests)
#include "LevelsSheetTests.moc"
