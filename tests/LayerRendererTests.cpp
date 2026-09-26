#include "IO/ImageExporter.h"
#include "Rendering/LayerRenderer.h"
#include "RenderFixtures.h"
#include <QPixmap>
#include "AddressSpaceLimit.h"
#include <QtTest>
#include <algorithm>

class LayerRendererTests : public QObject {
    Q_OBJECT
private slots:
    void placesScalesRotatesAndFlips();
    void opacityScalesTheWholeLayer();
    void layerOpacityIgnoresThePaintersOwn();
    void aMaskMultipliesCoverage();
    void aWhiteMaskChangesNothing();
    void aBlackMaskHidesARotatedLayerCompletely();
    void aGrayMaskHalvesARotatedLayer();
    void aMaskedEdgeIsCoverageSquared();
    void maskedLayersFollowRotationAndPainterScale();
    void masksAreSampledLikeTheirLayer();
    void compositeReducesTheMaskByItsOwnWidth();
    void aMaskMustBeEightBitGray();
    void aMaskedLayerOutsideTheDeviceDrawsNothing();
    void aSurfaceThatCannotBeAllocatedThrows();
    void aThrowingDrawLeavesThePainterBalanced();
    void aShrinkingPainterDrawsFromHalvings();
    void oddSizedReductionsKeepTheirOverhang();
    void nearestLayersKeepHardEdges();
    void nearestStaysBlockyAndSmoothInterpolates();
    void drawingLeavesThePainterAsItFoundIt();
    void interpolationAndReductionRules();
};

void LayerRendererTests::placesScalesRotatesAndFlips()
{
    const QImage image = coordinates(4, 2);
    QImage surface = BrushRaster::context(12, 12, false);
    QPainter painter(&surface);
    const LayerTransform plain = placedAt({2, 3}, {4, 2});
    LayerRenderer::draw(image, plain, plain.center(), painter);
    QCOMPARE(surface.pixel(2, 3), qRgba(0, 0, 0, 255));
    QCOMPARE(surface.pixel(5, 4), qRgba(30, 10, 0, 255));
    QCOMPARE(surface.pixel(6, 4), qRgba(0, 0, 0, 0));

    surface.fill(0);
    painter.save();
    painter.translate(5, 6);
    LayerRenderer::draw(image, plain, plain.center(), painter);
    painter.restore();
    QCOMPARE(surface.pixel(7, 9), qRgba(0, 0, 0, 255));
    QCOMPARE(surface.pixel(10, 10), qRgba(30, 10, 0, 255));
    QCOMPARE(surface.pixel(2, 3), qRgba(0, 0, 0, 0));

    surface.fill(0);
    LayerTransform flipped = plain;
    flipped.flipX = true;
    LayerRenderer::draw(image, flipped, flipped.center(), painter);
    QCOMPARE(surface.pixel(2, 3), qRgba(30, 0, 0, 255));
    QCOMPARE(surface.pixel(5, 4), qRgba(0, 10, 0, 255));
    flipped.flipX = false;
    flipped.flipY = true;
    surface.fill(0);
    LayerRenderer::draw(image, flipped, flipped.center(), painter);
    QCOMPARE(surface.pixel(2, 3), qRgba(0, 10, 0, 255));
    QCOMPARE(surface.pixel(5, 4), qRgba(30, 0, 0, 255));

    surface.fill(0);
    LayerTransform turned = placedAt({4, 3}, {4, 2});
    turned.rotation = 90;
    LayerRenderer::draw(image, turned, turned.center(), painter);
    QCOMPARE(surface.pixel(6, 2), qRgba(0, 0, 0, 255));
    QCOMPARE(surface.pixel(5, 2), qRgba(0, 10, 0, 255));
    QCOMPARE(surface.pixel(6, 5), qRgba(30, 0, 0, 255));
    QCOMPARE(surface.pixel(7, 3), qRgba(0, 0, 0, 0));

    surface.fill(0);
    LayerRenderer::draw(image, plain, plain.center() * 2, painter, {.scale = 2});
    QCOMPARE(surface.pixel(4, 6), qRgba(0, 0, 0, 255));
    QCOMPARE(surface.pixel(11, 9), qRgba(30, 10, 0, 255));
    QCOMPARE(surface.pixel(3, 6), qRgba(0, 0, 0, 0));
}

