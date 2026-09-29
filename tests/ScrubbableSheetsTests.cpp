#include "Document/BrushStroke.h"
#include "ScrubFixtures.h"
#include "UI/CanvasSizeSheet.h"
#include "UI/ColorPickerSheet.h"
#include "UI/EffectsSheet.h"
#include "UI/FilterSheet.h"
#include "UI/HueSaturationSheet.h"
#include "UI/ImageSizeSheet.h"
#include "UI/LevelsSheet.h"
#include <QCheckBox>
#include <QComboBox>

// Swift's scrubbable titles in the sheets and panels.
namespace {
// A document with one coloured layer, left active.
void paint(EditorSession &session)
{
    session.createDocument(8, 8);
    QImage image = BrushRaster::context(8, 8, false);
    image.fill(QColor(120, 160, 200));
    session.insert(ImportedImage(image, image, QStringLiteral("Colour")));
}

void choose(QComboBox &box, const QString &text)
{
    const int index = box.findText(text);
    box.setCurrentIndex(index);
    emit box.activated(index);
}
}

class ScrubbableSheetsTests : public QObject {
    Q_OBJECT
private slots:
    void effectsTitlesScrubToTheTypedLimit();
    void hueSaturationTitlesFollowColorize();
    void levelsTitlesScrubTonesAndGamma();
    void filterTitlesMoveALastDecimalAPoint();
    void colorPickerChannelsScrubWholeLevels();
    void filterTitlesKeepEveryDecimalsSensitivity();
    void canvasSizeTitlesScrubInTheirUnit();
    void canvasSizeCentimetresScrubTheirLimits();
    void imageSizeTitlesScrubPixelsPrintSizeAndResolution();
    void imageSizeLimitsFollowUnitsLockAndResolution();
};

void ScrubbableSheetsTests::effectsTitlesScrubToTheTypedLimit()
{
    EditorSession session;
    paint(session);
    session.addEffect(LayerEffectKind::stroke);
    EffectsSheet sheet(session, LayerEffectKind::stroke);
    showActive(sheet);
    const auto stroke = [&session] { return session.editingEffects().stroke.value(); };
    drag(scrubbed(sheet, "Size"), 10);
    QCOMPARE(stroke().size, 14.0);
    // Past the slider's end, to what typing reaches.
    drag(scrubbed(sheet, "Size"), 9000);
    QCOMPARE(stroke().size, 500.0);
    drag(scrubbed(sheet, "Size"), -9000);
    QCOMPARE(stroke().size, 0.0);
    drag(scrubbed(sheet, "Opacity"), -30.5);
    QVERIFY(qAbs(stroke().opacity - 0.695) < 1e-12);
    QCOMPARE(scrubOverTyping(find<QLineEdit>(sheet, "sizeField"), scrubbed(sheet, "Size"), 3), QString("3"));
}

void ScrubbableSheetsTests::hueSaturationTitlesFollowColorize()
{
    EditorSession session;
    paint(session);
    session.beginHueSaturation();
    HueSaturationSheet sheet(session);
    showActive(sheet);
    const auto settings = [&session] { return session.hueSaturation().value().settings; };
    drag(scrubbed(sheet, "Hue"), 25.5);
    QCOMPARE(settings().hue(), 25.5);
    drag(scrubbed(sheet, "Hue"), 900);
    QCOMPARE(settings().hue(), 180.0);
    drag(scrubbed(sheet, "Hue"), -900);
    QCOMPARE(settings().hue(), -180.0);
    drag(scrubbed(sheet, "Saturation"), -900);
    QCOMPARE(settings().saturation(), -100.0);
    drag(scrubbed(sheet, "Lightness"), 40);
    QCOMPARE(settings().lightness(), 40.0);
    // Colorize sets hue and saturation outright: new ranges.
    for (QCheckBox *box : sheet.findChildren<QCheckBox *>())
        if (box->text() == "Colorize")
            box->click();
    QVERIFY(settings().colorize);
    drag(scrubbed(sheet, "Hue"), 900);
    QCOMPARE(settings().hue(), 360.0);
    drag(scrubbed(sheet, "Saturation"), -900);
    QCOMPARE(settings().saturation(), 0.0);
    QCOMPARE(scrubOverTyping(find<QLineEdit>(sheet, "lightnessField"), scrubbed(sheet, "Lightness"), 3), QString("3"));
}

