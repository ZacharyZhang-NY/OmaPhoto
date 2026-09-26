#include "AddressSpaceLimit.h"
#include "IO/ImageExporter.h"
#include "LiveMaskFixtures.h"
#include <QtTest>

class LiveMaskRendererTests : public QObject {
    Q_OBJECT
private slots:
    void init();
    void aHiddenBlackSourceSuppliesAlphaAndRasterMasksMultiply();
    void movingTheSourceMovesCoverageAndChainsMultiply();
    void cyclesAndEndlessChainsDrawNothing();
    void coverageIsBuiltOncePerSource();
    void anOuterClipMultipliesIn();
    void aScaledPainterKeepsCoverageInDevicePixels();
    void pastThePixelBudgetStacksAndCoverageGiveWay();
    void rectangularDevicesKeepRowsAndColumnsApart();
    void drawOwnCannotLeakPainterState();
    void anAdjustmentAdjustsWithinItsBudgetAndStack();
    void anAdjustmentTakesAnyImageTarget();
    void surfacesThatCannotBeAllocatedAreLoggedAndTheRendererStaysUsable();
    void aClipThatCannotBeCombinedThrows();
};

void LiveMaskRendererTests::init()
{
    QTest::failOnWarning(QRegularExpression("saved states|Unbalanced save/restore"));
}

void LiveMaskRendererTests::aHiddenBlackSourceSuppliesAlphaAndRasterMasksMultiply()
{
    Scene scene;
    const QUuid target = scene.add(placed(asset({255, 255, 255, 255})));
    const QUuid source = scene.add(placed(asset({255, 0, 128, 255}, 0)));
    scene.layers[target].source = source;
    // The source is hidden: only the target is drawn.
    QCOMPARE(alphas(scene.render({target})), (std::array<int, 4>{255, 0, 128, 255}));
    QCOMPARE(reds(scene.render({target})), (std::array<int, 4>{255, 0, 128, 255}));
    scene.layers[target].opacity = 0.5;
    QCOMPARE(alphas(scene.render({target})), (std::array<int, 4>{127, 0, 64, 127}));
    scene.layers[target].mask = gray(2, 2, 128);
    QCOMPARE(alphas(scene.render({target})), (std::array<int, 4>{64, 0, 32, 64}));
    // The source's mask counts; its colour and visibility do not.
    scene.layers[target].opacity = 1;
    scene.layers[target].mask = QImage();
    scene.layers[source].mask = gray(2, 2, 128);
    QCOMPARE(alphas(scene.render({target})), (std::array<int, 4>{128, 0, 64, 128}));
}

void LiveMaskRendererTests::movingTheSourceMovesCoverageAndChainsMultiply()
{
    Scene scene;
    const QUuid target = scene.add(placed(asset({255, 255, 255, 255})));
    const QUuid source = scene.add(placed(asset({255, 0, 128, 255}, 0)));
    scene.layers[target].source = source;
    scene.layers[source].transform.origin.rx() += 1;
    QCOMPARE(alphas(scene.render({target})), (std::array<int, 4>{0, 255, 0, 128}));
    scene.layers[source].transform.origin.rx() -= 1;
    const QUuid second = scene.add(placed(asset({0, 255, 255, 255})));
    scene.layers[source].source = second;
    QCOMPARE(alphas(scene.render({target})), (std::array<int, 4>{0, 0, 128, 255}));
}

void LiveMaskRendererTests::cyclesAndEndlessChainsDrawNothing()
{
    Scene scene;
    const QUuid first = scene.add(placed(asset({255, 255, 255, 255})));
    const QUuid second = scene.add(placed(asset({255, 255, 255, 255})));
    scene.layers[first].source = second;
    scene.layers[second].source = first;
    QCOMPARE(scene.render({first, second}), BrushRaster::context(2, 2, false));
    // The cycle ends at once, not at the depth limit.
    QCOMPARE(scene.drawn[first], 1);
    QCOMPARE(scene.drawn[second], 2);

    // A chain past 256 sources stops instead of recursing on.
    Scene chain;
    std::vector<QUuid> ids;
    for (int index = 0; index < 300; ++index) {
        ids.push_back(chain.add(placed(asset({255, 255, 255, 255}))));
        if (index > 0)
            chain.layers[ids[index - 1]].source = ids[index];
    }
    QCOMPARE(chain.render({ids[0]}), BrushRaster::context(2, 2, false));
    // 255 sources deep still draws.
    QCOMPARE(alphas(chain.render({ids[44]})), (std::array<int, 4>{255, 255, 255, 255}));
}

