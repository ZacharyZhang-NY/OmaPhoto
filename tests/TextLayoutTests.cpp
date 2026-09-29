#include "Document/DocumentLimits.h"
#include "BrushFixtures.h"
#include "Document/EditorSession.h"
#include "Rendering/TextLayout.h"

// Text laid out as TextKit lays it: fonts, boxes, lines.
namespace {
LayerTextStyle styled(const QString &content, double size = 72)
{
    LayerTextStyle style;
    style.content = content;
    style.fontSize = size;
    return style;
}

// The rows and columns where a text image has ink.
QRect inked(const QImage &image)
{
    QRect found;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (alpha(image, x, y) > 64)
                found |= QRect(x, y, 1, 1);
        }
    }
    return found;
}
}

class TextLayoutTests : public QObject {
    Q_OBJECT
private slots:
    void facesAreFoundByPostScriptName();
    void pointTextMeasuresItsLines();
    void aBoxWrapsAndDropsWhatDoesNotFit();
    void linesAlignInTheirBox();
    void baselinesSitAtTheBottomOfFixedLines();
    void imagesKeepSwiftsLimits();
    void aBoxUnderItsPaddingHoldsAPixel();
    void glyphsTakeTheStyleColour();
    void paragraphsBreakWhereCocoaBreaksThem();
    void tabsStopEvery28Points();
    void linesStopWhereTheBoxDoes();
    void aSelectionFollowsItsRunsToTheLinesEdge();
};

void TextLayoutTests::facesAreFoundByPostScriptName()
{
    LayerTextStyle style = styled(QStringLiteral("Text"), 36.5);
    style.fontName = QStringLiteral("DejaVuSans-Bold");
    style.tracking = 4;
    const QFont bold = TextLayout::font(style);
    QCOMPARE(bold.family(), QString("DejaVu Sans"));
    QCOMPARE(bold.styleName(), QString("Bold"));
    QCOMPARE(QFontInfo(bold).pixelSize(), 37);
    // A whole size is that many pixels, as Swift's points.
    style.fontSize = 40;
    QFont pixels = TextLayout::font(style);
    const double advance = QFontMetricsF(pixels).horizontalAdvance(QStringLiteral("Hamburgefons"));
    pixels.setPixelSize(40);
    QVERIFY(std::abs(QFontMetricsF(pixels).horizontalAdvance(QStringLiteral("Hamburgefons")) - advance) < 0.01);
    // Qt draws faces in whole pixels: a fraction rounds.
    style.fontSize = 36.4;
    QCOMPARE(QFontInfo(TextLayout::font(style)).pixelSize(), 36);
    style.fontSize = 36.6;
    QCOMPARE(QFontInfo(TextLayout::font(style)).pixelSize(), 37);
    // Swift always sets a kern: no pairs, the tracking added.
    QVERIFY(!bold.kerning());
    QCOMPARE(bold.letterSpacingType(), QFont::AbsoluteSpacing);
    QCOMPARE(bold.letterSpacing(), 4.0);
    QCOMPARE(bold.hintingPreference(), QFont::PreferNoHinting);
    // Not installed: Qt's match, where macOS takes its own.
    style.fontName = QStringLiteral("Helvetica");
    QCOMPARE(TextLayout::font(style).family(), QString("Helvetica"));
    QCOMPARE(TextLayout::font(style).styleName(), QString());
    // Distributions split DejaVu differently: its faces, once each.
    const QStringList names = TextLayout::availableFonts();
    QVERIFY(names.contains(QStringLiteral("DejaVuSans")) && names.contains(QStringLiteral("DejaVuSerif-Bold")));
    QCOMPARE(QSet<QString>(names.begin(), names.end()).size(), names.size());
    QStringList sorted = names;
    sorted.sort();
    QCOMPARE(names, sorted);
}

