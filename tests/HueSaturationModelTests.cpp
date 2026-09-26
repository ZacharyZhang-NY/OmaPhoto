#include "Document/HueSaturation.h"
#include "Document/LayerTransform.h"
#include "Document/Selection.h"
#include <QtTest>
#include <cmath>

// Hue/Saturation's models and cube at their edges.
namespace {
std::array<int, 4> bytes(const QImage &image, int x = 0, int y = 0)
{
    const uchar *pixel = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied).constScanLine(y) + x * 4;
    return {pixel[0], pixel[1], pixel[2], pixel[3]};
}

QImage filled(QColor colour, int width = 1, int height = 1)
{
    QImage image(width, height, QImage::Format_RGBA8888_Premultiplied);
    image.fill(colour);
    return image;
}

bool close(const HueSaturationFilter::Color &color, double red, double green, double blue)
{
    return std::abs(color.red - red) < 1e-9 && std::abs(color.green - green) < 1e-9 && std::abs(color.blue - blue) < 1e-9;
}
}

class HueSaturationModelTests : public QObject {
    Q_OBJECT
private slots:
    void rangesKeepSwiftsNamesAndBands();
    void bandsMeasureForwardAndRamp();
    void bandsCentreWidenAndNarrow();
    void handlesStayInOrder();
    void settingsEditTheirRange();
    void adjustWorksInHsl();
    void theCubeRunsRedFastest();
    void theFilterLooksUpUnpremultiplied();
    void previewsScaleTheLongSideToEightThousand();
};

void HueSaturationModelTests::rangesKeepSwiftsNamesAndBands()
{
    QStringList names;
    for (const ColorRange range : allColorRanges)
        names << rawValue(range);
    QCOMPARE(names, (QStringList{"Master", "Reds", "Yellows", "Greens", "Cyans", "Blues", "Magentas"}));
    QCOMPARE(colorRanges.front(), ColorRange::reds);
    QCOMPARE(colorRanges.back(), ColorRange::magentas);
    const std::pair<ColorRange, std::array<double, 4>> bands[] = {
        {ColorRange::master, {0, 0, 360, 360}},       {ColorRange::reds, {315, 345, 15, 45}},    {ColorRange::yellows, {15, 45, 75, 105}},
        {ColorRange::greens, {75, 105, 135, 165}},    {ColorRange::cyans, {135, 165, 195, 225}}, {ColorRange::blues, {195, 225, 255, 285}},
        {ColorRange::magentas, {255, 285, 315, 345}}};
    for (const auto &[range, handles] : bands)
        QCOMPARE(defaultBand(range).handles(), handles);
    QStringList modes;
    for (const HueSampleMode mode : allHueSampleModes)
        modes << rawValue(mode) + QLatin1Char('|') + help(mode);
    QCOMPARE(modes, (QStringList{"Sample|Click the image to center this range on that color", "Add|Click the image to widen this range to include that color",
                                 "Remove|Click the image to narrow this range to exclude that color"}));
}

void HueSaturationModelTests::bandsMeasureForwardAndRamp()
{
    QCOMPARE(HueBand::forward(350, 10), 20.0);
    QCOMPARE(HueBand::forward(10, 350), 340.0);
    QCOMPARE(HueBand::forward(0, 720), 0.0);
    QCOMPARE(HueBand::forward(0, -30), 330.0);
    const HueBand yellows = defaultBand(ColorRange::yellows);
    QCOMPARE(yellows.weight(30), 0.5);
    QCOMPARE(yellows.weight(60), 1.0);
    QCOMPARE(yellows.weight(90), 0.5);
    QCOMPARE(yellows.weight(105), 0.0);
    QCOMPARE(yellows.weight(106), 0.0);
    // Shoulders of no width are full at once.
    const HueBand sharp{100, 100, 200, 200};
    QVERIFY(sharp.weight(100) == 1 && sharp.weight(200) == 1 && sharp.weight(99) == 0 && sharp.weight(201) == 0);
    QCOMPARE((HueBand{10, 20, 30, 10}.weight(12)), 1.0);
    // A span under a degree still measures its hues.
    QCOMPARE((HueBand{100, 100.2, 100.5, 100.8}.weight(200)), 0.0);
}

