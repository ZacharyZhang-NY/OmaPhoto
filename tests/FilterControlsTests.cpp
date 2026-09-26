#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include "SelectionFixtures.h"
#include "UI/ColorPaletteControls.h"
#include "UI/CurvesControls.h"
#include "UI/FilterSheet.h"
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>
#include <QWheelEvent>
#include <QtTest>

// Each kind's controls, as Swift's body lays them.
namespace {
struct Sheet {
    EditorSession session;
    std::unique_ptr<FilterSheet> sheet;
    explicit Sheet(FilterKind kind)
    {
        session.createDocument(8, 8);
        QImage image = BrushRaster::context(8, 8, false);
        image.fill(QColor(120, 160, 200));
        session.insert(ImportedImage(image, image, QStringLiteral("Colour")));
        // The fill opens only over a selection.
        if (kind == FilterKind::contentAwareFill)
            session.applySelection(rectPath(QRectF(2, 2, 3, 3)), SelectionMode::replace, "Select");
        session.beginFilter(kind);
        sheet = std::make_unique<FilterSheet>(session);
    }
    template <typename Widget> Widget &child(const char *name) const
    {
        auto *found = sheet->findChild<Widget *>(QString::fromLatin1(name));
        if (!found)
            throw std::runtime_error(name);
        return *found;
    }
    const FilterSettings &settings() const { return session.filterEdit().value().settings; }
    // Types into a field and presses Return.
    void type(const char *name, const QString &text)
    {
        QLineEdit &field = child<QLineEdit>(name);
        field.setFocus();
        field.selectAll();
        QTest::keyClicks(&field, text);
        QTest::keyClick(&field, Qt::Key_Return);
    }
};

// Counts the paint events a widget receives.
struct Paints : QObject {
    int count = 0;
    bool eventFilter(QObject *, QEvent *event) override
    {
        count += event->type() == QEvent::Paint;
        return false;
    }
};

// The unit label beside a field, Swift's unitSuffix.
QString unit(QLineEdit &field)
{
    const QList<QLabel *> labels = field.parentWidget()->findChildren<QLabel *>(QString(), Qt::FindDirectChildrenOnly);
    return labels.last()->text();
}
}

class FilterControlsTests : public QObject {
    Q_OBJECT
private slots:
    void eachKindShowsSwiftsControls();
    void logarithmicSlidersGiveSmallValuesTheirTravel();
    void theSlidersShowTheSessionsNumbersWithoutWritingBack();
    void arrowKeysStepTheSlidersOn();
    void fieldsApplyWhatTheyReadAndShowSwiftsDecimals();
    void aWheelOnASliderReplacesItsFieldsTyping();
    void noiseTakesItsDistributionAndChannels();
    void removeBackgroundShowsQualityAndItsAdvancedRows();
    void gradientMapSwatchesOpenThePickerAndFollowIt();
    void theMapRepaintsWhenItChanges();
    void curvesEditThroughTheSheet();
};