void ScrubbableSheetsTests::levelsTitlesScrubTonesAndGamma()
{
    EditorSession session;
    paint(session);
    session.beginLevels();
    LevelsSheet sheet(session);
    showActive(sheet);
    const auto range = [&session] { return session.levels().value().settings.current(); };
    // Gamma moves a hundredth a point, within 0.1 and 9.99.
    drag(scrubbed(sheet, "Gamma"), 50);
    QVERIFY(qAbs(range().gamma - 1.5) < 1e-12);
    drag(scrubbed(sheet, "Gamma"), 9000);
    QCOMPARE(range().gamma, 9.99);
    drag(scrubbed(sheet, "Gamma"), -9000);
    QCOMPARE(range().gamma, 0.1);
    drag(scrubbed(sheet, "Input black"), 20);
    QCOMPARE(range().black, 20.0);
    drag(scrubbed(sheet, "Input black"), -900);
    QCOMPARE(range().black, 0.0);
    drag(scrubbed(sheet, "Output white"), -30.5);
    QCOMPARE(range().outputWhite, 224.5);
    drag(scrubbed(sheet, "Output white"), 900);
    QCOMPARE(range().outputWhite, 255.0);
    QCOMPARE(scrubOverTyping(find<QLineEdit>(sheet, "levelsOutputblack"), scrubbed(sheet, "Output black"), 3), QString("3"));
}

void ScrubbableSheetsTests::filterTitlesMoveALastDecimalAPoint()
{
    EditorSession session;
    paint(session);
    session.beginFilter(FilterKind::gaussianBlur);
    FilterSheet sheet(session);
    showActive(sheet);
    const auto radius = [&session] { return session.filterEdit().value().settings.radius; };
    const double start = radius();
    // One decimal: a point is a tenth.
    drag(scrubbed(sheet, "Radius"), 10);
    QVERIFY(qAbs(radius() - (start + 1)) < 1e-9);
    drag(scrubbed(sheet, "Radius"), 90000);
    QCOMPARE(radius(), 250.0);
    drag(scrubbed(sheet, "Radius"), -90000);
    QCOMPARE(radius(), 0.1);
    QCOMPARE(scrubOverTyping(find<QLineEdit>(sheet, "radiusField"), scrubbed(sheet, "Radius"), 30), QString("3.1"));
}

void ScrubbableSheetsTests::filterTitlesKeepEveryDecimalsSensitivity()
{
    EditorSession session;
    paint(session);
    session.beginFilter(FilterKind::gaussianBlur);
    {
        FilterSheet sheet(session);
        showActive(sheet);
        const double start = session.filterEdit().value().settings.radius;
        // No snap: a point and a half is 0.15.
        drag(scrubbed(sheet, "Radius"), 1.5);
        QVERIFY(qAbs(session.filterEdit().value().settings.radius - (start + 0.15)) < 1e-9);
    }
    session.cancelFilter();
    session.beginFilter(FilterKind::exposure);
    {
        FilterSheet sheet(session);
        showActive(sheet);
        const auto exposure = [&session] { return session.filterEdit().value().settings.exposure; };
        drag(scrubbed(sheet, "Exposure"), 50.5);
        QVERIFY(qAbs(exposure().exposure - 0.505) < 1e-9);
        // Four decimals: fifty points are 0.005.
        drag(scrubbed(sheet, "Offset"), 50);
        QVERIFY(qAbs(exposure().offset - 0.005) < 1e-12);
        const double gamma = exposure().gamma;
        drag(scrubbed(sheet, "Gamma"), 25);
        QVERIFY(qAbs(exposure().gamma - (gamma + 0.25)) < 1e-9);
        drag(scrubbed(sheet, "Gamma"), -90000);
        QCOMPARE(exposure().gamma, 0.01);
    }
    session.cancelFilter();
    session.beginFilter(FilterKind::motionBlur);
    FilterSheet sheet(session);
    showActive(sheet);
    // No decimals: a point is a degree, fractions kept.
    const double angle = session.filterEdit().value().settings.angle;
    drag(scrubbed(sheet, "Angle"), 15.5);
    QCOMPARE(session.filterEdit().value().settings.angle, angle + 15.5);
}

