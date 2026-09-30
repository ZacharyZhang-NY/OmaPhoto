#include "Document/BrushStroke.h"
#include "Document/Dither.h"
#include <QPainter>
#include <QPainterPath>
#include <QtTest>
#include <set>

// Dither's styles and colours, pixel for pixel, by hand.
namespace {
QImage filled(int width, int height, QColor colour)
{
    QImage image = BrushRaster::context(width, height, false);
    image.fill(colour);
    return image;
}

DitherSettings style(DitherStyle each, double pixelSize = 1)
{
    DitherSettings settings;
    settings.style = each;
    settings.pixelSize = pixelSize;
    return settings;
}

// White pixels as ones, row by row.
QString bits(const QImage &image)
{
    QString text;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x)
            text += qRed(image.pixel(x, y)) == 255 ? u'1' : u'0';
        text += u'/';
    }
    return text;
}

std::array<int, 4> bytes(const QImage &image, int x, int y)
{
    const uchar *pixel = image.constScanLine(y) + x * 4;
    return {pixel[0], pixel[1], pixel[2], pixel[3]};
}
}

class DitherKernelTests : public QObject {
    Q_OBJECT
private slots:
    void eachDiffusionPassesItsOwnShare();
    void eachBayerMatrixHasItsOwnPattern();
    void eachHalftoneShapeMarksItsOwnPixels();
    void macPatternsFillByTheirTone();
    void marksTakeTheOriginalOrTwoColours();
    void aBlockAveragesPremultipliedPixels();
    void coloursRoundAndOriginalDotsGapBlack();
    void eachRangeClampsAndRounds();
    void aStraightSourceIsPremultipliedFirst();
    void theCellSizeReachesTheScreen();
    void overlappingOutlinesStayInked();
};

void DitherKernelTests::eachDiffusionPassesItsOwnShare()
{
    // Half gray: Atkinson passes 1/8 on, Floyd–Steinberg 7/16.
    QCOMPARE(bits(style(DitherStyle::atkinson).apply(filled(3, 1, QColor(128, 128, 128)))), QString("100/"));
    QCOMPARE(bits(style(DitherStyle::floydSteinberg).apply(filled(3, 1, QColor(128, 128, 128)))), QString("101/"));
}

void DitherKernelTests::eachBayerMatrixHasItsOwnPattern()
{
    // Half gray: white where the threshold passes one half.
    QCOMPARE(bits(style(DitherStyle::bayer4).apply(filled(4, 4, QColor(128, 128, 128)))), QString("0101/1010/0101/1010/"));
    QCOMPARE(bits(style(DitherStyle::bayer8).apply(filled(8, 2, QColor(128, 128, 128)))), QString("01010101/10101010/"));
    // Gray 96 tells the nested matrices apart.
    QCOMPARE(bits(style(DitherStyle::bayer2).apply(filled(4, 4, QColor(96, 96, 96)))), QString("0101/1010/0101/1010/"));
    QCOMPARE(bits(style(DitherStyle::bayer4).apply(filled(4, 4, QColor(96, 96, 96)))), QString("0001/1010/0100/1010/"));
}

void DitherKernelTests::eachHalftoneShapeMarksItsOwnPixels()
{
    // Gray 87, 0°, cells of 8: dots, diamonds, lines differ.
    const QImage source = filled(8, 8, QColor(87, 87, 87));
    const auto lit = [&source](DitherStyle each, int x, int y) {
        DitherSettings halftone = style(each);
        halftone.angle = 0;
        halftone.cellSize = 8;
        return qRed(halftone.apply(source).pixel(x, y)) == 255;
    };
    QVERIFY(lit(DitherStyle::dots, 1, 4) && lit(DitherStyle::dots, 4, 1));
    QVERIFY(!lit(DitherStyle::diamonds, 1, 4) && !lit(DitherStyle::diamonds, 4, 1));
    QVERIFY(lit(DitherStyle::lines, 1, 4) && !lit(DitherStyle::lines, 4, 1));
}

void DitherKernelTests::macPatternsFillByTheirTone()
{
    // Half gray lights pattern 8: rows 0x88, 0x55, 0x22.
    QCOMPARE(bits(style(DitherStyle::patterns).apply(filled(8, 4, QColor(128, 128, 128)))), QString("10001000/01010101/00100010/01010101/"));
}