void LiveMaskRendererTests::coverageIsBuiltOncePerSource()
{
    Scene scene;
    const QUuid source = scene.add(placed(asset({255, 0, 128, 255}, 0)));
    const QUuid first = scene.add(placed(asset({255, 255, 255, 255})));
    const QUuid second = scene.add(placed(asset({255, 255, 255, 255})));
    scene.layers[first].source = source;
    scene.layers[second].source = source;
    LiveMaskRenderer live = scene.renderer();
    QImage surface = BrushRaster::context(2, 2, false);
    QPainter painter(&surface);
    live.draw(first, painter);
    live.draw(second, painter);
    live.draw(first, painter);
    painter.end();
    QCOMPARE(scene.drawn[source], 1);
    QCOMPARE(scene.drawn[first], 2);
    QCOMPARE(alphas(surface), (std::array<int, 4>{255, 0, 224, 255}));
}

void LiveMaskRendererTests::anOuterClipMultipliesIn()
{
    Scene scene;
    const QUuid base = scene.add(placed(asset({255, 128, 32, 0}, 0)));
    const QUuid top = scene.add(placed(asset({255, 255, 255, 255})));
    const QUuid target = scene.add(placed(asset({255, 255, 255, 255})));
    const QUuid source = scene.add(placed(asset({255, 0, 128, 255}, 0)));
    scene.layers[top].source = base;
    scene.layers[target].source = source;
    QImage folder(2, 2, QImage::Format_Alpha8);
    folder.fill(128);
    LiveMaskRenderer live = scene.renderer();
    live.prepareStacks({base, top}, [](QUuid) { return std::nullopt; }, [](QUuid) { return LayerBlendMode::normal; });
    QImage stack = BrushRaster::context(2, 2, false), linked = stack, plain = stack;
    {
        QPainter painter(&stack);
        live.drawComposite(base, painter, folder);
    }
    QCOMPARE(alphas(stack), (std::array<int, 4>{128, 64, 16, 0}));
    {
        QPainter painter(&linked);
        live.drawComposite(target, painter, folder);
    }
    QCOMPARE(alphas(linked), (std::array<int, 4>{128, 0, 64, 128}));
    {
        QPainter painter(&plain);
        live.drawComposite(source, painter, folder);
    }
    QCOMPARE(alphas(plain), (std::array<int, 4>{128, 0, 64, 128}));
}

void LiveMaskRendererTests::aScaledPainterKeepsCoverageInDevicePixels()
{
    Scene scene;
    const QUuid base = scene.add(placed(asset({255, 128, 32, 0}, 0)));
    const QUuid top = scene.add(placed(asset({255, 255, 255, 255})));
    const QUuid target = scene.add(placed(asset({255, 255, 255, 255})));
    const QUuid source = scene.add(placed(asset({255, 0, 128, 255}, 0)));
    scene.layers[top].source = base;
    scene.layers[target].source = source;
    for (const bool stacked : {true, false}) {
        LiveMaskRenderer live = scene.renderer();
        live.prepareStacks({base, top}, [](QUuid) { return std::nullopt; }, [](QUuid) { return LayerBlendMode::normal; });
        QImage surface = BrushRaster::context(6, 6, false);
        QPainter painter(&surface);
        painter.translate(1, 2);
        painter.scale(2, 2);
        live.drawComposite(stacked ? base : target, painter);
        painter.end();
        // Each layer pixel covers 2x2 device pixels, offset (1,2).
        const std::array<int, 4> expected = stacked ? std::array<int, 4>{255, 128, 32, 0} : std::array<int, 4>{255, 0, 128, 255};
        for (int index = 0; index < 4; ++index) {
            // Full-strength red: the child lies where the base does.
            QCOMPARE(surface.pixel(1 + index % 2 * 2, 2 + index / 2 * 2), qRgba(expected[index], 0, 0, expected[index]));
            QCOMPARE(surface.pixel(2 + index % 2 * 2, 3 + index / 2 * 2), qRgba(expected[index], 0, 0, expected[index]));
        }
        QCOMPARE(qAlpha(surface.pixel(0, 2)), 0);
        QCOMPARE(qAlpha(surface.pixel(5, 5)), 0);
    }
}

