#include "Document/EditorSession.h"
#include "IO/CanvasResizer.h"
#include "IO/ImageResizer.h"
#include "IO/ProjectStore+Json.h"
#include "ProjectFixtures.h"
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>

// Adjustment layers in the document, the manifest and the store.
namespace {
constexpr double notANumber = std::numeric_limits<double>::quiet_NaN();

// Hue/Saturation with a range and every optional set.
LayerAdjustment full()
{
    LayerAdjustment adjustment{.kind = AdjustmentKind::hsv, .hue = 10, .saturation = -20, .lightness = 30, .colorize = true};
    HueSaturationSettings settings(40, 15, -25, true, ColorRange::reds);
    settings.invertRange = true;
    settings.bands[ColorRange::greens] = HueBand{70, 100, 140, 170};
    adjustment.hsvSettings = settings;
    adjustment.levels.channel = LevelsChannel::red;
    adjustment.levels.ranges[1].outputWhite = 200;
    adjustment.curves.channel = LevelsChannel::blue;
    adjustment.curves.channels[2] = {{0, 30}, {255, 255}};
    adjustment.setExposure(ExposureSettings{.exposure = 1, .offset = 0.1, .gamma = 2});
    adjustment.setGradientMap(GradientMapSettings{{1, 0, 0}, {0, 0, 1}, true});
    adjustment.setGrain(GrainSettings{.amount = 40, .size = 3, .roughness = 10, .seed = 9});
    adjustment.setBlackWhite(BlackWhiteSettings{.reds = -50, .magentas = 250, .tint = true, .tintHue = 200, .tintSaturation = 35});
    adjustment.setColorBalance(ColorBalanceSettings{.shadowYellowBlue = -30, .midCyanRed = 20, .highlightMagentaGreen = 90, .preserveLuminosity = false});
    return adjustment;
}

bool refused(const QJsonObject &object)
{
    try {
        ManifestJson::adjustment(object);
    } catch (const ProjectError &) {
        return true;
    }
    return false;
}

// The same rules guard saving and loading.
std::optional<ProjectError::Kind> stored(const ProjectSnapshot &snapshot)
{
    QTemporaryDir root;
    const std::optional<ProjectError::Kind> saving = projectError([&] { ProjectStore::save(snapshot, root.filePath("Saved.comp")); });
    ProjectStore::save(twoLayers(), root.filePath("Read.comp"));
    for (const auto &[id, pixels] : snapshot.images) {
        if (!pixels.image().save(root.filePath("Read.comp/images/") + uuidString(id) + ".png"))
            throw std::runtime_error("cannot write a fixture image");
    }
    overwrite(root.filePath("Read.comp/manifest.json"), snapshot.manifest.encoded());
    const std::optional<ProjectError::Kind> loading = projectError([&] { ProjectStore::load(root.filePath("Read.comp")); });
    if (saving != loading)
        throw std::runtime_error("saving and loading judged the project apart");
    return saving;
}

std::unique_ptr<EditorSession> oneLayer()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(4, 4);
    session->insert(asset(halfRed().copy(0, 0, 4, 4), "Red"));
    return session;
}

// Swift's tests clear the request: no editor opens here.
QUuid added(EditorSession &session, AdjustmentKind kind)
{
    session.addAdjustment(kind);
    session.setAdjustmentEditingID(std::nullopt);
    return session.activeLayerID().value();
}
}

class LayerAdjustmentTests : public QObject {
    Q_OBJECT
private slots:
    void kindsKeepSwiftsNamesAndOrder();
    void validityFollowsSwiftsBounds();
    void theOptionalSettingsFallBackToTheirDefaults();
    void theManifestWritesSwiftsKeys();
    void decodingRefusesWhatSwiftRefuses();
    void aSeedReadsWhatQtsParserReads();
    void theStoreKeepsAdjustmentsToVersion7WithoutPixels();
    void nothingClipsToAnAdjustment();
    void addingPlacesTheLayerAboveTheActiveOne();
    void updatesTakeValidSettingsAlone();
    void adjustmentsTravelWithTheirLayers();
};