void HueSaturationModelTests::bandsCentreWidenAndNarrow()
{
    // Centred: core 30, shoulders 30, around the hue.
    const HueBand centred = defaultBand(ColorRange::greens).centered(0);
    QCOMPARE(centred, (HueBand{315, 345, 15, 45}));
    QCOMPARE(defaultBand(ColorRange::greens).centered(350).handles(), (std::array<double, 4>{305, 335, 5, 35}));
    // Widened by the nearer edge, shoulders kept.
    HueBand band = defaultBand(ColorRange::greens);
    band.include(100);
    QCOMPARE(band, (HueBand{70, 100, 135, 165}));
    band = defaultBand(ColorRange::greens);
    band.include(140);
    QCOMPARE(band, (HueBand{75, 105, 140, 170}));
    band = defaultBand(ColorRange::greens);
    band.include(120);
    QCOMPARE(band, defaultBand(ColorRange::greens));
    // Narrowed a degree past the hue, from the nearer end.
    band = defaultBand(ColorRange::greens);
    band.exclude(90);
    QCOMPARE(band, (HueBand{91, 121, 135, 165}));
    band = defaultBand(ColorRange::greens);
    band.exclude(150);
    QCOMPARE(band, (HueBand{75, 105, 119, 149}));
    band = defaultBand(ColorRange::greens);
    band.exclude(200);
    QCOMPARE(band, defaultBand(ColorRange::greens));
    // A tie widens the start, as Swift's <=.
    band.include(300);
    QCOMPARE(band, (HueBand{270, 300, 135, 165}));
    // Uneven shoulders keep their own widths.
    const HueBand uneven{300, 320, 340, 350};
    QCOMPARE(uneven.centered(100), (HueBand{70, 90, 110, 120}));
    band = uneven;
    band.include(250);
    QCOMPARE(band, (HueBand{230, 250, 340, 350}));
    band = uneven;
    band.include(355);
    QCOMPARE(band, (HueBand{300, 320, 355, 5}));
    band = uneven;
    band.exclude(310);
    QCOMPARE(band, (HueBand{311, 331, 340, 350}));
    band = uneven;
    band.exclude(345);
    QCOMPARE(band, (HueBand{300, 320, 334, 344}));
    // Across zero every handle wraps; the core may cross.
    band = defaultBand(ColorRange::reds);
    band.exclude(350);
    QCOMPARE(band, (HueBand{351, 21, 15, 45}));
    QCOMPARE(band.weight(21), 0.8);
    band = defaultBand(ColorRange::reds);
    band.exclude(10);
    QCOMPARE(band, (HueBand{315, 345, 339, 9}));
    // Widening stops short of a full circle.
    HueBand wide{10, 40, 300, 330};
    wide.include(320);
    QCOMPARE(wide, (HueBand{10, 40, 320, 350}));
    wide = HueBand{10, 40, 300, 330};
    wide.include(335);
    QCOMPARE(wide, (HueBand{10, 40, 335, 0}));
    wide = HueBand{10, 40, 300, 330};
    wide.include(330.5);
    QCOMPARE(wide, (HueBand{10, 40, 330.5, 0}));
    wide = HueBand{10, 40, 300, 330};
    wide.include(5);
    QCOMPARE(wide, (HueBand{335, 5, 300, 325}));
}

void HueSaturationModelTests::handlesStayInOrder()
{
    HueBand band = defaultBand(ColorRange::greens);
    band.setHandle(0, 60);
    QCOMPARE(band.falloffStart, 60.0);
    band.setHandle(2, 150);
    QCOMPARE(band.rangeEnd, 150.0);
    band.setHandle(3, 500);
    QCOMPARE(band.falloffEnd, 165.0);
    band.setHandle(3, 530);
    QCOMPARE(band.falloffEnd, 170.0);
    band = defaultBand(ColorRange::greens);
    band.setHandle(3, -150);
    QCOMPARE(band.falloffEnd, 210.0);
    // The end passes neither the core nor a circle.
    band.setHandle(3, 130);
    QCOMPARE(band.falloffEnd, 210.0);
    band.setHandle(3, 70);
    QCOMPARE(band.falloffEnd, 210.0);
    band.setHandle(0, 105);
    QCOMPARE(band.falloffStart, 105.0);
    band.setHandle(0, 106);
    QCOMPARE(band.falloffStart, 105.0);
    band = defaultBand(ColorRange::greens);
    band.setHandle(3, 67);
    QCOMPARE(band.falloffEnd, 165.0);
    // A span of a degree or less is refused.
    HueBand thin{100, 100, 100, 101};
    thin.setHandle(3, 101.5);
    QCOMPARE(thin.falloffEnd, 101.5);
    thin.setHandle(3, 100.5);
    QCOMPARE(thin.falloffEnd, 101.5);
}

