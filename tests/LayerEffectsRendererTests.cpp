#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include "Document/LayerEffects+Renderer.h"
#include "IO/ImageExporter.h"
#include "Rendering/LayerEffectsKernel.h"
#include "SessionFixtures.h"
#include <QPainter>
#include <QtTest>
#include <cmath>

// Swift's LayerEffectsRenderer and the effects in the export.
namespace {
QImage filled(int width, int height, const QColor &colour)
{
    QImage image = BrushRaster::context(width, height, false);
    image.fill(colour);
    return image;
}

LayerEffects stroke(double size, bool inside = false)
{
    LayerEffects effects;
    effects.stroke = StrokeEffect{.size = size, .red = 1, .green = 1, .blue = 0, .opacity = 1, .inside = inside};
    return effects;
}

std::array<int, 4> pixel(const QImage &image, int x, int y)
{
    const QImage bytes = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    const uchar *at = bytes.constScanLine(y) + x * 4;
    return {at[0], at[1], at[2], at[3]};
}

QImage exported(const EditorSession &session)
{
    return ImageExporter::render(session.projectSnapshot().value()).image;
}
}

class LayerEffectsRendererTests : public QObject {
    Q_OBJECT
private slots:
    void theMarginHoldsWhatReachesOut();
    void thePlacementGrowsAboutTheCentre();
    void renderingPadsMasksThenRunsTheKernel();
    void theCacheKeepsEightWithinItsBudget();
    void theExportAndTheCompositeDrawTheEffects();
};

void LayerEffectsRendererTests::theMarginHoldsWhatReachesOut()
{
    QCOMPARE(LayerEffectsRenderer::margin(LayerEffects()), 2.0);
    QCOMPARE(LayerEffectsRenderer::margin(stroke(4.2)), 7.0);
    // An inside stroke, overlay or inner shadow reach nowhere.
    LayerEffects within = stroke(40, true);
    within.colorOverlay = ColorOverlayEffect();
    within.innerShadow = InnerShadowEffect{.distance = 90, .blur = 90};
    QCOMPARE(LayerEffectsRenderer::margin(within), 2.0);
    // A shadow's distance and three blurs; the larger reach wins.
    LayerEffects cast = stroke(10);
    cast.shadow = ShadowEffect{.distance = 5, .blur = 2.5};
    QCOMPARE(LayerEffectsRenderer::margin(cast), 15.0);
    cast.shadow.value().blur = 4;
    QCOMPARE(LayerEffectsRenderer::margin(cast), 19.0);
    // Switched off, it takes no room.
    cast.shadow.value().enabled = false;
    cast.stroke.value().enabled = false;
    QCOMPARE(LayerEffectsRenderer::margin(cast), 2.0);
}

void LayerEffectsRendererTests::thePlacementGrowsAboutTheCentre()
{
    const LayerTransform transform{.origin = QPointF(10, 20), .size = QSizeF(40, 20), .rotation = 30, .flipX = true, .sampling = LayerSampling::nearest};
    const LayerTransform grown = LayerEffectsRenderer::placed(transform, QImage(60, 30, QImage::Format_Alpha8), 5);
    // The 50 by 20 inner pixels keep the layer's box.
    QCOMPARE(grown.size, QSizeF(48, 30));
    QCOMPARE(grown.center(), transform.center());
    QVERIFY(grown.rotation == 30 && grown.flipX && !grown.flipY && grown.sampling == LayerSampling::nearest);
    // No pixels inside the margin: the transform as it was.
    QVERIFY(LayerEffectsRenderer::placed(transform, QImage(10, 30, QImage::Format_Alpha8), 5) == transform);
    QVERIFY(LayerEffectsRenderer::placed(transform, QImage(60, 10, QImage::Format_Alpha8), 5) == transform);
}

