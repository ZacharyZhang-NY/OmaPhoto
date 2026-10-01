#include "BrushFixtures.h"
#include "Document/EditorSession.h"
#include "Document/PixelAdjust.h"
#include "IO/ImageExporter.h"
#include "SessionFixtures.h"
#include <cmath>

// Swift's BlurTool and Core Image's Gaussian, twinned.
namespace {
// Normalised bell weights, three sigmas each way.
std::vector<double> bell(double sigma)
{
    const int reach = int(std::ceil(3 * sigma));
    std::vector<double> weights;
    double total = 0;
    for (int i = 0; i <= reach; ++i) {
        weights.push_back(std::exp(-double(i * i) / (2 * sigma * sigma)));
        total += i == 0 ? weights.back() : 2 * weights.back();
    }
    for (double &weight : weights)
        weight /= total;
    return weights;
}

std::vector<int> exported(const EditorSession &session, int x, int y)
{
    return pixel(ImageExporter::render(session.projectSnapshot().value()).image, x, y);
}

// Red left of x 30, blue right: a hard edge.
QImage edge(int width, int height)
{
    QImage image = BrushRaster::context(width, height, false);
    QPainter painter(&image);
    painter.fillRect(QRect(0, 0, 30, height), QColor(255, 0, 0));
    painter.fillRect(QRect(30, 0, width - 30, height), QColor(0, 0, 255));
    return image;
}

void blurring(EditorSession &session, const BrushSettings &settings)
{
    session.createDocument(60, 40);
    const QImage image = edge(60, 40);
    session.insert(ImportedImage(image, image, QStringLiteral("Edge")));
    session.selectTool(NavigationTool::blur);
    session.setBlurMode(BlurToolMode::blur);
    session.setBrushSettings(settings);
}
}

class BlurToolTests : public QObject {
    Q_OBJECT
private slots:
    void aPointSpreadsAlongTheBellCurve();
    void clearEdgesFadeAndClampedEdgesHold();
    void grayBlursAndBadInputIsRefused();
    void theSampleFollowsTheRadius();
    void aMaskSampleKeepsItsEdgeToneAndPlacement();
    void aBlurStrokeSoftensUnderTheBrushAsOneStep();
    void aBlurStrokeSoftensAMask();
    void aMaskBetweenPixelsTakesTheFraction();
    void aMaskPastTheCanvasEdgeKeepsItsTone();
};

void BlurToolTests::aPointSpreadsAlongTheBellCurve()
{
    QImage point = BrushRaster::context(41, 41, false);
    point.setPixelColor(20, 20, QColor(255, 255, 255));
    const QImage soft = PixelAdjust::gaussianBlur(point, 2, false);
    const std::vector<double> weights = bell(2);
    for (const auto &[dx, dy] : {std::pair(0, 0), std::pair(1, 0), std::pair(0, 3), std::pair(2, 2), std::pair(-4, 1), std::pair(6, 0), std::pair(0, -6)}) {
        const int expected = int(std::lround(255 * weights[size_t(std::abs(dx))] * weights[size_t(std::abs(dy))]));
        const std::vector<int> found = pixel(soft, 20 + dx, 20 + dy);
        QVERIFY2(std::abs(found[3] - expected) <= 1 && found[0] == found[3], qPrintable(QString("%1,%2: %3 for %4").arg(dx).arg(dy).arg(found[3]).arg(expected)));
    }
    // Past three sigmas nothing arrives.
    QCOMPARE(pixel(soft, 27, 20)[3], 0);
    QCOMPARE(pixel(soft, 20, 13)[3], 0);
    // A line keeps the far taps a point loses.
    QImage line = BrushRaster::context(41, 41, false);
    QPainter(&line).fillRect(QRect(20, 0, 1, 41), Qt::white);
    const QImage spread = PixelAdjust::gaussianBlur(line, 2, true);
    for (int dx = 0; dx <= 7; ++dx)
        QCOMPARE(pixel(spread, 20 + dx, 20)[3], int(std::lround(255 * (dx < int(weights.size()) ? weights[size_t(dx)] : 0.0))));
    QCOMPARE(pixel(spread, 25, 20)[3], 2);
    QCOMPARE(pixel(spread, 26, 20)[3], 1);
    // What spreads is what was there.
    long total = 0;
    for (int y = 0; y < 41; ++y)
        for (int x = 0; x < 41; ++x)
            total += pixel(soft, x, y)[3];
    QVERIFY2(std::abs(total - 255) < 40, qPrintable(QString::number(total)));
}