void LayerAdjustmentTests::kindsKeepSwiftsNamesAndOrder()
{
    const std::pair<AdjustmentKind, const char *> kinds[] = {{AdjustmentKind::hsv, "Hue/Saturation"}, {AdjustmentKind::levels, "Levels"},
                                                             {AdjustmentKind::curves, "Curves"},      {AdjustmentKind::exposure, "Exposure"},
                                                             {AdjustmentKind::gradientMap, "Gradient Map"}, {AdjustmentKind::grain, "Grain"},
                                                             {AdjustmentKind::invert, "Invert"},      {AdjustmentKind::blackWhite, "Black & White"},
                                                             {AdjustmentKind::colorBalance, "Color Balance"}};
    QCOMPARE(allAdjustmentKinds.size(), std::size(kinds));
    for (size_t index = 0; index < std::size(kinds); ++index) {
        const auto &[kind, name] = kinds[index];
        QVERIFY(allAdjustmentKinds[index] == kind);
        QCOMPARE(rawValue(kind), QString(name));
        QVERIFY(adjustmentKind(QString(name)) == kind);
    }
    QVERIFY(!adjustmentKind(QStringLiteral("Vibrance")) && !adjustmentKind(QStringLiteral("levels")) && !adjustmentKind(QStringLiteral(" Levels")));
    // The filter panel edits four; Levels and Hue/Saturation have theirs.
    QVERIFY(filterKind(AdjustmentKind::curves) == FilterKind::curves && filterKind(AdjustmentKind::exposure) == FilterKind::exposure);
    QVERIFY(filterKind(AdjustmentKind::gradientMap) == FilterKind::gradientMap && filterKind(AdjustmentKind::grain) == FilterKind::grain);
    QVERIFY(filterKind(AdjustmentKind::blackWhite) == FilterKind::blackWhite && filterKind(AdjustmentKind::colorBalance) == FilterKind::colorBalance);
    QVERIFY(!filterKind(AdjustmentKind::levels) && !filterKind(AdjustmentKind::hsv) && !filterKind(AdjustmentKind::invert));
    // Every kind but Invert opens an editor.
    for (const AdjustmentKind kind : allAdjustmentKinds)
        QCOMPARE(isEditable(kind), kind != AdjustmentKind::invert);
}

void LayerAdjustmentTests::validityFollowsSwiftsBounds()
{
    QVERIFY(full().isValid() && LayerAdjustment{AdjustmentKind::levels}.isValid());
    // Hue within 360 either way, the others within 100.
    const std::tuple<double, double, double, bool> legacy[] = {
        {360, -100, 100, true}, {-360.5, 0, 0, false}, {0, 100.5, 0, false}, {0, 0, -100.5, false}, {notANumber, 0, 0, false}, {0, 0, notANumber, false}};
    for (const auto &[hue, saturation, lightness, valid] : legacy)
        QCOMPARE((LayerAdjustment{.kind = AdjustmentKind::hsv, .hue = hue, .saturation = saturation, .lightness = lightness}.isValid()), valid);
    // Each range's values too, and every band's handles finite.
    LayerAdjustment ranged = full();
    ranged.hsvSettings->adjustments[ColorRange::blues] = {0, 0, 101};
    QVERIFY(!ranged.isValid());
    ranged = full();
    ranged.hsvSettings->bands[ColorRange::cyans].falloffEnd = std::numeric_limits<double>::infinity();
    QVERIFY(!ranged.isValid());
    // The legacy fields count only when no range settings exist.
    ranged = full();
    ranged.hsvSettings->adjustments[ColorRange::reds] = {-360, -100, -100};
    QVERIFY(ranged.isValid());
    // Levels as normalized, then every kind's own rules.
    LayerAdjustment levels{AdjustmentKind::hsv};
    levels.levels.ranges[3].white = levels.levels.ranges[3].black;
    QVERIFY(!levels.isValid());
    LayerAdjustment curves{AdjustmentKind::hsv};
    curves.curves.channels[1] = {{0, 0}};
    QVERIFY(!curves.isValid());
    LayerAdjustment exposure{AdjustmentKind::hsv};
    exposure.setExposure(ExposureSettings{.gamma = 0});
    QVERIFY(!exposure.isValid());
    LayerAdjustment map{AdjustmentKind::hsv};
    map.setGradientMap(GradientMapSettings{{1.5, 0, 0}, {1, 1, 1}, false});
    QVERIFY(!map.isValid());
    LayerAdjustment grain{AdjustmentKind::hsv};
    grain.setGrain(GrainSettings{.amount = 101});
    QVERIFY(!grain.isValid());
}

