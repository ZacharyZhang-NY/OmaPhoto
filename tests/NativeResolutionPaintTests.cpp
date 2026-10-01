#include "BrushFixtures.h"
#include "CanvasFixtures.h"
#include "UI/NativeLayerList.h"

// Swift's NativeResolutionPaintTests: paint lands at the layer's resolution.
class NativeResolutionPaintTests : public QObject {
    Q_OBJECT
private slots:
    void erasingAScaledDownImageKeepsItsResolution();
    void erasingAnUnevenOrTurnedLayerKeepsItsResolution();
    void canvasShowsTheErasedEdgeSmoothlyAfterScalingBackUp();
    void paintingABlankLayerThatWasScaledDownPaintsAtDocumentResolution();
    void cloneStampCopiesTheLayersOwnDetail();
    void cloneStampCarriesTheOffsetIntoTheLayer();
    void blurKeepsTheLayersResolution();
    void layersPanelSaysHowFarALayerIsScaled();
};

namespace {
std::vector<int> channel(const QImage &image, int which)
{
    const QImage bytes = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    std::vector<int> values;
    values.reserve(size_t(bytes.width()) * bytes.height());
    for (int y = 0; y < bytes.height(); ++y)
        for (int x = 0; x < bytes.width(); ++x)
            values.push_back(bytes.constScanLine(y)[x * 4 + which]);
    return values;
}

// Where each row first drops below half alpha, from `start`.
std::vector<int> edge(const std::vector<int> &values, int width, int first, int last, int start)
{
    std::vector<int> outline;
    for (int y = first; y <= last; ++y) {
        int x = start;
        while (x < width && values[size_t(y) * width + x] >= 128)
            ++x;
        outline.push_back(x);
    }
    return outline;
}

int largestStep(const std::vector<int> &outline)
{
    int largest = 0;
    for (size_t index = 1; index < outline.size(); ++index)
        largest = std::max(largest, std::abs(outline[index] - outline[index - 1]));
    return largest;
}

// A red square of `source` pixels, shown `scale` times, centred.
std::unique_ptr<EditorSession> session(double scale, int source = 1000)
{
    auto made = std::make_unique<EditorSession>();
    made->createDocument(200, 200);
    made->insert(filled(source, source, qRgba(255, 0, 0, 255), QStringLiteral("Photo")));
    const QUuid id = made->activeLayerID().value();
    const double side = source * scale;
    rewrite(*made, [&](ProjectSnapshot &snapshot) { record(snapshot, id).transform = LayerTransform{.origin = {100 - side / 2, 100 - side / 2}, .size = {side, side}}; });
    made->selectTool(NavigationTool::brush);
    made->setBrushMode(BrushToolMode::erase);
    made->setBrushSettings(brush(40, 1, 0, 0, 0));
    return made;
}

// Black and white two-pixel checks left, red right, at 10%.
std::unique_ptr<EditorSession> checkerSession()
{
    QImage image = BrushRaster::context(1000, 1000, false);
    QPainter painter(&image);
    painter.fillRect(QRect(500, 0, 500, 1000), Qt::red);
    painter.fillRect(QRect(0, 0, 500, 1000), Qt::white);
    for (int y = 0; y < 1000; y += 2)
        for (int x = (y / 2) % 2 == 0 ? 0 : 2; x < 500; x += 4)
            painter.fillRect(QRect(x, y, 2, 2), Qt::black);
    painter.end();
    auto made = std::make_unique<EditorSession>();
    made->createDocument(200, 200);
    made->insert(ImportedImage(image, image, QStringLiteral("Checker")));
    const QUuid id = made->activeLayerID().value();
    rewrite(*made, [&](ProjectSnapshot &snapshot) { record(snapshot, id).transform = LayerTransform{.origin = {50, 50}, .size = {100, 100}}; });
    return made;
}

bool holdsBoth(const std::vector<int> &values)
{
    return std::any_of(values.begin(), values.end(), [](int value) { return value < 30; })
        && std::any_of(values.begin(), values.end(), [](int value) { return value > 225; });
}
}

void NativeResolutionPaintTests::erasingAScaledDownImageKeepsItsResolution()
{
    const auto made = session(0.1);
    made->beginBrush(QPointF(100, 100));
    made->finishBrush();
    QVERIFY(!made->brushError());
    const QImage image = made->activeLayer().value().asset.value().image();
    QCOMPARE(image.size(), QSize(1000, 1000));
    // A 40-pixel hole at 10%: 400 image pixels.
    const std::vector<int> values = channel(image, 3);
    QCOMPARE(values[500 * 1000 + 500], 0);
    QCOMPARE(values[500 * 1000 + 250], 255);
    QCOMPARE(values[500 * 1000 + 280], 255);
    QCOMPARE(values[500 * 1000 + 320], 0);
    // A coarse grid's outline would step by ten a row.
    QVERIFY(largestStep(edge(values, 1000, 330, 470, 250)) <= 4);
}

