#include "AddressSpaceLimit.h"
#include "Document/LayerEffects+Renderer.h"
#include "Rendering/EditorCanvas.h"
#include "Rendering/LayerEffectsSurface.h"
#include "RenderFixtures.h"
#include "SessionFixtures.h"
#include <QtTest>
#include <malloc.h>

// A painted layer's effects surface, starved of memory.
class EffectsFailureTests : public QObject {
    Q_OBJECT
private slots:
    // Large asks map memory, so the address limit counts them.
    void initTestCase() { mallopt(M_MMAP_THRESHOLD, 64 * 1024); }
    // First, while malloc's heap holds nothing a limit would miss.
    void aStarvedLaterPassKeepsTheLastSurface();
    void aSurfaceWithoutMemoryLeavesTheLastEffects();
};

void EffectsFailureTests::aSurfaceWithoutMemoryLeavesTheLastEffects()
{
    EditorSession session;
    CanvasView canvas(session);
    session.createDocument(60, 40);
    session.setShowsTransformControls(false);
    canvas.resize(300, 200);
    session.viewport.resize(QSizeF(300, 200), 1, QSizeF(60, 40));
    session.zoom(1);
    const QImage white = solid(60, 40, qRgba(255, 255, 255, 255));
    session.insert(ImportedImage(white, white, QStringLiteral("White")));
    // Eighty pixels a document pixel: a 4800 by 3200 grid.
    const QImage fine = solid(800, 800, qRgba(0, 0, 255, 255));
    session.insert(ImportedImage(fine, fine, QStringLiteral("Square")));
    const QUuid square = session.activeLayerID().value();
    LayerEffects effects;
    effects.stroke = StrokeEffect{.size = 160, .red = 1, .green = 1, .blue = 0, .opacity = 1};
    rewrite(session, [&](ProjectSnapshot &snapshot) {
        record(snapshot, square).transform = placedAt(QPointF(25, 15), QSizeF(10, 10));
        record(snapshot, square).effects = effects;
    });
    session.zoom(1);
    const auto shown = [&](QPoint pixel) {
        const QPointF view = session.viewport.viewPoint(QPointF(pixel) + QPointF(0.5, 0.5), QSizeF(60, 40));
        return canvas.grab().toImage().pixelColor(int(std::floor(view.x())), int(std::floor(view.y())));
    };
    shown(QPoint(0, 0));
    QVERIFY(QTest::qWaitFor([&] { return session.effectsPreviews.rendered(square).has_value(); }, 10000));
    session.selectTool(NavigationTool::brush);
    BrushSettings settings = session.brushSettings();
    settings.diameter = 4;
    settings.red = 1;
    session.setBrushSettings(settings);
    session.beginBrush(QPointF(45, 20));
    session.continueBrush(QPointF(55, 20));
    {
        // The surface is made; its first pass finds no memory.
        const AddressSpaceLimit limit(200ll * 1024 * 1024);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("a painted layer's effects could not be redone"));
        QCOMPARE(shown(QPoint(23, 20)), QColor(255, 255, 0));
        QCOMPARE(shown(QPoint(50, 20)), QColor(Qt::red));
        QCOMPARE(shown(QPoint(50, 16)), QColor(Qt::white));
    }
    session.cancelBrush();
}

void EffectsFailureTests::aStarvedLaterPassKeepsTheLastSurface()
{
    if (!placesStarvation())
        QSKIP("the failure point is placed for Qt 6.4's allocations");
    const QImage base = solid(3000, 3000, qRgba(0, 0, 255, 255));
    LayerEffects effects;
    effects.stroke = StrokeEffect{.size = 5, .red = 1, .green = 1, .blue = 0, .opacity = 1};
    const std::unique_ptr<LayerEffectsSurface> surface =
        LayerEffectsSurface::make(QUuid::createUuid(), effects, QSizeF(3000, 3000), QRectF(0, 0, 3000, 3000));
    const ImportedImage asset(base, QImage(), QStringLiteral("Layer"));
    surface->update(asset, {}, std::nullopt);
    const qint64 first = surface->image().value().cacheKey();
    const BrushPatch patch{QRectF(0, 0, 256, 256), solid(256, 256, qRgba(255, 0, 0, 255))};
    {
        // The pass copies the whole surface; no memory is left.
        const AddressSpaceLimit limit(16ll * 1024 * 1024);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("a painted layer's effects could not be redone"));
        surface->update(asset, {patch}, std::nullopt);
    }
    QCOMPARE(surface->image().value().cacheKey(), first);
    // With memory back, the tile is redone: never counted taken.
    surface->update(asset, {patch}, std::nullopt);
    QVERIFY(surface->image().value() == LayerEffectsRenderer::render(composite(base, {patch}), std::nullopt, effects).image);
}

QTEST_MAIN(EffectsFailureTests)
#include "EffectsFailureTests.moc"
