#include "Document/BrushStroke.h"
#include "Document/Dither.h"
#include "Document/EditorSession.h"
#include "Document/PixelAdjust.h"
#include "UI/FilterSheet.h"
#include <QCheckBox>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPainter>
#include <QSlider>
#include <QtTest>
#include <numeric>

// Swift 1.4.4's Dither › Scanlines (CRT), on every core.
namespace {
QImage filled(int width, int height, QColor colour)
{
    QImage image = BrushRaster::context(width, height, false);
    image.fill(colour);
    return image;
}

DitherSettings scanlines(double spacing, double glow = 0)
{
    DitherSettings settings;
    settings.style = DitherStyle::scanlines;
    settings.lineSpacing = spacing;
    settings.glow = glow;
    return settings;
}

// One channel of one column, top to bottom.
std::vector<int> column(const QImage &image, int x, int channel = 0)
{
    std::vector<int> values;
    for (int y = 0; y < image.height(); ++y)
        values.push_back(image.constScanLine(y)[x * 4 + channel]);
    return values;
}

std::vector<int> row(const QImage &image, int y, int channel = 0)
{
    std::vector<int> values;
    for (int x = 0; x < image.width(); ++x)
        values.push_back(image.constScanLine(y)[x * 4 + channel]);
    return values;
}

// Swift's test: one column of a flat gray's render.
std::vector<int> gray(int level, double spacing, double glow = 0)
{
    return column(scanlines(spacing, glow).apply(filled(16, 32, QColor(level, level, level))), 5);
}

// Worst difference from Swift's first glow, blurred whole.
int glowError(const QImage &image, double glow)
{
    const QImage lines = scanlines(8).apply(image), result = scanlines(8, glow).apply(image);
    const QImage blurred = PixelAdjust::gaussianBlur(lines, 8 * 3 + 3, true);
    const float amount = float(glow / 100 * 2.5);
    int worst = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const int plain = lines.constScanLine(y)[x * 4], light = blurred.constScanLine(y)[x * 4];
            const int expected = int(std::lround(std::min(255.0f, float(plain) + float(light) * amount)));
            worst = std::max(worst, std::abs(result.constScanLine(y)[x * 4] - expected));
        }
    }
    return worst;
}

std::vector<int> repeated(const std::vector<int> &band, int times)
{
    std::vector<int> all;
    for (int each = 0; each < times; ++each)
        all.insert(all.end(), band.begin(), band.end());
    return all;
}

struct Shown {
    EditorSession session;
    std::unique_ptr<FilterSheet> sheet;
    Shown()
    {
        session.createDocument(40, 30);
        const QImage image = filled(40, 30, QColor(200, 120, 40));
        session.insert(ImportedImage(image, image, QStringLiteral("Colour")));
        session.beginFilter(FilterKind::dither);
        FilterSettings settings = session.filterEdit().value().settings;
        settings.dither.style = DitherStyle::scanlines;
        session.updateFilter(settings, true);
        sheet = std::make_unique<FilterSheet>(session);
        sheet->show();
    }
    const DitherSettings &dither() const { return session.filterEdit().value().settings.dither; }
    // The shown rows in the column's order, with their help.
    QStringList rows() const
    {
        QStringList titles;
        QLayout *column = sheet->layout();
        for (int index = 0; index < column->count(); ++index) {
            QWidget *each = column->itemAt(index)->widget();
            if (!each || each->isHidden())
                continue;
            if (auto *box = qobject_cast<QCheckBox *>(each); box && box->text() != QLatin1String("Preview"))
                titles << box->text();
            for (QLabel *label : each->findChildren<QLabel *>(QString(), Qt::FindDirectChildrenOnly)) {
                if (label->buddy())
                    titles << label->text() + u'|' + each->toolTip();
            }
        }
        return titles;
    }
};
}