void BlurToolTests::clearEdgesFadeAndClampedEdgesHold()
{
    QImage red = BrushRaster::context(30, 30, false);
    red.fill(QColor(255, 0, 0));
    // Past the edge: clear blurs in, unless clamped to it.
    const QImage faded = PixelAdjust::gaussianBlur(red, 3, false);
    const std::vector<double> weights = bell(3);
    double half = weights[0];
    for (size_t i = 1; i < weights.size(); ++i)
        half += weights[i];
    QVERIFY(std::abs(pixel(faded, 0, 0)[3] - int(std::lround(255 * half * half))) <= 1);
    QVERIFY(std::abs(pixel(faded, 0, 15)[3] - int(std::lround(255 * half))) <= 1);
    QCOMPARE(pixel(faded, 15, 15), (std::vector<int>{255, 0, 0, 255}));
    const QImage held = PixelAdjust::gaussianBlur(red, 3, true);
    QCOMPARE(held, red);
}

void BlurToolTests::grayBlursAndBadInputIsRefused()
{
    QImage step(20, 10, QImage::Format_Grayscale8);
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 20; ++x)
            step.scanLine(y)[x] = x < 10 ? 0 : 255;
    const QImage soft = PixelAdjust::gaussianBlur(step, 1.5, true);
    QCOMPARE(soft.format(), QImage::Format_Grayscale8);
    // Every row softens alike; clamped, the edges hold.
    QCOMPARE(int(soft.constScanLine(0)[0]), 0);
    QCOMPARE(int(soft.constScanLine(9)[19]), 255);
    QCOMPARE(int(soft.constScanLine(0)[9]), int(soft.constScanLine(9)[9]));
    QVERIFY(soft.constScanLine(5)[9] > 40 && soft.constScanLine(5)[9] < 128 && soft.constScanLine(5)[10] > 128);
    QVERIFY_THROWS_EXCEPTION(std::logic_error, PixelAdjust::gaussianBlur(step, 0, true));
    QVERIFY_THROWS_EXCEPTION(std::logic_error, PixelAdjust::gaussianBlur(step, std::nan(""), true));
    QVERIFY_THROWS_EXCEPTION(std::logic_error, PixelAdjust::gaussianBlur(step.convertToFormat(QImage::Format_RGB32), 2, true));
}

void BlurToolTests::theSampleFollowsTheRadius()
{
    EditorSession session;
    blurring(session, BrushSettings{.diameter = 60});
    const CanvasDocument document = session.document().value();
    const QImage sharp = edge(60, 40);
    // The bar's Radius, within 0.5 and 50, whatever the size.
    for (const auto &[radius, sigma] : {std::pair(6.0, 6.0), std::pair(0.1, 0.5), std::pair(90.0, 50.0)}) {
        BrushSettings settings = session.brushSettings();
        settings.blurRadius = radius;
        settings.diameter = 3 * radius;
        session.setBrushSettings(settings);
        const BrushStroke::Clone sample = session.blurSample(document).value();
        QCOMPARE(sample.image, sharp);
        QCOMPARE(sample.render(sharp.rect()), PixelAdjust::gaussianBlur(sharp, sigma, false));
    }
    // No layer, nothing to soften.
    session.selectLayer(std::nullopt);
    QVERIFY(!session.blurSample(document));
    QVERIFY(!session.blurSample(document, true));
}

