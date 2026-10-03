#include "Document/EditorSession.h"
#include "IO/CanvasResizer.h"
#include "IO/ImageExporter.h"
#include "IO/ImageResizer.h"
#include "IO/ProjectStore+Json.h"
#include "SelectionFixtures.h"
#include "SessionFixtures.h"
#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <limits>

// Layer effects in the document, the manifest and the store.
namespace {
constexpr double notANumber = std::numeric_limits<double>::quiet_NaN();
constexpr double infinity = std::numeric_limits<double>::infinity();

// Every effect, each with a value of its own.
LayerEffects every()
{
    LayerEffects effects;
    effects.stroke = StrokeEffect{.enabled = false, .size = 7, .red = 0.1, .green = 0.2, .blue = 0.3, .opacity = 0.4, .inside = true};
    effects.shadow = ShadowEffect{.angle = 45, .distance = 12, .blur = 6, .red = 0.5, .green = 0.6, .blue = 0.7, .opacity = 0.8};
    effects.colorOverlay = ColorOverlayEffect{.enabled = true, .red = 0.9, .green = 0.25, .blue = 0.75, .opacity = 0.35};
    effects.innerShadow = InnerShadowEffect{.angle = -30, .distance = 3, .blur = 2, .red = 0.05, .green = 0.15, .blue = 0.95, .opacity = 0.65};
    effects.outerGlow = OuterGlowEffect{.size = 33, .red = 0.2, .green = 0.4, .blue = 0.6, .opacity = 0.55};
    effects.innerGlow = InnerGlowEffect{.enabled = false, .size = 14, .red = 0.3, .green = 0.7, .blue = 0.1, .opacity = 0.45};
    return effects;
}

ImportedImage filled(int width, int height, const QColor &colour)
{
    QImage pixels(width, height, QImage::Format_RGBA8888_Premultiplied);
    pixels.fill(colour);
    return ImportedImage(pixels, pixels, QStringLiteral("Filled"));
}

// A 20 by 10 blue layer carrying every effect.
std::unique_ptr<EditorSession> withEffects()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(20, 10);
    session->insert(filled(20, 10, Qt::blue));
    session->setEffects(every());
    return session;
}

bool refused(const QJsonObject &object)
{
    try {
        ManifestJson::effects(object);
    } catch (const ProjectError &) {
        return true;
    }
    return false;
}

bool awaited(const std::function<void(std::function<void()>)> &call)
{
    bool done = false;
    call([&done] { done = true; });
    return QTest::qWaitFor([&done] { return done; }, 10'000);
}
}

class LayerEffectsTests : public QObject {
    Q_OBJECT
private slots:
    void kindsKeepSwiftsNamesAndOrder();
    void validityFollowsSwiftsBounds();
    void shadowsFallAwayFromTheLight();
    void theHelpersReadAndWriteOneKind();
    void theManifestWritesSwiftsKeys();
    void decodingRefusesWhatSwiftRefuses();
    void effectsSaveLoadAndExportWithTheirLayer();
    void duplicatesAndBrushCommitsKeepThem();
    void rebuildsCopiesAndResizesDropThem();
};

void LayerEffectsTests::kindsKeepSwiftsNamesAndOrder()
{
    const std::pair<LayerEffectKind, const char *> kinds[] = {{LayerEffectKind::stroke, "Stroke"}, {LayerEffectKind::shadow, "Drop Shadow"},
                                                              {LayerEffectKind::colorOverlay, "Color Overlay"}, {LayerEffectKind::innerShadow, "Inner Shadow"},
                                                              {LayerEffectKind::outerGlow, "Outer Glow"}, {LayerEffectKind::innerGlow, "Inner Glow"}};
    QCOMPARE(allLayerEffectKinds.size(), std::size(kinds));
    for (size_t index = 0; index < std::size(kinds); ++index) {
        QVERIFY(allLayerEffectKinds[index] == kinds[index].first);
        QCOMPARE(rawValue(kinds[index].first), QString(kinds[index].second));
    }
    // Swift's defaults.
    QVERIFY((StrokeEffect() == StrokeEffect{std::nullopt, 4, 0, 0, 0, 1, false}));
    QVERIFY((ShadowEffect() == ShadowEffect{std::nullopt, 90, 20, 20, 0, 0, 0, 0.5}));
    QVERIFY((OuterGlowEffect() == OuterGlowEffect{std::nullopt, 20, 1, 1, 1, 0.75}));
    QVERIFY((InnerGlowEffect() == InnerGlowEffect{std::nullopt, 10, 1, 1, 1, 0.75}));
    QVERIFY((ColorOverlayEffect() == ColorOverlayEffect{std::nullopt, 0, 0, 0, 1}));
    QVERIFY((InnerShadowEffect() == InnerShadowEffect{std::nullopt, 90, 10, 10, 0, 0, 0, 0.5}));
    QCOMPARE(StrokeEffect::maxSize, 500.0);
}