class ScanlinesTests : public QObject {
    Q_OBJECT
private slots:
    void scanlinesAreLinesOfLightThatBloomWithBrightness();
    void glowLightsBetweenTheLines();
    void dotsBreakTheLinesIntoBeads();
    void wobbleMovesLinesSideways();
    void theStyleDrawsAtFullResolutionWithoutMarks();
    void normalizingClampsTheFourSettings();
    void eachLineIsTheAverageOfItsRows();
    void theScreenIsTheDarkColourOrBlack();
    void clearPixelsStayClear();
    void everyBandOfATallImageIsDrawn();
    void bandsDrawAsOneBandDoes();
    void theGlowIsTheBlurredLightAtItsAmount();
    void theGlowScalesBackExactlyOffTheStep();
    void theGlowNeverPassesAlpha();
    void theSheetShowsScanlinesRows();
    void theRowsWriteTheirSettings();
};

void ScanlinesTests::scanlinesAreLinesOfLightThatBloomWithBrightness()
{
    const std::vector<int> white = gray(255, 8), dim = gray(89, 8), black = gray(0, 8);
    for (int band = 0; band < 4; ++band) {
        const std::vector<int> rows(white.begin() + band * 8, white.begin() + band * 8 + 8);
        QVERIFY(rows[3] == 255 && rows[4] == 255);
        QVERIFY(std::any_of(rows.begin(), rows.end(), [](int value) { return value < 40; }));
    }
    QVERIFY(std::accumulate(dim.begin(), dim.end(), 0) * 2 < std::accumulate(white.begin(), white.end(), 0));
    QVERIFY(std::all_of(black.begin(), black.end(), [](int value) { return value == 0; }));
    // The beam's half height, 2.8 white, about 1.98 at 0.35.
    QCOMPARE(white, repeated({0, 255, 255, 255, 255, 255, 255, 0}, 4));
    QCOMPARE(dim, repeated({0, 0, 118, 120, 120, 118, 0, 0}, 4));
}

void ScanlinesTests::glowLightsBetweenTheLines()
{
    const std::vector<int> plain = gray(255, 8), glowing = gray(255, 8, 100);
    QVERIFY2(glowing[0] > plain[0] + 40, qPrintable(QString::number(glowing[0])));
}

void ScanlinesTests::dotsBreakTheLinesIntoBeads()
{
    const QImage white = filled(64, 16, Qt::white);
    DitherSettings settings = scanlines(8);
    QCOMPARE(row(settings.apply(white), 4), std::vector<int>(64, 255));
    settings.dots = 100;
    // Each bead lit at its middle, dark at its ends.
    const std::vector<int> lit = row(settings.apply(white), 4);
    QCOMPARE(std::vector<int>(lit.begin(), lit.begin() + 8), (std::vector<int>{0, 255, 255, 255, 255, 255, 255, 0}));
    QCOMPARE(std::vector<int>(lit.begin() + 8, lit.begin() + 16), (std::vector<int>{0, 255, 255, 255, 255, 255, 255, 0}));
    // Half dots: the bead ends dim off the middle.
    settings.dots = 50;
    QCOMPARE(row(settings.apply(white), 1)[0], 85);
}

void ScanlinesTests::wobbleMovesLinesSideways()
{
    QImage edge = filled(64, 64, Qt::black);
    QPainter(&edge).fillRect(QRect(32, 0, 32, 64), Qt::white);
    const auto edges = [&edge](double wobble) {
        DitherSettings settings = scanlines(8);
        settings.wobble = wobble;
        const QImage result = settings.apply(edge);
        std::vector<int> found;
        for (int line = 0; line < 8; ++line) {
            const std::vector<int> middle = row(result, line * 8 + 4);
            found.push_back(int(std::find_if(middle.begin(), middle.end(), [](int value) { return value > 128; }) - middle.begin()));
        }
        return found;
    };
    QCOMPARE(edges(0), std::vector<int>(8, 32));
    // Each line's shift: 12 times the two waves, rounded.
    QCOMPARE(edges(12), (std::vector<int>{35, 36, 35, 41, 44, 37, 32, 34}));
}