void LayerRendererTests::opacityScalesTheWholeLayer()
{
    QImage surface = BrushRaster::context(4, 4, false);
    QPainter painter(&surface);
    const LayerTransform transform = placedAt({0, 0}, {4, 4});
    LayerRenderer::draw(solid(4, 4, qRgba(200, 100, 0, 255)), transform, transform.center(), painter, {.opacity = 0.5});
    const QRgb pixel = surface.pixel(1, 1);
    QVERIFY(std::abs(qAlpha(pixel) - 128) <= 1);
    QVERIFY(std::abs(qRed(pixel) - 100) <= 1);
    QVERIFY(std::abs(qGreen(pixel) - 50) <= 1);
    QCOMPARE(qBlue(pixel), 0);
}

void LayerRendererTests::aMaskMultipliesCoverage()
{
    QImage mask = gray(4, 2, 255);
    mask.scanLine(0)[1] = mask.scanLine(1)[1] = 128;
    mask.scanLine(0)[2] = mask.scanLine(1)[2] = 0;
    QImage surface = BrushRaster::context(4, 2, false);
    QPainter painter(&surface);
    const LayerTransform transform = placedAt({0, 0}, {4, 2});
    LayerRenderer::draw(solid(4, 2, qRgba(0, 200, 0, 255)), transform, transform.center(), painter, {.mask = mask});
    painter.end();
    QCOMPARE(surface.pixel(0, 0), qRgba(0, 200, 0, 255));
    QVERIFY(std::abs(qAlpha(surface.pixel(1, 1)) - 128) <= 1);
    QVERIFY(std::abs(qGreen(surface.pixel(1, 1)) - 100) <= 1);
    QCOMPARE(surface.pixel(2, 0), qRgba(0, 0, 0, 0));
    QCOMPARE(surface.pixel(3, 1), qRgba(0, 200, 0, 255));
}

void LayerRendererTests::aMaskMustBeEightBitGray()
{
    QImage surface = BrushRaster::context(4, 4, false);
    QPainter painter(&surface);
    const LayerTransform transform = placedAt({0, 0}, {4, 4});
    QVERIFY_THROWS_EXCEPTION(std::logic_error, LayerRenderer::draw(solid(4, 4, qRgba(9, 9, 9, 255)), transform,
                                                                   transform.center(), painter,
                                                                   {.mask = solid(4, 4, qRgba(255, 255, 255, 255))}));
}

void LayerRendererTests::aMaskedLayerOutsideTheDeviceDrawsNothing()
{
    QImage surface = solid(4, 4, qRgba(1, 2, 3, 255));
    QPainter painter(&surface);
    const LayerTransform away = placedAt({100, 100}, {4, 4});
    LayerRenderer::draw(solid(4, 4, qRgba(9, 9, 9, 255)), away, away.center(), painter, {.mask = gray(4, 4, 255)});
    LayerRenderer::draw(solid(4, 4, qRgba(9, 9, 9, 255)), away, away.center(), painter, {.blendMode = LayerBlendMode::hue});
    painter.end();
    QCOMPARE(surface, solid(4, 4, qRgba(1, 2, 3, 255)));
}