void LayerEffectsTests::validityFollowsSwiftsBounds()
{
    QVERIFY(every().isValid() && LayerEffects().isValid());
    // Each bound at its edge, then just past it.
    for (const double size : {0.0, 500.0})
        QVERIFY(StrokeEffect{.size = size}.isValid());
    for (const double size : {-0.5, 500.5, notANumber, infinity})
        QVERIFY(!StrokeEffect{.size = size}.isValid());
    for (const double opacity : {-0.01, 1.01, notANumber})
        QVERIFY(!StrokeEffect{.opacity = opacity}.isValid() && !ShadowEffect{.opacity = opacity}.isValid()
                && !ColorOverlayEffect{.opacity = opacity}.isValid() && !InnerShadowEffect{.opacity = opacity}.isValid());
    for (const double channel : {-0.01, 1.01, notANumber}) {
        QVERIFY(!StrokeEffect{.red = channel}.isValid() && !ShadowEffect{.green = channel}.isValid());
        QVERIFY(!ColorOverlayEffect{.blue = channel}.isValid() && !InnerShadowEffect{.red = channel}.isValid());
    }
    QVERIFY((ShadowEffect{.angle = -360, .distance = 5000, .blur = 500}.isValid() && InnerShadowEffect{.angle = 360, .distance = 0, .blur = 0}.isValid()));
    QVERIFY(!ShadowEffect{.angle = 360.5}.isValid() && !ShadowEffect{.distance = 5000.5}.isValid() && !ShadowEffect{.blur = 500.5}.isValid());
    QVERIFY(!InnerShadowEffect{.angle = -360.5}.isValid() && !InnerShadowEffect{.distance = -1}.isValid() && !InnerShadowEffect{.blur = notANumber}.isValid());
    // One bad effect spoils the set.
    LayerEffects effects = every();
    effects.innerShadow.value().blur = infinity;
    QVERIFY(!effects.isValid());
    effects = every();
    effects.stroke.value().size = 501;
    QVERIFY(!effects.isValid());
    effects = every();
    effects.shadow.value().distance = -1;
    QVERIFY(!effects.isValid());
    effects = every();
    effects.colorOverlay.value().opacity = 2;
    QVERIFY(!effects.isValid());
}

void LayerEffectsTests::shadowsFallAwayFromTheLight()
{
    // Light from above drops the shadow straight down, y growing.
    const QSizeF down = ShadowEffect{.angle = 90, .distance = 20}.offset();
    QVERIFY(std::abs(down.width()) < 1e-12 && down.height() == 20);
    const QSizeF left = ShadowEffect{.angle = 0, .distance = 8}.offset();
    QVERIFY(left.width() == -8 && std::abs(left.height()) < 1e-12);
    const QSizeF inner = InnerShadowEffect{.angle = 180, .distance = 4}.offset();
    QVERIFY(std::abs(inner.width() - 4) < 1e-12 && std::abs(inner.height()) < 1e-12);
    const QSizeF slant = InnerShadowEffect{.angle = -45, .distance = std::sqrt(2.0)}.offset();
    QVERIFY(std::abs(slant.width() + 1) < 1e-12 && std::abs(slant.height() + 1) < 1e-12);
}

