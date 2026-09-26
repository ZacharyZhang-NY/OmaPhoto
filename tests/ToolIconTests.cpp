#include "UI/ToolIcons.h"
#include <QtTest>

// Every tool's icon: drawn, tinted, its own.
namespace {
const NavigationTool tools[] = {NavigationTool::move, NavigationTool::marquee, NavigationTool::lasso, NavigationTool::wand,
                                NavigationTool::crop, NavigationTool::brush, NavigationTool::spotHealing, NavigationTool::cloneStamp,
                                NavigationTool::blur, NavigationTool::gradient, NavigationTool::shape, NavigationTool::type,
                                NavigationTool::eyedropper, NavigationTool::hand, NavigationTool::zoom};

QImage drawn(NavigationTool tool, const QColor &colour, int side = 36, QPointF origin = {0, 0}, ToolIconKind kind = ToolIconKind::plain)
{
    QImage image(side + 8, side + 8, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    ToolIcons::paint(painter, tool, origin, side, colour, kind);
    return image;
}

int inked(const QImage &image)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x)
            count += qAlpha(image.pixel(x, y)) > 0;
    }
    return count;
}

// The smallest rectangle around every pixel well inked.
QRect extent(const QImage &image)
{
    QRect result;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) > 127)
                result |= QRect(x, y, 1, 1);
        }
    }
    return result;
}
}

class ToolIconTests : public QObject {
    Q_OBJECT
private slots:
    void everyToolHasItsOwnIcon();
    void anIconTakesTheColourItIsGivenAndNoOther();
    void anIconStaysInsideItsBoxAtAnySizeAndPlace();
    void thePainterComesBackAsItWent();
    void swiftsTwoDrawnIconsKeepTheirGeometry();
    void noToolHasNoIcon();
    void anIconKeepsToTheCallersClip();
    void nothingOfTheCallersPainterShowsInAnIcon();
    void theFadeHasNoSeamsAtAFractionalScale();
    void theDrawingsMatchTheirReference();
};

void ToolIconTests::everyToolHasItsOwnIcon()
{
    QList<QImage> icons;
    for (const NavigationTool tool : tools) {
        const QImage icon = drawn(tool, Qt::white);
        // An icon is a drawing: no smudge, no slab.
        QVERIFY2(inked(icon) > 60 && inked(icon) < 36 * 36 * 9 / 10, qPrintable(QString::number(inked(icon))));
        QVERIFY(!icons.contains(icon));
        icons << icon;
    }
    QCOMPARE(icons.size(), 15);
    // The Marquee's, the Lasso's and the brush's kinds are icons.
    for (const auto &[tool, kind] : {std::pair(NavigationTool::marquee, ToolIconKind::ellipse), std::pair(NavigationTool::lasso, ToolIconKind::polygonal),
                                     std::pair(NavigationTool::brush, ToolIconKind::eraser)}) {
        const QImage icon = drawn(tool, Qt::white, 36, {0, 0}, kind);
        QVERIFY(inked(icon) > 60 && !icons.contains(icon));
        icons << icon;
    }
    // A kind another tool lacks leaves its icon alone.
    QCOMPARE(drawn(NavigationTool::brush, Qt::white, 36, {0, 0}, ToolIconKind::ellipse), drawn(NavigationTool::brush, Qt::white));
    QCOMPARE(drawn(NavigationTool::move, Qt::white, 36, {0, 0}, ToolIconKind::eraser), drawn(NavigationTool::move, Qt::white));
}

void ToolIconTests::anIconTakesTheColourItIsGivenAndNoOther()
{
    const QColor colour(200, 100, 50);
    for (const NavigationTool tool : tools) {
        const QImage icon = drawn(tool, colour);
        bool solid = false;
        for (int y = 0; y < icon.height(); ++y) {
            for (int x = 0; x < icon.width(); ++x) {
                const QColor pixel = icon.pixelColor(x, y);
                if (pixel.alpha() < 32)
                    continue;
                // Antialiased edges keep the hue; only alpha falls.
                QVERIFY2(std::abs(pixel.red() - 200) <= 6 && std::abs(pixel.green() - 100) <= 6 && std::abs(pixel.blue() - 50) <= 6,
                         qPrintable(QString("%1: %2").arg(int(tool)).arg(pixel.name(QColor::HexArgb))));
                solid = solid || pixel.alpha() == 255;
            }
        }
        QVERIFY(solid);
    }
}