void LayerEffectsRendererTests::renderingPadsMasksThenRunsTheKernel()
{
    const QImage image = filled(20, 10, QColor(0, 0, 255));
    const LayerEffectsRenderer::Rendered made = LayerEffectsRenderer::render(image, std::nullopt, stroke(3));
    QCOMPARE(made.inset, 5.0);
    QCOMPARE(made.image.size(), QSize(30, 20));
    // The kernel over the pixels padded by the inset.
    QImage padded = filled(30, 20, Qt::transparent);
    {
        QPainter painter(&padded);
        painter.drawImage(QRectF(5, 5, 20, 10), image);
    }
    QVERIFY(made.image == LayerEffectsKernel::render(padded, stroke(3)));
    QVERIFY((pixel(made.image, 3, 10) == std::array{255, 255, 0, 255}) && (pixel(made.image, 1, 10)[3] == 0));
    // A mask hides pixels first: the stroke follows what shows.
    QImage mask(20, 10, QImage::Format_Grayscale8);
    mask.fill(0);
    for (int y = 0; y < 10; ++y)
        std::fill(mask.scanLine(y), mask.scanLine(y) + 10, uchar(255));
    const LayerEffectsRenderer::Rendered masked = LayerEffectsRenderer::render(image, mask, stroke(3));
    QVERIFY((pixel(masked.image, 10, 10) == std::array{0, 0, 255, 255}) && (pixel(masked.image, 20, 10) == std::array{0, 0, 0, 0}));
    QVERIFY((pixel(masked.image, 16, 10) == std::array{255, 255, 0, 255}) && (pixel(masked.image, 18, 10)[3] == 0));
    // A small mask scales smoothly, as CoreGraphics clips.
    QImage halves(2, 1, QImage::Format_Grayscale8);
    halves.scanLine(0)[0] = 0;
    halves.scanLine(0)[1] = 255;
    const QImage soft = LayerEffectsRenderer::render(filled(64, 16, QColor(0, 0, 255)), halves, stroke(0)).image;
    const int left = pixel(soft, 2 + 31, 10)[3], right = pixel(soft, 2 + 32, 10)[3];
    QVERIFY2(left > 100 && left < right && right < 160, qPrintable(QStringLiteral("%1 %2").arg(left).arg(right)));
    QVERIFY(pixel(soft, 2, 10)[3] == 0 && pixel(soft, 65, 10)[3] == 255);
    // Switched-off effects are none; invalid or vast ones refuse.
    LayerEffects hidden = stroke(3);
    hidden.stroke.value().enabled = false;
    QCOMPARE(LayerEffectsRenderer::render(image, std::nullopt, hidden).image.size(), QSize(24, 14));
    // A switched-off effect is not judged.
    LayerEffects wild = stroke(600);
    wild.stroke.value().enabled = false;
    QVERIFY(LayerEffectsRenderer::render(image, std::nullopt, wild).image == LayerEffectsRenderer::render(image, std::nullopt, hidden).image);
    try {
        LayerEffectsRenderer::render(image, std::nullopt, stroke(501));
        QFAIL("an invalid stroke rendered");
    } catch (const ProjectError &error) {
        QCOMPARE(error.kind, ProjectError::Kind::invalid);
    }
    LayerEffects shadow;
    shadow.shadow = ShadowEffect{.distance = 10};
    try {
        LayerEffectsRenderer::render(QImage(14'140, 14'140, QImage::Format_Mono), std::nullopt, shadow);
        QFAIL("a vast layer rendered");
    } catch (const ProjectError &error) {
        QCOMPARE(error.kind, ProjectError::Kind::tooLarge);
    }
}

void LayerEffectsRendererTests::theCacheKeepsEightWithinItsBudget()
{
    const QImage image = filled(12, 8, Qt::red);
    // Nothing to draw, nothing made: the layer draws as is.
    QVERIFY(!LayerEffectsRenderer::cached(image, std::nullopt, std::nullopt));
    QVERIFY(!LayerEffectsRenderer::cached(image, std::nullopt, LayerEffects()));
    LayerEffects hidden = stroke(2);
    hidden.stroke.value().enabled = false;
    QVERIFY(!LayerEffectsRenderer::cached(image, std::nullopt, hidden));
    QVERIFY(!LayerEffectsRenderer::cached(image, std::nullopt, stroke(600)));
    // The same pixels, mask and effects: the same result.
    const auto first = LayerEffectsRenderer::cached(image, std::nullopt, stroke(2)).value();
    QCOMPARE(LayerEffectsRenderer::cached(image, std::nullopt, stroke(2)).value().image.cacheKey(), first.image.cacheKey());
    QCOMPARE(first.inset, 4.0);
    // Switched-off effects are left out of the key.
    LayerEffects extra = stroke(2);
    extra.shadow = ShadowEffect{.enabled = false};
    QCOMPARE(LayerEffectsRenderer::cached(image, std::nullopt, extra).value().image.cacheKey(), first.image.cacheKey());
    // Equal pixels elsewhere, or another mask: made anew.
    const QImage twin = image.copy();
    QVERIFY(LayerEffectsRenderer::cached(twin, std::nullopt, stroke(2)).value().image.cacheKey() != first.image.cacheKey());
    QImage mask(12, 8, QImage::Format_Grayscale8);
    mask.fill(255);
    const auto masked = LayerEffectsRenderer::cached(image, mask, stroke(2)).value();
    QVERIFY(masked.image.cacheKey() != first.image.cacheKey() && masked.image == first.image);
    QCOMPARE(LayerEffectsRenderer::cached(image, mask, stroke(2)).value().image.cacheKey(), masked.image.cacheKey());
    // Another mask of the same size is another key.
    QImage half = mask.copy();
    half.fill(128);
    QVERIFY(LayerEffectsRenderer::cached(image, half, stroke(2)).value().image.cacheKey() != masked.image.cacheKey());
    // Eight are kept: the ninth pushes the first out.
    for (int size = 3; size <= 6; ++size)
        LayerEffectsRenderer::cached(image, std::nullopt, stroke(size));
    QCOMPARE(LayerEffectsRenderer::cached(image, std::nullopt, stroke(2)).value().image.cacheKey(), first.image.cacheKey());
    LayerEffectsRenderer::cached(image, std::nullopt, stroke(7));
    QVERIFY(LayerEffectsRenderer::cached(image, std::nullopt, stroke(2)).value().image.cacheKey() != first.image.cacheKey());
    // Two 32 MiB entries fill it; a third evicts one.
    const QImage one = filled(2'000, 2'000, Qt::red), two = filled(2'000, 2'000, Qt::green), three = filled(2'000, 2'000, Qt::blue);
    const qint64 kept = LayerEffectsRenderer::cached(one, std::nullopt, stroke(1)).value().image.cacheKey();
    const qint64 second = LayerEffectsRenderer::cached(two, std::nullopt, stroke(1)).value().image.cacheKey();
    QCOMPARE(LayerEffectsRenderer::cached(one, std::nullopt, stroke(1)).value().image.cacheKey(), kept);
    LayerEffectsRenderer::cached(three, std::nullopt, stroke(1));
    QCOMPARE(LayerEffectsRenderer::cached(two, std::nullopt, stroke(1)).value().image.cacheKey(), second);
    QVERIFY(LayerEffectsRenderer::cached(one, std::nullopt, stroke(1)).value().image.cacheKey() != kept);
    // Past 64 MiB: made, not kept, nothing evicted.
    const qint64 small = LayerEffectsRenderer::cached(image, std::nullopt, stroke(5)).value().image.cacheKey();
    const QImage big = filled(3'000, 3'000, Qt::red);
    const auto large = LayerEffectsRenderer::cached(big, std::nullopt, stroke(1)).value();
    QVERIFY(LayerEffectsRenderer::cached(big, std::nullopt, stroke(1)).value().image.cacheKey() != large.image.cacheKey());
    QCOMPARE(LayerEffectsRenderer::cached(image, std::nullopt, stroke(5)).value().image.cacheKey(), small);
    // Too large to make: the layer draws bare.
    LayerEffects shadow;
    shadow.shadow = ShadowEffect{.distance = 10};
    QVERIFY(!LayerEffectsRenderer::cached(QImage(14'140, 14'140, QImage::Format_Mono), std::nullopt, shadow));
}