void LayerEffectsTests::theHelpersReadAndWriteOneKind()
{
    LayerEffects effects;
    QVERIFY(effects.isEmpty() && effects.kinds().empty());
    effects.innerShadow = InnerShadowEffect();
    effects.stroke = StrokeEffect{.enabled = false};
    QVERIFY(!effects.isEmpty());
    // Swift's allCases order, whatever was added first.
    QVERIFY((effects.kinds() == std::vector{LayerEffectKind::stroke, LayerEffectKind::innerShadow}));
    QVERIFY(effects.contains(LayerEffectKind::stroke) && !effects.contains(LayerEffectKind::shadow));
    // Absent `enabled` is shown; false is hidden; missing is neither.
    QVERIFY(!effects.isEnabled(LayerEffectKind::stroke) && effects.isEnabled(LayerEffectKind::innerShadow) && !effects.isEnabled(LayerEffectKind::shadow));
    QVERIFY(effects.color(LayerEffectKind::innerShadow) == PaletteColor::black() && !effects.color(LayerEffectKind::colorOverlay));
    effects.setColor(PaletteColor{0.2, 0.4, 0.6}, LayerEffectKind::stroke);
    effects.setColor(PaletteColor{1, 1, 1}, LayerEffectKind::colorOverlay);
    QVERIFY(effects.color(LayerEffectKind::stroke) == (PaletteColor{0.2, 0.4, 0.6}) && !effects.colorOverlay);
    effects.setEnabled(true, LayerEffectKind::stroke);
    effects.setEnabled(false, LayerEffectKind::innerShadow);
    effects.setEnabled(true, LayerEffectKind::shadow);
    QVERIFY(effects.stroke.value().enabled == true && effects.innerShadow.value().enabled == false && !effects.shadow);
    // Visible keeps what is switched on.
    const LayerEffects shown = effects.visible();
    QVERIFY(shown.stroke == effects.stroke && !shown.innerShadow && !shown.shadow && !shown.colorOverlay);
    for (const LayerEffectKind kind : allLayerEffectKinds) {
        LayerEffects full = every();
        full.remove(kind);
        QVERIFY(!full.contains(kind) && full.kinds().size() == 5);
        full.setColor(PaletteColor{1, 0, 0}, kind);
        QVERIFY(!full.color(kind));
    }
    // Each kind writes its own colour and switch alone.
    for (const LayerEffectKind kind : allLayerEffectKinds) {
        LayerEffects full = every();
        const LayerEffects before = full;
        full.setColor(PaletteColor{0.5, 0.5, 0.25}, kind);
        full.setEnabled(!before.isEnabled(kind), kind);
        for (const LayerEffectKind other : allLayerEffectKinds) {
            QCOMPARE(full.color(other) == (PaletteColor{0.5, 0.5, 0.25}), other == kind);
            QCOMPARE(full.isEnabled(other) == before.isEnabled(other), other != kind);
        }
    }
    const LayerEffects colours = every();
    QVERIFY(colours.color(LayerEffectKind::shadow) == (PaletteColor{0.5, 0.6, 0.7}));
    QVERIFY(colours.color(LayerEffectKind::colorOverlay) == (PaletteColor{0.9, 0.25, 0.75}));
    QVERIFY(colours.color(LayerEffectKind::innerShadow) == (PaletteColor{0.05, 0.15, 0.95}));
    QVERIFY(colours.isEnabled(LayerEffectKind::colorOverlay) && colours.isEnabled(LayerEffectKind::shadow));
}