void ToolIconTests::anIconStaysInsideItsBoxAtAnySizeAndPlace()
{
    for (const NavigationTool tool : tools) {
        for (const int side : {18, 36, 54}) {
            const QImage icon = drawn(tool, Qt::white, side, QPointF(4, 4));
            for (int y = 0; y < icon.height(); ++y) {
                for (int x = 0; x < icon.width(); ++x) {
                    const bool inside = x >= 3 && y >= 3 && x <= side + 4 && y <= side + 4;
                    QVERIFY2(inside || qAlpha(icon.pixel(x, y)) == 0, qPrintable(QString("%1 %2,%3 at %4").arg(int(tool)).arg(x).arg(y).arg(side)));
                }
            }
            // The drawing grows with its box, from the box's corner.
            const QRect small = extent(drawn(tool, Qt::white, 18, QPointF(4, 4))), grown = extent(icon);
            const double scale = side / 18.0;
            const auto near = [&](int actual, double expected) { return std::abs(actual - expected) <= scale + 1; };
            QVERIFY2(near(grown.left() - 4, (small.left() - 4) * scale) && near(grown.top() - 4, (small.top() - 4) * scale)
                         && near(grown.width(), small.width() * scale) && near(grown.height(), small.height() * scale),
                     qPrintable(QString("%1 at %2").arg(int(tool)).arg(side)));
        }
    }
}

void ToolIconTests::thePainterComesBackAsItWent()
{
    QImage image(40, 40, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setPen(QPen(Qt::red, 3));
    painter.setBrush(Qt::blue);
    painter.translate(2, 2);
    const QTransform before = painter.transform();
    const QFont font = painter.font();
    // An icon that restores more than it saved warns here.
    QTest::failOnWarning(QRegularExpression(".*"));
    for (const NavigationTool tool : tools) {
        ToolIcons::paint(painter, tool, QPointF(0, 0), 36, Qt::white);
        QCOMPARE(painter.transform(), before);
        QCOMPARE(painter.pen(), QPen(Qt::red, 3));
        QCOMPARE(painter.brush(), QBrush(Qt::blue));
        QCOMPARE(painter.font(), font);
        QVERIFY(!painter.testRenderHint(QPainter::Antialiasing) && !painter.hasClipping());
    }
    painter.end();
    // The caller's blue brush and red pen stayed out.
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            QVERIFY(pixel.alpha() < 32 || (pixel.red() > 200 && pixel.green() > 200 && pixel.blue() > 200));
        }
    }
}

void ToolIconTests::swiftsTwoDrawnIconsKeepTheirGeometry()
{
    // The stamp, a pixel a point: pad, body, neck, handle.
    const QImage stamp = drawn(NavigationTool::cloneStamp, Qt::white, 18);
    QCOMPARE(qAlpha(stamp.pixel(9, 15)), 255);
    QCOMPARE(qAlpha(stamp.pixel(3, 15)), 255);
    QCOMPARE(qAlpha(stamp.pixel(9, 12)), 255);
    QCOMPARE(qAlpha(stamp.pixel(9, 7)), 255);
    QCOMPARE(qAlpha(stamp.pixel(9, 3)), 255);
    QCOMPARE(qAlpha(stamp.pixel(4, 7)), 0);
    QCOMPARE(qAlpha(stamp.pixel(3, 3)), 0);
    // The fade: empty at the left, solid at the right.
    const QImage fade = drawn(NavigationTool::gradient, Qt::white, 18);
    int left = 0, right = 0;
    for (int y = 4; y < 14; ++y) {
        for (int x = 3; x < 6; ++x)
            left += qAlpha(fade.pixel(x, y)) > 127;
        for (int x = 12; x < 15; ++x)
            right += qAlpha(fade.pixel(x, y)) > 127;
    }
    QVERIFY2(left <= 6 && right >= 24, qPrintable(QString("%1 %2").arg(left).arg(right)));
    // Floyd and Steinberg's dots, as Swift's weights place them.
    const QStringList rows{"...#.#.#####", ".#..#.#.#.#.", "..#..#.#####", "...#.#.#.#.#"};
    for (int row = 0; row < 4; ++row) {
        QString dots;
        for (int x = 3; x < 15; ++x)
            dots += qAlpha(fade.pixel(x, 5 + row)) > 127 ? '#' : '.';
        QCOMPARE(dots, rows[row]);
    }
    // Dots stop at the rounded frame, solid side included.
    QVERIFY2(qAlpha(fade.pixel(16, 1)) < 120, qPrintable(QString::number(qAlpha(fade.pixel(16, 1)))));
    // Swift's frame is 1.4 wide, clipped to its inner half.
    QVERIFY2(std::abs(qAlpha(fade.pixel(1, 9)) - 179) <= 6, qPrintable(QString::number(qAlpha(fade.pixel(1, 9)))));
    for (int along = 4; along < 14; ++along) {
        QCOMPARE(qAlpha(fade.pixel(0, along)), 0);
        QCOMPARE(qAlpha(fade.pixel(17, along)), 0);
        QCOMPARE(qAlpha(fade.pixel(along, 0)), 0);
        QCOMPARE(qAlpha(fade.pixel(along, 17)), 0);
    }
    // A rounded frame: a clear corner, a drawn edge.
    QVERIFY2(qAlpha(fade.pixel(1, 1)) < 120 && qAlpha(fade.pixel(1, 9)) > 150 && qAlpha(fade.pixel(9, 1)) > 150,
             qPrintable(QString("%1 %2 %3").arg(qAlpha(fade.pixel(1, 1))).arg(qAlpha(fade.pixel(1, 9))).arg(qAlpha(fade.pixel(9, 1)))));
    // The stamp's parts overlap and still fill as one shape.
    QVERIFY(qAlpha(stamp.pixel(9, 5)) > 200);
    QCOMPARE(qAlpha(stamp.pixel(9, 9)), 255);
    // Curves are smoothed: a circle's edge has soft pixels.
    const QImage lens = drawn(NavigationTool::zoom, Qt::white, 18);
    int soft = 0;
    for (int y = 0; y < lens.height(); ++y) {
        for (int x = 0; x < lens.width(); ++x)
            soft += qAlpha(lens.pixel(x, y)) > 40 && qAlpha(lens.pixel(x, y)) < 215;
    }
    QVERIFY2(soft > 20, qPrintable(QString::number(soft)));
}

