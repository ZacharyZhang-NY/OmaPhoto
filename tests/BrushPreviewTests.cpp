#include "Rendering/LayerRenderer.h"
#include "RenderFixtures.h"
#include <QtTest>

namespace {
QImage coverage(int width, int height, int step)
{
    QImage image = gray(width, height, 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x)
            image.scanLine(y)[x] = uchar((x * step + y) % 256);
    }
    return image;
}

QImage surface(const std::function<void(QPainter &)> &draw)
{
    QImage result = noise(96, 96, 77);
    QPainter painter(&result);
    draw(painter);
    return result;
}
}

class BrushPreviewTests : public QObject {
    Q_OBJECT
private slots:
    void init();
    void tilesReplaceTheImageAndBlendOnce_data();
    void tilesReplaceTheImageAndBlendOnce();
    void aPaintedSourceDrawsFromItsTiles();
    void paintingAMaskShowsTheSourceThroughTheTiles();
    void paintPastTheSourceIsRevealed();
    void edgesStayHardAndSmoothLayersInterpolate();
    void aSourcePastTheGridIsCutThere_data();
    void aSourcePastTheGridIsCutThere();
    void aPreviewNeedsAPixelGrid();
};

void BrushPreviewTests::init()
{
    QTest::failOnWarning(QRegularExpression("saved states|Unbalanced save/restore"));
}

void BrushPreviewTests::tilesReplaceTheImageAndBlendOnce_data()
{
    QTest::addColumn<double>("scale");
    QTest::addColumn<LayerBlendMode>("blendMode");
    QTest::addColumn<double>("rotation");
    QTest::addColumn<bool>("flipX");
    QTest::addColumn<bool>("flipY");
    QTest::newRow("1x") << 1.0 << LayerBlendMode::normal << 0.0 << false << false;
    QTest::newRow("2x multiplied") << 2.0 << LayerBlendMode::multiply << 0.0 << false << false;
    QTest::newRow("a right angle, flipped across") << 1.0 << LayerBlendMode::normal << 90.0 << true << false;
    QTest::newRow("half a turn, flipped down") << 1.0 << LayerBlendMode::normal << 180.0 << false << true;
}

void BrushPreviewTests::tilesReplaceTheImageAndBlendOnce()
{
    QFETCH(double, scale);
    QFETCH(LayerBlendMode, blendMode);
    QFETCH(double, rotation);
    QFETCH(bool, flipX);
    QFETCH(bool, flipY);
    const QImage image = noise(40, 30, 1, 200);
    // Translucent tiles that touch: a double draw would show.
    const std::vector<BrushPatch> patches{{QRectF(8, 4, 16, 16), noise(16, 16, 2, 120)}, {QRectF(24, 4, 16, 16), noise(16, 16, 3, 60)}};
    LayerTransform transform = placedAt({6, 5}, {40, 30});
    transform.rotation = rotation;
    transform.flipX = flipX;
    transform.flipY = flipY;
    const QPointF center(6 + 20 * scale, 25 + 15 * scale);
    const LayerRenderer::Options options{.scale = scale, .opacity = 0.5, .blendMode = blendMode};
    const QImage expected = surface([&](QPainter &painter) { LayerRenderer::draw(composite(image, patches), transform, center, painter, options); });
    const QImage drawn = surface([&](QPainter &painter) {
        LayerRenderer::drawBrushPreview(image, transform, center, painter, options, {.patches = patches, .pixelWidth = 40, .pixelHeight = 30, .paintingMask = false});
    });
    QCOMPARE(drawn, expected);
    QVERIFY(drawn != surface([&](QPainter &painter) { LayerRenderer::draw(image, transform, center, painter, options); }));

    // Without an image the tiles stand alone.
    const QImage alone = surface([&](QPainter &painter) {
        LayerRenderer::drawBrushPreview(QImage(), transform, center, painter, options, {.patches = patches, .pixelWidth = 40, .pixelHeight = 30, .paintingMask = false});
    });
    QCOMPARE(alone, surface([&](QPainter &painter) {
        LayerRenderer::draw(composite(BrushRaster::context(40, 30, false), patches), transform, center, painter, options);
    }));
}