void FilterControlsTests::eachKindShowsSwiftsControls()
{
    const std::pair<FilterKind, QStringList> rows[] = {
        {FilterKind::gaussianBlur, {"radius"}}, {FilterKind::motionBlur, {"angle", "distance"}}, {FilterKind::addNoise, {"amount"}},
        {FilterKind::lensCorrection, {"removeDistortion"}}, {FilterKind::exposure, {"exposure", "offset", "gamma"}},
        {FilterKind::grain, {"amount", "size", "roughness"}}, {FilterKind::contentAwareFill, {}}, {FilterKind::curves, {}},
        {FilterKind::gradientMap, {}}, {FilterKind::removeBackground, {"refine", "contrast", "shiftEdge"}}};
    for (const auto &[kind, names] : rows) {
        Sheet sheet(kind);
        QVERIFY2(sheet.sheet->findChildren<QSlider *>().size() == names.size(), qPrintable(rawValue(kind)));
        for (const QString &name : names) {
            QLineEdit &field = sheet.child<QLineEdit>(qPrintable(name + QStringLiteral("Field")));
            QCOMPARE(field.width(), 56);
            QVERIFY(field.alignment() & Qt::AlignRight);
            QVERIFY(!field.accessibleName().isEmpty() && field.placeholderText() == field.accessibleName());
            QSlider &slider = sheet.child<QSlider>(qPrintable(name + QStringLiteral("Slider")));
            // Swift's title: sixty points at least, naming the slider.
            QLabel *title = nullptr;
            for (QLabel *each : slider.parentWidget()->findChildren<QLabel *>())
                title = each->buddy() == &slider ? each : title;
            QVERIFY(title && title->text() == field.accessibleName() && title->minimumWidth() == 60);
        }
        QCOMPARE(sheet.sheet->findChild<CurvesControls *>() != nullptr, kind == FilterKind::curves);
        QCOMPARE(sheet.sheet->findChild<GradientMapControls *>() != nullptr, kind == FilterKind::gradientMap);
        QCOMPARE(sheet.sheet->findChild<QCheckBox *>(QStringLiteral("monochromatic")) != nullptr, kind == FilterKind::addNoise);
    }
    // Units as Swift's: px, degrees, percent, none.
    const std::tuple<FilterKind, const char *, QString> units[] = {
        {FilterKind::gaussianBlur, "radiusField", "px"}, {FilterKind::motionBlur, "angleField", "°"}, {FilterKind::motionBlur, "distanceField", "px"},
        {FilterKind::addNoise, "amountField", "%"}, {FilterKind::lensCorrection, "removeDistortionField", ""}, {FilterKind::grain, "sizeField", "px"},
        {FilterKind::exposure, "gammaField", ""}, {FilterKind::removeBackground, "refineField", "px"},
        {FilterKind::removeBackground, "contrastField", "%"}, {FilterKind::removeBackground, "shiftEdgeField", "px"}};
    for (const auto &[kind, name, expected] : units) {
        Sheet sheet(kind);
        QCOMPARE(unit(sheet.child<QLineEdit>(name)), expected);
    }
    Sheet lens(FilterKind::lensCorrection);
    QCOMPARE(lens.child<QLabel>("filterWords").text(),
             QString("Positive straightens lines that bow outward (barrel); negative, lines that bow inward (pincushion)."));
    QCOMPARE(lens.child<QLabel>("filterWords").foregroundRole(), QPalette::PlaceholderText);
    QCOMPARE(lens.child<QLabel>("filterWords").font().pixelSize(), 12);
    // Wrapped within the sheet, and never read as markup.
    for (const char *name : {"filterWords", "filterError", "filterLimited", "filterActivity"}) {
        QVERIFY2(lens.child<QLabel>(name).wordWrap(), name);
        QCOMPARE(lens.child<QLabel>(name).textFormat(), Qt::PlainText);
    }
    QCOMPARE(lens.sheet->layout()->contentsMargins(), QMargins(24, 24, 24, 24));
    Sheet fill(FilterKind::contentAwareFill);
    QCOMPARE(fill.child<QLabel>("filterWords").text(), QString("Fill the selection using surrounding pixels from this layer."));
    QCOMPARE(fill.child<QLabel>("filterLimited").font().pixelSize(), 12);
    QVERIFY(fill.child<QLabel>("filterWords").font().pixelSize() != 12);
}

