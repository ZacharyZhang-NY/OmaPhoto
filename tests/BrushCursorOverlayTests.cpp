#include "Rendering/BrushCursorOverlay.h"
#include <QImage>
#include <QtTest>
#include <cmath>
#include <numbers>

// Swift's brush circle: what it repaints, and how it looks.
namespace {
const QColor ground(40, 60, 160);
// Eight pixels a point: the pens and dashes grow alike.
constexpr int scale = 8;

// The overlay over the blue ground, eight pixels a point.
QImage drawn(const BrushCursorOverlay &overlay)
{
    QImage image(100 * scale, 100 * scale, QImage::Format_RGB32);
    image.fill(ground);
    QPainter painter(&image);
    painter.scale(scale, scale);
    overlay.draw(painter);
    painter.end();
    return image;
}

QColor point(const QImage &image, double x, double y)
{
    return image.pixelColor(int(x * scale), int(y * scale));
}

// A solid green preview; a tip opaque within ten points.
QImage green()
{
    QImage image(40, 40, QImage::Format_RGBA8888_Premultiplied);
    image.fill(QColor(0, 255, 0));
    return image;
}

QImage tip()
{
    QImage image(40, 40, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setBrush(Qt::white);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPointF(20, 20), 10, 10);
    return image;
}

// A 60-point circle at the middle of a blue square.
QImage drawn(std::optional<double> hardness)
{
    BrushCursorOverlay overlay;
    overlay.update(QPointF(50, 50), 60, hardness);
    QImage image(100 * scale, 100 * scale, QImage::Format_RGB32);
    image.fill(ground);
    QPainter painter(&image);
    painter.scale(scale, scale);
    overlay.draw(painter);
    painter.end();
    return image;
}

bool white(QColor colour)
{
    return colour.red() > 200 && colour.green() > 200 && colour.blue() > 200;
}

bool black(QColor colour)
{
    return colour.red() < 40 && colour.green() < 40 && colour.blue() < 40;
}

// The pixel a given number of points from the middle.
QColor at(const QImage &image, double radius, double angle)
{
    return image.pixelColor(int(50 * scale + radius * scale * std::cos(angle)), int(50 * scale + radius * scale * std::sin(angle)));
}

// How many of 720 angles pass `test` at a radius.
int around(const QImage &image, double radius, const std::function<bool(QColor)> &test)
{
    int found = 0;
    for (int step = 0; step < 720; ++step)
        found += test(at(image, radius, step * std::numbers::pi / 360));
    return found;
}
}

class BrushCursorOverlayTests : public QObject {
    Q_OBJECT
private slots:
    void updatesRepaintTheOldAndNewRings();
    void theRingIsBlackOverWhiteAndAntialiased();
    void theHardnessRingDashesInStepInsideTheCircle();
    void theSourceMarkerRepaintsAroundItselfWhiteUnderBlack();
    void thePreviewFillsTheCircleCutToTheTipAtItsOpacity();
    void imagesCompareByIdentity();
    void theTipCutsAtTheDrawingsResolution();
};

void BrushCursorOverlayTests::updatesRepaintTheOldAndNewRings()
{
    BrushCursorOverlay overlay;
    // Three points round the circle hold the white stroke.
    QCOMPARE(overlay.update(QPointF(50, 50), 20, std::nullopt), QRegion(QRect(37, 37, 26, 26)));
    QCOMPARE(overlay.update(QPointF(50, 50), 20, std::nullopt), QRegion());
    // Hardness alone repaints the same circle.
    QCOMPARE(overlay.update(QPointF(50, 50), 20, 0.5), QRegion(QRect(37, 37, 26, 26)));
    QCOMPARE(overlay.hardness(), std::optional(0.5));
    // A move repaints where it was and where it went.
    QCOMPARE(overlay.update(QPointF(90, 50), 20, 0.5), QRegion(QRect(37, 37, 26, 26)) + QRegion(QRect(77, 37, 26, 26)));
    QCOMPARE(overlay.update(std::nullopt, 20, std::nullopt), QRegion(QRect(77, 37, 26, 26)));
    QVERIFY(!overlay.circle().has_value());
}