void ScrubbableSheetsTests::colorPickerChannelsScrubWholeLevels()
{
    EditorSession session;
    paint(session);
    session.openColorPicker(false);
    ColorPickerSheet sheet(session, [](bool) {});
    showActive(sheet);
    const auto color = [&session] { return session.colorPicker().value().color(); };
    // Whole levels: 100.6 points lands on 101.
    drag(scrubbed(sheet, "R"), 100.6);
    QCOMPARE(color().red, 101 / 255.0);
    drag(scrubbed(sheet, "G"), 900);
    QCOMPARE(color().green, 1.0);
    drag(scrubbed(sheet, "B"), 30);
    QCOMPARE(std::lround(color().blue * 255), 30L);
    drag(scrubbed(sheet, "R"), -900);
    QCOMPARE(color().red, 0.0);
    QCOMPARE(scrubOverTyping(find<QLineEdit>(sheet, "b"), scrubbed(sheet, "B"), 3), QString("33"));
}

void ScrubbableSheetsTests::canvasSizeTitlesScrubInTheirUnit()
{
    CanvasDocument document{1200, 800};
    document.resolution = 72;
    CanvasSizeSheet sheet(document, PaletteColor::black(), PaletteColor::white(), [](std::optional<CanvasSizeOptions>) {});
    showActive(sheet);
    QLineEdit &width = find<QLineEdit>(sheet, "canvasWidth");
    QLineEdit &height = find<QLineEdit>(sheet, "canvasHeight");
    // A held drag keeps its start through each reshape.
    QLabel &title = scrubbed(sheet, "Width");
    drag(title, 10, false);
    QCOMPARE(width.text(), QString("1210"));
    send(title, QEvent::MouseMove, QPointF(22, 2), Qt::NoButton, Qt::LeftButton);
    QCOMPARE(width.text(), QString("1220"));
    send(title, QEvent::MouseButtonRelease, QPointF(22, 2), Qt::LeftButton, Qt::NoButton);
    drag(title, -9.6);
    QCOMPARE(width.text(), QString("1210"));
    drag(scrubbed(sheet, "Height"), 90000);
    QCOMPARE(height.text(), QString("30000"));
    drag(scrubbed(sheet, "Height"), -90000);
    QCOMPARE(height.text(), QString("1"));
    // A percent is twelve points of a 1,200-pixel side.
    choose(find<QComboBox>(sheet, "canvasUnits"), "Percent");
    drag(scrubbed(sheet, "Width"), 12);
    QCOMPARE(width.text(), QString("102"));
    choose(find<QComboBox>(sheet, "canvasUnits"), "Inches");
    drag(scrubbed(sheet, "Width"), -144);
    QCOMPARE(width.text(), QString("15"));
    // Relative counts from the current size, locked keeps the ratio.
    choose(find<QComboBox>(sheet, "canvasUnits"), "Pixels");
    find<QCheckBox>(sheet, "canvasRelative").click();
    drag(scrubbed(sheet, "Width"), -90000);
    QCOMPARE(width.text(), QString("-1199"));
    find<QCheckBox>(sheet, "canvasRelative").click();
    find<QCheckBox>(sheet, "canvasLocked").click();
    drag(scrubbed(sheet, "Height"), -90000);
    QCOMPARE(height.text(), QString("1"));
    drag(scrubbed(sheet, "Width"), -90000);
    QCOMPARE(width.text(), QString("1.5"));
    drag(scrubbed(sheet, "Height"), 90000);
    QCOMPARE(height.text(), QString("20000"));
    drag(scrubbed(sheet, "Width"), 90000);
    QCOMPARE(width.text(), QString("30000"));
    QCOMPARE(height.text(), QString("20000"));
    QCOMPARE(scrubOverTyping(find<QLineEdit>(sheet, "canvasWidth"), scrubbed(sheet, "Width"), -2), QString("29998"));
}