void LayerAdjustmentTests::theOptionalSettingsFallBackToTheirDefaults()
{
    LayerAdjustment adjustment{.kind = AdjustmentKind::hsv, .hue = 120, .saturation = 10, .lightness = -5, .colorize = true};
    QVERIFY(!adjustment.exposureSettings && !adjustment.gradientMapSettings && !adjustment.grainSettings);
    QVERIFY(adjustment.exposure() == ExposureSettings() && adjustment.gradientMap() == GradientMapSettings() && adjustment.grain() == GrainSettings());
    // Without range settings Hue/Saturation reads the legacy four.
    QVERIFY(adjustment.resolvedHSV() == HueSaturationSettings(120, 10, -5, true));
    adjustment.hsvSettings = HueSaturationSettings(30);
    QVERIFY(adjustment.resolvedHSV() == HueSaturationSettings(30));
    adjustment.setExposure(ExposureSettings{.exposure = 2});
    adjustment.setGradientMap(GradientMapSettings{{0, 1, 0}, {1, 1, 1}, false});
    adjustment.setGrain(GrainSettings{.seed = 5});
    QCOMPARE(adjustment.exposureSettings.value().exposure, 2.0);
    QVERIFY(adjustment.gradientMap().shadows == AdjustmentColor(0, 1, 0) && adjustment.grainSettings.value().seed == 5);
}

