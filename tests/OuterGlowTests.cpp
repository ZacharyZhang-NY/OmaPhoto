#include "Document/EditorSession.h"
#include "Document/LayerEffects+Renderer.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectStore+Json.h"
#include <QPainter>
#include <QtTest>

// Swift's OuterGlowTests: the glow's model, drawing and export.
namespace {
// A white square centred on clear, Swift's createSquareImage.
QImage square(int size = 40, int inner = 20)
{
    QImage image(size, size, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    const int origin = (size - inner) / 2;
    painter.fillRect(QRect(origin, origin, inner, inner), Qt::white);
    return image;
}

QColor at(const LayerEffectsRenderer::Rendered &rendered, int x, int y)
{
    const int inset = int(rendered.inset);
    return rendered.image.pixelColor(inset + x, inset + y);
}

LayerEffects glowing(double size, double red, double green, double blue, double opacity)
{
    LayerEffects effects;
    effects.outerGlow = OuterGlowEffect{.enabled = true, .size = size, .red = red, .green = green, .blue = blue, .opacity = opacity};
    return effects;
}
}

class OuterGlowTests : public QObject {
    Q_OBJECT
private slots:
    void outerGlowDefaultsAndValidation();
    void outerGlowCodableRoundTrip();
    void layerEffectsIntegration();
    void layerEffectsCodableBackwardCompatibility();
    void outerGlowRendersOmnidirectionally();
    void outerGlowSizeAndOpacityVariations();
    void outerGlowRendersAroundTextGlyphs();
    void outerGlowCombinedWithStrokeAndDropShadow();
    void outerGlowPreservedInExport();
    void theGlowWidensTheMargin();
    void theSessionAddsCopiesAndCancelsIt();
};

void OuterGlowTests::outerGlowDefaultsAndValidation()
{
    const OuterGlowEffect glow;
    QVERIFY(glow.isEnabled() && glow.isValid());
    QCOMPARE(glow.size, 20.0);
    QCOMPARE(glow.opacity, 0.75);
    QVERIFY(glow.color() == (PaletteColor{1, 1, 1}));
    for (const auto &broken : std::vector<std::function<void(OuterGlowEffect &)>>{
             [](OuterGlowEffect &e) { e.size = -1; }, [](OuterGlowEffect &e) { e.size = 500.5; }, [](OuterGlowEffect &e) { e.size = qQNaN(); },
             [](OuterGlowEffect &e) { e.opacity = 1.5; }, [](OuterGlowEffect &e) { e.red = 2; }, [](OuterGlowEffect &e) { e.green = -0.1; },
             [](OuterGlowEffect &e) { e.blue = qInf(); }}) {
        OuterGlowEffect changed = glow;
        broken(changed);
        QVERIFY(!changed.isValid());
        LayerEffects effects;
        effects.outerGlow = changed;
        QVERIFY(!effects.isValid());
    }
    OuterGlowEffect edge = glow;
    edge.size = 500;
    QVERIFY(edge.isValid());
}

void OuterGlowTests::outerGlowCodableRoundTrip()
{
    const LayerEffects original = glowing(35, 0.2, 0.8, 1, 0.6);
    const QJsonObject encoded = ManifestJson::encoded(original);
    const QJsonObject glow = encoded.value("outerGlow").toObject();
    QCOMPARE(glow.keys(), (QStringList{"blue", "enabled", "green", "opacity", "red", "size"}));
    QCOMPARE(ManifestJson::effects(encoded), original);
    // Absent `enabled` stays absent, read as shown.
    LayerEffects shown = original;
    shown.outerGlow.value().enabled = std::nullopt;
    QVERIFY(!ManifestJson::encoded(shown).value("outerGlow").toObject().contains("enabled"));
    QCOMPARE(ManifestJson::effects(ManifestJson::encoded(shown)), shown);
    // A key of the wrong type, or missing, refuses it.
    QJsonObject wrong = encoded;
    QJsonObject bent = glow;
    bent.insert("size", "big");
    wrong.insert("outerGlow", bent);
    QVERIFY_THROWS_EXCEPTION(ProjectError, ManifestJson::effects(wrong));
    bent = glow;
    bent.remove("opacity");
    wrong.insert("outerGlow", bent);
    QVERIFY_THROWS_EXCEPTION(ProjectError, ManifestJson::effects(wrong));
}

void OuterGlowTests::layerEffectsIntegration()
{
    LayerEffects effects;
    QVERIFY(effects.isEmpty() && !effects.contains(LayerEffectKind::outerGlow));
    effects.outerGlow = OuterGlowEffect{.size = 25, .red = 1, .green = 0, .blue = 0, .opacity = 0.8};
    QVERIFY(!effects.isEmpty() && effects.contains(LayerEffectKind::outerGlow) && effects.isEnabled(LayerEffectKind::outerGlow));
    QVERIFY(effects.color(LayerEffectKind::outerGlow) == (PaletteColor{1, 0, 0}));
    QVERIFY((effects.kinds() == std::vector{LayerEffectKind::outerGlow}));
    effects.setColor(PaletteColor{0, 1, 0}, LayerEffectKind::outerGlow);
    QVERIFY(effects.outerGlow.value().green == 1 && effects.outerGlow.value().red == 0);
    effects.setEnabled(false, LayerEffectKind::outerGlow);
    QVERIFY(!effects.isEnabled(LayerEffectKind::outerGlow) && !effects.visible().outerGlow);
    effects.remove(LayerEffectKind::outerGlow);
    QVERIFY(!effects.outerGlow && effects.isEmpty());
}

void OuterGlowTests::layerEffectsCodableBackwardCompatibility()
{
    // Effects written before the glow read without it.
    const QJsonObject older{{"stroke", QJsonObject{{"size", 3}, {"red", 0}, {"green", 0}, {"blue", 0}, {"opacity", 1}, {"inside", false}}}};
    LayerEffects decoded = ManifestJson::effects(older);
    QCOMPARE(decoded.stroke.value().size, 3.0);
    QVERIFY(!decoded.outerGlow && decoded.isValid());
    decoded.outerGlow = OuterGlowEffect{.size = 15, .red = 1, .green = 0.5, .blue = 0, .opacity = 0.9};
    const LayerEffects again = ManifestJson::effects(ManifestJson::encoded(decoded));
    QCOMPARE(again.outerGlow.value().size, 15.0);
    QCOMPARE(again.outerGlow.value().opacity, 0.9);
}

void OuterGlowTests::outerGlowRendersOmnidirectionally()
{
    const LayerEffectsRenderer::Rendered rendered = LayerEffectsRenderer::render(square(), std::nullopt, glowing(10, 0, 1, 0, 1));
    QCOMPARE(rendered.inset, 32.0);
    // The square keeps its white; the glow shows all round.
    const QColor centre = at(rendered, 20, 20);
    QVERIFY(centre.alphaF() > 0.95 && centre.redF() > 0.95 && centre.greenF() > 0.95 && centre.blueF() > 0.95);
    const QColor sides[] = {at(rendered, 5, 20), at(rendered, 35, 20), at(rendered, 20, 5), at(rendered, 20, 35)};
    for (const QColor &side : sides) {
        QVERIFY(side.alphaF() > 0.1);
        QVERIFY(side.greenF() > 0.8 && side.redF() < 0.05);
        QVERIFY(std::abs(side.alphaF() - sides[0].alphaF()) < 0.05);
    }
    // Nothing reaches past three sigmas and a pixel.
    QCOMPARE(at(rendered, -17, 20).alpha(), 0);
}

void OuterGlowTests::outerGlowSizeAndOpacityVariations()
{
    const QImage image = square();
    const LayerEffectsRenderer::Rendered small = LayerEffectsRenderer::render(image, std::nullopt, glowing(4, 1, 0, 0, 1));
    const LayerEffectsRenderer::Rendered large = LayerEffectsRenderer::render(image, std::nullopt, glowing(20, 1, 0, 0, 1));
    QVERIFY(large.inset > small.inset);
    QVERIFY(at(large, 2, 20).alphaF() > at(small, 2, 20).alphaF());
    const LayerEffectsRenderer::Rendered low = LayerEffectsRenderer::render(image, std::nullopt, glowing(10, 0, 0, 1, 0.2));
    const LayerEffectsRenderer::Rendered high = LayerEffectsRenderer::render(image, std::nullopt, glowing(10, 0, 0, 1, 1));
    QVERIFY(at(high, 5, 20).alphaF() > at(low, 5, 20).alphaF());
}

void OuterGlowTests::outerGlowRendersAroundTextGlyphs()
{
    // A T: a bar over a stem, white on clear.
    QImage glyph(60, 60, QImage::Format_RGBA8888_Premultiplied);
    glyph.fill(Qt::transparent);
    QPainter painter(&glyph);
    painter.fillRect(QRect(15, 15, 30, 8), Qt::white);
    painter.fillRect(QRect(26, 23, 8, 22), Qt::white);
    painter.end();
    const LayerEffectsRenderer::Rendered rendered = LayerEffectsRenderer::render(glyph, std::nullopt, glowing(8, 0, 1, 1, 1));
    const QColor stem = at(rendered, 30, 30), above = at(rendered, 30, 12), notch = at(rendered, 20, 27);
    QVERIFY(stem.alphaF() > 0.9 && stem.redF() > 0.9 && stem.greenF() > 0.9 && stem.blueF() > 0.9);
    QVERIFY(above.alphaF() > 0.05 && above.greenF() > 0.5 && above.blueF() > 0.5);
    QVERIFY(notch.alphaF() > 0.05 && notch.greenF() > 0.5 && notch.blueF() > 0.5);
}

void OuterGlowTests::outerGlowCombinedWithStrokeAndDropShadow()
{
    LayerEffects effects = glowing(10, 1, 0, 0, 1);
    effects.stroke = StrokeEffect{.size = 3, .red = 0, .green = 0, .blue = 0, .opacity = 1, .inside = false};
    effects.shadow = ShadowEffect{.enabled = true, .angle = 180, .distance = 25, .blur = 4, .red = 0, .green = 0, .blue = 1, .opacity = 1};
    const LayerEffectsRenderer::Rendered rendered = LayerEffectsRenderer::render(square(50, 20), std::nullopt, effects);
    const QColor centre = at(rendered, 25, 25);
    QVERIFY(centre.redF() > 0.9 && centre.greenF() > 0.9 && centre.blueF() > 0.9);
    // The stroke lies over the glow, the glow over shadow.
    const QColor stroke = at(rendered, 13, 25);
    QVERIFY(stroke.alphaF() > 0.9 && stroke.redF() < 0.2 && stroke.greenF() < 0.2 && stroke.blueF() < 0.2);
    const QColor glow = at(rendered, 10, 25);
    QVERIFY(glow.alphaF() > 0.05 && glow.redF() > 0.6);
    const QColor shadow = at(rendered, 50, 25);
    QVERIFY(shadow.alphaF() > 0.1 && shadow.blueF() > 0.6);
}

void OuterGlowTests::outerGlowPreservedInExport()
{
    EditorSession session;
    session.createDocument(100, 100);
    session.insert(ImportedImage(square(30, 10), QImage(), QStringLiteral("GlowLayer")), QPointF(35, 35));
    session.setEffects(glowing(15, 1, 0.5, 0, 1));
    const QImage exported = ImageExporter::render(session.projectSnapshot().value()).image;
    QVERIFY(exported.pixelColor(35, 35).alphaF() > 0.9);
    const QColor glow = exported.pixelColor(27, 35);
    QVERIFY(glow.alphaF() > 0.1 && glow.redF() > 0.6);
}

void OuterGlowTests::theGlowWidensTheMargin()
{
    LayerEffects effects = glowing(7.2, 1, 1, 1, 1);
    QCOMPARE(LayerEffectsRenderer::margin(effects), 24.0);
    effects.outerGlow.value().enabled = false;
    QCOMPARE(LayerEffectsRenderer::margin(effects), 2.0);
}

void OuterGlowTests::theSessionAddsCopiesAndCancelsIt()
{
    EditorSession session;
    session.createDocument(40, 40);
    session.insert(ImportedImage(square(), QImage(), QStringLiteral("One")));
    const QUuid first = session.activeLayerID().value();
    session.addEffect(LayerEffectKind::outerGlow);
    QCOMPARE(session.history.undoName(), QString("Add Outer Glow"));
    QVERIFY(session.activeEffects().outerGlow == OuterGlowEffect());
    session.changeEffects([](LayerEffects &effects) { effects.outerGlow.value().size = 44; });
    session.finishEffectsEditing(false);
    QVERIFY(!session.activeEffects().outerGlow);
    session.addEffect(LayerEffectKind::outerGlow);
    session.changeEffects([](LayerEffects &effects) { effects.outerGlow.value().size = 44; });
    session.finishEffectsEditing(true);
    session.insert(ImportedImage(square(), QImage(), QStringLiteral("Two")));
    const QUuid second = session.activeLayerID().value();
    QVERIFY(session.canCopyEffect(LayerEffectKind::outerGlow, first, second));
    session.copyEffect(LayerEffectKind::outerGlow, first, second);
    QCOMPARE(session.history.undoName(), QString("Copy Outer Glow"));
    QCOMPARE(session.activeEffects().outerGlow.value().size, 44.0);
}

QTEST_MAIN(OuterGlowTests)
#include "OuterGlowTests.moc"