void BrushPreviewTests::aPaintedSourceDrawsFromItsTiles()
{
    const QImage base = noise(32, 24, 4);
    const std::vector<BrushPatch> committed{{QRectF(8, 8, 16, 16), noise(16, 16, 5, 90)}};
    // The raster's base covers a part of it only.
    const auto raster = std::make_shared<const RasterSnapshot>(40, 30, base, QRectF(4, 3, 32, 24), committed);
    const std::vector<BrushPatch> patches{{QRectF(20, 10, 16, 16), noise(16, 16, 6, 150)}};
    const LayerTransform transform = placedAt({6, 5}, {40, 30});
    const LayerRenderer::Options options{.opacity = 0.5};
    QImage finished = BrushRaster::context(40, 30, false);
    {
        QPainter painter(&finished);
        raster->draw(QRectF(0, 0, 40, 30), painter);
        for (const BrushPatch &patch : patches)
            BrushRaster::draw(patch.image, patch.rect, painter);
    }
    const QImage expected = surface([&](QPainter &painter) { LayerRenderer::draw(finished, transform, transform.center(), painter, options); });
    const QImage drawn = surface([&](QPainter &painter) {
        LayerRenderer::drawBrushPreview(QImage(), transform, transform.center(), painter, options,
                                        {.patches = patches, .pixelWidth = 40, .pixelHeight = 30, .paintingMask = false, .raster = raster});
    });
    QCOMPARE(drawn, expected);
    QVERIFY(!raster->hasMaterializedPixels());

    // A painted source may sit inside a grown grid.
    const QRectF sourceRect(5, 3, 40, 30);
    QImage grownFinished = BrushRaster::context(48, 36, false);
    {
        QPainter painter(&grownFinished);
        raster->draw(sourceRect, painter);
    }
    const LayerTransform grown = placedAt({6, 5}, {48, 36});
    const QImage placed = surface([&](QPainter &painter) {
        LayerRenderer::drawBrushPreview(QImage(), grown, grown.center(), painter, options,
                                        {.patches = {}, .pixelWidth = 48, .pixelHeight = 36, .paintingMask = false, .sourceRect = sourceRect, .raster = raster});
    });
    QCOMPARE(placed, surface([&](QPainter &painter) { LayerRenderer::draw(grownFinished, grown, grown.center(), painter, options); }));

    // A cropped raster's cut-away base stays hidden.
    const auto cropped = std::make_shared<const RasterSnapshot>(40, 30, base, QRectF(-4, -3, 32, 24), committed);
    QImage croppedFinished = BrushRaster::context(48, 36, false);
    {
        QPainter painter(&croppedFinished);
        cropped->draw(sourceRect, painter);
    }
    const QImage hidden = surface([&](QPainter &painter) {
        LayerRenderer::drawBrushPreview(QImage(), grown, grown.center(), painter, options,
                                        {.patches = {}, .pixelWidth = 48, .pixelHeight = 36, .paintingMask = false, .sourceRect = sourceRect, .raster = cropped});
    });
    QCOMPARE(hidden, surface([&](QPainter &painter) { LayerRenderer::draw(croppedFinished, grown, grown.center(), painter, options); }));
    QCOMPARE(hidden.pixel(6 + 3, 5 + 1), noise(96, 96, 77).pixel(6 + 3, 5 + 1));

    // A stand-in base replaces the raster's own.
    const QImage standIn = solid(32, 24, qRgba(0, 255, 0, 255));
    const QImage replaced = surface([&](QPainter &painter) {
        LayerRenderer::drawBrushPreview(QImage(), transform, transform.center(), painter, {},
                                        {.patches = {}, .pixelWidth = 40, .pixelHeight = 30, .paintingMask = false, .raster = raster, .rasterBase = standIn});
    });
    QCOMPARE(replaced.pixel(6 + 5, 5 + 4), qRgba(0, 255, 0, 255));
    QCOMPARE(replaced.pixel(6 + 35, 5 + 26), qRgba(0, 255, 0, 255));
    QCOMPARE(replaced.pixel(6 + 3, 5 + 4), noise(96, 96, 77).pixel(6 + 3, 5 + 4));
}

