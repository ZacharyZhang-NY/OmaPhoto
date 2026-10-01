#include "Document/BrushStroke.h"
#include "Document/Dither.h"
#include "Document/EditorSession.h"
#include <QPainter>
#include <QPainterPath>
#include <QtTest>
#include <set>

// Swift 1.3.3's Dither: the model and the kernel through `apply`.
namespace {
QImage filled(int width, int height, QColor colour)
{
    QImage image = BrushRaster::context(width, height, false);
    image.fill(colour);
    return image;
}

// A horizontal ramp of grays, left black, right white.
QImage ramp(int width, int height)
{
    QImage image = BrushRaster::context(width, height, false);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            image.setPixelColor(x, y, QColor::fromRgbF(float(x) / float(width - 1), float(x) / float(width - 1), float(x) / float(width - 1)));
    return image;
}

std::set<QRgb> colours(const QImage &image)
{
    std::set<QRgb> found;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            found.insert(image.pixel(x, y));
    return found;
}

DitherSettings style(DitherStyle each, double pixelSize = 1)
{
    DitherSettings settings;
    settings.style = each;
    settings.pixelSize = pixelSize;
    return settings;
}
}

class DitherTests : public QObject {
    Q_OBJECT
private slots:
    void stylesKeepSwiftsNamesOrderAndGroups();
    void normalizingClampsRoundsAndTrimsTheCharacters();
    void bayerThresholdsAGrayIntoItsPattern();
    void diffusionMakesTwoTonesOrMore();
    void twoColorsAndTheOriginalOnes();
    void alphaIsKeptAndClearPixelsLeftAlone();
    void chunkyPixelsFillTheirBlocks();
    void dotsLeaveTheirCornersToTheDarkColour();
    void marksDrawLightOnDarkOrDarkOnLight();
    void glyphsAreSortedByTheirInk();
    void densityAndContrastMoveTheThreshold();
    void theScreenTurnsWithItsAngle();
    void asciiDrawsWithItsOwnCharacters();
};

void DitherTests::stylesKeepSwiftsNamesOrderAndGroups()
{
    QStringList names;
    for (const DitherStyle each : allDitherStyles)
        names << rawValue(each);
    QCOMPARE(names, (QStringList{"Atkinson (Classic Mac)", "Floyd–Steinberg", "Bayer 2 × 2", "Bayer 4 × 4", "Bayer 8 × 8", "Halftone Dots",
                                 "Halftone Lines", "Halftone Diamonds", "Mac Patterns", "ASCII", "Scanlines (CRT)"}));
    QString groups;
    for (const DitherStyle each : allDitherStyles)
        groups += QString::number(ditherGroup(each));
    QCOMPARE(groups, QString("00111222333"));
    QVERIFY(diffuses(DitherStyle::floydSteinberg) && !diffuses(DitherStyle::bayer2));
    QVERIFY(hasTones(DitherStyle::bayer8) && !hasTones(DitherStyle::dots));
    QVERIFY(isHalftone(DitherStyle::diamonds) && !isHalftone(DitherStyle::patterns));
    QVERIFY(drawsMarks(DitherStyle::ascii) && drawsMarks(DitherStyle::lines) && !drawsMarks(DitherStyle::atkinson));
    QCOMPARE(rawValue(DitherPixelShape::dot), QString("Dot"));
    QCOMPARE(rawValue(DitherColors::twoColors), QString("Two Colors"));
    // Chunky square pixels, 1-bit Atkinson, glowing marks by default.
    const DitherSettings defaults;
    QVERIFY(defaults.style == DitherStyle::atkinson && defaults.pixelSize == 2 && defaults.pixelShape == DitherPixelShape::square);
    QVERIFY(defaults.levels == 2 && defaults.lightOnDark && defaults.colors == DitherColors::blackWhite);
}