void BrushCursorOverlayTests::theRingIsBlackOverWhiteAndAntialiased()
{
    const QImage image = drawn(std::nullopt);
    // Black a point wide on the circle, white round it.
    QCOMPARE(around(image, 30, black), 720);
    QCOMPARE(around(image, 30.9, white), 720);
    QCOMPARE(around(image, 29.1, white), 720);
    // Nothing fills it, and nothing lies past the white.
    for (const double radius : {0.0, 10.0, 28.5, 31.5, 40.0})
        QCOMPARE(around(image, radius, [](QColor colour) { return colour == ground; }), 720);
    // The white's outer edge blends into the ground.
    QVERIFY(around(image, 31.25, [](QColor colour) { return colour != ground && !white(colour); }) > 300);
}

void BrushCursorOverlayTests::theHardnessRingDashesInStepInsideTheCircle()
{
    // A quarter hard: the ring at three eighths across.
    const QImage image = drawn(0.25);
    QCOMPARE(around(image, 22.5, [](QColor colour) { return colour == ground; }), 720);
    const int dashes = around(image, 7.5, black);
    // Four points on, three off: black and white halo together.
    int apart = 0, turns = 0;
    for (int step = 0; step < 720; ++step) {
        const double angle = step * std::numbers::pi / 360, next = (step + 1) * std::numbers::pi / 360;
        apart += black(at(image, 7.5, angle)) != white(at(image, 8.4, angle));
        turns += white(at(image, 8.4, angle)) != white(at(image, 8.4, next));
    }
    QVERIFY2(dashes > 300 && dashes < 520 && apart < 30 && turns >= 12, qPrintable(QString("%1 %2 %3").arg(dashes).arg(apart).arg(turns)));
    // At no hardness there is no second ring.
    const QImage soft = drawn(0.0);
    for (const double radius : {0.0, 7.5, 15.0, 28.5})
        QCOMPARE(around(soft, radius, [](QColor colour) { return colour == ground; }), 720);
}

void BrushCursorOverlayTests::theSourceMarkerRepaintsAroundItselfWhiteUnderBlack()
{
    BrushCursorOverlay overlay;
    // Seven-point arms, three points of white round them.
    QCOMPARE(overlay.update(std::nullopt, 20, std::nullopt, QPointF(30, 40)), QRegion(QRect(20, 30, 20, 20)));
    QCOMPARE(overlay.update(std::nullopt, 20, std::nullopt, QPointF(30, 40)), QRegion());
    QCOMPARE(overlay.update(std::nullopt, 20, std::nullopt, QPointF(60, 40)), QRegion(QRect(20, 30, 20, 20)) + QRegion(QRect(50, 30, 20, 20)));
    QCOMPARE(overlay.marker(), std::optional(QPointF(60, 40)));
    // The marker draws without a circle.
    const QImage image = drawn(overlay);
    QVERIFY(black(point(image, 60, 40)) && black(point(image, 66, 40.1)) && black(point(image, 60.1, 34)));
    QVERIFY(white(point(image, 63, 41)) && white(point(image, 67.9, 40.1)) && white(point(image, 59, 46)));
    QCOMPARE(point(image, 63, 42), ground);
    QCOMPARE(point(image, 69, 40.1), ground);
    QCOMPARE(point(image, 63, 43), ground);
    QCOMPARE(overlay.update(std::nullopt, 20, std::nullopt, std::nullopt), QRegion(QRect(50, 30, 20, 20)));
}

void BrushCursorOverlayTests::thePreviewFillsTheCircleCutToTheTipAtItsOpacity()
{
    BrushCursorOverlay overlay;
    overlay.update(QPointF(50, 50), 40, std::nullopt, std::nullopt, green(), 0.5, tip());
    const QImage cut = drawn(overlay);
    // Half green over the ground, only where a click paints.
    const QColor middle = point(cut, 50, 50);
    QVERIFY2(std::abs(middle.red() - 20) <= 2 && std::abs(middle.green() - 158) <= 2 && std::abs(middle.blue() - 80) <= 2,
             qPrintable(middle.name()));
    QCOMPARE(point(cut, 65, 50), ground);
    QCOMPARE(point(cut, 50, 38), ground);
    // A new tip over the same preview cuts it anew.
    QImage small = tip();
    small.fill(Qt::transparent);
    QPainter(&small).fillRect(QRect(18, 18, 4, 4), Qt::white);
    overlay.update(QPointF(50, 50), 40, std::nullopt, std::nullopt, overlay.preview(), 0.5, small);
    QCOMPARE(point(drawn(overlay), 50, 45), ground);
    // Without a tip the whole circle shows it, nothing past.
    overlay.update(QPointF(50, 50), 40, std::nullopt, std::nullopt, green(), 1, QImage());
    const QImage whole = drawn(overlay);
    QCOMPARE(point(whole, 65, 50), QColor(0, 255, 0));
    QCOMPARE(point(whole, 50, 32), QColor(0, 255, 0));
    QCOMPARE(point(whole, 50, 27), ground);
    QCOMPARE(point(whole, 66, 34), ground);
}