void HueSaturationModelTests::settingsEditTheirRange()
{
    const HueSaturationSettings plain;
    QCOMPARE(plain.adjustments.size(), size_t(1));
    QCOMPARE(plain.adjustments.at(ColorRange::master), RangeAdjustment());
    QCOMPARE(plain.bands.size(), size_t(7));
    QVERIFY(plain.isIdentity() && plain.range == ColorRange::master && !plain.colorize && !plain.invertRange);
    HueSaturationSettings settings(10, 20, 30, false, ColorRange::cyans);
    QVERIFY(settings.hue() == 10 && settings.saturation() == 20 && settings.lightness() == 30);
    settings.range = ColorRange::blues;
    QVERIFY(settings.hue() == 0 && settings.saturation() == 0 && settings.lightness() == 0);
    settings.setSaturation(-5);
    settings.setLightness(7);
    QCOMPARE(settings.adjustments.at(ColorRange::blues), (RangeAdjustment{0, -5, 7}));
    QCOMPARE(settings.band(), defaultBand(ColorRange::blues));
    settings.setBand(HueBand{1, 2, 3, 4});
    QCOMPARE(settings.bands.at(ColorRange::blues), (HueBand{1, 2, 3, 4}));
    settings.bands.erase(ColorRange::blues);
    QCOMPARE(settings.band(), defaultBand(ColorRange::blues));
    QCOMPARE(settings.weight(ColorRange::blues, 240), 1.0);
    QCOMPARE(HueSaturationSettings::colorizeStart(), HueSaturationSettings(0, 25, 0, true));
    QVERIFY(!HueSaturationSettings(0, 0, 0, true).isIdentity());
    // Invert flips only the selected range's weight.
    settings.invertRange = true;
    QCOMPARE(settings.weight(ColorRange::blues, 240), 0.0);
    QCOMPARE(settings.weight(ColorRange::reds, 0), 1.0);
    QCOMPARE(settings.weight(ColorRange::master, 120), 1.0);
    settings.range = ColorRange::master;
    QCOMPARE(settings.weight(ColorRange::master, 120), 1.0);
}