void FilterControlsTests::logarithmicSlidersGiveSmallValuesTheirTravel()
{
    // Halfway along a logarithmic slider: the ends' geometric mean, rounded.
    const std::tuple<FilterKind, const char *, double FilterSettings::*, double> middles[] = {
        {FilterKind::gaussianBlur, "radiusSlider", &FilterSettings::radius, 5},
        {FilterKind::motionBlur, "distanceSlider", &FilterSettings::distance, 45},
        {FilterKind::motionBlur, "angleSlider", &FilterSettings::angle, 0},
        {FilterKind::addNoise, "amountSlider", &FilterSettings::amount, 6.3},
        {FilterKind::lensCorrection, "removeDistortionSlider", &FilterSettings::distortion, 0}};
    for (const auto &[kind, name, field, expected] : middles) {
        Sheet sheet(kind);
        sheet.child<QSlider>(name).setValue(500);
        QCOMPARE(sheet.settings().*field, expected);
    }
    Sheet exposure(FilterKind::exposure);
    exposure.child<QSlider>("gammaSlider").setValue(500);
    QCOMPARE(exposure.settings().exposure.gamma, 0.32);
    exposure.child<QSlider>("offsetSlider").setValue(250);
    QCOMPARE(exposure.settings().exposure.offset, -0.25);
    exposure.child<QSlider>("offsetSlider").setValue(1000);
    QCOMPARE(exposure.child<QLineEdit>("offsetField").text(), QString("0.5"));
    exposure.child<QSlider>("exposureSlider").setValue(333);
    QCOMPARE(exposure.settings().exposure.exposure, -6.68);
    Sheet grain(FilterKind::grain);
    grain.child<QSlider>("amountSlider").setValue(333);
    QCOMPARE(grain.settings().grain.amount, 33.0);
    grain.child<QSlider>("sizeSlider").setValue(500);
    QCOMPARE(grain.settings().grain.size, 3.2);
    grain.child<QSlider>("roughnessSlider").setValue(333);
    QCOMPARE(grain.settings().grain.roughness, 33.0);
}

void FilterControlsTests::theSlidersShowTheSessionsNumbersWithoutWritingBack()
{
    Sheet sheet(FilterKind::gaussianBlur);
    QSlider &radius = sheet.child<QSlider>("radiusSlider");
    // Radius 5 lies halfway along its logarithmic travel.
    sheet.session.updateFilter(FilterSettings{.radius = 5}, true);
    QCOMPARE(radius.value(), 500);
    // Between steps, a number shows without rounding back.
    sheet.session.updateFilter(FilterSettings{.radius = 5.04}, true);
    QCOMPARE(radius.value(), 501);
    QCOMPARE(sheet.settings().radius, 5.04);
    QCOMPARE(sheet.child<QLineEdit>("radiusField").text(), QString("5"));
    // Return, untouched, keeps 5.04, not the shown 5.
    QTest::keyClick(&sheet.child<QLineEdit>("radiusField"), Qt::Key_Return);
    QCOMPARE(sheet.settings().radius, 5.04);
    // With Preview off, a slider keeps it off.
    sheet.session.updateFilter(sheet.settings(), false);
    radius.setValue(700);
    QVERIFY(!sheet.session.filterEdit().value().preview && sheet.settings().radius == 23.9);
    QVERIFY(!sheet.child<QCheckBox>("filterPreview").isChecked());
}

void FilterControlsTests::arrowKeysStepTheSlidersOn()
{
    // A step rounding to the same value leaves the thumb.
    Sheet blur(FilterKind::gaussianBlur);
    QSlider &radius = blur.child<QSlider>("radiusSlider");
    for (int press = 0; press < 100; ++press)
        QTest::keyClick(&radius, Qt::Key_Right);
    QCOMPARE(blur.settings().radius, 3.8);
    Sheet motion(FilterKind::motionBlur);
    QSlider &angle = motion.child<QSlider>("angleSlider");
    for (int press = 0; press < 100; ++press)
        QTest::keyClick(&angle, Qt::Key_Right);
    QCOMPARE(motion.settings().angle, 31.0);
}