void BrushPreviewTests::paintingAMaskShowsTheSourceThroughTheTiles()
{
    const QImage image = noise(40, 30, 7);
    const QImage oldMask = coverage(40, 30, 3);
    const std::vector<BrushPatch> patches{{QRectF(8, 4, 16, 16), coverage(16, 16, 11)}};
    const LayerTransform transform = placedAt({6, 5}, {40, 30});
    QImage finishedMask = oldMask;
    {
        QPainter painter(&finishedMask);
        BrushRaster::draw(patches[0].image, patches[0].rect, painter);
    }
    const LayerRenderer::BrushPreview preview{.patches = patches, .pixelWidth = 40, .pixelHeight = 30, .paintingMask = true};
    const QImage drawn = surface([&](QPainter &painter) {
        LayerRenderer::drawBrushPreview(image, transform, transform.center(), painter, {.opacity = 0.5, .mask = oldMask}, preview);
    });
    QCOMPARE(drawn, surface([&](QPainter &painter) {
        LayerRenderer::draw(image, transform, transform.center(), painter, {.opacity = 0.5, .mask = finishedMask});
    }));

    // With no old mask the rest of the source shows.
    QImage fresh = gray(40, 30, 255);
    {
        QPainter painter(&fresh);
        BrushRaster::draw(patches[0].image, patches[0].rect, painter);
    }
    const QImage bare = surface([&](QPainter &painter) { LayerRenderer::drawBrushPreview(image, transform, transform.center(), painter, {}, preview); });
    QCOMPARE(bare, surface([&](QPainter &painter) { LayerRenderer::draw(image, transform, transform.center(), painter, {.mask = fresh}); }));

    // No source, nothing to show.
    const QImage nothing = surface([&](QPainter &painter) { LayerRenderer::drawBrushPreview(QImage(), transform, transform.center(), painter, {}, preview); });
    QCOMPARE(nothing, noise(96, 96, 77));
}

void BrushPreviewTests::paintPastTheSourceIsRevealed()
{
    // The old 24x20 layer sits at (10,6) in the grid.
    const QImage old = noise(24, 20, 8);
    const QImage oldMask = coverage(24, 20, 5);
    const QRectF sourceRect(10, 6, 24, 20);
    // The last tile hangs over the grid's corner.
    const std::vector<BrushPatch> patches{{QRectF(26, 12, 16, 16), noise(16, 16, 9, 200)}, {QRectF(0, 0, 8, 8), noise(8, 8, 10)},
                                          {QRectF(44, 28, 8, 8), noise(8, 8, 12)}};
    QImage finished = BrushRaster::context(52, 36, false), finishedMask = gray(52, 36, 255);
    {
        QPainter painter(&finished), masking(&finishedMask);
        BrushRaster::draw(old, sourceRect, painter);
        for (const BrushPatch &patch : patches)
            BrushRaster::draw(patch.image, patch.rect, painter);
        BrushRaster::draw(oldMask, sourceRect, masking);
    }
    const LayerTransform transform = placedAt({6, 5}, {48, 32});
    const LayerRenderer::Options options{.opacity = 0.7, .mask = oldMask};
    const QImage drawn = surface([&](QPainter &painter) {
        LayerRenderer::drawBrushPreview(old, transform, transform.center(), painter, options,
                                        {.patches = patches, .pixelWidth = 48, .pixelHeight = 32, .paintingMask = false, .sourceRect = sourceRect});
    });
    const LayerTransform grown = placedAt({6, 5}, {52, 36});
    QCOMPARE(drawn, surface([&](QPainter &painter) {
        LayerRenderer::draw(finished, grown, grown.center(), painter, {.opacity = 0.7, .mask = finishedMask});
    }));
}

void BrushPreviewTests::edgesStayHardAndSmoothLayersInterpolate()
{
    // Antialiased tile edges would blend twice where tiles meet.
    const std::vector<BrushPatch> patches{{QRectF(4, 4, 8, 8), solid(8, 8, qRgba(0, 0, 255, 255))}};
    const LayerTransform turned{.origin = {20, 20}, .size = {24, 16}, .rotation = 30, .sampling = LayerSampling::smooth};
    QImage target = BrushRaster::context(64, 56, false);
    {
        QPainter painter(&target);
        LayerRenderer::drawBrushPreview(solid(24, 16, qRgba(255, 0, 0, 255)), turned, turned.center(), painter, {},
                                        {.patches = patches, .pixelWidth = 24, .pixelHeight = 16, .paintingMask = false});
    }
    int opaque = 0;
    for (int y = 0; y < 56; ++y) {
        for (int x = 0; x < 64; ++x) {
            QVERIFY(qAlpha(target.pixel(x, y)) == 0 || qAlpha(target.pixel(x, y)) == 255);
            opaque += qAlpha(target.pixel(x, y)) == 255;
        }
    }
    QVERIFY(std::abs(opaque - 24 * 16) <= 12);

    // The layer's own quality resamples the pixels.
    QImage steps = BrushRaster::context(2, 1, false);
    steps.setPixel(0, 0, qRgba(0, 0, 0, 255));
    steps.setPixel(1, 0, qRgba(255, 255, 255, 255));
    const auto enlarged = [&](LayerSampling sampling) {
        const LayerTransform transform{.origin = {0, 0}, .size = {2, 1}, .sampling = sampling};
        QImage result = BrushRaster::context(16, 8, false);
        QPainter painter(&result);
        LayerRenderer::drawBrushPreview(steps, transform, QPointF(8, 4), painter, {.scale = 8},
                                        {.patches = {}, .pixelWidth = 2, .pixelHeight = 1, .paintingMask = false});
        return result;
    };
    QCOMPARE(enlarged(LayerSampling::nearest).pixel(7, 4), qRgba(0, 0, 0, 255));
    QCOMPARE(enlarged(LayerSampling::nearest).pixel(8, 4), qRgba(255, 255, 255, 255));
    const int blended = qRed(enlarged(LayerSampling::smooth).pixel(7, 4));
    QVERIFY2(blended > 90 && blended < 130, qPrintable(QString::number(blended)));
}