void DitherTests::normalizingClampsRoundsAndTrimsTheCharacters()
{
    DitherSettings wild;
    wild.pixelSize = 40.6;
    wild.cellSize = 2;
    wild.textSize = 64.6;
    wild.angle = std::nan("");
    wild.levels = 4.5;
    wild.diffusion = -5;
    wild.density = 300;
    wild.contrast = std::numeric_limits<double>::infinity();
    wild.dark = AdjustmentColor(-1, 0.5, 2);
    wild.light = AdjustmentColor(3, -2, 0.25);
    wild.characters = QStringLiteral("ab\ncd\r\ne\u2028f\u000Bg\u000Ch\u0085i\u2029\t j") + QString(80, u'x');
    const DitherSettings normal = wild.normalized();
    QCOMPARE(normal.pixelSize, 32.0);
    QCOMPARE(normal.cellSize, 4.0);
    QCOMPARE(normal.textSize, 64.0);
    QCOMPARE(normal.angle, 45.0);
    QCOMPARE(normal.levels, 5.0);
    QCOMPARE(normal.diffusion, 0.0);
    QCOMPARE(normal.density, 100.0);
    QCOMPARE(normal.contrast, 0.0);
    wild.textSize = 5.4;
    QCOMPARE(wild.normalized().textSize, 6.0);
    wild.textSize = std::nan("");
    QCOMPARE(wild.normalized().textSize, 14.0);
    wild.textSize = 9.5;
    QCOMPARE(wild.normalized().textSize, 10.0);
    QVERIFY(normal.dark == AdjustmentColor(0, 0.5, 1) && normal.light == AdjustmentColor(1, 0, 0.25));
    // Every line break goes, tabs and spaces stay; 64 kept.
    QCOMPARE(normal.characters, QStringLiteral("abcdefghi\t j") + QString(52, u'x'));
    // A grapheme of many units counts once, never split.
    DitherSettings emoji;
    emoji.characters = QString(63, u'.') + QStringLiteral("\U0001F44D\U0001F3FD") + QStringLiteral("zz");
    QCOMPARE(emoji.normalized().characters, QString(63, u'.') + QStringLiteral("\U0001F44D\U0001F3FD"));
    QCOMPARE(DitherSettings().normalized(), DitherSettings());
}

void DitherTests::bayerThresholdsAGrayIntoItsPattern()
{
    // Half gray over Bayer 2 × 2: high thresholds light.
    const QImage out = style(DitherStyle::bayer2).apply(filled(4, 4, QColor(128, 128, 128)));
    const QRgb black = qRgb(0, 0, 0), white = qRgb(255, 255, 255);
    const std::array<QRgb, 4> pattern{black, white, white, black};
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            QCOMPARE(out.pixel(x, y), pattern[size_t((y & 1) * 2 + (x & 1))]);
    // Four tones on a ramp: 0, 85, 170, 255 alone.
    DitherSettings tones = style(DitherStyle::bayer4);
    tones.levels = 4;
    std::set<int> levels;
    const QImage toned = tones.apply(ramp(64, 8));
    for (int x = 0; x < 64; ++x)
        levels.insert(qRed(toned.pixel(x, 3)));
    QCOMPARE(levels, (std::set<int>{0, 85, 170, 255}));
}

void DitherTests::diffusionMakesTwoTonesOrMore()
{
    for (const DitherStyle each : {DitherStyle::atkinson, DitherStyle::floydSteinberg}) {
        const QImage out = style(each).apply(ramp(64, 16));
        QCOMPARE(colours(out), (std::set<QRgb>{qRgb(0, 0, 0), qRgb(255, 255, 255)}));
        // More white towards the light end.
        int left = 0, right = 0;
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                left += qRed(out.pixel(x, y)) == 255;
                right += qRed(out.pixel(63 - x, y)) == 255;
            }
        }
        QVERIFY2(left < 40 && right > 216, qPrintable(QStringLiteral("%1 %2").arg(left).arg(right)));
    }
    // No diffusion: a flat mid gray rounds whole.
    DitherSettings flat = style(DitherStyle::atkinson);
    flat.diffusion = 0;
    QCOMPARE(colours(flat.apply(filled(8, 8, QColor(140, 140, 140)))), (std::set<QRgb>{qRgb(255, 255, 255)}));
}

void DitherTests::twoColorsAndTheOriginalOnes()
{
    DitherSettings two = style(DitherStyle::bayer8);
    two.colors = DitherColors::twoColors;
    two.dark = AdjustmentColor(0.2, 0, 0.4);
    two.light = AdjustmentColor(1, 0.8, 0.6);
    QCOMPARE(colours(two.apply(ramp(32, 8))), (std::set<QRgb>{qRgb(51, 0, 102), qRgb(255, 204, 153)}));
    // Original: red stays red, dithered on its own channels.
    DitherSettings original = style(DitherStyle::bayer8);
    original.colors = DitherColors::original;
    for (const QRgb each : colours(original.apply(filled(8, 8, QColor(255, 0, 0)))))
        QCOMPARE(each, qRgb(255, 0, 0));
}