void LayerRendererTests::aSurfaceThatCannotBeAllocatedThrows()
{
    QImage device = BrushRaster::context(8192, 8192, true);
    QPainter painter(&device);
    const LayerTransform transform = placedAt({0, 0}, {8192, 8192});
    const QImage image = solid(2, 2, qRgba(9, 9, 9, 255)), mask = gray(2, 2, 255);
    std::optional<AddressSpaceLimit> limit(std::in_place, 16 * 1024 * 1024);
    std::optional<ExportError::Kind> thrown;
    try {
        LayerRenderer::draw(image, transform, transform.center(), painter, {.mask = mask});
    } catch (const ExportError &error) {
        thrown = error.kind;
    }
    limit.reset();
    QCOMPARE(thrown, std::optional(ExportError::Kind::render));

    // Room for the colour surface, not for the mask's quarter.
    const qint64 surfaceBytes = qint64(8192) * 8192 * 4;
    limit.emplace(surfaceBytes + surfaceBytes / 8);
    std::optional<ExportError::Kind> maskThrown;
    try {
        LayerRenderer::draw(image, transform, transform.center(), painter, {.mask = mask});
    } catch (const ExportError &error) {
        maskThrown = error.kind;
    }
    limit.reset();
    QCOMPARE(maskThrown, std::optional(ExportError::Kind::render));
}

void LayerRendererTests::aShrinkingPainterDrawsFromHalvings()
{
    QImage stripes = BrushRaster::context(2048, 64, false);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 2048; ++x)
            stripes.setPixel(x, y, x % 4 == 1 || x % 4 == 2 ? qRgba(255, 255, 255, 255) : qRgba(0, 0, 0, 255));
    }
    QImage surface = BrushRaster::context(256, 8, false);
    QPainter painter(&surface);
    painter.scale(0.125, 0.125);
    const LayerTransform transform{.origin = {0, 0}, .size = {2048, 64}, .sampling = LayerSampling::high};
    LayerRenderer::draw(stripes, transform, transform.center(), painter);
    painter.end();
    for (int x = 8; x < 248; ++x)
        QVERIFY(std::abs(qRed(surface.pixel(x, 4)) - 127) <= 8);

    QImage fineMask = gray(2048, 64, 0);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 2048; ++x)
            fineMask.scanLine(y)[x] = x % 4 == 1 || x % 4 == 2 ? 255 : 0;
    }
    QImage masked = BrushRaster::context(256, 8, false);
    QPainter masking(&masked);
    masking.scale(0.125, 0.125);
    LayerRenderer::draw(solid(2048, 64, qRgba(255, 255, 255, 255)), transform, transform.center(), masking, {.mask = fineMask});
    masking.end();
    for (int x = 8; x < 248; ++x)
        QVERIFY(std::abs(qAlpha(masked.pixel(x, 4)) - 127) <= 8);
}

void LayerRendererTests::oddSizedReductionsKeepTheirOverhang()
{
    // 1025 px halve twice to 257, covering 1028 source pixels.
    QImage edge = BrushRaster::context(1025, 16, false);
    QImage edgeMask = gray(1025, 16, 0);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 1025; ++x) {
            edge.setPixel(x, y, x < 1000 ? qRgba(0, 0, 0, 255) : qRgba(255, 255, 255, 255));
            edgeMask.scanLine(y)[x] = x < 1000 ? 0 : 255;
        }
    }
    const LayerTransform transform{.origin = {0, 0}, .size = {1025, 16}, .sampling = LayerSampling::high};
    QImage direct = BrushRaster::context(260, 4, false), masked = direct;
    QPainter first(&direct), second(&masked);
    LayerRenderer::draw(edge, transform, transform.center() * 0.25, first, {.scale = 0.25});
    LayerRenderer::draw(solid(1025, 16, qRgba(255, 255, 255, 255)), transform, transform.center() * 0.25, second,
                        {.scale = 0.25, .mask = edgeMask});
    first.end();
    second.end();
    QVERIFY(qRed(direct.pixel(249, 2)) < 40);
    QVERIFY(qRed(direct.pixel(250, 2)) > 215);
    QVERIFY(qAlpha(masked.pixel(249, 2)) < 40);
    QVERIFY(qAlpha(masked.pixel(250, 2)) > 215);

    QImage aside = BrushRaster::context(260, 4, false);
    QPainter third(&aside);
    LayerRenderer::draw(edge, transform, transform.center() * 0.25, third, {.scale = 0.25, .mask = gray(1025, 16, 255)});
    third.end();
    QVERIFY(qRed(aside.pixel(249, 2)) < 40);
    QVERIFY(qRed(aside.pixel(250, 2)) > 215);
}

