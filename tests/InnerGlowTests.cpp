#include "Document/EditorSession.h"
#include "Document/LayerEffects+Renderer.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectStore+Json.h"
#include <QPainter>
#include <QtTest>

// Swift's InnerGlowTests: the glow's model, drawing and export.
namespace {
// Swift's solidSquare: opaque, one colour.
QImage square(int size, const QColor &colour)
{
    QImage image(size, size, QImage::Format_RGBA8888_Premultiplied);
    image.fill(colour);
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
    effects.innerGlow = InnerGlowEffect{.enabled = true, .size = size, .red = red, .green = green, .blue = blue, .opacity = opacity};
    return effects;
}
}

class InnerGlowTests : public QObject {
    Q_OBJECT
private slots:
    void innerGlowDefaultsAndValidation();
    void innerGlowCodableRoundTrip();
    void innerGlowBackwardCompatibility();
    void layerEffectsIntegration();
    void innerGlowRendersInsideSourceWithoutBoundsExpansion();
    void innerGlowRendersAroundTextGlyphs();
    void itSitsOverTheOverlayAndUnderTheInnerShadow();
    void innerGlowPreservedInExport();
    void theSessionAddsCopiesAndCancelsIt();
};

void InnerGlowTests::innerGlowDefaultsAndValidation()
{
    const InnerGlowEffect glow;
    QVERIFY(glow.isEnabled() && glow.isValid());
    QCOMPARE(glow.size, 10.0);
    QCOMPARE(glow.opacity, 0.75);
    QVERIFY(glow.color() == (PaletteColor{1, 1, 1}));
    for (const auto &broken : std::vector<std::function<void(InnerGlowEffect &)>>{
             [](InnerGlowEffect &e) { e.size = -1; }, [](InnerGlowEffect &e) { e.size = 500.5; }, [](InnerGlowEffect &e) { e.size = qQNaN(); },
             [](InnerGlowEffect &e) { e.opacity = 1.5; }, [](InnerGlowEffect &e) { e.opacity = -0.1; }, [](InnerGlowEffect &e) { e.red = 2; },
             [](InnerGlowEffect &e) { e.green = -0.1; }, [](InnerGlowEffect &e) { e.blue = qInf(); }}) {
        InnerGlowEffect changed = glow;
        broken(changed);
        QVERIFY(!changed.isValid());
        LayerEffects effects;
        effects.innerGlow = changed;
        QVERIFY(!effects.isValid());
    }
    InnerGlowEffect edge = glow;
    edge.size = 500;
    edge.opacity = 1;
    QVERIFY(edge.isValid());
}

void InnerGlowTests::innerGlowCodableRoundTrip()
{
    const LayerEffects original = glowing(25, 1, 0.5, 0.2, 0.85);
    const QJsonObject encoded = ManifestJson::encoded(original);
    const QJsonObject glow = encoded.value("innerGlow").toObject();
    QCOMPARE(glow.keys(), (QStringList{"blue", "enabled", "green", "opacity", "red", "size"}));
    QVERIFY(glow.value("size") == 25 && glow.value("opacity") == 0.85 && glow.value("green") == 0.5);
    QCOMPARE(ManifestJson::effects(encoded), original);
    // Absent `enabled` stays absent, read as shown.
    LayerEffects shown = original;
    shown.innerGlow.value().enabled = std::nullopt;
    QVERIFY(!ManifestJson::encoded(shown).value("innerGlow").toObject().contains("enabled"));
    QCOMPARE(ManifestJson::effects(ManifestJson::encoded(shown)), shown);
    // A key of the wrong type, or missing, refuses it.
    for (const char *key : {"size", "red", "green", "blue", "opacity"}) {
        QJsonObject wrong = encoded, bent = glow;
        bent.insert(QLatin1String(key), "big");
        wrong.insert("innerGlow", bent);
        QVERIFY_THROWS_EXCEPTION(ProjectError, ManifestJson::effects(wrong));
        bent.remove(QLatin1String(key));
        wrong.insert("innerGlow", bent);
        QVERIFY_THROWS_EXCEPTION(ProjectError, ManifestJson::effects(wrong));
    }
}