void BrushPreviewTests::aSourcePastTheGridIsCutThere_data()
{
    QTest::addColumn<bool>("paintedSource");
    QTest::newRow("an image") << false;
    QTest::newRow("a painted source") << true;
}

void BrushPreviewTests::aSourcePastTheGridIsCutThere()
{
    QFETCH(bool, paintedSource);
    // The source passes the grid; a tile overhangs its right.
    const QImage image = noise(40, 30, 13);
    const auto raster = std::make_shared<const RasterSnapshot>(40, 30, image, QRectF(0, 0, 40, 30),
        std::vector<BrushPatch>{BrushPatch{QRectF(20, 10, 16, 16), noise(16, 16, 15, 90)}});
    const QRectF sourceRect(-6, -4, 40, 30);
    const std::vector<BrushPatch> patches{{QRectF(28, 0, 8, 8), noise(8, 8, 14)}};
    QImage finished = BrushRaster::context(36, 24, false);
    {
        QPainter painter(&finished);
        painter.setClipRect(QRectF(0, 0, 32, 24));
        if (paintedSource)
            raster->draw(sourceRect, painter);
        else
            BrushRaster::draw(image, sourceRect, painter);
        painter.setClipping(false);
        BrushRaster::draw(patches[0].image, patches[0].rect, painter);
    }
    const LayerTransform transform = placedAt({20, 16}, {32, 24}), wide = placedAt({20, 16}, {36, 24});
    const QImage drawn = surface([&](QPainter &painter) {
        LayerRenderer::drawBrushPreview(paintedSource ? QImage() : image, transform, transform.center(), painter, {.opacity = 0.5},
                                        {.patches = patches, .pixelWidth = 32, .pixelHeight = 24, .paintingMask = false, .sourceRect = sourceRect,
                                         .raster = paintedSource ? raster : nullptr});
    });
    QCOMPARE(drawn, surface([&](QPainter &painter) { LayerRenderer::draw(finished, wide, wide.center(), painter, {.opacity = 0.5}); }));
    // Left of the grid, and right below the tile: untouched.
    QCOMPARE(drawn.pixel(17, 20), noise(96, 96, 77).pixel(17, 20));
    QCOMPARE(drawn.pixel(20 + 33, 16 + 12), noise(96, 96, 77).pixel(20 + 33, 16 + 12));
    QVERIFY(drawn.pixel(20 + 30, 16 + 12) != noise(96, 96, 77).pixel(20 + 30, 16 + 12));
}

void BrushPreviewTests::aPreviewNeedsAPixelGrid()
{
    QImage target = BrushRaster::context(8, 8, false);
    QPainter painter(&target);
    const LayerTransform transform = placedAt({0, 0}, {8, 8});
    QVERIFY_EXCEPTION_THROWN(LayerRenderer::drawBrushPreview(QImage(), transform, transform.center(), painter, {},
                                                             {.patches = {}, .pixelWidth = 0, .pixelHeight = 8, .paintingMask = false}),
                             std::logic_error);
    QVERIFY_EXCEPTION_THROWN(LayerRenderer::drawBrushPreview(QImage(), transform, transform.center(), painter, {},
                                                             {.patches = {}, .pixelWidth = 8, .pixelHeight = 0, .paintingMask = false}),
                             std::logic_error);
}

QTEST_MAIN(BrushPreviewTests)
#include "BrushPreviewTests.moc"