void ScrubbableSheetsTests::canvasSizeCentimetresScrubTheirLimits()
{
    CanvasDocument document{1200, 800};
    document.resolution = 72;
    CanvasSizeSheet sheet(document, PaletteColor::black(), PaletteColor::white(), [](std::optional<CanvasSizeOptions>) {});
    showActive(sheet);
    QLineEdit &width = find<QLineEdit>(sheet, "canvasWidth");
    QLineEdit &height = find<QLineEdit>(sheet, "canvasHeight");
    choose(find<QComboBox>(sheet, "canvasUnits"), "Centimeters");
    // A point is 2.54 / 72 cm; 72 add 2.54.
    drag(scrubbed(sheet, "Width"), 72);
    QCOMPARE(width.text(), QString("45"));
    drag(scrubbed(sheet, "Width"), 90000);
    QCOMPARE(width.text(), QString("1058.333"));
    drag(scrubbed(sheet, "Width"), -90000);
    QCOMPARE(width.text(), QString("0.035"));
    find<QCheckBox>(sheet, "canvasRelative").click();
    drag(scrubbed(sheet, "Height"), -90000);
    QCOMPARE(height.text(), QString("-28.187"));
    find<QCheckBox>(sheet, "canvasRelative").click();
    find<QCheckBox>(sheet, "canvasLocked").click();
    drag(scrubbed(sheet, "Height"), 90000);
    QCOMPARE(height.text(), QString("705.556"));
}

void ScrubbableSheetsTests::imageSizeLimitsFollowUnitsLockAndResolution()
{
    {
        // Unlocked, a hundred megapixels over a 5,000-pixel height.
        CanvasDocument tall{1000, 5000};
        tall.resolution = 72;
        ImageSizeSheet sheet(tall, [](std::optional<ImageSizeOptions>) {});
        showActive(sheet);
        find<QCheckBox>(sheet, "imageLocked").click();
        drag(scrubbed(sheet, "Width"), 90000);
        QCOMPARE(find<QLineEdit>(sheet, "imageWidth").text(), QString("20000"));
        QCOMPARE(find<QLineEdit>(sheet, "imageHeight").text(), QString("5000"));
    }
    CanvasDocument document{1200, 800};
    document.resolution = 72;
    ImageSizeSheet sheet(document, [](std::optional<ImageSizeOptions>) {});
    showActive(sheet);
    QLineEdit &width = find<QLineEdit>(sheet, "imageWidth");
    QLineEdit &height = find<QLineEdit>(sheet, "imageHeight");
    QComboBox &units = find<QComboBox>(sheet, "imageUnits");
    // A percent is twelve points of 1,200 pixels.
    choose(units, "Percent");
    drag(scrubbed(sheet, "Width"), 12);
    QCOMPARE(width.text(), QString("101"));
    drag(scrubbed(sheet, "Height"), -16);
    QCOMPARE(height.text(), QString("99"));
    // Resampled inches: 72 points an inch, the pixels following.
    choose(units, "Inches");
    drag(scrubbed(sheet, "Width"), 72);
    QCOMPARE(width.text(), QString("18"));
    QCOMPARE(find<QLabel>(sheet, "imageResult").text(), QString("Result: 1,296 × 864 pixels"));
    // A resolution scrub keeps print sizes and scales the pixels.
    drag(scrubbed(sheet, "Resolution"), 72);
    QCOMPARE(find<QLineEdit>(sheet, "imageResolution").text(), QString("144"));
    QCOMPARE(width.text(), QString("18"));
    QCOMPARE(find<QLabel>(sheet, "imageResult").text(), QString("Result: 2,592 × 1,728 pixels"));
    // Resampled centimetres: 144 points add 2.54 at 144 ppi.
    choose(units, "Centimeters");
    drag(scrubbed(sheet, "Width"), 144);
    QCOMPARE(width.text(), QString("48"));
    choose(units, "Inches");
    drag(scrubbed(sheet, "Width"), 90000);
    QCOMPARE(width.text(), QString("85.052"));
    choose(units, "Centimeters");
    drag(scrubbed(sheet, "Height"), -90000);
    QCOMPARE(height.text(), QString("0.018"));
}