void LiveMaskRendererTests::pastThePixelBudgetStacksAndCoverageGiveWay()
{
    Scene scene;
    const QUuid base = scene.add(placed(asset({255, 128, 32, 0}, 0)));
    const QUuid top = scene.add(placed(asset({255, 255, 255, 255})));
    scene.layers[top].source = base;
    LiveMaskRenderer live = scene.renderer(3);
    live.prepareStacks({base, top}, [](QUuid) { return std::nullopt; }, [](QUuid) { return LayerBlendMode::normal; });
    QImage surface = BrushRaster::context(2, 2, false);
    QPainter painter(&surface);
    // No group: the base draws alone; its child lacks coverage.
    live.drawComposite(base, painter);
    QTest::ignoreMessage(QtWarningMsg, "clip coverage passes its pixel budget: 2 x 2");
    live.drawComposite(top, painter);
    painter.end();
    QCOMPARE(alphas(surface), (std::array<int, 4>{255, 128, 32, 0}));
    QCOMPARE(reds(surface), (std::array<int, 4>{0, 0, 0, 0}));
    QCOMPARE(scene.drawn[top], 0);

    // A budget of exactly the device's four pixels is enough.
    LiveMaskRenderer fitting = scene.renderer(4);
    fitting.prepareStacks({base, top}, [](QUuid) { return std::nullopt; }, [](QUuid) { return LayerBlendMode::normal; });
    QImage shared = BrushRaster::context(2, 2, false);
    QPainter second(&shared);
    fitting.drawComposite(base, second);
    second.end();
    QCOMPARE(reds(shared), (std::array<int, 4>{255, 128, 32, 0}));
}

void LiveMaskRendererTests::rectangularDevicesKeepRowsAndColumnsApart()
{
    // A 3x2 layer in a 4x3 device's far corner.
    const int alpha[6] = {255, 200, 150, 100, 50, 25};
    const Layer soft{.image = wide({255, 200, 150, 100, 50, 25}), .transform = placedAt({1, 1}, {3, 2})};
    QImage surface(4, 3, QImage::Format_RGBA8888_Premultiplied);
    surface.fill(0);
    // Half grey over full red, then the base's alpha.
    Scene stack;
    const QUuid base = stack.add(soft);
    const QUuid top = stack.add({.image = solid(3, 2, qRgba(128, 128, 128, 255)), .transform = placedAt({1, 1}, {3, 2}), .opacity = 0.5});
    stack.layers[top].source = base;
    const QImage grouped = stack.render({base, top}, surface);
    for (int index = 0; index < 6; ++index) {
        const int a = alpha[index];
        QCOMPARE(grouped.pixel(1 + index % 3, 1 + index / 3), qRgba((192 * a + 127) / 255, (64 * a + 127) / 255, (64 * a + 127) / 255, a));
    }
    QCOMPARE(grouped.pixel(0, 0), qRgba(0, 0, 0, 0));
    // A hidden source.
    Scene linked;
    const QUuid target = linked.add({.image = wide({255, 255, 255, 255, 255, 255}), .transform = placedAt({1, 1}, {3, 2})});
    const QUuid source = linked.add(soft);
    linked.layers[target].source = source;
    const QImage through = linked.render({target}, surface);
    for (int index = 0; index < 6; ++index)
        QCOMPARE(through.pixel(1 + index % 3, 1 + index / 3), qRgba(alpha[index], 0, 0, alpha[index]));
    QCOMPARE(through.pixel(0, 2), qRgba(0, 0, 0, 0));
}