void DitherTests::alphaIsKeptAndClearPixelsLeftAlone()
{
    QImage image = ramp(16, 4);
    image.setPixelColor(0, 0, Qt::transparent);
    image.setPixelColor(5, 1, QColor::fromRgbF(1, 1, 1, 0.5f));
    const QImage out = style(DitherStyle::floydSteinberg).apply(image);
    QCOMPARE(out.pixelColor(0, 0).alpha(), 0);
    QCOMPARE(out.pixelColor(5, 1).alpha(), 128);
    for (int x = 1; x < 16; ++x)
        QCOMPARE(out.pixelColor(x, 3).alpha(), 255);
}

void DitherTests::chunkyPixelsFillTheirBlocks()
{
    const QImage out = style(DitherStyle::bayer8, 4).apply(ramp(18, 10));
    QCOMPARE(out.size(), QSize(18, 10));
    // One colour a 4 × 4 block; edge blocks cut.
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 18; ++x)
            QCOMPARE(out.pixel(x, y), out.pixel(x / 4 * 4, y / 4 * 4));
    QCOMPARE(colours(out.copy(0, 0, 16, 8)), (std::set<QRgb>{qRgb(0, 0, 0), qRgb(255, 255, 255)}));
    // A cut block averages in the clear beyond the edge.
    QCOMPARE(out.pixelColor(17, 0).alpha(), 128);
    QCOMPARE(out.pixelColor(0, 9).alpha(), 128);
    QCOMPARE(out.pixelColor(17, 9).alpha(), 64);
    // Diffused small: each block's exact average, then its pixel.
    const QImage source = ramp(18, 10);
    QImage averaged = BrushRaster::context(5, 3, false);
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 5; ++x) {
            std::array<int, 4> sum{};
            for (int sy = y * 4; sy < std::min(10, y * 4 + 4); ++sy)
                for (int sx = x * 4; sx < std::min(18, x * 4 + 4); ++sx)
                    for (int channel = 0; channel < 4; ++channel)
                        sum[size_t(channel)] += source.constScanLine(sy)[sx * 4 + channel];
            for (int channel = 0; channel < 4; ++channel)
                averaged.scanLine(y)[x * 4 + channel] = uchar((sum[size_t(channel)] + 8) / 16);
        }
    }
    const QImage small = style(DitherStyle::floydSteinberg).apply(averaged);
    const QImage chunky = style(DitherStyle::floydSteinberg, 4).apply(source);
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 18; ++x)
            QCOMPARE(chunky.pixel(x, y), small.pixel(x / 4, y / 4));
    // A block's average: half black, half white reads as gray.
    QImage halves = BrushRaster::context(8, 8, false);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            halves.setPixelColor(x, y, x % 2 ? Qt::white : Qt::black);
    DitherSettings levels = style(DitherStyle::bayer2, 8);
    levels.levels = 3;
    QCOMPARE(colours(levels.apply(halves)), (std::set<QRgb>{qRgb(128, 128, 128)}));
}

void DitherTests::dotsLeaveTheirCornersToTheDarkColour()
{
    DitherSettings dots = style(DitherStyle::bayer2, 8);
    dots.pixelShape = DitherPixelShape::dot;
    const QImage out = dots.apply(filled(16, 16, Qt::white));
    // White dots on black, each centred in its block.
    QCOMPARE(out.pixel(0, 0), qRgb(0, 0, 0));
    QCOMPARE(out.pixel(4, 4), qRgb(255, 255, 255));
    QCOMPARE(out.pixel(8, 15), qRgb(0, 0, 0));
    QCOMPARE(out.pixel(12, 12), qRgb(255, 255, 255));
    // Two Colors: the gaps are the dark one.
    dots.colors = DitherColors::twoColors;
    dots.dark = AdjustmentColor(0, 0, 1);
    QCOMPARE(dots.apply(filled(16, 16, Qt::white)).pixel(0, 0), qRgb(0, 0, 255));
    // Square pixels have no gaps.
    dots.pixelShape = DitherPixelShape::square;
    QCOMPARE(dots.apply(filled(16, 16, Qt::white)).pixel(0, 0), qRgb(255, 255, 255));
}

void DitherTests::marksDrawLightOnDarkOrDarkOnLight()
{
    for (const DitherStyle each : {DitherStyle::dots, DitherStyle::lines, DitherStyle::diamonds, DitherStyle::patterns, DitherStyle::ascii}) {
        DitherSettings marks = style(each);
        const QImage glowing = marks.apply(ramp(64, 64));
        // Hard marks, but for the characters' smoothed ink.
        if (each == DitherStyle::ascii) {
            for (const QRgb colour : colours(glowing))
                QVERIFY(qRed(colour) == qGreen(colour) && qGreen(colour) == qBlue(colour));
        } else {
            QCOMPARE(colours(glowing), (std::set<QRgb>{qRgb(0, 0, 0), qRgb(255, 255, 255)}));
        }
        marks.lightOnDark = false;
        const QImage inked = marks.apply(ramp(64, 64));
        QVERIFY2(inked != glowing, qPrintable(rawValue(each)));
        // Black stays black, white white, either way.
        QCOMPARE(qRed(glowing.pixel(0, 32)), 0);
        QCOMPARE(qRed(inked.pixel(63, 32)), 255);
    }
}

