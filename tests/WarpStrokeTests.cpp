#include "BrushFixtures.h"
#include "Document/SmudgeLiquify.h"
#include <cmath>

// Swift's MetalWarpTests: Liquify stays sharp, Smudge leaves no ghosts.
namespace {
WarpStroke warp(const QImage &image, BlurToolMode mode, const BrushSettings &settings)
{
    const ImageLayer layer(ImportedImage(image, image, QStringLiteral("Picture")), QPointF(0, 0));
    return WarpStroke(layer, image, layer.transform, QSizeF(image.size()), mode, settings);
}
}

class WarpStrokeTests : public QObject {
    Q_OBJECT
private slots:
    void liquifyStaysSharp();
    void liquifyDrawsFromTheUntouchedLayer();
    void smudgeLeavesOneFadingTrail();
};

// Pushed across and back, the layer ends nearly unchanged.
void WarpStrokeTests::liquifyStaysSharp()
{
    // Swift's photo: two ramps, a checkerboard, a half-clear strip.
    QImage image = BrushRaster::context(300, 200, false);
    for (int y = 0; y < 200; ++y) {
        uchar *row = image.scanLine(y);
        for (int x = 0; x < 300; ++x) {
            const int a = x < 250 ? 255 : 128;
            const int check = (x / 12 + y / 12) % 2 == 0 ? 220 : 40;
            row[x * 4] = uchar(x * 255 / 300 * a / 255);
            row[x * 4 + 1] = uchar(y * 255 / 200 * a / 255);
            row[x * 4 + 2] = uchar(check * a / 255);
            row[x * 4 + 3] = uchar(a);
        }
    }
    WarpStroke stroke = warp(image, BlurToolMode::liquify, brush(60, 0.3, 0, 0, 0, 0.7));
    for (double x = 60; x <= 200; x += 4)
        stroke.append(QPointF(x, 100));
    for (double x = 200; x >= 60; x -= 4)
        stroke.append(QPointF(x, 100));
    double change = 0;
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 300 * 4; ++x)
            change += std::abs(int(stroke.image().constScanLine(y)[x]) - int(image.constScanLine(y)[x]));
    change /= 300.0 * 200 * 4;
    // Swift measured 0.49 sampled once, 2.34 resampled per dab.
    QVERIFY2(change < 1, qPrintable(QString::number(change)));
    QVERIFY(change > 0);
}

// Half-pixel pushes add up to one: stripes stay crisp.
void WarpStrokeTests::liquifyDrawsFromTheUntouchedLayer()
{
    QImage image = BrushRaster::context(60, 20, false);
    for (int x = 0; x < 60; ++x)
        for (int y = 0; y < 20; ++y)
            image.setPixel(x, y, qRgba(x % 2 ? 200 : 0, 0, 0, 255));
    WarpStroke stroke = warp(image, BlurToolMode::liquify, brush(10, 0.98, 0, 0, 0, 0.5));
    stroke.append(QPointF(20, 10));
    stroke.append(QPointF(21, 10));
    stroke.append(QPointF(22, 10));
    // Resampled per dab these read 100; now each, one stripe.
    QCOMPARE(pixel(stroke.image(), 22, 10), (std::vector<int>{200, 0, 0, 255}));
    QCOMPARE(pixel(stroke.image(), 21, 10), (std::vector<int>{0, 0, 0, 255}));
    QCOMPARE(pixel(stroke.image(), 23, 10), (std::vector<int>{0, 0, 0, 255}));
    QCOMPARE(pixel(stroke.image(), 40, 10), (std::vector<int>{0, 0, 0, 255}));
    QCOMPARE(pixel(stroke.image(), 41, 10), (std::vector<int>{200, 0, 0, 255}));
}

void WarpStrokeTests::smudgeLeavesOneFadingTrail()
{
    QImage image = BrushRaster::context(300, 100, false);
    image.fill(QColor::fromRgbF(0.1, 0.1, 0.1));
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        painter.drawEllipse(QRectF(52, 42, 16, 16));
    }
    WarpStroke stroke = warp(image, BlurToolMode::smudge, brush(40, 0.5, 0, 0, 0, 0.6));
    for (double x = 60; x <= 240; x += 3)
        stroke.append(QPointF(x, 50));
    std::vector<int> row;
    for (int x = 70; x < 240; ++x)
        row.push_back(stroke.image().constScanLine(50)[x * 4]);
    // A ghost is a bump: brighter than two either side.
    int peaks = 0;
    for (size_t i = 2; i + 2 < row.size(); ++i)
        peaks += row[i] > row[i - 2] + 2 && row[i] > row[i + 2] + 2;
    QCOMPARE(peaks, 0);
    QVERIFY2(row.front() > row.back() + 20, qPrintable(QString("%1 to %2").arg(row.front()).arg(row.back())));
}

QTEST_GUILESS_MAIN(WarpStrokeTests)
#include "WarpStrokeTests.moc"