void LayerAdjustmentTests::theManifestWritesSwiftsKeys()
{
    const QJsonObject object = ManifestJson::encoded(full());
    QCOMPARE(object.keys(), (QStringList{"blackWhiteSettings", "colorBalanceSettings", "colorize", "curves", "exposureSettings", "gradientMapSettings",
                                         "grainSettings", "hsvSettings", "hue", "kind", "levels", "lightness", "saturation"}));
    QCOMPARE(object.value("kind").toString(), QString("Hue/Saturation"));
    const QJsonObject hsv = object.value("hsvSettings").toObject();
    QCOMPARE(hsv.keys(), (QStringList{"adjustments", "bands", "colorize", "invertRange", "range"}));
    QCOMPARE(hsv.value("range").toString(), QString("Reds"));
    // A dictionary keyed by ColorRange: key and value in turn.
    const QJsonArray adjustments = hsv.value("adjustments").toArray();
    QVERIFY(adjustments.size() == 2 && adjustments.at(0).toString() == QString("Reds"));
    QCOMPARE(adjustments.at(1).toObject(), (QJsonObject{{"hue", 40}, {"saturation", 15}, {"lightness", -25}}));
    const QJsonArray bands = hsv.value("bands").toArray();
    QVERIFY(bands.size() == 14 && bands.at(0).toString() == QString("Master"));
    QCOMPARE(bands.at(3).toObject(), (QJsonObject{{"falloffStart", 315}, {"rangeStart", 345}, {"rangeEnd", 15}, {"falloffEnd", 45}}));
    const QJsonObject levels = object.value("levels").toObject();
    QVERIFY(levels.value("channel").toString() == QString("Red") && levels.value("ranges").toArray().size() == 4);
    QCOMPARE(levels.value("ranges").toArray().at(1).toObject(),
             (QJsonObject{{"black", 0}, {"gamma", 1}, {"white", 255}, {"outputBlack", 0}, {"outputWhite", 200}}));
    const QJsonObject curves = object.value("curves").toObject();
    QVERIFY(curves.value("channel").toString() == QString("Blue") && curves.value("channels").toArray().size() == 4);
    QCOMPARE(curves.value("channels").toArray().at(2).toArray().at(0).toObject(), (QJsonObject{{"x", 0}, {"y", 30}}));
    QCOMPARE(object.value("exposureSettings").toObject(), (QJsonObject{{"exposure", 1}, {"offset", 0.1}, {"gamma", 2}}));
    QCOMPARE(object.value("gradientMapSettings").toObject(),
             (QJsonObject{{"shadows", QJsonObject{{"red", 1}, {"green", 0}, {"blue", 0}}}, {"highlights", QJsonObject{{"red", 0}, {"green", 0}, {"blue", 1}}},
                          {"reversed", true}}));
    QCOMPARE(object.value("grainSettings").toObject(), (QJsonObject{{"amount", 40}, {"size", 3}, {"roughness", 10}, {"seed", 9}}));
    QVERIFY(ManifestJson::adjustment(object) == full());
    // Through the whole manifest, beside the other keys.
    ProjectManifest manifest = twoLayers().manifest;
    manifest.layers[1].adjustment = full();
    const ProjectManifest decoded = ProjectManifest::decoded(manifest.encoded());
    QVERIFY(decoded.layers[1].adjustment == full() && !decoded.layers[0].adjustment);
}