void FilterControlsTests::fieldsApplyWhatTheyReadAndShowSwiftsDecimals()
{
    Sheet sheet(FilterKind::motionBlur);
    // English would group thousands; the twin's fields never do.
    sheet.child<QLineEdit>("distanceField").setLocale(QLocale(QLocale::English, QLocale::UnitedStates));
    QCOMPARE(sheet.child<QLineEdit>("distanceField").text(), QString("10"));
    sheet.type("distanceField", QStringLiteral("2000"));
    // Ungrouped, as every field of the twin.
    QVERIFY(sheet.settings().distance == 2000 && sheet.child<QLineEdit>("distanceField").text() == QString("2000"));
    // Normalizing clamps what passes the range; words go back.
    sheet.type("angleField", QStringLiteral("135"));
    QCOMPARE(sheet.settings().angle, 90.0);
    QCOMPARE(sheet.child<QLineEdit>("angleField").text(), QString("90"));
    for (const QString &words : {QStringLiteral("steep"), QStringLiteral("inf")}) {
        sheet.type("angleField", words);
        QCOMPARE(sheet.settings().angle, 90.0);
        QCOMPARE(sheet.child<QLineEdit>("angleField").text(), QString("90"));
    }
    // Up to the kind's decimals, none trailing.
    Sheet exposure(FilterKind::exposure);
    exposure.type("offsetField", QStringLiteral("0.12345"));
    QCOMPARE(exposure.settings().exposure.offset, 0.12345);
    QCOMPARE(exposure.child<QLineEdit>("offsetField").text(), QString("0.1235"));
    exposure.type("exposureField", QStringLiteral("1.5"));
    QCOMPARE(exposure.child<QLineEdit>("exposureField").text(), QString("1.5"));
    // A German field reads and writes its own decimal mark.
    exposure.child<QLineEdit>("exposureField").setLocale(QLocale(QLocale::German, QLocale::Germany));
    exposure.type("exposureField", QStringLiteral("2,25"));
    QCOMPARE(exposure.settings().exposure.exposure, 2.25);
    QCOMPARE(exposure.child<QLineEdit>("exposureField").text(), QString("2,25"));
    // A field being typed in keeps its typing.
    QLineEdit &gamma = exposure.child<QLineEdit>("gammaField");
    gamma.setFocus();
    gamma.selectAll();
    QTest::keyClicks(&gamma, QStringLiteral("2."));
    exposure.child<QSlider>("exposureSlider").setValue(0);
    QCOMPARE(gamma.text(), QString("2."));
}