void ScanlinesTests::theStyleDrawsAtFullResolutionWithoutMarks()
{
    QVERIFY(!usesPixelSize(DitherStyle::scanlines) && !usesPixelSize(DitherStyle::ascii) && usesPixelSize(DitherStyle::patterns));
    QVERIFY(!drawsMarks(DitherStyle::scanlines) && drawsMarks(DitherStyle::ascii));
    QVERIFY(!hasTones(DitherStyle::scanlines) && !isHalftone(DitherStyle::scanlines) && ditherGroup(DitherStyle::scanlines) == 3);
    // A pixel size changes nothing: no chunks, no dots.
    const QImage image = filled(20, 24, QColor(180, 90, 30));
    DitherSettings settings = scanlines(4);
    const QImage plain = settings.apply(image);
    settings.pixelSize = 5;
    settings.pixelShape = DitherPixelShape::dot;
    QCOMPARE(settings.apply(image), plain);
    // The cell is the line spacing, not the cell size.
    settings.cellSize = 32;
    QCOMPARE(settings.apply(image), plain);
}

void ScanlinesTests::normalizingClampsTheFourSettings()
{
    DitherSettings settings;
    QCOMPARE(settings.lineSpacing, 4.0);
    QCOMPARE(settings.glow, 35.0);
    QCOMPARE(settings.dots, 0.0);
    QCOMPARE(settings.wobble, 0.0);
    settings.lineSpacing = 1.4;
    settings.glow = 150;
    settings.dots = -5;
    settings.wobble = 3.5;
    DitherSettings result = settings.normalized();
    QCOMPARE(result.lineSpacing, 2.0);
    QCOMPARE(result.glow, 100.0);
    QCOMPARE(result.dots, 0.0);
    QCOMPARE(result.wobble, 3.5);
    settings.lineSpacing = 40;
    settings.glow = -1;
    settings.dots = 120;
    settings.wobble = 70;
    result = settings.normalized();
    QCOMPARE(result.lineSpacing, 32.0);
    QCOMPARE(result.glow, 0.0);
    QCOMPARE(result.dots, 100.0);
    QCOMPARE(result.wobble, 64.0);
    // Rounded within; not a number takes the default.
    settings.lineSpacing = 5.5;
    settings.glow = std::nan("");
    settings.dots = std::nan("");
    settings.wobble = std::nan("");
    result = settings.normalized();
    QCOMPARE(result.lineSpacing, 6.0);
    QCOMPARE(result.glow, 35.0);
    QCOMPARE(result.dots, 0.0);
    QCOMPARE(result.wobble, 0.0);
    settings.lineSpacing = std::nan("");
    QCOMPARE(settings.normalized().lineSpacing, 4.0);
    // Glow and dots keep their fractions.
    settings.glow = 12.5;
    settings.dots = 33.3;
    QCOMPARE(settings.normalized().glow, 12.5);
    QCOMPARE(settings.normalized().dots, 33.3);
}

void ScanlinesTests::eachLineIsTheAverageOfItsRows()
{
    // Rows of white and black: each line lights at half.
    QImage stripes = filled(8, 16, Qt::black);
    for (int y = 0; y < 16; y += 2)
        QPainter(&stripes).fillRect(QRect(0, y, 8, 1), Qt::white);
    QCOMPARE(column(scanlines(8).apply(stripes), 3), repeated({0, 37, 172, 172, 172, 172, 37, 0}, 2));
}

void ScanlinesTests::theScreenIsTheDarkColourOrBlack()
{
    const QImage image = filled(8, 8, QColor(255, 128, 0));
    DitherSettings settings = scanlines(8);
    settings.colors = DitherColors::twoColors;
    settings.dark = AdjustmentColor(0, 0, 1);
    settings.light = AdjustmentColor(1, 1, 0);
    const QImage two = settings.apply(image);
    // Off the line the dark colour; on it, the mix.
    QCOMPARE(two.pixelColor(2, 0), QColor(0, 0, 255));
    QCOMPARE(two.pixelColor(2, 4), QColor(197, 197, 147));
    settings.colors = DitherColors::original;
    const QImage original = settings.apply(image);
    QCOMPARE(original.pixelColor(2, 0), QColor(0, 0, 0));
    QCOMPARE(original.pixelColor(2, 4), QColor(255, 173, 0));
}