void TextLayoutTests::pointTextMeasuresItsLines()
{
    // One line of 72: 86.4 high, rounded up, padded.
    const QSizeF one = EditorSession::textBoxSize(styled(QStringLiteral("Text")));
    QCOMPARE(one.height(), 111.0);
    const TextLines lines(styled(QStringLiteral("Text")), QSizeF(100'000, 100'000));
    QCOMPARE(one.width(), std::ceil(lines.usedSize().width() + 24 + 7.2));
    QVERIFY(lines.usedSize().width() > 100 && !lines.overflows());
    QCOMPARE(EditorSession::textBoxSize(styled(QStringLiteral("A\nB"))).height(), 197.0);
    // The widest line sets the width, wherever it sits.
    QCOMPARE(EditorSession::textBoxSize(styled(QStringLiteral("WWWW\nI"))).width(), EditorSession::textBoxSize(styled(QStringLiteral("WWWW"))).width());
    // A final newline adds no line; two newlines make two.
    QCOMPARE(EditorSession::textBoxSize(styled(QStringLiteral("Text\n"))), one);
    QCOMPARE(EditorSession::textBoxSize(styled(QStringLiteral("\n\n"))).height(), 197.0);
    // Empty text keeps a line and a caret's room.
    QCOMPARE(EditorSession::textBoxSize(styled(QString())), QSizeF(32, 111));
    QCOMPARE(EditorSession::textBoxSize(styled(QString(), 2)), QSizeF(25, 27));
    // Tracking widens by its amount a letter.
    LayerTextStyle spaced = styled(QStringLiteral("Text"));
    spaced.tracking = 10;
    QVERIFY(std::abs(EditorSession::textBoxSize(spaced).width() - one.width() - 40) <= 1);
    LayerTextStyle led = styled(QStringLiteral("Text"));
    led.leading = 100;
    QCOMPARE(EditorSession::textBoxSize(led).height(), 124.0);
    QCOMPARE(EditorSession::textImage(styled(QStringLiteral("Text"))).size(), one.toSize());
    // Point text never wraps short of 100,000 pixels.
    const QSizeF wide = EditorSession::textBoxSize(styled(QStringLiteral("Hamburgefonstiv ").repeated(8)));
    QVERIFY(wide.width() > 3'000 && wide.height() == 111);
}

void TextLayoutTests::aBoxWrapsAndDropsWhatDoesNotFit()
{
    LayerTextStyle style = styled(QStringLiteral("Words that wrap inside the box and keep on going"), 24);
    style.boxSize = QSizeF(160, 120);
    QCOMPARE(EditorSession::textBoxSize(style), QSizeF(160, 120));
    const QImage image = EditorSession::textImage(style);
    QCOMPARE(image.size(), QSize(160, 120));
    // 96 pixels hold three 28.8 lines; the rest drops.
    const TextLines lines(style, QSizeF(136, 96));
    QCOMPARE(lines.usedSize().height(), 3 * 28.8);
    QVERIFY(lines.overflows() && lines.usedSize().width() <= 136);
    // Half a pixel short of three lines holds two.
    QCOMPARE(TextLines(style, QSizeF(136, 3 * 28.8 - 0.5)).usedSize().height(), 2 * 28.8);
    const QRect ink = inked(image);
    QVERIFY2(ink.left() >= 12 && ink.right() < 148 && ink.bottom() < 12 + 3 * 28.8 + 1, qPrintable(QString("%1 %2 %3").arg(ink.left()).arg(ink.right()).arg(ink.bottom())));
    // Room for all: nothing falls past the bottom.
    style.boxSize = QSizeF(160, 400);
    QVERIFY(!TextLines(style, QSizeF(136, 376)).overflows());
    // A word longer than the line breaks anywhere.
    style.content = QStringLiteral("Unbreakablewordthatislong");
    const TextLines broken(style, QSizeF(136, 376));
    QVERIFY(broken.lineCount() > 1);
    // With no space, its line ends where the next begins.
    QCOMPARE(broken.lineEnd(0), broken.lineStart(1));
}

void TextLayoutTests::linesAlignInTheirBox()
{
    LayerTextStyle style = styled(QStringLiteral("WWWWW\nI"), 40);
    style.boxSize = QSizeF(400, 200);
    const auto lineInk = [&](int top, int bottom) {
        const QImage image = EditorSession::textImage(style);
        return inked(image.copy(0, top, image.width(), bottom - top));
    };
    // The short second line: left, middle, then right.
    const QRect left = lineInk(12 + 48, 12 + 96);
    QVERIFY(left.left() < 30);
    style.alignment = TextAlignment::center;
    const QRect centre = lineInk(12 + 48, 12 + 96);
    QVERIFY2(std::abs(centre.center().x() - 200) <= 3, qPrintable(QString::number(centre.center().x())));
    style.alignment = TextAlignment::right;
    const QRect right = lineInk(12 + 48, 12 + 96);
    QVERIFY(right.right() > 400 - 12 - 12 && right.right() < 400 - 12);
}

void TextLayoutTests::baselinesSitAtTheBottomOfFixedLines()
{
    // A tall line: letters sit low, the space above.
    LayerTextStyle style = styled(QStringLiteral("H"), 20);
    style.leading = 300;
    const QImage image = EditorSession::textImage(style);
    QCOMPARE(image.height(), 324);
    const QRect ink = inked(image);
    const QFontMetricsF metrics(TextLayout::font(style));
    const double baseline = 12 + 300 - metrics.descent();
    QVERIFY2(std::abs(ink.bottom() + 1 - baseline) <= 1.5 && ink.top() > 260,
             qPrintable(QString("%1 %2 %3").arg(ink.top()).arg(ink.bottom()).arg(baseline)));
    // A short line: letters reach into the one before.
    style.content = QStringLiteral("H\nH");
    style.leading = 10;
    const QImage tight = EditorSession::textImage(style);
    QCOMPARE(tight.height(), 44);
    QVERIFY(inked(tight).top() < 12);
}

void TextLayoutTests::imagesKeepSwiftsLimits()
{
    const auto refused = [](const LayerTextStyle &style) -> std::optional<ProjectError::Kind> {
        try {
            EditorSession::textImage(style);
        } catch (const ProjectError &error) {
            return error.kind;
        }
        return std::nullopt;
    };
    QCOMPARE(refused(styled(QStringLiteral("Text"), 0)), std::optional(ProjectError::Kind::invalid));
    // A side past 30,000, then the area past one surface.
    const LayerTextStyle wide = styled(QStringLiteral("M").repeated(20), 2000);
    const QSizeF wideSize = EditorSession::textBoxSize(wide);
    QVERIFY(wideSize.width() > 30'000 && wideSize.width() < 40'000 && wideSize.width() * wideSize.height() < DocumentLimits::maxSurfacePixels);
    QCOMPARE(refused(wide), std::optional(ProjectError::Kind::tooLarge));
    const LayerTextStyle tall = styled(QStringLiteral("a\n").repeated(13), 2000);
    const QSizeF tallSize = EditorSession::textBoxSize(tall);
    QVERIFY(tallSize.height() > 30'000 && tallSize.height() < 40'000 && tallSize.width() * tallSize.height() < DocumentLimits::maxSurfacePixels);
    QCOMPARE(refused(tall), std::optional(ProjectError::Kind::tooLarge));
    const LayerTextStyle large = styled(QStringLiteral("M").repeated(14) + QStringLiteral("\nM").repeated(3), 2000);
    const QSizeF largeSize = EditorSession::textBoxSize(large);
    QVERIFY(largeSize.width() < 30'000 && largeSize.width() * largeSize.height() > DocumentLimits::maxSurfacePixels);
    QCOMPARE(refused(large), std::optional(ProjectError::Kind::tooLarge));
    QVERIFY(!refused(styled(QStringLiteral("M").repeated(10), 2000)));
    // A fractional box rounds up to whole pixels.
    LayerTextStyle fractional = styled(QStringLiteral("Box"), 12);
    fractional.boxSize = QSizeF(100.2, 50.5);
    QCOMPARE(EditorSession::textImage(fractional).size(), QSize(101, 51));
}

void TextLayoutTests::aBoxUnderItsPaddingHoldsAPixel()
{
    // Both boxes leave a one-pixel container, as Swift's max(1, …).
    LayerTextStyle style = styled(QStringLiteral("I"), 10);
    style.leading = 1;
    style.alignment = TextAlignment::right;
    style.boxSize = QSizeF(16, 16);
    const QImage small = EditorSession::textImage(style);
    style.boxSize = QSizeF(25, 25);
    const QImage exact = EditorSession::textImage(style).copy(0, 0, 16, 16);
    QVERIFY(inked(small).isValid());
    QCOMPARE(small, exact);
}

void TextLayoutTests::glyphsTakeTheStyleColour()
{
    LayerTextStyle style = styled(QStringLiteral("H"), 72);
    style.red = 0.2;
    style.green = 0.4;
    style.blue = 0.8;
    const QImage image = EditorSession::textImage(style);
    // A solid pixel of the stem shows the colour itself.
    int solid = 0;
    QCOMPARE(image.format(), QImage::Format_RGBA8888_Premultiplied);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const uchar *at = image.constScanLine(y) + x * 4;
            if (at[3] == 255) {
                ++solid;
                QCOMPARE((std::vector<int>{at[0], at[1], at[2]}), (std::vector<int>{51, 102, 204}));
            }
        }
    }
    QVERIFY(solid > 100);
}