void DitherKernelTests::marksTakeTheOriginalOrTwoColours()
{
    const QImage source = filled(8, 8, QColor(200, 100, 50));
    // Original: marks in the pixel's colour on black or white.
    for (const bool glowing : {true, false}) {
        DitherSettings dots = style(DitherStyle::dots);
        dots.colors = DitherColors::original;
        dots.lightOnDark = glowing;
        const QImage out = dots.apply(source);
        std::set<QRgb> found;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                found.insert(out.pixel(x, y));
        QCOMPARE(found, (std::set<QRgb>{qRgb(200, 100, 50), glowing ? qRgb(0, 0, 0) : qRgb(255, 255, 255)}));
    }
    // Two Colors: light ink on dark, dark on light.
    DitherSettings two = style(DitherStyle::dots);
    two.colors = DitherColors::twoColors;
    two.dark = AdjustmentColor(0, 0, 1);
    two.light = AdjustmentColor(1, 1, 0);
    two.angle = 0;
    const QImage glowing = two.apply(filled(8, 8, QColor(87, 87, 87)));
    two.lightOnDark = false;
    const QImage inked = two.apply(filled(8, 8, QColor(87, 87, 87)));
    QCOMPARE(glowing.pixel(4, 4), qRgb(255, 255, 0));
    QCOMPARE(glowing.pixel(0, 0), qRgb(0, 0, 255));
    QCOMPARE(inked.pixel(4, 4), qRgb(0, 0, 255));
    QCOMPARE(inked.pixel(0, 0), qRgb(255, 255, 0));
}

void DitherKernelTests::aBlockAveragesPremultipliedPixels()
{
    // A straight (64, 128, 192, 128) beside three clear.
    QImage source = BrushRaster::context(2, 2, false);
    source.setPixelColor(0, 0, QColor(64, 128, 192, 128));
    QCOMPARE(bytes(source, 0, 0), (std::array<int, 4>{32, 64, 96, 128}));
    DitherSettings original = style(DitherStyle::atkinson, 2);
    original.colors = DitherColors::original;
    original.levels = 8;
    original.diffusion = 0;
    // Averaged (8, 16, 24, 32); tones 2/7, 4/7, 5/7.
    const QImage out = original.apply(source);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
            QCOMPARE(bytes(out, x, y), (std::array<int, 4>{9, 18, 23, 32}));
}

void DitherKernelTests::coloursRoundAndOriginalDotsGapBlack()
{
    // (0.5, 0.1, 0.3) is (128, 26, 77), rounded.
    DitherSettings two = style(DitherStyle::bayer2);
    two.colors = DitherColors::twoColors;
    two.dark = AdjustmentColor(0.5, 0.1, 0.3);
    QCOMPARE(two.apply(filled(2, 2, Qt::black)).pixel(0, 0), qRgb(128, 26, 77));
    // Original dots gap black, whatever dark colour is kept.
    DitherSettings dots = style(DitherStyle::bayer2, 8);
    dots.pixelShape = DitherPixelShape::dot;
    dots.colors = DitherColors::original;
    dots.dark = AdjustmentColor(1, 0, 0);
    const QImage out = dots.apply(filled(8, 8, Qt::white));
    QCOMPARE(out.pixel(0, 0), qRgb(0, 0, 0));
    QCOMPARE(out.pixel(4, 4), qRgb(255, 255, 255));
    // A dot's rim is smoothed: part colour, part gap.
    const int rim = qRed(out.pixel(0, 4));
    QVERIFY2(rim > 0 && rim < 255, qPrintable(QString::number(rim)));
    // One-pixel dots are squares: no gaps.
    dots.pixelSize = 1;
    QCOMPARE(dots.apply(filled(8, 8, Qt::white)).pixel(0, 0), qRgb(255, 255, 255));
}

void DitherKernelTests::eachRangeClampsAndRounds()
{
    const auto normal = [](const std::function<void(DitherSettings &)> &change) {
        DitherSettings settings;
        change(settings);
        return settings.normalized();
    };
    QCOMPARE(normal([](DitherSettings &d) { d.pixelSize = 2.5; }).pixelSize, 3.0);
    QCOMPARE(normal([](DitherSettings &d) { d.pixelSize = 32; }).pixelSize, 32.0);
    QCOMPARE(normal([](DitherSettings &d) { d.pixelSize = 0; }).pixelSize, 1.0);
    QCOMPARE(normal([](DitherSettings &d) { d.pixelSize = -std::numeric_limits<double>::infinity(); }).pixelSize, 2.0);
    QCOMPARE(normal([](DitherSettings &d) { d.cellSize = 64; }).cellSize, 64.0);
    QCOMPARE(normal([](DitherSettings &d) { d.cellSize = 65; }).cellSize, 64.0);
    QCOMPARE(normal([](DitherSettings &d) { d.cellSize = 12.5; }).cellSize, 13.0);
    QCOMPARE(normal([](DitherSettings &d) { d.levels = 8; }).levels, 8.0);
    QCOMPARE(normal([](DitherSettings &d) { d.levels = 9; }).levels, 8.0);
    QCOMPARE(normal([](DitherSettings &d) { d.levels = 1; }).levels, 2.0);
    // Angle, diffusion, density and contrast keep their fractions.
    const DitherSettings fractions = normal([](DitherSettings &d) {
        d.angle = 12.25;
        d.diffusion = 33.5;
        d.density = -7.75;
        d.contrast = 99.5;
    });
    QVERIFY(fractions.angle == 12.25 && fractions.diffusion == 33.5 && fractions.density == -7.75 && fractions.contrast == 99.5);
    QCOMPARE(normal([](DitherSettings &d) { d.angle = -91; }).angle, -90.0);
    QCOMPARE(normal([](DitherSettings &d) { d.angle = 180; }).angle, 90.0);
    QCOMPARE(normal([](DitherSettings &d) { d.angle = std::numeric_limits<double>::infinity(); }).angle, 45.0);
    QCOMPARE(normal([](DitherSettings &d) { d.cellSize = std::nan(""); }).cellSize, 8.0);
    QCOMPARE(normal([](DitherSettings &d) { d.cellSize = 1; }).cellSize, 4.0);
    QCOMPARE(normal([](DitherSettings &d) { d.levels = std::nan(""); }).levels, 2.0);
    QCOMPARE(normal([](DitherSettings &d) { d.diffusion = std::nan(""); }).diffusion, 100.0);
    QCOMPARE(normal([](DitherSettings &d) { d.diffusion = 101; }).diffusion, 100.0);
    QCOMPARE(normal([](DitherSettings &d) { d.density = std::nan(""); }).density, 0.0);
    QCOMPARE(normal([](DitherSettings &d) { d.density = -101; }).density, -100.0);
    QCOMPARE(normal([](DitherSettings &d) { d.contrast = -200; }).contrast, -100.0);
    QCOMPARE(normal([](DitherSettings &d) { d.contrast = 101; }).contrast, 100.0);
    // A lone CR is a line break too.
    QCOMPARE(normal([](DitherSettings &d) { d.characters = QStringLiteral("a\rb"); }).characters, QString("ab"));
}