void NativeResolutionPaintTests::erasingAnUnevenOrTurnedLayerKeepsItsResolution()
{
    // Swift's CPU fallback case: the twin's only path.
    for (const bool rotated : {false, true}) {
        const auto made = session(0.1);
        ImageLayer layer = made->activeLayer().value();
        layer.transform.size.setHeight(layer.transform.size.height() + 0.013);
        if (rotated)
            layer.transform.rotation = 30;
        BrushSettings settings = made->brushSettings();
        settings.erasing = true;
        BrushStroke stroke(layer, false, settings, QSizeF(200, 200));
        stroke.append(QPointF(100, 100));
        stroke.flush();
        const QImage image = stroke.paintSnapshot().asset.image();
        QCOMPARE(image.width(), 1000);
        const std::vector<int> values = channel(image, 3);
        QCOMPARE(values[500 * 1000 + 500], 0);
        QVERIFY2(largestStep(edge(values, 1000, 330, 470, 250)) <= 4, rotated ? "rotated" : "uneven");
    }
}

void NativeResolutionPaintTests::canvasShowsTheErasedEdgeSmoothlyAfterScalingBackUp()
{
    Shown shown(QSize(400, 400), QSize(800, 600));
    shown.settle();
    EditorSession &session = shown.session;
    session.insert(filled(1000, 1000, qRgba(255, 0, 0, 255), QStringLiteral("Photo")));
    const QUuid id = session.activeLayerID().value();
    LayerTransform small{.origin = {180, 180}, .size = {40, 40}};
    small.sampling = LayerSampling::high;
    rewrite(session, [&](ProjectSnapshot &snapshot) { record(snapshot, id).transform = small; });
    session.zoom(1);
    shown.canvas->synchronizeDisplay();
    shown.canvas->grab();
    session.selectTool(NavigationTool::brush);
    session.setBrushMode(BrushToolMode::erase);
    session.setBrushSettings(brush(20, 1, 0, 0, 0));
    session.beginBrush(QPointF(200, 200));
    QVERIFY(session.finishBrushImmediately());
    shown.canvas->synchronizeDisplay();
    shown.canvas->grab();
    // Scaled back up as the Move tool would.
    LayerTransform large = session.activeLayer().value().transform;
    large.origin = {0, 0};
    large.size = {400, 400};
    session.selectTool(NavigationTool::move);
    session.beginTransform();
    session.previewTransform(large);
    session.commitTransform();
    shown.canvas->synchronizeDisplay();
    const QImage view = shown.canvas->grab().toImage();
    // A 500-pixel hole: 200 document pixels, a point each.
    const QPointF point = session.viewport.viewPoint(QPointF(200, 200), shown.documentSize());
    const int cx = int(point.x()), cy = int(point.y());
    const auto erased = [&](int x, int y) {
        const QColor colour = view.pixelColor(x, y);
        return colour.redF() < 0.6 || colour.greenF() > 0.6;
    };
    QVERIFY(erased(cx, cy));
    QVERIFY(!erased(cx - 120, cy));
    std::vector<int> outline;
    for (int y = cy - 70; y <= cy - 5; ++y) {
        int x = cx - 120;
        while (x < cx && !erased(x, y))
            ++x;
        outline.push_back(x);
    }
    QVERIFY(largestStep(outline) <= 3);
    // The arc's widest row reaches a hundred points out.
    QVERIFY(std::abs(outline.back() - (cx - 100)) <= 2);
}

void NativeResolutionPaintTests::paintingABlankLayerThatWasScaledDownPaintsAtDocumentResolution()
{
    EditorSession made;
    made.createDocument(200, 200);
    made.addBlankLayer();
    const QUuid id = made.activeLayerID().value();
    rewrite(made, [&](ProjectSnapshot &snapshot) { record(snapshot, id).transform = LayerTransform{.origin = {90, 90}, .size = {20, 20}}; });
    made.selectTool(NavigationTool::brush);
    made.setBrushSettings(brush(40, 1, 1, 0, 0));
    made.beginBrush(QPointF(100, 100));
    made.finishBrush();
    const ImageLayer layer = made.activeLayer().value();
    const QImage image = layer.asset.value().image();
    // A layer pixel per document pixel, however small it was.
    QVERIFY(std::abs(image.width() - layer.transform.size.width()) <= 1);
    QVERIFY(image.width() >= 38);
    QCOMPARE(layer.transform.size, QSizeF(image.size()));
}

void NativeResolutionPaintTests::cloneStampCopiesTheLayersOwnDetail()
{
    const auto made = checkerSession();
    made->selectTool(NavigationTool::cloneStamp);
    made->setCloneSource(QPointF(70, 100));
    made->setBrushSettings(brush(10, 1, 0, 0, 0));
    made->beginBrush(QPointF(130, 100));
    made->finishBrush();
    QVERIFY(!made->brushError());
    const QImage image = made->activeLayer().value().asset.value().image();
    QCOMPARE(image.width(), 1000);
    // The checks, too fine to see at 10%, arrive whole.
    const std::vector<int> values = channel(image, 1);
    QVERIFY(holdsBoth(std::vector<int>(values.begin() + 500 * 1000 + 790, values.begin() + 500 * 1000 + 810)));
    // Copied pixel for pixel, 600 image pixels to the left.
    for (int x = 790; x < 810; ++x)
        QCOMPARE(values[500 * 1000 + x], values[500 * 1000 + x - 600]);
}