void LiveMaskRendererTests::drawOwnCannotLeakPainterState()
{
    const QImage red = solid(2, 2, qRgba(255, 0, 0, 255));
    const QUuid plain = QUuid::createUuid(), linked = QUuid::createUuid(), source = QUuid::createUuid(), failing = QUuid::createUuid();
    // Every draw moves and clips the painter, as callbacks might.
    LiveMaskRenderer live([&](QUuid id) { return id == linked ? std::optional(source) : std::nullopt; },
                          [&](QUuid id, QPainter &painter, const QImage &clip) {
                              if (id == failing)
                                  painter.translate(3, 3);
                              if (id == failing)
                                  throw std::runtime_error("drawOwn failed");
                              LayerRenderer::draw(red, placedAt({0, 0}, {2, 2}), QPointF(1, 1), painter, {.clip = clip});
                              painter.translate(2, 0);
                              painter.setClipRect(QRectF(0, 0, 1, 1));
                          });
    QImage surface = BrushRaster::context(6, 2, false);
    QPainter painter(&surface);
    live.draw(plain, painter);
    QCOMPARE(painter.worldTransform(), QTransform());
    QVERIFY(!painter.hasClipping());
    live.draw(linked, painter);
    QCOMPARE(painter.worldTransform(), QTransform());
    QVERIFY(!painter.hasClipping());
    QVERIFY_EXCEPTION_THROWN(live.draw(failing, painter), std::runtime_error);
    QCOMPARE(painter.worldTransform(), QTransform());
    painter.end();
    QCOMPARE(surface.pixel(1, 1), qRgba(255, 0, 0, 255));
    QCOMPARE(surface.pixel(2, 0), qRgba(0, 0, 0, 0));
}

void LiveMaskRendererTests::surfacesThatCannotBeAllocatedAreLoggedAndTheRendererStaysUsable()
{
    Scene scene;
    const QUuid base = scene.add(placed(asset({255, 128, 32, 0}, 0)));
    const QUuid top = scene.add(placed(asset({255, 255, 255, 255})));
    scene.layers[top].source = base;
    LiveMaskRenderer live = scene.renderer();
    live.prepareStacks({base, top}, [](QUuid) { return std::nullopt; }, [](QUuid) { return LayerBlendMode::normal; });
    // A device whose colour surfaces need 256 MB each.
    QImage surface(8192, 8192, QImage::Format_Alpha8);
    surface.fill(0);
    QPainter painter(&surface);
    std::optional<AddressSpaceLimit> limit(std::in_place, 96 * 1024 * 1024);
    QTest::ignoreMessage(QtWarningMsg, "a clipping stack could not be allocated: QSize(8192, 8192)");
    live.drawComposite(base, painter);
    QTest::ignoreMessage(QtWarningMsg, "clip coverage could not be allocated: QSize(8192, 8192)");
    live.drawComposite(top, painter);
    limit.reset();
    QCOMPARE(scene.drawn[base], 1);
    QCOMPARE(scene.drawn[top], 0);
    // With room again the child draws through the base.
    live.drawComposite(top, painter);
    painter.end();
    QCOMPARE(scene.drawn[top], 1);
    QCOMPARE(int(surface.constScanLine(0)[1]), 192);
}

void LiveMaskRendererTests::aClipThatCannotBeCombinedThrows()
{
    Scene scene;
    const QUuid target = scene.add(placed(asset({255, 255, 255, 255})));
    const QUuid source = scene.add(placed(asset({255, 0, 128, 255}, 0)));
    scene.layers[target].source = source;
    LiveMaskRenderer live = scene.renderer();
    QImage surface(8192, 8192, QImage::Format_Alpha8), folder(8192, 8192, QImage::Format_Alpha8);
    surface.fill(0);
    folder.fill(255);
    QPainter painter(&surface);
    // The first draw caches the source's coverage.
    live.draw(target, painter);
    std::optional<AddressSpaceLimit> limit(std::in_place, 32 * 1024 * 1024);
    std::optional<ExportError::Kind> thrown;
    try {
        live.draw(target, painter, folder);
    } catch (const ExportError &error) {
        thrown = error.kind;
    }
    limit.reset();
    painter.end();
    QCOMPARE(thrown, std::optional(ExportError::Kind::render));
    QCOMPARE(scene.drawn[target], 1);
}