void BlurToolTests::aMaskSampleKeepsItsEdgeToneAndPlacement()
{
    EditorSession session;
    blurring(session, BrushSettings{.diameter = 20, .blurRadius = 2});
    const QUuid id = session.activeLayerID().value();
    // A white mask with a black bar, placed mid-layer.
    QImage mask(20, 20, QImage::Format_Grayscale8);
    mask.fill(255);
    for (int y = 0; y < 20; ++y)
        for (int x = 8; x < 12; ++x)
            mask.scanLine(y)[x] = 0;
    rewrite(session, [&](ProjectSnapshot &snapshot) {
        setMask(snapshot, id, LayerMask::assetFrom(mask));
        record(snapshot, id).maskPlacement = LayerTransform{.origin = {20, 10}, .size = {20, 20}};
        record(snapshot, id).maskLinked = false;
    });
    const BrushStroke::Clone clone = session.blurSample(session.document().value(), true).value();
    const QImage sample = clone.render(clone.image.rect());
    QCOMPARE(sample.format(), QImage::Format_Grayscale8);
    QImage expected(60, 40, QImage::Format_Grayscale8);
    // Past its pixels the mask keeps its edge's tone: white.
    expected.fill(255);
    QPainter painter(&expected);
    painter.drawImage(QRectF(20, 10, 20, 20), mask);
    painter.end();
    QCOMPARE(sample, PixelAdjust::gaussianBlur(expected, 2, true));
    QVERIFY(sample.constScanLine(20)[30] < 128 && sample.constScanLine(2)[2] == 255);
    // Turned, the black box turns with the mask.
    EditorSession turned;
    turned.createDocument(100, 60);
    const QImage wide = edge(100, 60);
    turned.insert(ImportedImage(wide, wide, QStringLiteral("Edge")));
    const QUuid wideID = turned.activeLayerID().value();
    QImage white(40, 40, QImage::Format_Grayscale8);
    white.fill(255);
    rewrite(turned, [&](ProjectSnapshot &snapshot) {
        setMask(snapshot, wideID, LayerMask::assetFrom(white));
        record(snapshot, wideID).maskPlacement = LayerTransform{.origin = {30, 10}, .size = {40, 40}, .rotation = 30};
        record(snapshot, wideID).maskLinked = false;
    });
    turned.setBrushSettings(BrushSettings{.diameter = 5, .blurRadius = 1.5});
    const BrushStroke::Clone sharp = turned.blurSample(turned.document().value(), true).value();
    const QImage soft = sharp.render(sharp.image.rect());
    // Past the true box, inside one turned the other way.
    QVERIFY2(soft.constScanLine(36)[73] > 240, qPrintable(QString::number(soft.constScanLine(36)[73])));
    QVERIFY2(soft.constScanLine(24)[27] > 240, qPrintable(QString::number(soft.constScanLine(24)[27])));
}

void BlurToolTests::aBlurStrokeSoftensUnderTheBrushAsOneStep()
{
    EditorSession session;
    blurring(session, BrushSettings{.diameter = 20, .hardness = 1, .blurRadius = 2});
    const int steps = session.history.undoCount();
    session.beginBrush(QPointF(30, 12));
    QVERIFY(session.brushStroke() && session.brushStroke()->isBlur && !session.warpStroke());
    session.continueBrush(QPointF(30, 20));
    session.finishBrush();
    QCOMPARE(session.history.undoCount(), steps + 1);
    QCOMPARE(session.history.undoName(), QString("Blur"));
    // Under the brush, the sample's edge; away, still hard.
    const QImage soft = PixelAdjust::gaussianBlur(edge(60, 40), 2, false);
    QCOMPARE(exported(session, 29, 16), pixel(soft, 29, 16));
    QCOMPARE(exported(session, 30, 16), pixel(soft, 30, 16));
    QVERIFY(exported(session, 29, 16)[2] > 20);
    QCOMPARE(exported(session, 29, 36), (std::vector<int>{255, 0, 0, 255}));
    QCOMPARE(exported(session, 30, 36), (std::vector<int>{0, 0, 255, 255}));
    // At half strength, halfway between sharp and soft.
    session.undo();
    BrushSettings half = session.brushSettings();
    half.opacity = 0.5;
    session.setBrushSettings(half);
    session.beginBrush(QPointF(30, 12));
    session.continueBrush(QPointF(30, 20));
    session.finishBrush();
    const int blended = exported(session, 29, 16)[2];
    QVERIFY2(std::abs(blended - pixel(soft, 29, 16)[2] / 2) <= 1, qPrintable(QString::number(blended)));
}