void HueSaturationModelTests::adjustWorksInHsl()
{
    QVERIFY(close(HueSaturationFilter::adjust(1, 0, 0, HueSaturationSettings(120)), 0, 1, 0));
    QVERIFY(close(HueSaturationFilter::adjust(1, 0, 0, HueSaturationSettings(-120)), 0, 0, 1));
    QVERIFY(close(HueSaturationFilter::adjust(0, 0, 1, HueSaturationSettings(180)), 1, 1, 0));
    QVERIFY(close(HueSaturationFilter::adjust(1, 0, 1, HueSaturationSettings(60)), 1, 0, 0));
    QVERIFY(close(HueSaturationFilter::adjust(0, 1, 1, HueSaturationSettings(60)), 0, 0, 1));
    // The last sectors: 180 to 240, 300 to 360.
    const HueSaturationFilter::Color azure = HueSaturationFilter::adjust(1, 0, 0, HueSaturationSettings(210));
    QVERIFY(close(azure, 0, 0.5, 1));
    const HueSaturationFilter::Color rose = HueSaturationFilter::adjust(1, 0, 0, HueSaturationSettings(330));
    QVERIFY(close(rose, 1, 0, 0.5));
    // Saturation multiplies: gray stays, colour halves.
    QVERIFY(close(HueSaturationFilter::adjust(0.5, 0.5, 0.5, HueSaturationSettings(0, 100)), 0.5, 0.5, 0.5));
    QVERIFY(close(HueSaturationFilter::adjust(1, 0, 0, HueSaturationSettings(0, -50)), 0.75, 0.25, 0.25));
    QVERIFY(close(HueSaturationFilter::adjust(0.75, 0.25, 0.25, HueSaturationSettings(0, 300)), 1, 0, 0));
    // Lightness pulls toward white or black.
    QVERIFY(close(HueSaturationFilter::adjust(1, 0, 0, HueSaturationSettings(0, 0, 50)), 1, 0.5, 0.5));
    QVERIFY(close(HueSaturationFilter::adjust(1, 0, 0, HueSaturationSettings(0, 0, -50)), 0.5, 0, 0));
    QVERIFY(close(HueSaturationFilter::adjust(1, 0, 0, HueSaturationSettings(0, 0, 250)), 1, 1, 1));
    // Colorize sets hue and saturation outright.
    QVERIFY(close(HueSaturationFilter::adjust(0.2, 0.2, 0.2, HueSaturationSettings(240, 100, 0, true)), 0, 0, 0.4));
    QVERIFY(close(HueSaturationFilter::adjust(0.2, 0.2, 0.2, HueSaturationSettings(600, 250, 0, true)), 0, 0, 0.4));
    QVERIFY(close(HueSaturationFilter::adjust(0.2, 0.4, 0.2, HueSaturationSettings(0, -20, 50, true)), 0.65, 0.65, 0.65));
    // A range's share follows its weight at the hue.
    const HueSaturationSettings reds(0, 0, -100, false, ColorRange::reds);
    QVERIFY(close(HueSaturationFilter::adjust(1, 0, 0, reds), 0, 0, 0));
    QVERIFY(close(HueSaturationFilter::adjust(0, 0, 1, reds), 0, 0, 1));
    const std::vector<HueSaturationFilter::HueResponse> response = HueSaturationFilter::hueResponse(HueSaturationSettings(20, 10, -30, false, ColorRange::yellows));
    QCOMPARE(response.size(), size_t(361));
    QVERIFY(response[30].shift == 10 && response[30].saturation == 5 && response[30].lightness == -15);
    QVERIFY(response[60].shift == 20 && response[0].shift == 0 && response[360].shift == 0);
    // Near gray still has a hue to turn.
    QVERIFY(close(HueSaturationFilter::adjust(0.5, 0.504, 0.5, HueSaturationSettings(120)), 0.5, 0.5, 0.504));
    // Between 60 and 120 degrees green leads.
    QVERIFY(close(HueSaturationFilter::adjust(1, 0, 0, HueSaturationSettings(90)), 0.5, 1, 0));
    // A hue below zero reads 330, where reds weigh half.
    const HueSaturationSettings redsTurn(60, 0, 0, false, ColorRange::reds);
    QVERIFY(close(HueSaturationFilter::adjust(1, 0, 0.5, redsTurn), 1, 0, 0));
    // The response rounds: hue 30.6 reads degree 31.
    QVERIFY(close(HueSaturationFilter::adjust(1, 0.51, 0, redsTurn), 1, 58.6 / 60, 0));
    // Degree 360 has its own response.
    QVERIFY(close(HueSaturationFilter::adjust(1, 0, 0.004, HueSaturationSettings(120)), 0.004, 1, 0));
    // Saturation caps at one before colour returns.
    QVERIFY(close(HueSaturationFilter::adjust(0.7, 0.1, 0.1, HueSaturationSettings(0, 100)), 0.8, 0, 0));
    QCOMPARE(HueSaturationFilter::shiftedHue(10, HueSaturationSettings(-30)), 340.0);
    QCOMPARE(HueSaturationFilter::shiftedHue(350, HueSaturationSettings(30)), 20.0);
}

void HueSaturationModelTests::theCubeRunsRedFastest()
{
    const std::vector<float> identity = HueSaturationFilter::cube(HueSaturationSettings());
    QCOMPARE(identity.size(), size_t(33 * 33 * 33 * 4));
    const auto at = [&](int red, int green, int blue) { return identity.data() + ((blue * 33 + green) * 33 + red) * 4; };
    QVERIFY(at(1, 0, 0)[0] == 1.0f / 32 && at(1, 0, 0)[1] == 0 && at(1, 0, 0)[3] == 1);
    QVERIFY(at(0, 2, 0)[1] == 2.0f / 32 && at(0, 0, 3)[2] == 3.0f / 32);
    QVERIFY(std::abs(at(32, 16, 8)[0] - 1) < 1e-6f && std::abs(at(32, 16, 8)[1] - 0.5f) < 1e-6f && std::abs(at(32, 16, 8)[2] - 0.25f) < 1e-6f);
    // Each corner is adjusted: red turns green.
    const std::vector<float> turned = HueSaturationFilter::cube(HueSaturationSettings(120));
    const float *red = turned.data() + 32 * 4;
    QVERIFY(std::abs(red[0]) < 1e-6f && std::abs(red[1] - 1) < 1e-6f && std::abs(red[2]) < 1e-6f);
}