void LayerRendererTests::nearestLayersKeepHardEdges()
{
    const auto partialPixels = [&](LayerSampling sampling) {
        QImage surface = BrushRaster::context(24, 24, false);
        QPainter painter(&surface);
        const LayerTransform transform{.origin = {6, 6}, .size = {12, 12}, .rotation = 30, .sampling = sampling};
        LayerRenderer::draw(solid(12, 12, qRgba(255, 255, 255, 255)), transform, transform.center(), painter);
        painter.end();
        int partial = 0;
        for (int y = 0; y < 24; ++y) {
            for (int x = 0; x < 24; ++x)
                partial += qAlpha(surface.pixel(x, y)) > 0 && qAlpha(surface.pixel(x, y)) < 255;
        }
        return partial;
    };
    QCOMPARE(partialPixels(LayerSampling::nearest), 0);
    QVERIFY(partialPixels(LayerSampling::smooth) > 10);
}

void LayerRendererTests::maskedLayersFollowRotationAndPainterScale()
{
    QImage mask = gray(2, 1, 255);
    mask.scanLine(0)[1] = 0;
    QImage surface = BrushRaster::context(40, 40, false);
    QPainter painter(&surface);
    painter.translate(20, 0);
    painter.scale(2, 2);
    LayerTransform transform = placedAt({0, 0}, {10, 4});
    transform.rotation = 90;
    LayerRenderer::draw(solid(2, 1, qRgba(255, 0, 0, 255)), transform, transform.center(), painter, {.mask = mask});
    painter.end();
    QCOMPARE(surface.pixel(30, 0), qRgba(255, 0, 0, 255));
    QCOMPARE(surface.pixel(30, 3), qRgba(255, 0, 0, 255));
    QCOMPARE(surface.pixel(30, 9), qRgba(0, 0, 0, 0));
    QCOMPARE(surface.pixel(24, 0), qRgba(0, 0, 0, 0));
    QCOMPARE(surface.pixel(27, 1), qRgba(255, 0, 0, 255));
    QCOMPARE(surface.pixel(33, 1), qRgba(255, 0, 0, 255));
    QCOMPARE(surface.pixel(35, 1), qRgba(0, 0, 0, 0));
}

void LayerRendererTests::nearestStaysBlockyAndSmoothInterpolates()
{
    QImage pair = BrushRaster::context(2, 1, false);
    pair.setPixel(0, 0, qRgba(0, 0, 0, 255));
    pair.setPixel(1, 0, qRgba(255, 255, 255, 255));
    const auto row = [&](LayerSampling sampling) {
        QImage surface = BrushRaster::context(8, 1, false);
        QPainter painter(&surface);
        const LayerTransform transform{.origin = {0, 0}, .size = {8, 1}, .sampling = sampling};
        LayerRenderer::draw(pair, transform, transform.center(), painter);
        painter.end();
        QList<int> values;
        for (int x = 0; x < 8; ++x)
            values << qRed(surface.pixel(x, 0));
        return values;
    };
    QCOMPARE(row(LayerSampling::nearest), QList<int>({0, 0, 0, 0, 255, 255, 255, 255}));
    for (LayerSampling sampling : {LayerSampling::smooth, LayerSampling::high}) {
        const QList<int> smooth = row(sampling);
        QVERIFY(std::is_sorted(smooth.begin(), smooth.end()));
        QVERIFY(std::count_if(smooth.begin(), smooth.end(), [](int value) { return value > 0 && value < 255; }) >= 2);
    }
}