void LayerEffectsRendererTests::theExportAndTheCompositeDrawTheEffects()
{
    EditorSession session;
    session.createDocument(40, 30);
    session.insert(ImportedImage(filled(10, 10, QColor(0, 0, 255)), QImage(), QStringLiteral("Square")), QPointF(20, 15));
    LayerEffects effects = stroke(2);
    effects.shadow = ShadowEffect{.angle = 90, .distance = 6, .blur = 0, .red = 1, .green = 0, .blue = 0, .opacity = 1};
    session.setEffects(effects);
    const QImage image = exported(session);
    // Square 15..25 by 10..20; stroke two out; shadow six down.
    QVERIFY((pixel(image, 20, 15) == std::array{0, 0, 255, 255}) && (pixel(image, 13, 15) == std::array{255, 255, 0, 255}));
    QVERIFY((pixel(image, 20, 24) == std::array{255, 0, 0, 255}) && (pixel(image, 20, 8) == std::array{255, 255, 0, 255}));
    QVERIFY(pixel(image, 20, 7)[3] == 0 && pixel(image, 12, 15)[3] == 0 && pixel(image, 20, 27)[3] == 0);
    // A mask hides pixels, the effects follow; opacity fades all.
    session.addLayerMask(false);
    QVERIFY(pixel(exported(session), 13, 15)[3] == 0 && pixel(exported(session), 20, 24)[3] == 0);
    session.deleteLayerMask();
    session.setLayerOpacity(0.5);
    QVERIFY(std::abs(pixel(exported(session), 13, 15)[3] - 128) <= 1);
    // Its blend mode applies to the effects too.
    session.setLayerOpacity(1);
    const QUuid square = session.activeLayerID().value();
    session.insert(ImportedImage(filled(40, 30, QColor(128, 128, 128)), QImage(), QStringLiteral("Gray")));
    session.moveActiveLayer(-1);
    session.selectLayer(square);
    session.setLayerBlendMode(LayerBlendMode::multiply);
    QVERIFY((pixel(exported(session), 13, 15) == std::array{128, 128, 0, 255}));
    // A folder's mask clips the effects inside it.
    session.groupSelectedLayers();
    session.addLayerMask(false);
    QVERIFY((pixel(exported(session), 13, 15) == std::array{128, 128, 128, 255}));
    // The composite draws what the export does.
    QImage live = BrushRaster::context(40, 30, false);
    live.fill(0);
    {
        QPainter painter(&live);
        session.drawLiveComposite(session.document().value(), painter);
    }
    QVERIFY(live == exported(session));
    session.deleteLayerMask();
    live.fill(0);
    {
        QPainter painter(&live);
        session.drawLiveComposite(session.document().value(), painter);
    }
    QVERIFY(live == exported(session) && pixel(live, 13, 15) == (std::array{128, 128, 0, 255}));
}

QTEST_GUILESS_MAIN(LayerEffectsRendererTests)
#include "LayerEffectsRendererTests.moc"