void ToolIconTests::noToolHasNoIcon()
{
    QImage image(20, 20, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    // A painter left saved warns when it ends.
    QTest::failOnWarning(QRegularExpression(".*"));
    {
        QPainter painter(&image);
        painter.setPen(QPen(Qt::red, 3));
        painter.setBrush(Qt::blue);
        painter.translate(2, 2);
        const QTransform before = painter.transform();
        QVERIFY_EXCEPTION_THROWN(ToolIcons::paint(painter, NavigationTool::idle, QPointF(0, 0), 18, Qt::white), std::logic_error);
        // The refusal left the painter as it was.
        QCOMPARE(painter.transform(), before);
        QCOMPARE(painter.pen(), QPen(Qt::red, 3));
        QCOMPARE(painter.brush(), QBrush(Qt::blue));
        QVERIFY(!painter.testRenderHint(QPainter::Antialiasing));
    }
    QCOMPARE(inked(image), 0);
}

void ToolIconTests::nothingOfTheCallersPainterShowsInAnIcon()
{
    QFont odd;
    odd.setUnderline(true);
    odd.setItalic(true);
    odd.setStrikeOut(true);
    odd.setLetterSpacing(QFont::AbsoluteSpacing, 6);
    for (const NavigationTool tool : tools) {
        QImage hostile(44, 44, QImage::Format_ARGB32_Premultiplied);
        hostile.fill(Qt::transparent);
        QPainter painter(&hostile);
        painter.setPen(QPen(Qt::red, 5, Qt::DotLine));
        painter.setBrush(Qt::blue);
        painter.setBackground(Qt::red);
        painter.setBackgroundMode(Qt::OpaqueMode);
        painter.setFont(odd);
        painter.setBrushOrigin(3, 3);
        ToolIcons::paint(painter, tool, QPointF(0, 0), 36, Qt::white);
        // And the caller gets all of it back.
        QCOMPARE(painter.backgroundMode(), Qt::OpaqueMode);
        QCOMPARE(painter.background(), QBrush(Qt::red));
        QCOMPARE(painter.font(), odd);
        painter.end();
        QVERIFY2(hostile == drawn(tool, Qt::white), qPrintable(QString::number(int(tool))));
    }
}

void ToolIconTests::theFadeHasNoSeamsAtAFractionalScale()
{
    // Which dots are on, read at one pixel a dot.
    const QImage plain = drawn(NavigationTool::gradient, Qt::white, 18);
    const auto on = [&](double x, double y) {
        const int column = int(std::floor(x)), row = int(std::floor(y));
        return column >= 3 && column <= 14 && row >= 3 && row <= 14 && qAlpha(plain.pixel(column, row)) > 127;
    };
    // A pixel wholly inside dots that are on is solid.
    const double scale = 22.5 / 18;
    const QImage odd = [&] {
        QImage image(36, 36, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        ToolIcons::paint(painter, NavigationTool::gradient, QPointF(4, 4), 22.5, Qt::white);
        return image;
    }();
    int solid = 0;
    for (int y = 0; y < odd.height(); ++y) {
        for (int x = 0; x < odd.width(); ++x) {
            const double left = (x - 4) / scale, right = (x - 3) / scale, top = (y - 4) / scale, bottom = (y - 3) / scale;
            const double inset = 0.001;
            if (!on(left + inset, top + inset) || !on(right - inset, top + inset) || !on(left + inset, bottom - inset) || !on(right - inset, bottom - inset))
                continue;
            solid += 1;
            QVERIFY2(qAlpha(odd.pixel(x, y)) == 255, qPrintable(QString("%1,%2: %3").arg(x).arg(y).arg(qAlpha(odd.pixel(x, y)))));
        }
    }
    // The reviewer's pixel among them: once 173, a seam.
    QCOMPARE(qAlpha(odd.pixel(21, 16)), 255);
    QVERIFY2(solid > 20, qPrintable(QString::number(solid)));
}

void ToolIconTests::anIconKeepsToTheCallersClip()
{
    // A widget repaints a strip: nothing may land beside it.
    for (const NavigationTool tool : tools) {
        QImage image(18, 18, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setClipRect(QRect(0, 0, 8, 18));
        ToolIcons::paint(painter, tool, QPointF(0, 0), 18, Qt::white);
        QVERIFY(painter.hasClipping());
        QCOMPARE(painter.clipBoundingRect(), QRectF(0, 0, 8, 18));
        painter.end();
        for (int y = 0; y < 18; ++y) {
            for (int x = 8; x < 18; ++x)
                QVERIFY2(qAlpha(image.pixel(x, y)) == 0, qPrintable(QString("%1 at %2,%3").arg(int(tool)).arg(x).arg(y)));
        }
        QVERIFY(inked(image) > 0);
    }
}

void ToolIconTests::theDrawingsMatchTheirReference()
{
    // Every stroke is pinned by the picture in the fixtures.
    QImage sheet(18 * 44 + 8, 2 * 52, QImage::Format_ARGB32_Premultiplied);
    sheet.fill(QColor(36, 36, 36));
    QPainter painter(&sheet);
    for (int index = 0; index < 15; ++index) {
        ToolIcons::paint(painter, tools[index], QPointF(8 + index * 44, 8), 36, QColor(235, 235, 235));
        ToolIcons::paint(painter, tools[index], QPointF(17 + index * 44, 66), 18, QColor(235, 235, 235));
    }
    // Three more: the ellipse Marquee, polygonal Lasso, the eraser.
    for (const auto &[index, tool, kind] : {std::tuple(15, NavigationTool::marquee, ToolIconKind::ellipse), std::tuple(16, NavigationTool::lasso, ToolIconKind::polygonal),
                                            std::tuple(17, NavigationTool::brush, ToolIconKind::eraser)}) {
        ToolIcons::paint(painter, tool, QPointF(8 + index * 44, 8), 36, QColor(235, 235, 235), kind);
        ToolIcons::paint(painter, tool, QPointF(17 + index * 44, 66), 18, QColor(235, 235, 235), kind);
    }
    painter.end();
    // After a wanted change, look at this file; commit it.
    QVERIFY(sheet.save(QStringLiteral("ToolIcons.png")));
    const QImage reference = QImage(QFINDTESTDATA("fixtures/ToolIcons.png")).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QCOMPARE(reference.size(), sheet.size());
    // Type's letters take the system's sans; the sheet's is DejaVu.
    const bool sheetFace = QFontInfo(QFont()).family() == QStringLiteral("DejaVu Sans");
    int differing = 0;
    for (int y = 0; y < sheet.height(); ++y) {
        for (int x = 0; x < sheet.width(); ++x) {
            if (!sheetFace && x >= 4 + 11 * 44 && x < 48 + 11 * 44)
                continue;
            differing += std::abs(qRed(sheet.pixel(x, y)) - qRed(reference.pixel(x, y))) > 48;
        }
    }
    // A font may differ by some pixels; no stroke may.
    QVERIFY2(differing <= 12, qPrintable(QString::number(differing)));
}

QTEST_MAIN(ToolIconTests)
#include "ToolIconTests.moc"