void LayerRendererTests::interpolationAndReductionRules()
{
    QCOMPARE(LayerRenderer::interpolation(LayerSampling::nearest, 0.5), InterpolationQuality::none);
    QCOMPARE(LayerRenderer::interpolation(LayerSampling::nearest, 4), InterpolationQuality::none);
    QCOMPARE(LayerRenderer::interpolation(LayerSampling::high, 1), InterpolationQuality::low);
    QCOMPARE(LayerRenderer::interpolation(LayerSampling::high, 1.01), InterpolationQuality::high);
    QCOMPARE(LayerRenderer::interpolation(LayerSampling::smooth, 3), InterpolationQuality::low);
    QCOMPARE(quality(LayerSampling::nearest), InterpolationQuality::none);
    QCOMPARE(quality(LayerSampling::smooth), InterpolationQuality::low);
    QCOMPARE(quality(LayerSampling::high), InterpolationQuality::high);

    const QImage image = coordinates(9, 5);
    const LayerRenderer::Reduced kept = LayerRenderer::reduced(image, 2, 1, LayerSampling::nearest);
    QCOMPARE(kept.level, 0);
    QCOMPARE(kept.image.cacheKey(), image.cacheKey());
    const LayerRenderer::Reduced half = LayerRenderer::reduced(image, 2, 2, LayerSampling::high);
    QCOMPARE(half.level, 1);
    QCOMPARE(half.image.size(), QSize(5, 3));
    QCOMPARE(half.widthScale, 10.0 / 9);
    QCOMPARE(half.heightScale, 6.0 / 5);
    QCOMPARE(LayerRenderer::coverage(half, QRectF(-9, -5, 18, 10)), QRectF(-9, -5, 20, 12));
    QCOMPARE(LayerRenderer::reduced(image, 9, 1, LayerSampling::high).level, 0);

    QImage surface = BrushRaster::context(4, 4, false);
    QPainter painter(&surface);
    QCOMPARE(LayerRenderer::deviceScale(painter), 1.0);
    painter.scale(3, 3);
    painter.rotate(40);
    QVERIFY(std::abs(LayerRenderer::deviceScale(painter) - 3) < 1e-9);
}

void LayerRendererTests::layerOpacityIgnoresThePaintersOwn()
{
    const LayerTransform transform = placedAt({0, 0}, {2, 2});
    const QImage image = solid(2, 2, qRgba(200, 100, 0, 255));
    for (const QImage &mask : {QImage(), gray(2, 2, 255)}) {
        QImage surface = BrushRaster::context(2, 2, false);
        QPainter painter(&surface);
        painter.setOpacity(0.25);
        LayerRenderer::draw(image, transform, transform.center(), painter, {.mask = mask});
        QCOMPARE(painter.opacity(), 0.25);
        painter.end();
        QCOMPARE(surface.pixel(0, 0), qRgba(200, 100, 0, 255));
    }
}

void LayerRendererTests::aWhiteMaskChangesNothing()
{
    const QImage image = coordinates(6, 4, 40);
    for (LayerBlendMode mode : {LayerBlendMode::normal, LayerBlendMode::multiply, LayerBlendMode::colorBurn}) {
        const LayerTransform transform{.origin = {1, 1}, .size = {6, 4}, .sampling = LayerSampling::smooth};
        const QImage paper = solid(20, 14, qRgba(40, 90, 160, 255));
        QImage direct = paper, masked = paper;
        QPainter first(&direct), second(&masked);
        for (QPainter *painter : {&first, &second}) {
            painter->translate(2, 1);
            painter->scale(2, 2);
        }
        LayerRenderer::draw(image, transform, transform.center(), first, {.opacity = 0.7, .blendMode = mode});
        LayerRenderer::draw(image, transform, transform.center(), second,
                            {.opacity = 0.7, .blendMode = mode, .mask = gray(6, 4, 255)});
        first.end();
        second.end();
        for (int y = 0; y < 14; ++y) {
            for (int x = 0; x < 20 * 4; ++x)
                QVERIFY(std::abs(direct.constScanLine(y)[x] - masked.constScanLine(y)[x]) <= 2);
        }
        QVERIFY(direct != paper);
    }
}