void InnerGlowTests::innerGlowBackwardCompatibility()
{
    // Effects written before the glow read without it.
    const QJsonObject older{{"shadow", QJsonObject{{"angle", 90}, {"distance", 10}, {"blur", 15}, {"red", 0}, {"green", 0}, {"blue", 0}, {"opacity", 0.5}}}};
    const LayerEffects decoded = ManifestJson::effects(older);
    QVERIFY(decoded.shadow && !decoded.innerGlow && decoded.isValid());
}

void InnerGlowTests::layerEffectsIntegration()
{
    LayerEffects effects;
    QVERIFY(effects.isEmpty() && !effects.contains(LayerEffectKind::innerGlow));
    effects.innerGlow = InnerGlowEffect{.size = 15};
    QVERIFY(!effects.isEmpty() && effects.contains(LayerEffectKind::innerGlow) && effects.isEnabled(LayerEffectKind::innerGlow));
    QVERIFY((effects.kinds() == std::vector{LayerEffectKind::innerGlow}));
    effects.setEnabled(false, LayerEffectKind::innerGlow);
    QVERIFY(!effects.isEnabled(LayerEffectKind::innerGlow) && !effects.visible().innerGlow);
    effects.setColor(PaletteColor{1, 0.8, 0}, LayerEffectKind::innerGlow);
    QVERIFY(effects.color(LayerEffectKind::innerGlow) == (PaletteColor{1, 0.8, 0}));
    effects.remove(LayerEffectKind::innerGlow);
    QVERIFY(!effects.contains(LayerEffectKind::innerGlow) && effects.isEmpty());
}

void InnerGlowTests::innerGlowRendersInsideSourceWithoutBoundsExpansion()
{
    const LayerEffects effects = glowing(12, 1, 1, 0, 1);
    // The margin stays the baseline two: nothing spills out.
    QCOMPARE(LayerEffectsRenderer::margin(effects), 2.0);
    const LayerEffectsRenderer::Rendered rendered = LayerEffectsRenderer::render(square(40, Qt::black), std::nullopt, effects);
    QCOMPARE(rendered.image.pixelColor(0, 0).alpha(), 0);
    QCOMPARE(at(rendered, -1, 20).alpha(), 0);
    // Yellow near the edge, black in the deep centre.
    const QColor edge = at(rendered, 2, 20), centre = at(rendered, 20, 20);
    QVERIFY(edge.alphaF() > 0.9 && edge.redF() > 0.3 && edge.greenF() > 0.3 && edge.blueF() < 0.05);
    QVERIFY(centre.redF() < 0.2 && centre.greenF() < 0.2);
    // It fades inward: the edge brightest.
    QVERIFY(at(rendered, 0, 20).redF() > at(rendered, 4, 20).redF() && at(rendered, 4, 20).redF() > at(rendered, 10, 20).redF());
}

void InnerGlowTests::innerGlowRendersAroundTextGlyphs()
{
    // Swift's black O: a ring on clear.
    QImage glyph(72, 72, QImage::Format_RGBA8888_Premultiplied);
    glyph.fill(Qt::transparent);
    QPainter painter(&glyph);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(Qt::black, 12));
    painter.drawEllipse(QRectF(12, 12, 48, 48));
    painter.end();
    const LayerEffectsRenderer::Rendered rendered = LayerEffectsRenderer::render(glyph, std::nullopt, glowing(8, 1, 0, 0, 0.9));
    QVERIFY(rendered.image.width() >= glyph.width() && rendered.image.height() >= glyph.height());
    QCOMPARE(rendered.image.pixelColor(0, 0).alpha(), 0);
    // Swift's search: some pixel glows red; the hole is clear.
    bool found = false;
    for (int y = 0; y < glyph.height() && !found; ++y) {
        for (int x = 0; x < glyph.width() && !found; ++x)
            found = at(rendered, x, y).alphaF() > 0.5 && at(rendered, x, y).redF() > 0.3;
    }
    QVERIFY(found);
    QCOMPARE(at(rendered, 36, 36).alpha(), 0);
}