void ScrubbableSheetsTests::imageSizeTitlesScrubPixelsPrintSizeAndResolution()
{
    CanvasDocument document{1200, 800};
    document.resolution = 72;
    ImageSizeSheet sheet(document, [](std::optional<ImageSizeOptions>) {});
    showActive(sheet);
    QLineEdit &width = find<QLineEdit>(sheet, "imageWidth");
    QLineEdit &height = find<QLineEdit>(sheet, "imageHeight");
    QLineEdit &resolution = find<QLineEdit>(sheet, "imageResolution");
    // Locked, the height follows; a hundred megapixels at most.
    drag(scrubbed(sheet, "Width"), 300);
    QCOMPARE(width.text(), QString("1500"));
    QCOMPARE(height.text(), QString("1000"));
    drag(scrubbed(sheet, "Width"), 90000);
    QCOMPARE(width.text(), QString("12247.449"));
    drag(scrubbed(sheet, "Height"), -90000);
    QCOMPARE(height.text(), QString("1"));
    QCOMPARE(width.text(), QString("1.5"));
    drag(scrubbed(sheet, "Resolution"), 10.4);
    QCOMPARE(resolution.text(), QString("82"));
    drag(scrubbed(sheet, "Resolution"), 90000);
    QCOMPARE(resolution.text(), QString("9600"));
    drag(scrubbed(sheet, "Resolution"), -90000);
    QCOMPARE(resolution.text(), QString("1"));
    // Without resampling a point is a hundredth of an inch.
    find<QCheckBox>(sheet, "imageResample").click();
    QCOMPARE(find<QComboBox>(sheet, "imageUnits").currentText(), QString("Inches"));
    QCOMPARE(width.text(), QString("1200"));
    drag(scrubbed(sheet, "Width"), -60000);
    QCOMPARE(width.text(), QString("600"));
    QCOMPARE(resolution.text(), QString("2"));
    // Up to one pixel per inch, down to 9,600.
    drag(scrubbed(sheet, "Width"), 90000);
    QCOMPARE(width.text(), QString("1200"));
    QCOMPARE(resolution.text(), QString("1"));
    drag(scrubbed(sheet, "Width"), -200000);
    QCOMPARE(width.text(), QString("0.125"));
    // A point is 0.0254 cm: a hundred make 2.54.
    choose(find<QComboBox>(sheet, "imageUnits"), "Centimeters");
    drag(scrubbed(sheet, "Width"), 100);
    QCOMPARE(width.text(), QString("3"));
    QCOMPARE(resolution.text(), QString("1016"));
    QCOMPARE(scrubOverTyping(resolution, scrubbed(sheet, "Resolution"), 4), QString("1020"));
    // A resolution of nothing leaves no print size to scrub.
    resolution.setFocus();
    resolution.selectAll();
    QTest::keyClicks(&resolution, QStringLiteral("0"));
    QTest::keyClick(&resolution, Qt::Key_Return);
    QVERIFY(!scrubbed(sheet, "Width").isEnabled());
    QVERIFY(!scrubbed(sheet, "Height").isEnabled());
    // A positive resolution again brings the titles back.
    drag(scrubbed(sheet, "Resolution"), 72);
    QCOMPARE(resolution.text(), QString("72"));
    QVERIFY(scrubbed(sheet, "Width").isEnabled());
    QVERIFY(scrubbed(sheet, "Height").isEnabled());
    drag(scrubbed(sheet, "Height"), -100);
    QCOMPARE(height.text(), QString("26"));
    QCOMPARE(resolution.text(), QString("78.154"));
}

QTEST_MAIN(ScrubbableSheetsTests)
#include "ScrubbableSheetsTests.moc"