void TextLayoutTests::paragraphsBreakWhereCocoaBreaksThem()
{
    const auto height = [](const QString &text) { return EditorSession::textBoxSize(styled(text)).height(); };
    const double one = height(QStringLiteral("A")), two = height(QStringLiteral("A\nB"));
    QCOMPARE(two, 197.0);
    // CR, CRLF, U+2029, and Qt's own U+2028, each break.
    for (const QString &text : {QStringLiteral("A\rB"), QStringLiteral("A\r\nB"), QStringLiteral("A") + QChar(0x2029) + QStringLiteral("B"),
                                QStringLiteral("A") + QChar(0x2028) + QStringLiteral("B")})
        QCOMPARE(height(text), two);
    // CRLF is one break; a final one adds no line.
    QCOMPARE(height(QStringLiteral("A\r\n")), one);
    QCOMPARE(height(QStringLiteral("A") + QChar(0x2029)), one);
    QCOMPARE(height(QStringLiteral("A\r\n\r\nB")), 284.0);
}

void TextLayoutTests::tabsStopEvery28Points()
{
    const QSizeF room(100'000, 100'000);
    const double letter = TextLines(styled(QStringLiteral("B"), 24), room).usedSize().width();
    const auto width = [&](const QString &text) { return TextLines(styled(text, 24), room).usedSize().width(); };
    // NSParagraphStyle's twelve stops: every 28 pixels to 336.
    QVERIFY(std::abs(width(QStringLiteral("\tB")) - (28 + letter)) < 0.5);
    QVERIFY(std::abs(width(QStringLiteral("\t\tB")) - (56 + letter)) < 0.5);
    QVERIFY(std::abs(width(QStringLiteral("WWW\tB")) - (84 + letter)) < 0.5);
    QVERIFY(std::abs(width(QString(12, QLatin1Char('\t')) + QStringLiteral("B")) - (336 + letter)) < 0.5);
    // Past 336 they go on, where Cocoa wraps (known difference).
    const TextLines thirteen(styled(QString(13, QLatin1Char('\t')) + QStringLiteral("B"), 24), room);
    QVERIFY(std::abs(thirteen.usedSize().width() - (364 + letter)) < 0.5);
    QCOMPARE(thirteen.usedSize().height(), 28.8);
}

