#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include "UI/ColorPaletteControls.h"
#include "UI/FilterSheet.h"
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QProgressBar>
#include <QSlider>
#include <QtTest>

// Swift 1.3.3's Dither: the kind, its colours, its sheet.
namespace {
struct Shown {
    EditorSession session;
    QImage image;
    std::unique_ptr<FilterSheet> sheet;
    explicit Shown(int width = 40, int height = 30)
    {
        session.createDocument(width, height);
        image = BrushRaster::context(width, height, false);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                image.setPixelColor(x, y, QColor(x * 255 / std::max(1, width - 1), 120, 200));
        session.insert(ImportedImage(image, image, QStringLiteral("Colour")));
        session.beginFilter(FilterKind::dither);
        sheet = std::make_unique<FilterSheet>(session);
        sheet->show();
    }
    const DitherSettings &dither() const { return session.filterEdit().value().settings.dither; }
    void set(const std::function<void(DitherSettings &)> &change)
    {
        FilterSettings settings = session.filterEdit().value().settings;
        change(settings.dither);
        session.updateFilter(settings, true);
    }
    template <typename Widget> Widget &child(const char *name) const
    {
        auto *found = sheet->findChild<Widget *>(QString::fromLatin1(name));
        if (!found)
            throw std::runtime_error(name);
        return *found;
    }
    // The rows shown, by their titles, sorted.
    QStringList shown() const
    {
        QStringList titles;
        for (const QLabel *label : sheet->findChildren<QLabel *>()) {
            if (label->buddy() && !label->parentWidget()->isHidden() && label->isVisibleTo(sheet.get()))
                titles << label->text();
        }
        for (const QCheckBox *box : sheet->findChildren<QCheckBox *>()) {
            if (box->isVisibleTo(sheet.get()) && box->text() != QLatin1String("Preview"))
                titles << box->text();
        }
        if (child<QWidget>("ditherDark").isVisibleTo(sheet.get()))
            titles << QStringLiteral("Swatches");
        titles.sort();
        return titles;
    }
};

QStringList sorted(QStringList titles)
{
    titles.sort();
    return titles;
}

void pick(EditorSession &session, const PaletteColor &color)
{
    PickerHSB hsb = session.colorPicker().value().hsb;
    hsb.setRGB(color);
    session.setColorPickerHSB(hsb);
}
}

class DitherSheetTests : public QObject {
    Q_OBJECT
private slots:
    void theKindSitsUnderBloomAndPreviewsWhole();
    void itsCommitIsTheSettingsApplied();
    void theTwoColoursHaveTheirPicker();
    void theControlsFollowTheStyle();
    void theMenusAndFieldsWriteTheSettings();
    void theSheetPreviewsThePickersColour();
    void eachNumberWritesItsOwnSetting();
    void leftTheCharactersShowWhatWasKept();
    void theSlidersReachTheirEnds();
    void aCommitSaysItIsApplying();
    void thePickerRefusesWhenItCannot();
    void anAsciiCommitDrawsOnTheWorker();
    void rowsKeepSwiftsOrderHelpAndScale();
};

void DitherSheetTests::theKindSitsUnderBloomAndPreviewsWhole()
{
    QCOMPARE(rawValue(FilterKind::dither), QString("Dither"));
    const auto at = std::find(allFilterKinds.begin(), allFilterKinds.end(), FilterKind::dither);
    QVERIFY(at != allFilterKinds.end() && *(at - 1) == FilterKind::bloomGlow && *(at + 1) == FilterKind::tonalContrast);
    QVERIFY(!isAutomatic(FilterKind::dither) && !isImageAdjustment(FilterKind::dither));
    // Past the preview limit, Dither still previews at full size.
    EditorSession session;
    session.createDocument(3000, 10);
    const QImage wide = BrushRaster::context(3000, 10, false);
    session.insert(ImportedImage(wide, wide, QStringLiteral("Wide")));
    session.beginFilter(FilterKind::dither);
    QCOMPARE(session.filterEdit().value().previewScale, 1.0);
    QCOMPARE(session.filterEdit().value().previewSource.size(), QSize(3000, 10));
    session.cancelFilter();
    session.beginFilter(FilterKind::gaussianBlur);
    QVERIFY(session.filterEdit().value().previewScale < 1);
}