void DitherKernelTests::aStraightSourceIsPremultipliedFirst()
{
    // Straight alpha, clear neighbours hiding green: premultiplied first.
    QImage straight(2, 2, QImage::Format_RGBA8888);
    straight.fill(QColor(0, 255, 0, 0));
    uchar *first = straight.scanLine(0);
    first[0] = 64;
    first[1] = 128;
    first[2] = 192;
    first[3] = 128;
    DitherSettings original = style(DitherStyle::atkinson, 2);
    original.colors = DitherColors::original;
    original.levels = 8;
    original.diffusion = 0;
    const QImage out = original.apply(straight);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
            QCOMPARE(bytes(out, x, y), (std::array<int, 4>{9, 18, 23, 32}));
}

void DitherKernelTests::theCellSizeReachesTheScreen()
{
    // Lines at gray 87, 0°: cells 12 and 8 differ.
    const auto lit = [](double cell, int x, int y) {
        DitherSettings lines = style(DitherStyle::lines);
        lines.angle = 0;
        lines.cellSize = cell;
        return qRed(lines.apply(filled(16, 16, QColor(87, 87, 87))).pixel(x, y)) == 255;
    };
    QVERIFY(!lit(12, 4, 3) && lit(12, 4, 7));
    QVERIFY(lit(8, 4, 3) && !lit(8, 4, 7));
    // ASCII: one character tiled at its own cell size.
    DitherSettings ascii = style(DitherStyle::ascii);
    ascii.cellSize = 12;
    ascii.characters = QStringLiteral("#");
    const QImage out = ascii.apply(filled(24, 24, Qt::white));
    for (int y = 0; y < 12; ++y)
        for (int x = 0; x < 12; ++x)
            QCOMPARE(out.pixel(x + 12, y + 12), out.pixel(x, y));
    const std::vector<uint8_t> map = DitherSettings::glyphs(QStringLiteral("#"), 12).maps;
    for (int index = 0; index < 144; ++index)
        QCOMPARE(qRed(out.pixel(index % 12, index / 12)), int(std::lround(map[size_t(index)] / 255.0f * 255.0f)));
}

void DitherKernelTests::overlappingOutlinesStayInked()
{
    // An accent over a letter: text rendering inks their overlap.
    const QString character = QStringLiteral("A\u0340");
    const int cell = 64;
    const std::vector<uint8_t> map = DitherSettings::glyphs(character, cell).maps;
    QFont font(QStringLiteral("monospace"));
    font.setWeight(QFont::Bold);
    font.setPixelSize(74);
    QPainterPath path;
    path.addText(0, 0, font, character);
    const QRectF bounds = path.boundingRect();
    QImage text(cell, cell, QImage::Format_Grayscale8);
    text.fill(0);
    {
        QPainter painter(&text);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(QPointF((cell - bounds.width()) / 2 - bounds.left(), (cell - bounds.height()) / 2 - bounds.top()), character);
    }
    int holes = 0;
    for (int y = 0; y < cell; ++y)
        for (int x = 0; x < cell; ++x)
            holes += text.constScanLine(y)[x] > 200 && map[size_t(y * cell + x)] < 50;
    QVERIFY2(holes <= 2, qPrintable(QString::number(holes)));
}

QTEST_MAIN(DitherKernelTests)
#include "DitherKernelTests.moc"