void BlurToolTests::aBlurStrokeSoftensAMask()
{
    EditorSession session;
    blurring(session, BrushSettings{.diameter = 20, .hardness = 1, .blurRadius = 2});
    const QUuid id = session.activeLayerID().value();
    QImage mask(60, 40, QImage::Format_Grayscale8);
    for (int y = 0; y < 40; ++y)
        for (int x = 0; x < 60; ++x)
            mask.scanLine(y)[x] = x < 30 ? 0 : 255;
    rewrite(session, [&](ProjectSnapshot &snapshot) { setMask(snapshot, id, LayerMask::assetFrom(mask)); });
    session.selectLayerTarget(id, true);
    session.selectTool(NavigationTool::blur);
    session.beginBrush(QPointF(30, 12));
    session.continueBrush(QPointF(30, 20));
    session.finishBrush();
    QCOMPARE(session.history.undoName(), QString("Paint Mask"));
    const QImage painted = layerWith(session, id).mask.value().asset.image();
    const QImage soft = PixelAdjust::gaussianBlur(mask, 2, true);
    QCOMPARE(int(painted.constScanLine(16)[29]), int(soft.constScanLine(16)[29]));
    QCOMPARE(int(painted.constScanLine(16)[30]), int(soft.constScanLine(16)[30]));
    QVERIFY(painted.constScanLine(16)[29] > 20 && painted.constScanLine(16)[30] < 235);
    QCOMPARE(int(painted.constScanLine(36)[29]), 0);
}

void BlurToolTests::aMaskBetweenPixelsTakesTheFraction()
{
    // Half a pixel right: the mask's grid falls between.
    ImageLayer layer(QStringLiteral("Blank"), QSizeF(40, 20));
    layer.transform.origin = QPointF(0.5, 0);
    QImage clear(40, 20, QImage::Format_Grayscale8);
    clear.fill(0);
    layer.mask = LayerMask(LayerMask::assetFrom(clear));
    BrushSettings settings = brush(20, 1, 1, 1, 1);
    BrushStroke paint(layer, true, settings, QSizeF(80, 20));
    QImage steps(80, 20, QImage::Format_Grayscale8);
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 80; ++x)
            steps.scanLine(y)[x] = x < 21 ? 0 : 200;
    paint.setClone(BrushStroke::Clone{steps, QSizeF(0, 0), {}});
    paint.isBlur = true;
    paint.append(QPointF(21, 10));
    const QImage tile = paint.patches().at(0).image;
    QCOMPARE(tile.format(), QImage::Format_Grayscale8);
    // Mask pixel 20 spans document 20.5 to 21.5.
    QCOMPARE(int(tile.constScanLine(10)[19]), 0);
    QCOMPARE(int(tile.constScanLine(10)[20]), 100);
    QCOMPARE(int(tile.constScanLine(10)[21]), 200);
}

void BlurToolTests::aMaskPastTheCanvasEdgeKeepsItsTone()
{
    // Half a pixel past the left edge: half covered.
    ImageLayer layer(QStringLiteral("Blank"), QSizeF(20, 20));
    layer.transform.origin = QPointF(-0.5, 0);
    QImage white(20, 20, QImage::Format_Grayscale8);
    white.fill(255);
    layer.mask = LayerMask(LayerMask::assetFrom(white));
    BrushStroke paint(layer, true, brush(6, 1, 1, 1, 1), QSizeF(20, 20));
    paint.setClone(BrushStroke::Clone{white, QSizeF(0, 0), {}});
    paint.isBlur = true;
    paint.append(QPointF(0, 10));
    const QImage tile = paint.patches().at(0).image;
    QCOMPARE(int(tile.constScanLine(10)[0]), 255);
    QCOMPARE(int(tile.constScanLine(10)[1]), 255);
    // Over a black mask, the covered half alone turns white.
    QImage black(20, 20, QImage::Format_Grayscale8);
    black.fill(0);
    layer.mask = LayerMask(LayerMask::assetFrom(black));
    BrushStroke over(layer, true, brush(6, 1, 1, 1, 1), QSizeF(20, 20));
    over.setClone(BrushStroke::Clone{white, QSizeF(0, 0), {}});
    over.isBlur = true;
    over.append(QPointF(0, 10));
    QCOMPARE(int(over.patches().at(0).image.constScanLine(10)[0]), 128);
    QCOMPARE(int(over.patches().at(0).image.constScanLine(10)[1]), 255);
}

QTEST_GUILESS_MAIN(BlurToolTests)
#include "BlurToolTests.moc"