void NativeResolutionPaintTests::cloneStampCarriesTheOffsetIntoTheLayer()
{
    // Flipped, the source lies the other way in pixels.
    const auto made = checkerSession();
    const QUuid id = made->activeLayerID().value();
    rewrite(*made, [&](ProjectSnapshot &snapshot) { record(snapshot, id).transform.flipX = true; });
    BrushStroke stroke(made->activeLayer().value(), false, brush(10, 1, 0, 0, 0), QSizeF(200, 200));
    QCOMPARE(stroke.gridRect(QRectF(0, 0, 1000, 1000), QSizeF(-60, 0)), QRectF(-600, 0, 1000, 1000));
    made->selectTool(NavigationTool::cloneStamp);
    made->setCloneSource(QPointF(130, 100));
    made->setBrushSettings(brush(10, 1, 0, 0, 0));
    made->beginBrush(QPointF(70, 100));
    made->finishBrush();
    const std::vector<int> values = channel(made->activeLayer().value().asset.value().image(), 1);
    for (int x = 790; x < 810; ++x)
        QCOMPARE(values[500 * 1000 + x], values[500 * 1000 + x - 600]);
    // Turned a quarter, across the canvas is down the pixels.
    rewrite(*made, [&](ProjectSnapshot &snapshot) {
        record(snapshot, id).transform.flipX = false;
        record(snapshot, id).transform.rotation = 90;
    });
    BrushStroke turned(made->activeLayer().value(), false, brush(10, 1, 0, 0, 0), QSizeF(200, 200));
    const QRectF shifted = turned.gridRect(turned.sourceRect, QSizeF(6, 0));
    QCOMPARE(shifted.topLeft() - turned.sourceRect.topLeft(), QPointF(0, 60));
    QCOMPARE(shifted.size(), turned.sourceRect.size());
}

void NativeResolutionPaintTests::blurKeepsTheLayersResolution()
{
    const auto made = checkerSession();
    made->setBlurMode(BlurToolMode::blur);
    made->selectTool(NavigationTool::blur);
    made->setBrushSettings(brush(20, 1, 0, 0, 0));
    made->beginBrush(QPointF(100, 100));
    QVERIFY(made->brushStroke());
    made->finishBrush();
    QVERIFY(!made->brushError());
    const QImage image = made->activeLayer().value().asset.value().image();
    QCOMPARE(image.width(), 1000);
    const std::vector<int> values = channel(image, 1);
    // Under the brush the checks go gray; far off, not.
    for (int x = 480; x < 490; ++x)
        QVERIFY(values[500 * 1000 + x] > 40 && values[500 * 1000 + x] < 215);
    QVERIFY(holdsBoth(std::vector<int>(values.begin() + 100 * 1000 + 100, values.begin() + 100 * 1000 + 104)));
}

void NativeResolutionPaintTests::layersPanelSaysHowFarALayerIsScaled()
{
    const auto made = session(0.05);
    ImageLayer layer = made->activeLayer().value();
    QCOMPARE(sizeLabel(layer), QString("50 × 50 px · 5%"));
    layer.transform.size = QSizeF(1000, 1000);
    QCOMPARE(sizeLabel(layer), QString("1000 × 1000 px"));
    layer.transform.size = QSizeF(25, 25);
    QCOMPARE(sizeLabel(layer), QString("25 × 25 px · 2.5%"));
    // Across the width; under a twentieth percent shows none.
    layer.transform.size = QSizeF(1000.45, 300);
    QCOMPARE(sizeLabel(layer), QString("1000 × 300 px"));
    layer.transform.size = QSizeF(1000.55, 1000);
    QCOMPARE(sizeLabel(layer), QString("1001 × 1000 px · 100.1%"));
    // Rounded to a whole number, no decimal shows.
    layer.transform.size = QSizeF(499.9, 300);
    QCOMPARE(sizeLabel(layer), QString("500 × 300 px · 50%"));
    layer.transform.size = QSizeF(2333, 2000);
    QCOMPARE(sizeLabel(layer), QString("2333 × 2000 px · 233.3%"));
    // A blank layer has no pixels to measure against.
    QCOMPARE(sizeLabel(ImageLayer(QStringLiteral("Blank"), QSizeF(30, 20))), QString("30 × 20 px"));
    // The row shows it.
    NativeLayerList list(*made);
    list.resize(300, 200);
    list.show();
    QVERIFY(QTest::qWaitForWindowExposed(&list));
    QCOMPARE(list.cells().at(0)->findChild<QLabel *>("layerDimensions")->text(), QString("50 × 50 px · 5%"));
}

QTEST_MAIN(NativeResolutionPaintTests)
#include "NativeResolutionPaintTests.moc"