void TextLayoutTests::linesStopWhereTheBoxDoes()
{
    // One line of room: "beta" and "more" fall past it.
    const TextLines lines(styled(QStringLiteral("alpha beta\nmore"), 24), QSizeF(90, 40));
    QCOMPARE(lines.lineCount(), 1);
    QVERIFY(lines.overflows());
    QCOMPARE(lines.lineOf(3), std::optional(0));
    QCOMPARE(lines.lineOf(5), std::optional(0));
    // Past the laid text: no line, no caret.
    for (const int position : {6, 8, 10, 11, 15})
        QVERIFY2(!lines.lineOf(position) && !lines.caret(position), qPrintable(QString::number(position)));
    // The laid line still ends before its breaking space.
    QCOMPARE(lines.lineEnd(0), 5);
    // TextKit's line after a final newline holds the caret alone.
    const TextLines ending(styled(QStringLiteral("A\n"), 24), QSizeF(90, 40));
    QCOMPARE(ending.lineCount(), 2);
    QVERIFY(!ending.overflows());
    QCOMPARE(ending.usedSize().height(), 28.8);
    QCOMPARE(ending.lineOf(2), std::optional(1));
    QCOMPARE(ending.caret(2).value().y(), 28.8);
    // So does a final U+2028; one inside makes a line.
    const QString separator(QChar::LineSeparator);
    const TextLines broken(styled(QStringLiteral("A") + separator, 24), QSizeF(90, 40));
    QCOMPARE(broken.lineCount(), 2);
    QCOMPARE(broken.usedSize().height(), 28.8);
    QCOMPARE(broken.lineOf(2), std::optional(1));
    QCOMPARE(broken.lineEnd(0), 1);
    QCOMPARE(TextLines(styled(QStringLiteral("A") + separator + QStringLiteral("\nB"), 24), QSizeF(90, 400)).usedSize().height(), 3 * 28.8);
    for (const QString &last : {QStringLiteral("\r"), QString(QChar::ParagraphSeparator)}) {
        const TextLines closed(styled(QStringLiteral("A") + last, 24), QSizeF(90, 40));
        QCOMPARE(closed.lineCount(), 2);
        QCOMPARE(closed.usedSize().height(), 28.8);
    }
    // Only the text's last break has a caret's line.
    QVERIFY(TextLines(styled(QStringLiteral("A") + separator + QStringLiteral("\n"), 24), QSizeF(90, 40)).overflows());
    const TextLines cut(styled(QStringLiteral("alpha beta") + separator, 24), QSizeF(90, 40));
    QVERIFY(cut.overflows());
    QCOMPARE(cut.lineCount(), 1);
    // Cut short, the text never reaches it.
    QCOMPARE(TextLines(styled(QStringLiteral("alpha beta\n"), 24), QSizeF(90, 40)).usedSize().height(), 28.8);
}