void ScanlinesTests::clearPixelsStayClear()
{
    QImage image = filled(8, 16, Qt::white);
    QPainter painter(&image);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(QRect(0, 0, 8, 8), Qt::transparent);
    painter.end();
    const QImage result = scanlines(8, 100).apply(image);
    QCOMPARE(column(result, 2, 3), (std::vector<int>{0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255}));
    QCOMPARE(column(result, 2, 0)[4], 0);
}

void ScanlinesTests::everyBandOfATallImageIsDrawn()
{
    // 150 two-row lines: 32 bands of 5, two empty.
    const QImage image = filled(3, 300, Qt::white);
    QCOMPARE(column(scanlines(2).apply(image), 1), repeated({241, 241}, 150));
    // 70 lines of 8: bands of 3, the last one.
    const std::vector<int> tall = column(scanlines(8).apply(filled(3, 560, Qt::white)), 1);
    QCOMPARE(tall, repeated({0, 255, 255, 255, 255, 255, 255, 0}, 70));
}

void ScanlinesTests::bandsDrawAsOneBandDoes()
{
    // One band of 62 rows, 32 of 300: tops agree.
    QImage noise = BrushRaster::context(7, 300, false);
    for (int y = 0; y < 300; ++y)
        for (int x = 0; x < 7; ++x)
            noise.setPixelColor(x, y, QColor((x * 37 + y * 11) % 256, (x * 91 + y * 53) % 256, (y * 29) % 256, 128 + (x + y) % 128));
    DitherSettings settings = scanlines(2);
    settings.colors = DitherColors::original;
    settings.dots = 50;
    settings.wobble = 5;
    const QImage whole = settings.apply(noise), one = settings.apply(noise.copy(0, 0, 7, 62));
    QCOMPARE(whole.copy(0, 0, 7, 62), one);
}

void ScanlinesTests::theGlowIsTheBlurredLightAtItsAmount()
{
    QImage image = filled(64, 160, Qt::black);
    QPainter(&image).fillRect(QRect(0, 0, 32, 40), Qt::white);
    for (const double glow : {20.0, 100.0}) {
        // Three levels of the shrink's blur, times 2.5.
        const int worst = glowError(image, glow);
        QVERIFY2(worst <= 8, qPrintable(QString::number(worst)));
    }
    // Below the light, at amount 0.5, it fades.
    const std::vector<int> fading = column(scanlines(8, 20).apply(image), 16);
    QVERIFY(fading[48] > 20 && fading[48] < fading[40] && fading[120] < 3);
}

void ScanlinesTests::theGlowScalesBackExactlyOffTheStep()
{
    // 73 plus two 84-point pads is off the 6 step.
    QImage image = filled(73, 73, Qt::black);
    QPainter(&image).fillRect(QRect(0, 0, 40, 40), Qt::white);
    const int worst = glowError(image, 100);
    QVERIFY2(worst <= 8, qPrintable(QString::number(worst)));
}

void ScanlinesTests::theGlowNeverPassesAlpha()
{
    QImage image = BrushRaster::context(16, 32, false);
    image.fill(QColor(255, 255, 255, 128));
    const QImage result = scanlines(4, 100).apply(image);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 16; ++x) {
            const uchar *at = result.constScanLine(y) + x * 4;
            QCOMPARE(int(at[3]), 128);
            QVERIFY(at[0] <= 128 && at[1] <= 128 && at[2] <= 128);
        }
    }
    QCOMPARE(int(result.constScanLine(0)[0]), 128);
}