void LayerAdjustmentTests::decodingRefusesWhatSwiftRefuses()
{
    const QJsonObject good = ManifestJson::encoded(full());
    QVERIFY(!refused(good));
    const auto without = [&good](const char *key) {
        QJsonObject object = good;
        object.remove(QLatin1String(key));
        return object;
    };
    const auto with = [&good](const char *key, const QJsonValue &value) {
        QJsonObject object = good;
        object.insert(QLatin1String(key), value);
        return object;
    };
    // Synthesized decoding needs every key but the optionals.
    for (const char *key : {"kind", "hue", "saturation", "lightness", "colorize", "levels", "curves"})
        QVERIFY2(refused(without(key)), key);
    for (const char *key : {"hsvSettings", "exposureSettings", "gradientMapSettings", "grainSettings", "blackWhiteSettings", "colorBalanceSettings"}) {
        QVERIFY2(!refused(without(key)) && !refused(with(key, QJsonValue::Null)), key);
        QVERIFY2(refused(with(key, QJsonArray())), key);
    }
    QVERIFY(refused(with("kind", "Vibrance")) && refused(with("hue", "10")) && refused(with("colorize", 1)));
    // Pairs come whole, with known ranges, in an array.
    const auto hsv = [&](const char *key, const QJsonValue &value) {
        QJsonObject settings = good.value("hsvSettings").toObject();
        settings.insert(QLatin1String(key), value);
        return with("hsvSettings", settings);
    };
    QVERIFY(refused(hsv("adjustments", QJsonArray{"Reds"})) && refused(hsv("adjustments", QJsonArray{"Pinks", QJsonObject{{"hue", 1}, {"saturation", 0}, {"lightness", 0}}})));
    QVERIFY(refused(hsv("adjustments", QJsonObject())) && refused(hsv("range", "Violets")));
    QVERIFY(!refused(hsv("adjustments", QJsonArray())));
    // Later pairs win, as a Swift dictionary assigns.
    const QJsonObject twice = hsv("adjustments", QJsonArray{"Reds", QJsonObject{{"hue", 1}, {"saturation", 0}, {"lightness", 0}}, "Reds",
                                                            QJsonObject{{"hue", 2}, {"saturation", 0}, {"lightness", 0}}});
    QCOMPARE(ManifestJson::adjustment(twice).hsvSettings.value().adjustments.at(ColorRange::reds).hue, 2.0);
    // Levels holds four ranges; channels are Swift's names.
    QJsonObject levels = good.value("levels").toObject();
    QJsonArray ranges = levels.value("ranges").toArray();
    ranges.append(ranges.first());
    levels.insert("ranges", ranges);
    QVERIFY(refused(with("levels", levels)));
    ranges.removeLast();
    ranges.removeLast();
    levels.insert("ranges", ranges);
    QVERIFY(refused(with("levels", levels)));
    levels = good.value("levels").toObject();
    levels.insert("channel", "Alpha");
    QVERIFY(refused(with("levels", levels)));
    QJsonObject curves = good.value("curves").toObject();
    curves.insert("channels", QJsonObject());
    QVERIFY(refused(with("curves", curves)));
    // A seed is Swift's UInt32.
    const auto seeded = [&](const QJsonValue &seed) {
        QJsonObject grain = good.value("grainSettings").toObject();
        grain.insert("seed", seed);
        return with("grainSettings", grain);
    };
    QVERIFY(refused(seeded(-1)) && refused(seeded(qint64(4'294'967'296))) && refused(seeded(1.5)));
    QCOMPARE(ManifestJson::adjustment(seeded(qint64(4'294'967'295))).grainSettings.value().seed, 4'294'967'295u);
}

void LayerAdjustmentTests::aSeedReadsWhatQtsParserReads()
{
    ProjectManifest manifest = twoLayers().manifest;
    manifest.layers[1].adjustment = full();
    const QByteArray json = manifest.encoded();
    QVERIFY(json.contains("\"seed\": 9"));
    const auto seed = [&json](const char *token) {
        QByteArray changed = json;
        changed.replace("\"seed\": 9", QByteArray("\"seed\": ") + token);
        return ProjectManifest::decoded(changed).layers[1].adjustment.value().grainSettings.value().seed;
    };
    // Whole numbers written 5.0 or 5e0 read as Swift's.
    QCOMPARE(seed("5.0"), 5u);
    QCOMPARE(seed("5e0"), 5u);
    // Qt's parser fails a token that underflows; Swift reads zero.
    QCOMPARE(projectError([&] { seed("1e-400"); }), ProjectError::Kind::invalid);
}

void LayerAdjustmentTests::theStoreKeepsAdjustmentsToVersion7WithoutPixels()
{
    ProjectSnapshot snapshot = twoLayers();
    ProjectLayerRecord &blank = snapshot.manifest.layers[1];
    blank.adjustment = full();
    QCOMPARE(stored(snapshot), std::nullopt);
    snapshot.manifest.version = 6;
    QCOMPARE(stored(snapshot), ProjectError::Kind::invalid);
    snapshot.manifest.version = 7;
    // Pixels, a folder or invalid settings refuse it.
    blank.imageFile = uuidString(blank.id) + ".png";
    snapshot.images.insert({blank.id, asset(halfRed(), "pixels")});
    QCOMPARE(stored(snapshot), ProjectError::Kind::invalid);
    blank.imageFile = std::nullopt;
    snapshot.images.erase(blank.id);
    blank.isGroup = true;
    QCOMPARE(stored(snapshot), ProjectError::Kind::invalid);
    blank.isGroup = std::nullopt;
    blank.adjustment->hue = 400;
    QCOMPARE(stored(snapshot), ProjectError::Kind::invalid);
}

void LayerAdjustmentTests::nothingClipsToAnAdjustment()
{
    ProjectSnapshot snapshot = twoLayers();
    ProjectLayerRecord &image = snapshot.manifest.layers[0], &blank = snapshot.manifest.layers[1];
    // An adjustment clips to a layer; nothing clips to it.
    blank.adjustment = full();
    blank.maskSourceID = image.id;
    QCOMPARE(stored(snapshot), std::nullopt);
    blank.maskSourceID = std::nullopt;
    std::swap(snapshot.manifest.layers[0], snapshot.manifest.layers[1]);
    snapshot.manifest.layers[1].maskSourceID = snapshot.manifest.layers[0].id;
    QCOMPARE(stored(snapshot), ProjectError::Kind::invalid);
    // The session will not link a layer to one either.
    const std::unique_ptr<EditorSession> session = oneLayer();
    const QUuid red = session->activeLayerID().value();
    const QUuid levels = added(*session, AdjustmentKind::levels);
    QVERIFY(session->canLinkMask(red, levels) && !session->canLinkMask(levels, red));
}

void LayerAdjustmentTests::addingPlacesTheLayerAboveTheActiveOne()
{
    const std::unique_ptr<EditorSession> session = oneLayer();
    session->setForegroundColor(PaletteColor{1, 0, 0});
    session->setBackgroundColor(PaletteColor{0, 0, 1});
    session->addBlankLayer();
    const QUuid top = session->activeLayerID().value();
    session->selectLayer(session->document().value().layers[0].id);
    const int steps = session->history.undoCount();
    session->addAdjustment(AdjustmentKind::gradientMap);
    // Above the active layer, blank, the canvas's size.
    const std::vector<ImageLayer> &layers = session->document().value().layers;
    QCOMPARE(layers.size(), size_t(3));
    const ImageLayer &made = layers[1];
    QVERIFY(made.id == session->activeLayerID() && layers[2].id == top && !made.asset && !made.parentID);
    QVERIFY(made.name == QString("Gradient Map") && made.transform.size == QSizeF(4, 4));
    QVERIFY(session->history.undoCount() == steps + 1 && session->history.undoName() == QString("New Gradient Map Adjustment"));
    // Its editor is asked for; Swift's panel then opens it.
    QCOMPARE(session->adjustmentEditingID(), std::optional(made.id));
    session->setAdjustmentEditingID(std::nullopt);
    // A Gradient Map runs from the foreground to the background.
    const LayerAdjustment &map = made.adjustment.value();
    QVERIFY(map.kind == AdjustmentKind::gradientMap && map.gradientMap().shadows == AdjustmentColor(1, 0, 0));
    QVERIFY(map.gradientMap().highlights == AdjustmentColor(0, 0, 1) && !map.grainSettings);
    // Each Grain layer rolls its own pattern; others keep defaults.
    added(*session, AdjustmentKind::grain);
    const quint32 first = session->activeLayer().value().adjustment.value().grain().seed;
    added(*session, AdjustmentKind::grain);
    QVERIFY(session->activeLayer().value().adjustment.value().grain().seed != first);
    added(*session, AdjustmentKind::curves);
    QVERIFY(session->activeLayer().value().adjustment.value() == LayerAdjustment{AdjustmentKind::curves});
    // A folder takes it inside, uncollapsed.
    session->selectLayer(top);
    session->groupSelectedLayers();
    const QUuid folder = session->activeLayerID().value();
    session->toggleGroupExpansion(folder);
    QVERIFY(session->collapsedGroupIDs().contains(folder));
    added(*session, AdjustmentKind::levels);
    QVERIFY(session->activeLayer().value().parentID == folder && !session->collapsedGroupIDs().contains(folder));
    // Beside a layer in a folder, it joins that folder.
    session->selectLayer(top);
    added(*session, AdjustmentKind::exposure);
    QVERIFY(session->activeLayer().value().parentID == folder);
    // Ten thousand layers are the most a project holds.
    ProjectSnapshot crowded = twoLayers();
    while (crowded.manifest.layers.size() < 9'999)
        crowded.manifest.layers.push_back({.id = QUuid::createUuid(), .name = "n", .isVisible = true,
                                           .transform = {.origin = {0, 0}, .size = {1, 1}}, .imageFile = std::nullopt});
    EditorSession packed;
    packed.installProject(crowded, QString());
    added(packed, AdjustmentKind::levels);
    packed.addAdjustment(AdjustmentKind::levels);
    QCOMPARE(packed.document().value().layers.size(), size_t(10'000));
    // Without a document or with the layers busy, nothing.
    EditorSession empty;
    empty.addAdjustment(AdjustmentKind::levels);
    QVERIFY(!empty.document());
    session->setIsProjectBusy(true);
    const size_t count = session->document().value().layers.size();
    session->addAdjustment(AdjustmentKind::levels);
    QCOMPARE(session->document().value().layers.size(), count);
}

void LayerAdjustmentTests::updatesTakeValidSettingsAlone()
{
    const std::unique_ptr<EditorSession> session = oneLayer();
    const QUuid id = added(*session, AdjustmentKind::levels);
    const int steps = session->history.undoCount(), revision = session->brushRevision();
    QSignalSpy changed(session.get(), &EditorSession::changed);
    LayerAdjustment value{AdjustmentKind::levels};
    value.levels.ranges[0].outputWhite = 0;
    session->updateAdjustment(id, value);
    // No step of its own; the canvas hears of it.
    QVERIFY(session->activeLayer().value().adjustment == value && session->history.undoCount() == steps);
    QVERIFY(session->brushRevision() == revision + 1 && changed.count() == 1);
    // Invalid settings, or a missing layer, change nothing.
    LayerAdjustment wild = value;
    wild.hue = 1000;
    session->updateAdjustment(id, wild);
    session->updateAdjustment(QUuid::createUuid(), LayerAdjustment{AdjustmentKind::levels});
    QVERIFY(session->activeLayer().value().adjustment == value && changed.count() == 1);
    // An adjustment layer paints nothing; its mask still paints.
    QVERIFY(!session->canPaint());
    session->addMask();
    QVERIFY(session->isMaskSelected() && session->canPaint());
}

void LayerAdjustmentTests::adjustmentsTravelWithTheirLayers()
{
    const std::unique_ptr<EditorSession> session = oneLayer();
    const QUuid id = added(*session, AdjustmentKind::hsv);
    session->updateAdjustment(id, full());
    // The record, the store and the install carry it whole.
    QVERIFY(session->activeLayer().value().hierarchyRecord().adjustment == full());
    QTemporaryDir root;
    ProjectStore::save(session->projectSnapshot().value(), root.filePath("Adjusted.comp"));
    const ProjectSnapshot loaded = ProjectStore::load(root.filePath("Adjusted.comp"));
    EditorSession restored;
    restored.installProject(loaded, root.filePath("Adjusted.comp"));
    QVERIFY(restored.document().value().layers[1].adjustment == full());
    // Both resizers pass it on, as Swift's rebuilds list it.
    const ProjectSnapshot resized = ImageResizer::resize(loaded, ImageSizeOptions{.width = 8, .height = 8, .resolution = 72});
    QVERIFY(resized.manifest.layers[1].adjustment == full());
    const ProjectSnapshot extended = CanvasResizer::resize(loaded, CanvasSizeOptions{.width = 6, .height = 5});
    QVERIFY(extended.manifest.layers[1].adjustment == full());
    // A duplicate keeps it; the layer's equality reads it.
    session->duplicateActiveLayer();
    QVERIFY(session->activeLayer().value().adjustment == full());
    ImageLayer changed = session->activeLayer().value();
    changed.adjustment->hue = 11;
    QVERIFY(!(changed == session->activeLayer().value()));
}

QTEST_MAIN(LayerAdjustmentTests)
#include "LayerAdjustmentTests.moc"