void TextLayoutTests::aSelectionFollowsItsRunsToTheLinesEdge()
{
    const TextLines lines(styled(QStringLiteral("abc אבג xyz\nnext"), 24), QSizeF(400, 100));
    const auto painted = [&](TextRange selection) {
        QImage image(400, 100, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setPen(Qt::NoPen);
        lines.draw(painter, QPointF(0, 0), selection, {}, Qt::red);
        return image;
    };
    // The first five letters: "abc " and aleph, not bet.
    const QImage five = painted({0, 5});
    const int boundary = int(lines.caret(5).value().x()), middle = 14;
    QCOMPARE(five.pixelColor(boundary + 3, middle), QColor(Qt::red));
    QCOMPARE(five.pixelColor(boundary - 3, middle), QColor(Qt::white));
    QCOMPARE(five.pixelColor(5, middle), QColor(Qt::red));
    // A tall line highlights top to bottom, past its letters.
    LayerTextStyle tall = styled(QStringLiteral("abc"), 24);
    tall.leading = 100;
    QImage high(400, 100, QImage::Format_ARGB32_Premultiplied);
    high.fill(Qt::white);
    QPainter onHigh(&high);
    onHigh.setPen(Qt::NoPen);
    TextLines(tall, QSizeF(400, 100)).draw(onHigh, QPointF(0, 0), {0, 3}, {}, Qt::red);
    onHigh.end();
    QCOMPARE(high.pixelColor(5, 2), QColor(Qt::red));
    QCOMPARE(high.pixelColor(5, 97), QColor(Qt::red));
    // Past a line's end it runs to the edge.
    const QImage across = painted({1, 14});
    QCOMPARE(across.pixelColor(395, middle), QColor(Qt::red));
    QCOMPARE(across.pixelColor(1, 28 + middle), QColor(Qt::red));
    QCOMPARE(across.pixelColor(int(lines.caret(14).value().x()) + 4, 28 + middle), QColor(Qt::white));
    QCOMPARE(across.pixelColor(1, middle), QColor(Qt::white));
    // A lone newline does too; a wrap's start does not.
    QCOMPARE(painted({11, 12}).pixelColor(395, middle), QColor(Qt::red));
    LayerTextStyle wrapped = styled(QStringLiteral("alpha beta"), 24);
    const TextLines two(wrapped, QSizeF(90, 100));
    QImage second(90, 100, QImage::Format_ARGB32_Premultiplied);
    second.fill(Qt::white);
    QPainter onSecond(&second);
    onSecond.setPen(Qt::NoPen);
    two.draw(onSecond, QPointF(0, 0), {6, 10}, {}, Qt::red);
    onSecond.end();
    QCOMPARE(second.pixelColor(85, middle), QColor(Qt::white));
    QCOMPARE(second.pixelColor(2, 28 + middle), QColor(Qt::red));
    QCOMPARE(across.pixelColor(395, 28 + middle), QColor(Qt::white));
    // Margins fill where a line reads from, or to.
    const auto aligned = [](const QString &content, TextAlignment alignment) {
        LayerTextStyle style = styled(content, 24);
        style.alignment = alignment;
        return style;
    };
    const auto margins = [&](const LayerTextStyle &style, TextRange selection) {
        QImage image(400, 100, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setPen(Qt::NoPen);
        TextLines(style, QSizeF(400, 100)).draw(painter, QPointF(0, 0), selection, {}, Qt::red);
        return image;
    };
    const LayerTextStyle centred = aligned(QStringLiteral("ab\ncd"), TextAlignment::center);
    const TextLines centredLines(centred, QSizeF(400, 100));
    QCOMPARE(margins(centred, {1, 5}).pixelColor(5, 28 + middle), QColor(Qt::red));
    QCOMPARE(margins(centred, {1, 5}).pixelColor(5, middle), QColor(Qt::white));
    QCOMPARE(margins(centred, {0, 3}).pixelColor(5, 28 + middle), QColor(Qt::white));
    const int d = int((centredLines.caret(4).value().x() + centredLines.caret(5).value().x()) / 2);
    QCOMPARE(margins(centred, {1, 4}).pixelColor(d, 28 + middle), QColor(Qt::white));
    QCOMPARE(margins(aligned(QStringLiteral("אב\nגד"), TextAlignment::left), {1, 5}).pixelColor(395, 28 + middle), QColor(Qt::red));
    const QImage right = margins(aligned(QStringLiteral("אב\nגד"), TextAlignment::right), {1, 5});
    QCOMPARE(right.pixelColor(5, middle), QColor(Qt::red));
    QCOMPARE(right.pixelColor(395, middle), QColor(Qt::white));
}

QTEST_MAIN(TextLayoutTests)
#include "TextLayoutTests.moc"