void ScanlinesTests::theSheetShowsScanlinesRows()
{
    Shown shown;
    QCOMPARE(shown.rows(), (QStringList{"Style|", "Line Spacing|How far apart the screen's lines are",
                                        "Glow|Light blooming around the lines, like a CRT's phosphors", "Dots|Break the lines into glowing beads",
                                        "Wobble|Make the lines waver sideways down the screen, like a CRT losing sync",
                                        "Density|More ink (darker) or less before dithering", "Contrast|", "Colors|"}));
    // A value mid-range sits mid-travel; each unit is Swift's.
    const std::tuple<const char *, double DitherSettings::*, double, QString> middles[] = {
        {"lineSpacing", &DitherSettings::lineSpacing, 17, "px"}, {"glow", &DitherSettings::glow, 50, "%"},
        {"dots", &DitherSettings::dots, 50, "%"},                {"wobble", &DitherSettings::wobble, 32, "px"}};
    for (const auto &[name, setting, middle, unit] : middles) {
        FilterSettings settings = shown.session.filterEdit().value().settings;
        settings.dither.*setting = middle;
        shown.session.updateFilter(settings, true);
        QSlider *slider = shown.sheet->findChild<QSlider *>(QString::fromLatin1(name) + "Slider");
        QLineEdit *field = shown.sheet->findChild<QLineEdit *>(QString::fromLatin1(name) + "Field");
        QVERIFY2(slider && field, name);
        QCOMPARE(slider->value(), slider->maximum() / 2);
        QCOMPARE(slider->minimum(), 0);
        const QList<QLabel *> labels = field->parentWidget()->findChildren<QLabel *>(QString(), Qt::FindDirectChildrenOnly);
        QCOMPARE(labels.last()->text(), unit);
    }
    // Each slider's travel reaches its range's ends.
    const std::tuple<const char *, double DitherSettings::*, double, double> sliders[] = {
        {"lineSpacingSlider", &DitherSettings::lineSpacing, 2, 32}, {"glowSlider", &DitherSettings::glow, 0, 100},
        {"dotsSlider", &DitherSettings::dots, 0, 100},               {"wobbleSlider", &DitherSettings::wobble, 0, 64}};
    for (const auto &[name, setting, low, high] : sliders) {
        QSlider *slider = shown.sheet->findChild<QSlider *>(QString::fromLatin1(name));
        QVERIFY2(slider, name);
        slider->setValue(slider->maximum());
        QCOMPARE(shown.dither().*setting, high);
        slider->setValue(slider->minimum());
        QCOMPARE(shown.dither().*setting, low);
    }
    // Pixel Size and Pixel Shape stay away under Scanlines.
    FilterSettings settings = shown.session.filterEdit().value().settings;
    settings.dither.pixelSize = 4;
    shown.session.updateFilter(settings, true);
    QVERIFY(!shown.rows().join(',').contains("Pixel"));
    settings.dither.style = DitherStyle::bayer4;
    shown.session.updateFilter(settings, true);
    QVERIFY(shown.rows().contains("Pixel Shape|Draw each chunky pixel as a solid square, or as a round dot like a dot-matrix screen"));
    QVERIFY(!shown.rows().join(',').contains("Glow"));
}

void ScanlinesTests::theRowsWriteTheirSettings()
{
    Shown shown;
    const std::tuple<const char *, double DitherSettings::*, const char *, double> rows[] = {
        {"lineSpacingField", &DitherSettings::lineSpacing, "12", 12}, {"glowField", &DitherSettings::glow, "60", 60},
        {"dotsField", &DitherSettings::dots, "25", 25},               {"wobbleField", &DitherSettings::wobble, "9", 9}};
    for (const auto &[name, setting, typed, expected] : rows) {
        const DitherSettings before = shown.dither();
        QLineEdit *field = shown.sheet->findChild<QLineEdit *>(QString::fromLatin1(name));
        QVERIFY2(field, name);
        field->setFocus();
        field->selectAll();
        QTest::keyClicks(field, QString::fromLatin1(typed));
        QTest::keyClick(field, Qt::Key_Return);
        DitherSettings wanted = before;
        wanted.*setting = expected;
        QVERIFY2(shown.dither() == wanted, name);
    }
}

QTEST_MAIN(ScanlinesTests)
#include "ScanlinesTests.moc"