void LayerEffectsTests::theManifestWritesSwiftsKeys()
{
    const QJsonObject encoded = ManifestJson::encoded(every());
    QCOMPARE(encoded.keys(), (QStringList{"colorOverlay", "innerGlow", "innerShadow", "outerGlow", "shadow", "stroke"}));
    const QJsonObject stroke = encoded.value("stroke").toObject();
    QCOMPARE(stroke.keys(), (QStringList{"blue", "enabled", "green", "inside", "opacity", "red", "size"}));
    QVERIFY(stroke.value("enabled") == false && stroke.value("inside") == true && stroke.value("size") == 7);
    // `enabled` stays out while absent, as Swift's encodeIfPresent.
    QCOMPARE(encoded.value("shadow").toObject().keys(), (QStringList{"angle", "blue", "blur", "distance", "green", "opacity", "red"}));
    QCOMPARE(encoded.value("colorOverlay").toObject().keys(), (QStringList{"blue", "enabled", "green", "opacity", "red"}));
    QCOMPARE(encoded.value("innerShadow").toObject().value("angle"), QJsonValue(-30));
    QCOMPARE(encoded.value("outerGlow").toObject().keys(), (QStringList{"blue", "green", "opacity", "red", "size"}));
    QCOMPARE(encoded.value("outerGlow").toObject().value("size"), QJsonValue(33));
    QCOMPARE(encoded.value("innerGlow").toObject().keys(), (QStringList{"blue", "enabled", "green", "opacity", "red", "size"}));
    QVERIFY(encoded.value("innerGlow").toObject().value("size") == 14 && encoded.value("innerGlow").toObject().value("enabled") == false);
    QVERIFY(ManifestJson::effects(encoded) == every());
    // Absent effects stay out; null reads as absent.
    LayerEffects one;
    one.shadow = ShadowEffect();
    QCOMPARE(ManifestJson::encoded(one).keys(), QStringList{"shadow"});
    QVERIFY(ManifestJson::encoded(LayerEffects()).isEmpty());
    QJsonObject nulled = encoded;
    nulled.insert("stroke", QJsonValue::Null);
    nulled.insert("future", 3);
    LayerEffects expected = every();
    expected.stroke.reset();
    QVERIFY(ManifestJson::effects(nulled) == expected);
    // The record carries them through the manifest and back.
    ProjectManifest manifest = withEffects()->projectSnapshot().value().manifest;
    QVERIFY(manifest.layers[0].effects == every());
    QVERIFY(ProjectManifest::decoded(manifest.encoded()).layers[0].effects == every());
    manifest.layers[0].effects.reset();
    QVERIFY(!manifest.encoded().contains("effects"));
}

void LayerEffectsTests::decodingRefusesWhatSwiftRefuses()
{
    const QJsonObject encoded = ManifestJson::encoded(every());
    QVERIFY(refused(QJsonObject{{"stroke", 3}}) && refused(QJsonObject{{"shadow", QJsonArray{}}}));
    QVERIFY(!refused(QJsonObject{}));
    // Every required key of every effect, taken away or mistyped.
    for (const QString &effect : encoded.keys()) {
        const QJsonObject keys = encoded.value(effect).toObject();
        for (const QString &key : keys.keys()) {
            QJsonObject missing = keys, mistyped = keys;
            missing.remove(key);
            mistyped.insert(key, QStringLiteral("x"));
            QJsonObject without = encoded, wrong = encoded;
            without.insert(effect, missing);
            wrong.insert(effect, mistyped);
            QVERIFY2(refused(wrong), qPrintable(effect + " " + key));
            QCOMPARE(refused(without), key != QStringLiteral("enabled"));
        }
    }
    // A number for a flag, a flag for a number.
    QJsonObject stroke = encoded.value("stroke").toObject();
    stroke.insert("inside", 1);
    QVERIFY(refused(QJsonObject{{"stroke", stroke}}));
    QJsonObject shadow = encoded.value("shadow").toObject();
    shadow.insert("blur", true);
    QVERIFY(refused(QJsonObject{{"shadow", shadow}}));
    try {
        ManifestJson::effects(QJsonValue(3));
        QFAIL("a number is no set of effects");
    } catch (const ProjectError &error) {
        QCOMPARE(error.kind, ProjectError::Kind::invalid);
    }
}

void LayerEffectsTests::effectsSaveLoadAndExportWithTheirLayer()
{
    // Beyond Swift, whose snapshot drops them: they save and load.
    const std::unique_ptr<EditorSession> session = withEffects();
    const QUuid id = session->activeLayerID().value();
    const ProjectSnapshot snapshot = session->projectSnapshot().value();
    QVERIFY(snapshot.manifest.layers[0].effects == every());
    QTemporaryDir root;
    ProjectStore::save(snapshot, root.filePath("Effects.comp"));
    const ProjectSnapshot loaded = ProjectStore::load(root.filePath("Effects.comp"));
    QVERIFY(loaded.manifest.layers[0].effects == every());
    EditorSession restored;
    restored.installProject(loaded, root.filePath("Effects.comp"));
    QVERIFY(layerWith(restored, id).effects == every());
    // A layer without them records none.
    session->setEffects(LayerEffects());
    QVERIFY(!session->activeLayer().value().effects && !session->projectSnapshot().value().manifest.layers[0].effects);
}