void LayerRendererTests::masksAreSampledLikeTheirLayer()
{
    QImage step = gray(2, 1, 0);
    step.scanLine(0)[1] = 255;
    const auto alphas = [&](LayerSampling sampling) {
        QImage surface = BrushRaster::context(8, 1, false);
        QPainter painter(&surface);
        const LayerTransform transform{.origin = {0, 0}, .size = {8, 1}, .sampling = sampling};
        LayerRenderer::draw(solid(2, 1, qRgba(255, 255, 255, 255)), transform, transform.center(), painter, {.mask = step});
        painter.end();
        QList<int> values;
        for (int x = 0; x < 8; ++x)
            values << qAlpha(surface.pixel(x, 0));
        return values;
    };
    QCOMPARE(alphas(LayerSampling::nearest), QList<int>({0, 0, 0, 0, 255, 255, 255, 255}));
    const QList<int> smooth = alphas(LayerSampling::smooth);
    QVERIFY(std::is_sorted(smooth.begin(), smooth.end()));
    QVERIFY(std::count_if(smooth.begin(), smooth.end(), [](int value) { return value > 0 && value < 255; }) >= 2);
}

void LayerRendererTests::compositeReducesTheMaskByItsOwnWidth()
{
    QImage stripes = gray(1024, 8, 0);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 1024; ++x)
            stripes.scanLine(y)[x] = x % 4 == 1 || x % 4 == 2 ? 255 : 0;
    }
    QImage surface = BrushRaster::context(1024, 8, false);
    QPainter painter(&surface);
    // The mask covers an eighth of the layer's extent.
    const QRectF extent(0, 0, 1024, 8), maskBounds(0, 0, 128, 8);
    LayerRenderer::composite(painter, QTransform(), extent, LayerSampling::high, InterpolationQuality::low, {.mask = stripes},
                             maskBounds, [&](QPainter &aside) { aside.fillRect(extent, Qt::white); });
    painter.end();
    for (int x = 8; x < 120; ++x)
        QVERIFY(std::abs(qAlpha(surface.pixel(x, 4)) - 127) <= 12);
    QCOMPARE(qAlpha(surface.pixel(400, 4)), 0);
}

void LayerRendererTests::aBlackMaskHidesARotatedLayerCompletely()
{
    QImage surface = BrushRaster::context(24, 24, false);
    QPainter painter(&surface);
    const LayerTransform transform{.origin = {8, 8}, .size = {8, 8}, .rotation = 30, .sampling = LayerSampling::smooth};
    LayerRenderer::draw(solid(8, 8, qRgba(255, 255, 255, 255)), transform, transform.center(), painter, {.mask = gray(8, 8, 0)});
    painter.end();
    QCOMPARE(surface, BrushRaster::context(24, 24, false));
}

void LayerRendererTests::aGrayMaskHalvesARotatedLayer()
{
    QImage surface = BrushRaster::context(24, 24, false);
    QPainter painter(&surface);
    const LayerTransform transform{.origin = {4, 4}, .size = {16, 16}, .rotation = 30, .sampling = LayerSampling::smooth};
    LayerRenderer::draw(solid(16, 16, qRgba(255, 255, 255, 255)), transform, transform.center(), painter, {.mask = gray(16, 16, 128)});
    painter.end();
    QVERIFY(std::abs(qAlpha(surface.pixel(12, 12)) - 128) <= 1);
    QVERIFY(std::abs(qAlpha(surface.pixel(9, 14)) - 128) <= 1);
    QCOMPARE(qAlpha(surface.pixel(0, 0)), 0);
    for (int y = 0; y < 24; ++y) {
        for (int x = 0; x < 24; ++x)
            QVERIFY(qAlpha(surface.pixel(x, y)) <= 129);
    }
}