void HueSaturationModelTests::theFilterLooksUpUnpremultiplied()
{
    // Half-clear red turns half-clear green, alpha kept.
    const AdjustedPixels half = HueSaturationFilter::run({filled(QColor(255, 0, 0, 128)), HueSaturationSettings(120), std::nullopt, QTransform(), false});
    QVERIFY((bytes(half.image) == std::array<int, 4>{0, 128, 0, 128}));
    QVERIFY(!half.thumbnail);
    QCOMPARE(half.image.format(), QImage::Format_RGBA8888_Premultiplied);
    const AdjustedPixels clear = HueSaturationFilter::run({filled(Qt::transparent), HueSaturationSettings(0, 0, 100), std::nullopt, QTransform(), true});
    QVERIFY((bytes(clear.image) == std::array<int, 4>{0, 0, 0, 0}));
    QCOMPARE(clear.thumbnail.value().size(), QSize(1, 1));
    // Between corners the cube interpolates, trilinear.
    const HueSaturationSettings turn(40, -30, 20);
    const std::vector<float> cube = HueSaturationFilter::cube(turn);
    const AdjustedPixels between = HueSaturationFilter::run({filled(QColor(100, 150, 200)), turn, std::nullopt, QTransform(), false});
    const std::array<int, 4> made = bytes(between.image);
    const double place[3] = {100.0 / 255 * 32, 150.0 / 255 * 32, 200.0 / 255 * 32};
    for (int channel = 0; channel < 3; ++channel) {
        double value = 0;
        for (int corner = 0; corner < 8; ++corner) {
            double share = 1;
            int index[3];
            for (int axis = 0; axis < 3; ++axis) {
                const int low = int(place[axis]), high = corner >> axis & 1;
                index[axis] = low + high;
                share *= high ? place[axis] - low : 1 - (place[axis] - low);
            }
            value += share * cube[size_t(((index[2] * 33 + index[1]) * 33 + index[0]) * 4 + channel)];
        }
        QVERIFY(std::abs(made[size_t(channel)] - value * 255) <= 0.51);
    }
    const AdjustedPixels white = HueSaturationFilter::run({filled(QColor(255, 255, 255)), HueSaturationSettings(0, 0, -50), std::nullopt, QTransform(), false});
    QVERIFY((bytes(white.image) == std::array<int, 4>{128, 128, 128, 255}));
    // A selection blends: the left pixel turns, the right stays.
    QPainterPath left;
    left.addRect(0, 0, 1, 1);
    const SelectionClip clip = DocumentSelection{left, false}.clip(QSizeF(2, 1));
    const AdjustedPixels selected = HueSaturationFilter::run({filled(Qt::red, 2, 1), HueSaturationSettings(120), clip, QTransform(), false});
    QVERIFY((bytes(selected.image, 0) == std::array<int, 4>{0, 255, 0, 255}));
    QVERIFY((bytes(selected.image, 1) == std::array<int, 4>{255, 0, 0, 255}));
}

void HueSaturationModelTests::previewsScaleTheLongSideToEightThousand()
{
    // Swift's Int() truncates: 8311 wide previews 7999.
    for (const auto &[size, preview] : {std::pair(QSize(2, 9000), QSize(1, 8000)), std::pair(QSize(8311, 2), QSize(7999, 1))}) {
        const HueSaturationEdit edit(QUuid::createUuid(), ImportedImage(filled(Qt::red, size.width(), size.height()), QImage(), QStringLiteral("Red")),
                                     std::nullopt, LayerTransform{QPointF(0, 0), QSizeF(size)});
        QCOMPARE(edit.previewSource.size(), preview);
        // The preview's far corner is the layer's.
        QCOMPARE(edit.previewPixelToDocument.map(QPointF(preview.width(), preview.height())), QPointF(size.width(), size.height()));
    }
}

QTEST_GUILESS_MAIN(HueSaturationModelTests)
#include "HueSaturationModelTests.moc"