void BrushCursorOverlayTests::imagesCompareByIdentity()
{
    BrushCursorOverlay overlay;
    const QImage preview = green(), cut = tip();
    QCOMPARE(overlay.update(QPointF(50, 50), 20, std::nullopt, std::nullopt, preview, 1, cut), QRegion(QRect(37, 37, 26, 26)));
    QCOMPARE(overlay.update(QPointF(50, 50), 20, std::nullopt, std::nullopt, preview, 1, cut), QRegion());
    // Equal pixels in another image differ, as Swift's `!==` says.
    QCOMPARE(overlay.update(QPointF(50, 50), 20, std::nullopt, std::nullopt, preview.copy(), 1, cut), QRegion(QRect(37, 37, 26, 26)));
    QCOMPARE(overlay.update(QPointF(50, 50), 20, std::nullopt, std::nullopt, overlay.preview(), 1, cut.copy()), QRegion(QRect(37, 37, 26, 26)));
    QCOMPARE(overlay.update(QPointF(50, 50), 20, std::nullopt, std::nullopt, overlay.preview(), 0.4, overlay.preview()), QRegion(QRect(37, 37, 26, 26)));
    QCOMPARE(overlay.update(QPointF(50, 50), 20, std::nullopt, std::nullopt, overlay.preview(), 0.6, overlay.preview()), QRegion(QRect(37, 37, 26, 26)));
}

void BrushCursorOverlayTests::theTipCutsAtTheDrawingsResolution()
{
    // A capped preview enlarged fourfold; a tip of one-pixel columns.
    QImage preview(100, 100, QImage::Format_RGBA8888_Premultiplied);
    preview.fill(QColor(0, 255, 0));
    QImage columns(400, 400, QImage::Format_RGBA8888_Premultiplied);
    columns.fill(Qt::transparent);
    QPainter stripes(&columns);
    for (int x = 0; x < 400; x += 2)
        stripes.fillRect(QRect(x, 0, 1, 400), Qt::white);
    stripes.end();
    const auto draw = [](const BrushCursorOverlay &overlay) {
        QImage image(500, 500, QImage::Format_RGB32);
        image.fill(ground);
        QPainter painter(&image);
        overlay.draw(painter);
        return image;
    };
    BrushCursorOverlay overlay;
    overlay.update(QPointF(250, 250), 400, std::nullopt, std::nullopt, preview, 1, columns);
    const QImage cut = draw(overlay);
    // Each column keeps or drops the preview, as clicks would.
    QCOMPARE(cut.pixelColor(250, 250), QColor(0, 255, 0));
    QCOMPARE(cut.pixelColor(251, 250), ground);
    QCOMPARE(cut.pixelColor(120, 300), QColor(0, 255, 0));
    QCOMPARE(cut.pixelColor(121, 300), ground);
    // The enlarged preview is smoothed, as Swift's medium quality.
    QImage halves = preview.copy();
    QPainter(&halves).fillRect(QRect(0, 0, 50, 100), QColor(255, 0, 0));
    overlay.update(QPointF(250, 250), 400, std::nullopt, std::nullopt, halves, 1, QImage());
    const QColor seam = draw(overlay).pixelColor(250, 250);
    QVERIFY2(seam.red() > 40 && seam.green() > 40, qPrintable(seam.name()));
}

QTEST_GUILESS_MAIN(BrushCursorOverlayTests)
#include "BrushCursorOverlayTests.moc"