void LiveMaskRendererTests::anAdjustmentAdjustsWithinItsBudgetAndStack()
{
    Scene scene;
    const QUuid base = scene.add(placed(asset({255, 255, 255, 255})));
    const QUuid levels = scene.add(Layer());
    LayerAdjustment darker{AdjustmentKind::levels};
    darker.levels.ranges[0].outputWhite = 0;
    const auto render = [&](qint64 budget) {
        LiveMaskRenderer live = scene.renderer(budget);
        live.adjustment = [&](QUuid id) { return id == levels ? std::optional(darker) : std::nullopt; };
        live.prepareStacks({base, levels}, [&](QUuid id) { return scene.layers[id].parent; }, [](QUuid) { return LayerBlendMode::normal; });
        QImage surface = BrushRaster::context(2, 2, false);
        QPainter painter(&surface);
        live.drawComposite(base, painter);
        live.drawComposite(levels, painter);
        painter.end();
        return surface;
    };
    // Four pixels fit: red turns black; three warn instead.
    QCOMPARE(reds(render(4)), (std::array<int, 4>{0, 0, 0, 0}));
    QTest::ignoreMessage(QtWarningMsg, "an adjustment passes its pixel budget: QSize(2, 2)");
    QCOMPARE(reds(render(3)), (std::array<int, 4>{255, 255, 255, 255}));
    // Clipped outside any stack, under another parent, it changes nothing.
    scene.layers[levels].source = base;
    scene.layers[levels].parent = QUuid::createUuid();
    QCOMPARE(reds(render(4)), (std::array<int, 4>{255, 255, 255, 255}));
}

void LiveMaskRendererTests::anAdjustmentTakesAnyImageTarget()
{
    // Four colours, so every channel and position tells.
    QImage colours = BrushRaster::context(2, 2, false);
    colours.setPixel(0, 0, qRgba(200, 100, 50, 255));
    colours.setPixel(1, 0, qRgba(30, 160, 220, 255));
    colours.setPixel(0, 1, qRgba(90, 90, 90, 255));
    colours.setPixel(1, 1, qRgba(250, 240, 10, 255));
    Scene scene;
    const QUuid base = scene.add(placed(colours));
    const QUuid curve = scene.add(Layer());
    LayerAdjustment lifted{AdjustmentKind::curves};
    lifted.curves.channels[0] = {{0, 40}, {255, 200}};
    const auto draw = [&](QImage target, LayerBlendMode mode, double scale) {
        LiveMaskRenderer live = scene.renderer();
        live.adjustment = [&](QUuid id) { return id == curve ? std::optional(lifted) : std::nullopt; };
        live.prepareStacks({base, curve}, [](QUuid) { return std::nullopt; }, [&](QUuid id) { return id == curve ? mode : LayerBlendMode::normal; });
        target.fill(0);
        QPainter painter(&target);
        painter.scale(scale, scale);
        live.drawComposite(base, painter);
        live.drawComposite(curve, painter);
        painter.end();
        return target.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    };
    const QImage untouched = draw(BrushRaster::context(2, 2, false), LayerBlendMode::normal, 1);
    for (const LayerBlendMode mode : {LayerBlendMode::normal, LayerBlendMode::multiply}) {
        const QImage plain = draw(BrushRaster::context(2, 2, false), mode, 1);
        QVERIFY(plain != colours && (mode == LayerBlendMode::normal || plain != untouched));
        // ARGB32 byte order: it reads colours, not bytes.
        QCOMPARE(draw(QImage(2, 2, QImage::Format_ARGB32_Premultiplied), mode, 1), plain);
        // Pixel ratio two draws as a painter scale of two.
        QImage sharp = BrushRaster::context(4, 4, false);
        sharp.setDevicePixelRatio(2);
        QImage ratio = draw(sharp, mode, 1);
        ratio.setDevicePixelRatio(1);
        QCOMPARE(ratio, draw(BrushRaster::context(4, 4, false), mode, 2));
    }
}

QTEST_MAIN(LiveMaskRendererTests)
#include "LiveMaskRendererTests.moc"