void FilterControlsTests::aWheelOnASliderReplacesItsFieldsTyping()
{
    Sheet sheet(FilterKind::motionBlur);
    const auto wheel = [&sheet](const char *name) {
        QSlider &slider = sheet.child<QSlider>(name);
        QWheelEvent notch(QPointF(10, 5), slider.mapToGlobal(QPointF(10, 5)), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
        QApplication::sendEvent(&slider, &notch);
    };
    const auto typing = [&sheet](const char *name, const QString &text) {
        QLineEdit &field = sheet.child<QLineEdit>(name);
        field.setFocus();
        field.selectAll();
        QTest::keyClicks(&field, text);
        return &field;
    };
    // A notch that leaves the rounded value leaves the typing.
    QLineEdit *distance = typing("distanceField", QStringLiteral("123"));
    wheel("distanceSlider");
    QVERIFY(sheet.settings().distance == 10 && distance->text() == "123" && distance->isModified());
    // One that moves it shows the value instead.
    QLineEdit *angle = typing("angleField", QStringLiteral("45"));
    wheel("angleSlider");
    QVERIFY(sheet.settings().angle == 1 && angle->text() == "1" && !angle->isModified());
    // Return then keeps the slider's value.
    QTest::keyClick(angle, Qt::Key_Return);
    QCOMPARE(sheet.settings().angle, 1.0);
}

void FilterControlsTests::noiseTakesItsDistributionAndChannels()
{
    Sheet sheet(FilterKind::addNoise);
    QToolButton &uniform = sheet.child<QToolButton>("uniformChoice"), &gaussian = sheet.child<QToolButton>("gaussianChoice");
    QVERIFY(uniform.isChecked() && !gaussian.isChecked());
    QCOMPARE(uniform.parentWidget()->findChild<QLabel *>()->text(), QString("Distribution"));
    gaussian.click();
    QVERIFY(sheet.settings().gaussian);
    QCheckBox &mono = sheet.child<QCheckBox>("monochromatic");
    mono.click();
    QVERIFY(sheet.settings().monochromatic);
    // The session's own changes show without being written back.
    sheet.session.updateFilter(FilterSettings{.amount = 20}, true);
    QVERIFY(uniform.isChecked() && !mono.isChecked());
}

void FilterControlsTests::removeBackgroundShowsQualityAndItsAdvancedRows()
{
    Sheet sheet(FilterKind::removeBackground);
    QCOMPARE(sheet.child<QLabel>("filterWords").text(), QString("Hide the background behind a layer mask, keeping the foreground subjects. "
                                                                "The pixels stay, so the background can be painted back at any time."));
    QVERIFY(sheet.child<QLabel>("filterWords").font().pixelSize() != 12);
    // Swift's segments: Basic, then Advanced, the label hidden.
    QWidget &quality = sheet.child<QWidget>("backgroundQuality");
    QCOMPARE(quality.accessibleName(), QString("Quality"));
    QCOMPARE(quality.layout()->spacing(), 0);
    QCOMPARE(quality.toolTip(), QString("Basic is quick; Advanced refines the mask against the layer's own detail, for hair and fur"));
    const QList<QToolButton *> choices = quality.findChildren<QToolButton *>();
    QVERIFY(choices.size() == 2 && choices[0]->text() == "Basic" && choices[1]->text() == "Advanced" && choices[0]->isChecked());
    const std::pair<const char *, QString> rows[] = {{"refineSlider", "Pull the mask onto the image's own edges, which recovers hair and fur"},
                                                     {"contrastSlider", "Clear the haze that leaves background showing through thin areas"},
                                                     {"shiftEdgeSlider", "Shrink the mask to drop the rim of background color around the subject, or grow it"}};
    // Basic hides Advanced's rows; Advanced shows them.
    for (const auto &[name, help] : rows)
        QVERIFY2(!sheet.child<QSlider>(name).parentWidget()->isVisibleTo(sheet.sheet.get()), name);
    choices[1]->click();
    QVERIFY(sheet.settings().backgroundQuality == BackgroundQuality::advanced && choices[1]->isChecked());
    for (const auto &[name, help] : rows) {
        QVERIFY2(sheet.child<QSlider>(name).parentWidget()->isVisibleTo(sheet.sheet.get()), name);
        QCOMPARE(sheet.child<QSlider>(name).parentWidget()->toolTip(), help);
    }
    // Each slider sets its own setting over Swift's range.
    sheet.child<QSlider>("refineSlider").setValue(500);
    sheet.child<QSlider>("contrastSlider").setValue(1000);
    sheet.child<QSlider>("shiftEdgeSlider").setValue(0);
    QVERIFY(sheet.settings().refineEdges == 20 && sheet.settings().matteContrast == 100 && sheet.settings().shiftEdge == -10);
    // A quality set elsewhere shows at once.
    FilterSettings basic = sheet.settings();
    basic.backgroundQuality = BackgroundQuality::basic;
    sheet.session.updateFilter(basic, true);
    QVERIFY(choices[0]->isChecked() && !sheet.child<QSlider>("refineSlider").parentWidget()->isVisibleTo(sheet.sheet.get()));
}

void FilterControlsTests::gradientMapSwatchesOpenThePickerAndFollowIt()
{
    Sheet sheet(FilterKind::gradientMap);
    SwatchButton &shadows = sheet.child<SwatchButton>("shadowsSwatch");
    QCOMPARE(shadows.toolTip(), QString("Choose the shadows color"));
    shadows.click();
    QVERIFY(!sheet.session.colorPicker().value().target.highlights);
    sheet.session.closeColorPicker(false);
    SwatchButton &highlights = sheet.child<SwatchButton>("highlightsSwatch");
    QCOMPARE(highlights.size(), QSize(24, 24));
    QCOMPARE(highlights.toolTip(), QString("Choose the highlights color"));
    QCOMPARE(highlights.accessibleName(), QString("Highlights color"));
    highlights.click();
    QVERIFY(sheet.session.colorPicker().value().target.highlights);
    // The sheet hands the picker's working colour to the map.
    PickerHSB hsb = sheet.session.colorPicker().value().hsb;
    hsb.setRGB(PaletteColor{1, 0, 0});
    sheet.session.setColorPickerHSB(hsb);
    QCOMPARE(sheet.settings().gradientMap.highlights, AdjustmentColor(1, 0, 0));
    sheet.session.closeColorPicker(true);
    QCheckBox &reverse = sheet.child<QCheckBox>("gradientMapReverse");
    reverse.click();
    QVERIFY(sheet.settings().gradientMap.reversed);
    FilterSettings forward = sheet.settings();
    forward.gradientMap.reversed = false;
    sheet.session.updateFilter(forward, true);
    QVERIFY(!reverse.isChecked());
    reverse.click();
    // Each swatch shows its own end.
    QCOMPARE(highlights.grab().toImage().pixelColor(12, 12).rgb(), QColor(Qt::red).rgb());
    QCOMPARE(shadows.grab().toImage().pixelColor(12, 12).rgb(), QColor(Qt::black).rgb());
    // Reversed, the bar runs from the highlights to the shadows.
    const QImage drawn = sheet.child<QWidget>("gradientMapBar").grab().toImage();
    QCOMPARE(drawn.height(), 20);
    // A 35% black rim lies over the gradient's edge.
    const int middle = drawn.width() / 2;
    QVERIFY(std::abs(drawn.pixelColor(middle, 0).red() - int(std::lround(drawn.pixelColor(middle, 10).red() * 0.65))) <= 2);
    for (const int x : {2, drawn.width() / 2, drawn.width() - 3}) {
        const QColor seen = drawn.pixelColor(x, 10);
        const int red = int(std::lround(255 * (1 - (x + 0.5) / drawn.width())));
        QVERIFY2(std::abs(seen.red() - red) <= 3 && seen.green() == 0 && seen.blue() == 0, qPrintable(seen.name()));
    }
}

void FilterControlsTests::theMapRepaintsWhenItChanges()
{
    Sheet sheet(FilterKind::gradientMap);
    sheet.sheet->show();
    QVERIFY(QTest::qWaitForWindowExposed(sheet.sheet.get()));
    Paints bar, swatch;
    sheet.child<QWidget>("gradientMapBar").installEventFilter(&bar);
    sheet.child<SwatchButton>("shadowsSwatch").installEventFilter(&swatch);
    QTest::qWait(50);
    bar.count = swatch.count = 0;
    FilterSettings changed = sheet.settings();
    changed.gradientMap.shadows = AdjustmentColor(0, 1, 0);
    sheet.session.updateFilter(changed, true);
    QTRY_VERIFY(bar.count > 0 && swatch.count > 0);
}

void FilterControlsTests::curvesEditThroughTheSheet()
{
    Sheet sheet(FilterKind::curves);
    CurvesSettings lifted;
    lifted.channels[0] = {{0, 40}, {255, 255}};
    sheet.session.updateFilter(FilterSettings{.curves = lifted}, true);
    CurvesSettings red = lifted;
    red.channel = LevelsChannel::red;
    sheet.session.updateFilter(FilterSettings{.curves = red}, true);
    QCOMPARE(sheet.sheet->findChild<CurvesControls *>()->findChild<QComboBox *>()->currentIndex(), 1);
    sheet.session.updateFilter(FilterSettings{.curves = lifted}, true);
    // The hosted controls read the session's curve and write it.
    for (QPushButton *button : sheet.sheet->findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("Reset curve"))
            button->click();
    }
    QVERIFY(sheet.settings().curves == CurvesSettings());
}

QTEST_MAIN(FilterControlsTests)
#include "FilterControlsTests.moc"