void InnerGlowTests::itSitsOverTheOverlayAndUnderTheInnerShadow()
{
    // Metal's order: overlay, inner glow, inner shadow.
    LayerEffects effects = glowing(12, 0, 1, 0, 1);
    effects.colorOverlay = ColorOverlayEffect{.red = 0, .green = 0, .blue = 1, .opacity = 1};
    const LayerEffectsRenderer::Rendered covered = LayerEffectsRenderer::render(square(40, Qt::black), std::nullopt, effects);
    QVERIFY(at(covered, 0, 20).greenF() > 0.4 && at(covered, 20, 20).blueF() > 0.9);
    effects.innerShadow = InnerShadowEffect{.angle = 0, .distance = 0, .blur = 0.01, .red = 1, .green = 0, .blue = 0, .opacity = 1};
    const LayerEffectsRenderer::Rendered shadowed = LayerEffectsRenderer::render(square(40, Qt::black), std::nullopt, effects);
    QCOMPARE(at(shadowed, 0, 20), at(covered, 0, 20));
    // A sideways shadow covers the glow on one edge.
    effects.innerShadow.value().distance = 4;
    const int x = effects.innerShadow.value().offset().width() > 0 ? 0 : 39;
    const QColor edge = at(LayerEffectsRenderer::render(square(40, Qt::black), std::nullopt, effects), x, 20);
    QVERIFY(std::abs(effects.innerShadow.value().offset().width()) > 3.9);
    QVERIFY(edge.redF() > 0.9 && edge.greenF() < 0.1);
}

void InnerGlowTests::innerGlowPreservedInExport()
{
    EditorSession session;
    session.createDocument(100, 100);
    session.insert(ImportedImage(square(30, Qt::black), QImage(), QStringLiteral("InnerGlowLayer")), QPointF(35, 35));
    session.setEffects(glowing(8, 1, 0, 1, 0.9));
    const QImage exported = ImageExporter::render(session.projectSnapshot().value()).image;
    // The square spans 20 to 50: magenta at its edge.
    const QColor edge = exported.pixelColor(21, 35), centre = exported.pixelColor(35, 35);
    QVERIFY(edge.alphaF() > 0.8 && edge.redF() > 0.2 && edge.blueF() > 0.2 && edge.greenF() < 0.05);
    QVERIFY(centre.redF() < 0.1 && centre.blueF() < 0.1);
    QCOMPARE(exported.pixelColor(18, 35).alpha(), 0);
}

void InnerGlowTests::theSessionAddsCopiesAndCancelsIt()
{
    EditorSession session;
    session.createDocument(40, 40);
    session.insert(ImportedImage(square(20, Qt::black), QImage(), QStringLiteral("One")));
    const QUuid first = session.activeLayerID().value();
    session.addEffect(LayerEffectKind::innerGlow);
    QCOMPARE(session.history.undoName(), QString("Add Inner Glow"));
    QVERIFY(session.activeEffects().innerGlow == InnerGlowEffect());
    session.changeEffects([](LayerEffects &effects) { effects.innerGlow.value().size = 44; });
    session.finishEffectsEditing(false);
    QVERIFY(!session.activeEffects().innerGlow);
    session.addEffect(LayerEffectKind::innerGlow);
    session.changeEffects([](LayerEffects &effects) { effects.innerGlow.value().size = 44; });
    session.finishEffectsEditing(true);
    // Adding it again keeps the glow it has.
    session.addEffect(LayerEffectKind::innerGlow);
    QCOMPARE(session.activeEffects().innerGlow.value().size, 44.0);
    session.finishEffectsEditing(true);
    session.insert(ImportedImage(square(20, Qt::black), QImage(), QStringLiteral("Two")));
    const QUuid second = session.activeLayerID().value();
    QVERIFY(session.canCopyEffect(LayerEffectKind::innerGlow, first, second));
    session.copyEffect(LayerEffectKind::innerGlow, first, second);
    QCOMPARE(session.history.undoName(), QString("Copy Inner Glow"));
    QCOMPARE(session.activeEffects().innerGlow.value().size, 44.0);
}

QTEST_MAIN(InnerGlowTests)
#include "InnerGlowTests.moc"