void DitherTests::glyphsAreSortedByTheirInk()
{
    const DitherSettings::Glyphs glyphs = DitherSettings::glyphs(QStringLiteral("@. .#"), 14);
    // 14 tall; a bold monospace M at 12 pixels wide.
    QFont font(QStringLiteral("monospace"));
    font.setWeight(QFont::Bold);
    font.setPixelSize(12);
    QCOMPARE(glyphs.height, 14);
    QCOMPARE(glyphs.width, int(std::lround(QFontMetricsF(font).horizontalAdvance(QStringLiteral("M")))));
    QVERIFY(glyphs.width < glyphs.height);
    // Four distinct: space, dot, hash, at.
    QCOMPARE(glyphs.coverage.size(), size_t(4));
    QCOMPARE(glyphs.maps.size(), size_t(4 * glyphs.width * glyphs.height));
    QCOMPARE(glyphs.coverage[0], 0.0f);
    QVERIFY(glyphs.coverage[1] > 0 && glyphs.coverage[1] < glyphs.coverage[2] && glyphs.coverage[2] <= glyphs.coverage[3]);
    QVERIFY(*std::max_element(glyphs.maps.begin() + 3 * glyphs.width * glyphs.height, glyphs.maps.end()) > 200);
    QCOMPARE(DitherSettings::glyphs(DitherSettings::defaultCharacters(), 12).coverage.size(), size_t(10));
    // A 20-pixel line: letters at 17 pixels, on one baseline.
    font.setPixelSize(17);
    const QFontMetricsF metrics(font);
    const int baseline = 20 - int(std::round((20 - (metrics.ascent() + metrics.descent())) / 2 + metrics.descent()));
    const auto ink = [](const DitherSettings::Glyphs &glyph) {
        int top = glyph.height, bottom = -1;
        double sum = 0, sumX = 0;
        for (int y = 0; y < glyph.height; ++y)
            for (int x = 0; x < glyph.width; ++x) {
                const int value = glyph.maps[size_t(y * glyph.width + x)];
                sum += value;
                sumX += value * (x + 0.5);
                if (value > 128) {
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                }
            }
        return std::tuple(top, bottom, sumX / sum);
    };
    const auto [mTop, mBottom, mMiddle] = ink(DitherSettings::glyphs(QStringLiteral("M"), 20));
    const auto [dotTop, dotBottom, dotMiddle] = ink(DitherSettings::glyphs(QStringLiteral("."), 20));
    // M's caps stand on the baseline, as the dot does.
    QVERIFY2(mBottom - mTop + 1 >= 11 && mBottom - mTop + 1 <= 14, qPrintable(QString::number(mBottom - mTop + 1)));
    QCOMPARE(mBottom, baseline - 1);
    QCOMPARE(dotBottom, baseline - 1);
    QVERIFY(dotTop > mTop);
    // Exactly the letter drawn on that baseline, filled.
    const DitherSettings::Glyphs drawnM = DitherSettings::glyphs(QStringLiteral("M"), 20);
    QImage reference(drawnM.width, drawnM.height, QImage::Format_Grayscale8);
    reference.fill(0);
    {
        QPainterPath path;
        path.addText(0, baseline, font, QStringLiteral("M"));
        QPainter painter(&reference);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillPath(path, Qt::white);
    }
    for (int y = 0; y < drawnM.height; ++y)
        for (int x = 0; x < drawnM.width; ++x)
            QCOMPARE(int(drawnM.maps[size_t(y * drawnM.width + x)]), int(reference.constScanLine(y)[x]));
    // At 64: a 53-pixel face, a 32-wide cell, drawn exactly.
    font.setPixelSize(53);
    const QFontMetricsF large(font);
    const DitherSettings::Glyphs bigM = DitherSettings::glyphs(QStringLiteral("M"), 64);
    QCOMPARE(bigM.width, int(std::lround(large.horizontalAdvance(QStringLiteral("M")))));
    QCOMPARE(bigM.width, 32);
    const int bigBaseline = 64 - int(std::round((64 - (large.ascent() + large.descent())) / 2 + large.descent()));
    QImage bigReference(bigM.width, bigM.height, QImage::Format_Grayscale8);
    bigReference.fill(0);
    {
        QPainterPath path;
        path.addText(0, bigBaseline, font, QStringLiteral("M"));
        QPainter painter(&bigReference);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillPath(path, Qt::white);
    }
    long inkSum = 0;
    for (int y = 0; y < bigM.height; ++y)
        for (int x = 0; x < bigM.width; ++x) {
            QCOMPARE(int(bigM.maps[size_t(y * bigM.width + x)]), int(bigReference.constScanLine(y)[x]));
            inkSum += bigM.maps[size_t(y * bigM.width + x)];
        }
    // Coverage is the map's mean, ink over the whole cell.
    QCOMPARE(bigM.coverage.front(), float(inkSum) / float(255 * bigM.width * bigM.height));
    // Each centred across its cell by its advance.
    const int width = DitherSettings::glyphs(QStringLiteral("M"), 20).width;
    QVERIFY2(std::abs(mMiddle - width / 2.0) < 0.6 && std::abs(dotMiddle - width / 2.0) < 1, qPrintable(QStringLiteral("%1 %2").arg(mMiddle).arg(dotMiddle)));
    // Canonically equal graphemes are one, as Swift's Characters.
    QCOMPARE(DitherSettings::glyphs(QStringLiteral("\u00E9e\u0301"), 20).coverage.size(), size_t(1));
}