void LayerEffectsTests::duplicatesAndBrushCommitsKeepThem()
{
    const std::unique_ptr<EditorSession> session = withEffects();
    session->duplicateActiveLayer();
    QVERIFY(session->activeLayer().value().effects == every());
    // Swift's brush commit lists them: painted layers keep them.
    session->selectTool(NavigationTool::brush);
    session->beginBrush(QPointF(5, 5));
    session->continueBrush(QPointF(12, 5));
    session->finishBrushImmediately();
    QTRY_VERIFY(!session->isProjectBusy());
    QCOMPARE(session->history.undoName(), QString("Brush Stroke"));
    QVERIFY(session->activeLayer().value().effects == every());
    // So does a raster edit's commit.
    session->selectAll();
    QVERIFY(awaited([&](std::function<void()> done) { session->fillSelection(EditorSession::FillSource::foreground, std::move(done)); }));
    QCOMPARE(session->history.undoName(), QString("Fill"));
    QVERIFY(session->activeLayer().value().effects == every());
    // Since 1.2.4 (`e08dd8c`) a filter's commit keeps them.
    session->beginFilter(FilterKind::exposure);
    FilterSettings settings = session->filterEdit().value().settings;
    settings.exposure.exposure = 1;
    session->updateFilter(settings, false);
    QVERIFY(awaited([&](std::function<void()> done) { session->commitFilter(std::move(done)); }));
    QCOMPARE(session->history.undoName(), QString("Exposure"));
    QVERIFY(session->activeLayer().value().effects == every());
}

void LayerEffectsTests::rebuildsCopiesAndResizesDropThem()
{
    // Swift rebuilds these layers from lists that leave effects out.
    const auto dropped = [](const std::function<void(EditorSession &)> &edit, const char *name) {
        const std::unique_ptr<EditorSession> session = withEffects();
        edit(*session);
        return session->history.undoName() == QString::fromLatin1(name) && !session->activeLayer().value().effects;
    };
    QVERIFY(dropped([](EditorSession &session) { QVERIFY(awaited([&](std::function<void()> done) { session.invertPixels(std::move(done)); })); }, "Invert"));
    QVERIFY(dropped([](EditorSession &session) {
        session.beginLevels();
        LevelsSettings settings;
        settings.ranges[0].outputWhite = 100;
        session.updateLevels(settings, false);
        QVERIFY(awaited([&](std::function<void()> done) { session.commitLevels(std::move(done)); }));
    }, "Levels"));
    QVERIFY(dropped([](EditorSession &session) {
        session.beginHueSaturation();
        session.updateHueSaturation(HueSaturationSettings(90), false);
        QVERIFY(awaited([&](std::function<void()> done) { session.commitHueSaturation(std::move(done)); }));
    }, "Hue/Saturation"));
    QVERIFY(dropped([](EditorSession &session) {
        session.applySelection(rectPath(QRectF(2, 2, 6, 4)), SelectionMode::replace, QStringLiteral("Select"));
        QVERIFY(awaited([&](std::function<void()> done) { session.beginSelectionTransform(std::move(done)); }));
        LayerTransform draft = session.transformEdit().value().draft;
        draft.origin += QPointF(3, 0);
        session.previewTransform(draft);
        session.commitTransform();
    }, "Transform Selection"));
    // Both resizers rebuild their records without them, as Swift's.
    const ProjectSnapshot snapshot = withEffects()->projectSnapshot().value();
    QVERIFY(!ImageResizer::resize(snapshot, ImageSizeOptions{.width = 40, .height = 20, .resolution = 72}).manifest.layers[0].effects);
    QVERIFY(!CanvasResizer::resize(snapshot, CanvasSizeOptions{.width = 30, .height = 12}).manifest.layers[0].effects);
}

QTEST_GUILESS_MAIN(LayerEffectsTests)
#include "LayerEffectsTests.moc"