void LayerRendererTests::aMaskedEdgeIsCoverageSquared()
{
    // CoreGraphics antialiases the mask clip and the image alike.
    QImage direct = BrushRaster::context(24, 24, false), masked = direct;
    QPainter first(&direct), second(&masked);
    const LayerTransform transform{.origin = {4, 4}, .size = {16, 16}, .rotation = 30, .sampling = LayerSampling::smooth};
    const QImage white = solid(16, 16, qRgba(255, 255, 255, 255));
    LayerRenderer::draw(white, transform, transform.center(), first);
    LayerRenderer::draw(white, transform, transform.center(), second, {.mask = gray(16, 16, 255)});
    first.end();
    second.end();
    int partial = 0;
    for (int y = 0; y < 24; ++y) {
        for (int x = 0; x < 24; ++x) {
            const int edge = qAlpha(direct.pixel(x, y));
            partial += edge > 30 && edge < 225;
            QVERIFY(std::abs(qAlpha(masked.pixel(x, y)) - (edge * edge + 127) / 255) <= 2);
        }
    }
    QVERIFY(partial >= 20);
}

void LayerRendererTests::aThrowingDrawLeavesThePainterBalanced()
{
    QTest::failOnWarning(QRegularExpression(".*saved states.*"));
    const LayerTransform transform = placedAt({0, 0}, {4, 4});
    const QImage image = solid(4, 4, qRgba(9, 9, 9, 255));
    QPixmap pixmap(4, 4);
    QPainter painter(&pixmap);
    painter.translate(1, 2);
    const QTransform before = painter.worldTransform();
    QVERIFY_THROWS_EXCEPTION(std::logic_error, LayerRenderer::draw(image, transform, transform.center(), painter,
                                                                   {.blendMode = LayerBlendMode::hue}));
    QVERIFY_THROWS_EXCEPTION(std::logic_error, LayerRenderer::draw(image, transform, transform.center(), painter,
                                                                   {.mask = image}));
    QCOMPARE(painter.worldTransform(), before);
    QCOMPARE(painter.opacity(), 1.0);
    QCOMPARE(painter.compositionMode(), QPainter::CompositionMode_SourceOver);
    QVERIFY(painter.end());
}

void LayerRendererTests::drawingLeavesThePainterAsItFoundIt()
{
    QImage surface = BrushRaster::context(8, 8, false);
    QPainter painter(&surface);
    painter.translate(1, 2);
    const QTransform before = painter.worldTransform();
    const LayerTransform transform{.origin = {0, 0}, .size = {4, 4}, .rotation = 10, .sampling = LayerSampling::smooth};
    const LayerRenderer::Options options[] = {{.opacity = 0.5, .blendMode = LayerBlendMode::multiply},
                                              {.opacity = 0.5, .blendMode = LayerBlendMode::screen, .mask = gray(4, 4, 200)},
                                              {.opacity = 0.5, .blendMode = LayerBlendMode::hue}};
    for (const LayerRenderer::Options &option : options) {
        LayerRenderer::draw(solid(4, 4, qRgba(9, 9, 9, 255)), transform, transform.center(), painter, option);
        QCOMPARE(painter.worldTransform(), before);
        QCOMPARE(painter.opacity(), 1.0);
        QCOMPARE(painter.compositionMode(), QPainter::CompositionMode_SourceOver);
        QVERIFY(!painter.testRenderHint(QPainter::SmoothPixmapTransform));
        QVERIFY(!painter.testRenderHint(QPainter::Antialiasing));
    }
}

QTEST_MAIN(LayerRendererTests)
#include "LayerRendererTests.moc"