void DitherTests::densityAndContrastMoveTheThreshold()
{
    // Bayer 8 × 8 on flat gray: whites follow tone.
    const auto whites = [](int gray, double density, double contrast) {
        DitherSettings settings = style(DitherStyle::bayer8);
        settings.density = density;
        settings.contrast = contrast;
        const QImage out = settings.apply(filled(8, 8, QColor(gray, gray, gray)));
        int count = 0;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                count += qRed(out.pixel(x, y)) == 255;
        return count;
    };
    // Density: gamma 2^(1.5 d), more ink as it rises.
    QCOMPARE(whites(128, 0, 0), 32);
    QCOMPARE(whites(128, 50, 0), 20);
    QCOMPARE(whites(128, -50, 0), 42);
    // Contrast pivots on mid gray, steeper as it rises.
    QCOMPARE(whites(153, 0, 0), 38);
    QCOMPARE(whites(153, 0, 50), 44);
    QCOMPARE(whites(153, 0, -50), 35);
}

void DitherTests::theScreenTurnsWithItsAngle()
{
    // Halftone lines at 0°: rows alike; at 90°: columns alike.
    for (const double angle : {0.0, 90.0}) {
        DitherSettings lines = style(DitherStyle::lines);
        lines.angle = angle;
        lines.cellSize = 8;
        const QImage out = lines.apply(filled(32, 32, QColor(100, 100, 100)));
        bool alike = true, varied = false;
        for (int a = 0; a < 32; ++a) {
            for (int b = 0; b < 32; ++b) {
                const QRgb here = angle == 0 ? out.pixel(b, a) : out.pixel(a, b);
                alike = alike && here == (angle == 0 ? out.pixel(0, a) : out.pixel(a, 0));
                varied = varied || here != out.pixel(0, 0);
            }
        }
        QVERIFY2(alike && varied, qPrintable(QString::number(angle)));
    }
}

void DitherTests::asciiDrawsWithItsOwnCharacters()
{
    DitherSettings ascii = style(DitherStyle::ascii);
    ascii.textSize = 12;
    const QImage source = ramp(48, 24);
    // Full resolution: Pixel Size and Shape leave ASCII alone.
    DitherSettings chunky = ascii;
    chunky.pixelSize = 8;
    chunky.pixelShape = DitherPixelShape::dot;
    ascii.pixelSize = 1;
    QCOMPARE(chunky.apply(source), ascii.apply(source));
    // A blank alphabet marks nothing: all the dark paper.
    ascii.characters = QStringLiteral(" ");
    QCOMPARE(colours(ascii.apply(source)), (std::set<QRgb>{qRgb(0, 0, 0)}));
    // None at all: Swift's default alphabet.
    ascii.characters = QString();
    const QImage fallback = ascii.apply(source);
    ascii.characters = DitherSettings::defaultCharacters();
    QCOMPARE(fallback, ascii.apply(source));
    // Another alphabet draws otherwise.
    ascii.characters = QStringLiteral(" #");
    QVERIFY(ascii.apply(source) != fallback);
}

QTEST_MAIN(DitherTests)
#include "DitherTests.moc"