void DitherSheetTests::itsCommitIsTheSettingsApplied()
{
    Shown shown;
    shown.set([](DitherSettings &dither) {
        dither.style = DitherStyle::bayer4;
        dither.pixelSize = 3;
    });
    const DitherSettings settings = shown.dither();
    bool done = false;
    shown.session.commitFilter([&done] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(shown.session.history.undoName(), QString("Dither"));
    QCOMPARE(shown.session.activeLayer().value().asset.value().image(), settings.apply(shown.image));
}

void DitherSheetTests::theTwoColoursHaveTheirPicker()
{
    Shown shown;
    shown.session.openDitherColorPicker(false);
    QCOMPARE(shown.session.colorPicker().value().target.title(), QString("Color Picker (Dither Dark Color)"));
    QVERIFY(shown.session.colorPicker().value().original == PaletteColor::black());
    pick(shown.session, PaletteColor{0, 0, 1});
    shown.session.previewDitherColor();
    QVERIFY(shown.dither().dark == AdjustmentColor(0, 0, 1));
    // Cancel puts it back; OK on the light keeps it.
    shown.session.closeColorPicker(false);
    QVERIFY(shown.dither().dark == AdjustmentColor(0, 0, 0));
    shown.session.openDitherColorPicker(true);
    QCOMPARE(shown.session.colorPicker().value().target.title(), QString("Color Picker (Dither Light Color)"));
    pick(shown.session, PaletteColor{1, 1, 0});
    shown.session.closeColorPicker(true);
    QVERIFY(shown.dither().light == AdjustmentColor(1, 1, 0) && shown.dither().dark == AdjustmentColor(0, 0, 0));
    // The swatches open it; the panel's close takes it along.
    shown.set([](DitherSettings &dither) { dither.colors = DitherColors::twoColors; });
    QTest::mouseClick(&shown.child<SwatchButton>("ditherDark"), Qt::LeftButton);
    QVERIFY(shown.session.colorPicker().has_value() && !shown.session.colorPicker().value().target.light);
    pick(shown.session, PaletteColor{1, 0, 0});
    shown.session.previewDitherColor();
    shown.session.cancelFilter();
    QVERIFY(!shown.session.colorPicker() && !shown.session.filterEdit());
    // Not over another filter.
    shown.session.beginFilter(FilterKind::gaussianBlur);
    shown.session.openDitherColorPicker(true);
    QVERIFY(!shown.session.colorPicker());
}

void DitherSheetTests::theControlsFollowTheStyle()
{
    Shown shown;
    const QStringList common{"Style", "Pixel Size", "Density", "Contrast", "Colors", "Pixel Shape"};
    QCOMPARE(shown.shown(), sorted(common + QStringList{"Tones", "Diffusion"}));
    shown.set([](DitherSettings &dither) { dither.style = DitherStyle::bayer8; });
    QCOMPARE(shown.shown(), sorted(common + QStringList{"Tones"}));
    shown.set([](DitherSettings &dither) { dither.style = DitherStyle::lines; });
    QCOMPARE(shown.shown(), sorted(common + QStringList{"Cell Size", "Angle", "Light on Dark"}));
    shown.set([](DitherSettings &dither) { dither.style = DitherStyle::ascii; });
    QCOMPARE(shown.shown(), sorted(common + QStringList{"Cell Size", "Characters", "Light on Dark"}));
    shown.set([](DitherSettings &dither) { dither.style = DitherStyle::patterns; });
    QCOMPARE(shown.shown(), sorted(common + QStringList{"Light on Dark"}));
    // Pixel Shape needs chunky pixels; the swatches, Two Colors.
    shown.set([](DitherSettings &dither) {
        dither.pixelSize = 1;
        dither.colors = DitherColors::twoColors;
    });
    QCOMPARE(shown.shown(), sorted(QStringList{"Style", "Pixel Size", "Density", "Contrast", "Colors", "Light on Dark", "Swatches"}));
    // Style's menu: ten looks in four groups.
    QComboBox &style = shown.child<QComboBox>("ditherStyle");
    QStringList items;
    for (int index = 0; index < style.count(); ++index)
        items << (style.itemText(index).isEmpty() ? QStringLiteral("—") : style.itemText(index));
    QCOMPARE(items, (QStringList{"Atkinson (Classic Mac)", "Floyd–Steinberg", "—", "Bayer 2 × 2", "Bayer 4 × 4", "Bayer 8 × 8", "—", "Halftone Dots",
                                 "Halftone Lines", "Halftone Diamonds", "—", "Mac Patterns", "ASCII"}));
    QCOMPARE(style.currentText(), QString("Mac Patterns"));
    // Its quick preview never shows the busy line.
    QVERIFY(!shown.child<QProgressBar>("filterSpinner").isVisibleTo(shown.sheet.get()));
}

void DitherSheetTests::theMenusAndFieldsWriteTheSettings()
{
    Shown shown;
    QComboBox &style = shown.child<QComboBox>("ditherStyle");
    const int ascii = style.findData(int(DitherStyle::ascii));
    style.setCurrentIndex(ascii);
    emit style.activated(ascii);
    QVERIFY(shown.dither().style == DitherStyle::ascii);
    QComboBox &colors = shown.child<QComboBox>("ditherColors");
    colors.setCurrentIndex(2);
    emit colors.activated(2);
    QVERIFY(shown.dither().colors == DitherColors::original);
    QComboBox &shape = shown.child<QComboBox>("ditherPixelShape");
    shape.setCurrentIndex(1);
    emit shape.activated(1);
    QVERIFY(shown.dither().pixelShape == DitherPixelShape::dot);
    // Characters as typed, stored without line breaks.
    QLineEdit &characters = shown.child<QLineEdit>("ditherCharacters");
    QCOMPARE(characters.text(), DitherSettings::defaultCharacters());
    characters.setFocus();
    characters.selectAll();
    QTest::keyClicks(&characters, QStringLiteral("#o."));
    QCOMPARE(shown.dither().characters, QString("#o."));
    // Unfocused, it shows the settings as they stand.
    characters.clearFocus();
    shown.set([](DitherSettings &dither) { dither.characters = QStringLiteral("xy"); });
    QCOMPARE(characters.text(), QString("xy"));
}

void DitherSheetTests::theSheetPreviewsThePickersColour()
{
    Shown shown;
    shown.set([](DitherSettings &dither) { dither.colors = DitherColors::twoColors; });
    for (const bool light : {false, true}) {
        QTest::mouseClick(&shown.child<SwatchButton>(light ? "ditherLight" : "ditherDark"), Qt::LeftButton);
        QVERIFY(shown.session.colorPicker().has_value() && shown.session.colorPicker().value().target.light == light);
        // The sheet previews the working colour itself.
        pick(shown.session, PaletteColor{0, 1, 0});
        QVERIFY((light ? shown.dither().light : shown.dither().dark) == AdjustmentColor(0, 1, 0));
        shown.session.closeColorPicker(false);
        QVERIFY(shown.dither().dark == AdjustmentColor(0, 0, 0) && shown.dither().light == AdjustmentColor(1, 1, 1));
    }
    // The filter's OK takes an open picker's colour along.
    QTest::mouseClick(&shown.child<SwatchButton>("ditherLight"), Qt::LeftButton);
    pick(shown.session, PaletteColor{1, 0, 0});
    DitherSettings expected = shown.dither();
    expected.light = AdjustmentColor(1, 0, 0);
    bool done = false;
    shown.session.commitFilter([&done] { done = true; });
    QTRY_VERIFY(done);
    QVERIFY(!shown.session.colorPicker());
    QCOMPARE(shown.session.activeLayer().value().asset.value().image(), expected.apply(shown.image));
}

void DitherSheetTests::eachNumberWritesItsOwnSetting()
{
    Shown shown;
    shown.set([](DitherSettings &dither) { dither.style = DitherStyle::lines; });
    struct Row {
        const char *field;
        double DitherSettings::*setting;
        const char *typed;
        double expected;
    };
    const Row rows[] = {{"pixelSizeField", &DitherSettings::pixelSize, "5", 5}, {"cellSizeField", &DitherSettings::cellSize, "12", 12},
                        {"angleField", &DitherSettings::angle, "-30", -30},    {"densityField", &DitherSettings::density, "25", 25},
                        {"contrastField", &DitherSettings::contrast, "-40", -40}};
    for (const Row &row : rows) {
        const DitherSettings before = shown.dither();
        QLineEdit &field = shown.child<QLineEdit>(row.field);
        field.setFocus();
        field.selectAll();
        QTest::keyClicks(&field, QString::fromLatin1(row.typed));
        QTest::keyClick(&field, Qt::Key_Return);
        DitherSettings expected = before;
        expected.*row.setting = row.expected;
        QVERIFY2(shown.dither() == expected, row.field);
    }
    // Tones and Diffusion under a diffusion style.
    shown.set([](DitherSettings &dither) { dither.style = DitherStyle::floydSteinberg; });
    for (const auto &[name, setting, value] : {std::tuple("tonesField", &DitherSettings::levels, 4.0), std::tuple("diffusionField", &DitherSettings::diffusion, 60.0)}) {
        QLineEdit &field = shown.child<QLineEdit>(name);
        field.setFocus();
        field.selectAll();
        QTest::keyClicks(&field, QString::number(value));
        QTest::keyClick(&field, Qt::Key_Return);
        QCOMPARE(shown.dither().*setting, value);
    }
    // Light on Dark, under marks.
    shown.set([](DitherSettings &dither) { dither.style = DitherStyle::dots; });
    QCheckBox *glow = nullptr;
    for (QCheckBox *box : shown.sheet->findChildren<QCheckBox *>())
        glow = box->text() == QLatin1String("Light on Dark") ? box : glow;
    QVERIFY(glow && glow->isChecked());
    glow->click();
    QVERIFY(!shown.dither().lightOnDark);
}

void DitherSheetTests::leftTheCharactersShowWhatWasKept()
{
    Shown shown;
    QVERIFY(QTest::qWaitForWindowActive(shown.sheet.get()));
    shown.session.updateFilter(shown.session.filterEdit().value().settings, false);
    shown.set([](DitherSettings &dither) { dither.style = DitherStyle::ascii; });
    shown.session.updateFilter(shown.session.filterEdit().value().settings, false);
    QLineEdit &characters = shown.child<QLineEdit>("ditherCharacters");
    characters.setFocus();
    QTRY_VERIFY(characters.hasFocus());
    characters.selectAll();
    QTest::keyClicks(&characters, QString(70, u'x'));
    // Kept to 64; the typing shows until left.
    QCOMPARE(shown.dither().characters, QString(64, u'x'));
    QCOMPARE(characters.text(), QString(70, u'x'));
    shown.child<QLineEdit>("densityField").setFocus();
    QTRY_VERIFY(!characters.hasFocus());
    QCOMPARE(characters.text(), QString(64, u'x'));
    QVERIFY(!shown.session.filterEdit().value().preview);
}

void DitherSheetTests::theSlidersReachTheirEnds()
{
    Shown shown;
    shown.set([](DitherSettings &dither) { dither.style = DitherStyle::lines; });
    const std::tuple<const char *, double DitherSettings::*, double, double> rows[] = {
        {"pixelSizeSlider", &DitherSettings::pixelSize, 1, 32}, {"cellSizeSlider", &DitherSettings::cellSize, 4, 64},
        {"angleSlider", &DitherSettings::angle, -90, 90},       {"densitySlider", &DitherSettings::density, -100, 100},
        {"contrastSlider", &DitherSettings::contrast, -100, 100}};
    for (const auto &[name, setting, low, high] : rows) {
        QSlider &slider = shown.child<QSlider>(name);
        slider.setValue(slider.maximum());
        QCOMPARE(shown.dither().*setting, high);
        slider.setValue(slider.minimum());
        QCOMPARE(shown.dither().*setting, low);
    }
    QCOMPARE(shown.child<QSlider>("pixelSizeSlider").parentWidget()->toolTip(),
             QString("Make each dithered pixel this many pixels across, for a chunky old-screen look"));
    shown.set([](DitherSettings &dither) { dither.style = DitherStyle::atkinson; });
    for (const auto &[name, setting, low, high] : {std::tuple("tonesSlider", &DitherSettings::levels, 2.0, 8.0),
                                                   std::tuple("diffusionSlider", &DitherSettings::diffusion, 0.0, 100.0)}) {
        QSlider &slider = shown.child<QSlider>(name);
        slider.setValue(slider.maximum());
        QCOMPARE(shown.dither().*setting, high);
        slider.setValue(slider.minimum());
        QCOMPARE(shown.dither().*setting, low);
    }
}

void DitherSheetTests::aCommitSaysItIsApplying()
{
    // Quick previews never show the line; a commit does.
    Shown shown;
    bool done = false;
    shown.session.commitFilter([&done] { done = true; });
    QVERIFY(shown.child<QProgressBar>("filterSpinner").isVisibleTo(shown.sheet.get()));
    QCOMPARE(shown.child<QLabel>("filterActivity").text(), QString("Applying…"));
    QTRY_VERIFY(done);
}

void DitherSheetTests::thePickerRefusesWhenItCannot()
{
    Shown shown;
    // Busy, the palette rests.
    shown.session.setIsProjectBusy(true);
    shown.session.openDitherColorPicker(false);
    QVERIFY(!shown.session.colorPicker());
    shown.session.setIsProjectBusy(false);
    // Another picker open stays so.
    shown.session.openDitherColorPicker(true);
    shown.session.openDitherColorPicker(false);
    QVERIFY(shown.session.colorPicker().value().target.light);
    shown.session.closeColorPicker(false);
    // A style change keeps an open picker on its colour.
    shown.session.openDitherColorPicker(false);
    shown.set([](DitherSettings &dither) { dither.style = DitherStyle::ascii; });
    pick(shown.session, PaletteColor{1, 0, 1});
    shown.session.previewDitherColor();
    QVERIFY(shown.dither().dark == AdjustmentColor(1, 0, 1) && shown.dither().style == DitherStyle::ascii);
    shown.session.closeColorPicker(false);
    QVERIFY(shown.dither().dark == AdjustmentColor(0, 0, 0));
}

void DitherSheetTests::anAsciiCommitDrawsOnTheWorker()
{
    // Glyphs are drawn off the UI thread, in the commit.
    Shown shown(96, 48);
    shown.set([](DitherSettings &dither) {
        dither.style = DitherStyle::ascii;
        dither.cellSize = 12;
        dither.pixelSize = 1;
    });
    const DitherSettings settings = shown.dither();
    bool done = false;
    shown.session.commitFilter([&done] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(shown.session.activeLayer().value().asset.value().image(), settings.apply(shown.image));
}

void DitherSheetTests::rowsKeepSwiftsOrderHelpAndScale()
{
    Shown shown;
    // The shown rows, in the column's order, with their help.
    const auto rows = [&shown] {
        QStringList titles;
        QLayout *column = shown.sheet->layout();
        for (int index = 0; index < column->count(); ++index) {
            QWidget *row = column->itemAt(index)->widget();
            if (!row || row->isHidden())
                continue;
            if (auto *box = qobject_cast<QCheckBox *>(row); box && box->text() != QLatin1String("Preview"))
                titles << box->text() + u'|' + box->toolTip();
            for (QLabel *label : row->findChildren<QLabel *>(QString(), Qt::FindDirectChildrenOnly)) {
                if (label->buddy())
                    titles << label->text() + u'|' + row->toolTip();
            }
        }
        return titles;
    };
    QCOMPARE(rows(), (QStringList{"Style|", "Pixel Size|Make each dithered pixel this many pixels across, for a chunky old-screen look",
                                  "Tones|Tones per channel: 2 is pure black and white",
                                  "Diffusion|How much of each pixel's error spreads to its neighbors. Less gives flatter areas",
                                  "Density|More ink (darker) or less before dithering", "Contrast|", "Colors|",
                                  "Pixel Shape|Draw each chunky pixel as a solid square, or as a round dot like a dot-matrix screen"}));
    shown.set([](DitherSettings &dither) { dither.style = DitherStyle::ascii; });
    QCOMPARE(rows().mid(2, 2), (QStringList{"Cell Size|", "Characters|The characters to draw with, in any order: each spot gets the one whose ink best matches its tone"}));
    QCOMPARE(rows().last(), QString("Light on Dark|Draw the marks for the light tones on the dark color, like a glowing screen"));
    // Linear: half the Pixel Size travel is 16.5, so 17.
    QSlider &pixels = shown.child<QSlider>("pixelSizeSlider");
    pixels.setValue(pixels.maximum() / 2);
    QCOMPARE(shown.dither().pixelSize, 17.0);
    // Across Colors a picker keeps on; without an edit, none.
    shown.set([](DitherSettings &dither) { dither.colors = DitherColors::twoColors; });
    shown.session.updateFilter(shown.session.filterEdit().value().settings, false);
    QTest::mouseClick(&shown.child<SwatchButton>("ditherDark"), Qt::LeftButton);
    FilterSettings original = shown.session.filterEdit().value().settings;
    original.dither.colors = DitherColors::original;
    shown.session.updateFilter(original, false);
    pick(shown.session, PaletteColor{0, 1, 1});
    QVERIFY(shown.dither().dark == AdjustmentColor(0, 1, 1) && shown.dither().colors == DitherColors::original);
    QVERIFY(!shown.session.filterEdit().value().preview);
    shown.session.closeColorPicker(false);
    QVERIFY(shown.dither().dark == AdjustmentColor(0, 0, 0));
    shown.session.cancelFilter();
    shown.session.openDitherColorPicker(false);
    QVERIFY(!shown.session.colorPicker());
}

QTEST_MAIN(DitherSheetTests)
#include "DitherSheetTests.moc"
