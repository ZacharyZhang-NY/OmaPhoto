#include "ProjectFixtures.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>

namespace {
// A manifest as macOS writes it: spaced colons, escaped slashes.
const char *fromMacOS = R"({
  "activeLayerID" : "0F0E0D0C-0B0A-4908-8706-050403020100",
  "colorSpace" : "sRGB",
  "documentID" : "00112233-4455-4677-8899-AABBCCDDEEFF",
  "format" : "com.compositor.project",
  "height" : 80,
  "layers" : [
    {
      "blendMode" : "Color Dodge",
      "id" : "0F0E0D0C-0B0A-4908-8706-050403020100",
      "imageFile" : "0F0E0D0C-0B0A-4908-8706-050403020100.png",
      "isVisible" : false,
      "maskEnabled" : false,
      "maskFile" : "0F0E0D0C-0B0A-4908-8706-050403020100.mask.png",
      "maskLinked" : false,
      "maskPlacement" : {
        "flipX" : false,
        "flipY" : true,
        "origin" : [ 3, 4.5 ],
        "rotation" : 10,
        "sampling" : "Smooth",
        "size" : [ 50, 60 ]
      },
      "name" : "Paint \/ sky",
      "opacity" : 0.25,
      "text" : {
        "alignment" : "Center",
        "blue" : 0.75,
        "boxSize" : [ 300, 150 ],
        "content" : "Two \/ lines\nof text",
        "fontName" : "Helvetica-Bold",
        "fontSize" : 36.5,
        "green" : 0.5,
        "leading" : 0,
        "red" : 0.25,
        "tracking" : -2
      },
      "transform" : {
        "flipX" : true,
        "flipY" : false,
        "origin" : [ -27.5, 88.25 ],
        "rotation" : 38,
        "sampling" : "Nearest",
        "size" : [ 123, 47 ]
      }
    }
  ],
  "resolution" : 300,
  "version" : 7,
  "width" : 100
})";

QJsonObject object(const QByteArray &json)
{
    return QJsonDocument::fromJson(json).object();
}

QByteArray json(const QJsonObject &object)
{
    return QJsonDocument(object).toJson();
}

// Sets `key` on the first layer; undefined removes it.
QByteArray withLayer(const QByteArray &manifest, const QString &key, const QJsonValue &value)
{
    QJsonObject whole = object(manifest);
    QJsonArray layers = whole.value("layers").toArray();
    QJsonObject layer = layers.at(0).toObject();
    value.isUndefined() ? layer.remove(key) : void(layer.insert(key, value));
    layers.replace(0, layer);
    whole.insert("layers", layers);
    return json(whole);
}

QByteArray with(const QByteArray &manifest, const QString &key, const QJsonValue &value)
{
    QJsonObject whole = object(manifest);
    value.isUndefined() ? whole.remove(key) : void(whole.insert(key, value));
    return json(whole);
}

bool decodes(const QByteArray &manifest)
{
    return !projectError([&] { ProjectManifest::decoded(manifest); }).has_value();
}
}

class ProjectManifestTests : public QObject {
    Q_OBJECT
private slots:
    void aMacOSManifestDecodesFieldByField();
    void encodingWritesSwiftsKeysAndLeavesAbsentOnesOut();
    void everyRawValueRoundTrips();
    void decodingRefusesWhatSwiftRefuses();
    void validationFollowsTheFormatsVersions_data();
    void validationFollowsTheFormatsVersions();
};

void ProjectManifestTests::aMacOSManifestDecodesFieldByField()
{
    const ProjectManifest manifest = ProjectManifest::decoded(fromMacOS);
    QCOMPARE(manifest.format, QString("com.compositor.project"));
    QCOMPARE(manifest.version, 7);
    QCOMPARE(manifest.colorSpace, QString("sRGB"));
    QCOMPARE(manifest.resolution, std::optional<double>(300));
    QCOMPARE(manifest.documentID, QUuid("{00112233-4455-4677-8899-aabbccddeeff}"));
    QCOMPARE(manifest.width, 100);
    QCOMPARE(manifest.height, 80);
    QCOMPARE(manifest.activeLayerID, std::optional(QUuid("{0f0e0d0c-0b0a-4908-8706-050403020100}")));
    QCOMPARE(int(manifest.layers.size()), 1);
    const ProjectLayerRecord &layer = manifest.layers[0];
    QCOMPARE(layer.id, QUuid("{0f0e0d0c-0b0a-4908-8706-050403020100}"));
    QCOMPARE(layer.name, QString("Paint / sky"));
    QCOMPARE(layer.isVisible, false);
    QCOMPARE(layer.transform, (LayerTransform{.origin = {-27.5, 88.25}, .size = {123, 47}, .rotation = 38, .flipX = true, .flipY = false,
                                              .sampling = LayerSampling::nearest}));
    QCOMPARE(layer.imageFile, std::optional(QString("0F0E0D0C-0B0A-4908-8706-050403020100.png")));
    QCOMPARE(layer.opacity, std::optional(0.25));
    QCOMPARE(layer.blendMode, std::optional(LayerBlendMode::colorDodge));
    QCOMPARE(layer.maskFile, std::optional(QString("0F0E0D0C-0B0A-4908-8706-050403020100.mask.png")));
    QCOMPARE(layer.maskEnabled, std::optional(false));
    QCOMPARE(layer.maskLinked, std::optional(false));
    QCOMPARE(layer.maskPlacement.value(), (LayerTransform{.origin = {3, 4.5}, .size = {50, 60}, .rotation = 10, .flipX = false, .flipY = true,
                                                          .sampling = LayerSampling::smooth}));
    QVERIFY(!layer.parentID && !layer.isGroup && !layer.maskSourceID);
    const LayerTextStyle &text = layer.text.value();
    QCOMPARE(text.content, QString("Two / lines\nof text"));
    QVERIFY(text.fontName == QString("Helvetica-Bold") && text.fontSize == 36.5 && text.alignment == TextAlignment::center);
    QVERIFY(text.red == 0.25 && text.green == 0.5 && text.blue == 0.75 && text.tracking == -2 && text.leading == 0);
    QCOMPARE(text.boxSize, std::optional(QSizeF(300, 150)));
    // What it writes back, the decoder reads the same.
    QCOMPARE(ProjectManifest::decoded(manifest.encoded()).encoded(), manifest.encoded());
}

void ProjectManifestTests::encodingWritesSwiftsKeysAndLeavesAbsentOnesOut()
{
    ProjectSnapshot snapshot = twoLayers();
    snapshot.manifest.resolution = std::nullopt;
    snapshot.manifest.activeLayerID = std::nullopt;
    // Two spaces a level, as Swift prints it.
    QVERIFY(snapshot.manifest.encoded().startsWith("{\n  \"colorSpace\": \"sRGB\",\n  \"documentID\": "));
    QVERIFY(snapshot.manifest.encoded().contains("\n      \"id\": "));
    QVERIFY(snapshot.manifest.encoded().endsWith("\n}\n"));
    const QJsonObject bare = object(snapshot.manifest.encoded());
    QCOMPARE(bare.keys(), (QStringList{"colorSpace", "documentID", "format", "height", "layers", "version", "width"}));
    QCOMPARE(bare.value("documentID").toString(), uuidString(snapshot.manifest.documentID));
    QCOMPARE(bare.value("documentID").toString(), bare.value("documentID").toString().toUpper());
    const QJsonObject blank = bare.value("layers").toArray().at(1).toObject();
    QCOMPARE(blank.keys(), (QStringList{"id", "isVisible", "name", "transform"}));
    const QJsonObject transform = bare.value("layers").toArray().at(0).toObject().value("transform").toObject();
    QCOMPARE(transform.keys(), (QStringList{"flipX", "flipY", "origin", "rotation", "sampling", "size"}));
    QCOMPARE(transform.value("origin"), QJsonValue(QJsonArray{-27.5, 88.25}));
    QCOMPARE(transform.value("size"), QJsonValue(QJsonArray{123, 47}));
    QCOMPARE(transform.value("sampling").toString(), QString("Nearest"));
    // Every optional, once present, under its own key.
    const QUuid parent = QUuid::createUuid(), source = QUuid::createUuid();
    ProjectLayerRecord full = snapshot.manifest.layers[0];
    full.parentID = parent;
    full.isGroup = false;
    full.opacity = 0.5;
    full.blendMode = LayerBlendMode::colorBurn;
    full.maskFile = "m.png";
    full.maskEnabled = true;
    full.maskSourceID = source;
    full.maskPlacement = LayerTransform{.origin = {1, 2}, .size = {3, 4}};
    full.maskLinked = false;
    snapshot.manifest.layers = {full};
    snapshot.manifest.resolution = 144.5;
    snapshot.manifest.activeLayerID = full.id;
    const QJsonObject whole = object(snapshot.manifest.encoded());
    QCOMPARE(whole.value("resolution").toDouble(), 144.5);
    QCOMPARE(whole.value("activeLayerID").toString(), uuidString(full.id));
    const QJsonObject layer = whole.value("layers").toArray().at(0).toObject();
    QCOMPARE(layer.keys(), (QStringList{"blendMode", "id", "imageFile", "isGroup", "isVisible", "maskEnabled", "maskFile", "maskLinked",
                                        "maskPlacement", "maskSourceID", "name", "opacity", "parentID", "transform"}));
    QCOMPARE(layer.value("parentID").toString(), uuidString(parent));
    QCOMPARE(layer.value("maskSourceID").toString(), uuidString(source));
    QCOMPARE(layer.value("blendMode").toString(), QString("Color Burn"));
    QCOMPARE(layer.value("isGroup"), QJsonValue(false));
    QCOMPARE(layer.value("maskEnabled"), QJsonValue(true));
    QCOMPARE(layer.value("maskLinked"), QJsonValue(false));
    QCOMPARE(layer.value("opacity").toDouble(), 0.5);
    QCOMPARE(layer.value("maskPlacement").toObject().value("size"), QJsonValue(QJsonArray{3, 4}));
    const ProjectLayerRecord back = ProjectManifest::decoded(snapshot.manifest.encoded()).layers[0];
    QCOMPARE(back.parentID, std::optional(parent));
    QCOMPARE(back.maskSourceID, std::optional(source));
    QCOMPARE(back.isGroup, std::optional(false));
    QCOMPARE(back.maskEnabled, std::optional(true));
    QCOMPARE(back.maskLinked, std::optional(false));
    QCOMPARE(back.maskPlacement.value(), (LayerTransform{.origin = {1, 2}, .size = {3, 4}}));
}

void ProjectManifestTests::everyRawValueRoundTrips()
{
    const QStringList blends{"Normal", "Multiply", "Screen", "Overlay", "Soft Light", "Darken", "Lighten", "Difference", "Color Dodge", "Color Burn",
                             "Hue", "Saturation", "Color", "Luminosity"};
    for (int index = 0; index < blends.size(); ++index) {
        QCOMPARE(rawValue(LayerBlendMode(index)), blends[index]);
        QCOMPARE(layerBlendMode(blends[index]), std::optional(LayerBlendMode(index)));
    }
    QCOMPARE(layerBlendMode("normal"), std::nullopt);
    QCOMPARE(layerBlendMode(""), std::nullopt);
    const QStringList samplings{"Nearest", "Smooth", "High quality"};
    for (int index = 0; index < samplings.size(); ++index) {
        QCOMPARE(rawValue(LayerSampling(index)), samplings[index]);
        QCOMPARE(layerSampling(samplings[index]), std::optional(LayerSampling(index)));
    }
    QCOMPARE(layerSampling("High Quality"), std::nullopt);
    QCOMPARE(uuidString(QUuid("{0f0e0d0c-0b0a-4908-8706-0504030201ab}")), QString("0F0E0D0C-0B0A-4908-8706-0504030201AB"));
}

void ProjectManifestTests::decodingRefusesWhatSwiftRefuses()
{
    const QByteArray good = fromMacOS;
    QVERIFY(decodes(good));
    // Required keys, each missing in turn.
    for (const char *key : {"format", "version", "colorSpace", "documentID", "width", "height", "layers"})
        QVERIFY2(!decodes(with(good, key, QJsonValue::Undefined)), key);
    for (const char *key : {"id", "name", "isVisible", "transform"})
        QVERIFY2(!decodes(withLayer(good, key, QJsonValue::Undefined)), key);
    for (const char *key : {"origin", "size", "rotation", "flipX", "flipY", "sampling"}) {
        QJsonObject transform = object(good).value("layers").toArray().at(0).toObject().value("transform").toObject();
        transform.remove(key);
        QVERIFY2(!decodes(withLayer(good, "transform", transform)), key);
    }
    // Optional keys may be missing or null.
    for (const char *key : {"resolution", "activeLayerID"}) {
        QVERIFY2(decodes(with(good, key, QJsonValue::Undefined)), key);
        QVERIFY2(decodes(with(good, key, QJsonValue::Null)), key);
    }
    for (const char *key : {"imageFile", "parentID", "isGroup", "opacity", "blendMode", "maskFile", "maskEnabled", "maskSourceID",
                            "maskPlacement", "maskLinked"}) {
        QVERIFY2(decodes(withLayer(good, key, QJsonValue::Undefined)), key);
        QVERIFY2(decodes(withLayer(good, key, QJsonValue::Null)), key);
    }
    QVERIFY(!ProjectManifest::decoded(withLayer(good, "opacity", QJsonValue::Null)).layers[0].opacity.has_value());
    // The wrong type under each key.
    QVERIFY(!decodes(with(good, "format", 7)));
    QVERIFY(!decodes(with(good, "colorSpace", true)));
    QVERIFY(!decodes(with(good, "version", "7")));
    QVERIFY(!decodes(with(good, "width", 100.5)));
    QVERIFY(decodes(with(good, "width", 100.0)));
    // Swift's Int holds 64 bits: wild sizes decode, validation refuses.
    QCOMPARE(ProjectManifest::decoded(with(good, "height", 1e12)).height, qint64(1'000'000'000'000));
    QCOMPARE(ProjectManifest::decoded(with(good, "width", -3e9)).width, qint64(-3'000'000'000));
    // Every bit of a whole token survives: no double between.
    for (const qint64 exact : {qint64(9007199254740993), std::numeric_limits<qint64>::max(), std::numeric_limits<qint64>::min()})
        QCOMPARE(ProjectManifest::decoded(with(good, "height", QJsonValue(exact))).height, exact);
    QVERIFY(!decodes(QByteArray(good).replace("\"width\" : 100", "\"width\" : 9223372036854775808")));
    QVERIFY(!decodes(with(good, "height", 1e19)));
    QVERIFY(!decodes(with(good, "height", -1e19)));
    QVERIFY(!decodes(with(good, "version", 7.5)));
    QVERIFY(!decodes(with(good, "resolution", "300")));
    QVERIFY(!decodes(with(good, "layers", QJsonObject())));
    QVERIFY(!decodes(with(good, "layers", QJsonArray{7})));
    QVERIFY(!decodes(withLayer(good, "name", 7)));
    QVERIFY(!decodes(withLayer(good, "isVisible", 1)));
    QVERIFY(!decodes(withLayer(good, "isGroup", "yes")));
    QVERIFY(!decodes(withLayer(good, "opacity", "0.5")));
    QVERIFY(!decodes(withLayer(good, "imageFile", 7)));
    QVERIFY(!decodes(withLayer(good, "blendMode", "Glow")));
    QVERIFY(!decodes(withLayer(good, "blendMode", 3)));
    QVERIFY(!decodes(withLayer(good, "transform", "upright")));
    QVERIFY(!decodes(withLayer(good, "maskPlacement", QJsonArray{1, 2})));
    // UUIDs: Swift's form only, in either case.
    QVERIFY(decodes(with(good, "documentID", "00112233-4455-4677-8899-aabbccddeeff")));
    QVERIFY(!decodes(with(good, "documentID", "{00112233-4455-4677-8899-AABBCCDDEEFF}")));
    QVERIFY(!decodes(with(good, "documentID", "001122334455467788 99AABBCCDDEEFF")));
    QVERIFY(!decodes(with(good, "documentID", "00112233-4455-4677-8899-AABBCCDDEEFG")));
    QVERIFY(!decodes(with(good, "documentID", 7)));
    QVERIFY(!decodes(with(good, "documentID", "00112233-4455-4677-8899-AABBCCDDEEFF\n")));
    QVERIFY(!decodes(with(good, "documentID", "\n00112233-4455-4677-8899-AABBCCDDEEFF")));
    QVERIFY(!decodes(withLayer(good, "parentID", "not-a-uuid")));
    // Points and sizes are two-number arrays; a third is ignored.
    QJsonObject transform = object(good).value("layers").toArray().at(0).toObject().value("transform").toObject();
    transform.insert("origin", QJsonArray{1, 2, 3});
    QCOMPARE(ProjectManifest::decoded(withLayer(good, "transform", transform)).layers[0].transform.origin, QPointF(1, 2));
    transform.insert("origin", QJsonArray{1});
    QVERIFY(!decodes(withLayer(good, "transform", transform)));
    transform.insert("origin", QJsonArray{"1", 2});
    QVERIFY(!decodes(withLayer(good, "transform", transform)));
    transform.insert("origin", QJsonObject{{"x", 1}, {"y", 2}});
    QVERIFY(!decodes(withLayer(good, "transform", transform)));
    transform.insert("origin", QJsonArray{1, 2});
    transform.insert("sampling", "Bicubic");
    QVERIFY(!decodes(withLayer(good, "transform", transform)));
    transform.insert("sampling", "High quality");
    transform.insert("flipX", 0);
    QVERIFY(!decodes(withLayer(good, "transform", transform)));
    transform.insert("flipX", false);
    transform.insert("rotation", "38");
    QVERIFY(!decodes(withLayer(good, "transform", transform)));
}

void ProjectManifestTests::validationFollowsTheFormatsVersions_data()
{
    QTest::addColumn<QString>("change");
    QTest::addColumn<int>("version");
    QTest::addColumn<std::optional<ProjectError::Kind>>("refused");
    const auto invalid = std::optional(ProjectError::Kind::invalid), tooLarge = std::optional(ProjectError::Kind::tooLarge);
    const std::optional<ProjectError::Kind> fine;
    for (int version = 1; version <= 8; ++version)
        QTest::addRow("plain at version %d", version) << "" << version << fine;
    QTest::newRow("version 0") << "" << 0 << std::optional(ProjectError::Kind::version);
    QTest::newRow("version 9") << "" << 9 << std::optional(ProjectError::Kind::version);
    QTest::newRow("another format") << "format" << 7 << invalid;
    QTest::newRow("another colour space") << "colorSpace" << 7 << invalid;
    QTest::newRow("resolution 0.5") << "resolution=0.5" << 7 << invalid;
    QTest::newRow("resolution 9601") << "resolution=9601" << 7 << invalid;
    QTest::newRow("resolution 1") << "resolution=1" << 1 << fine;
    QTest::newRow("resolution 9600") << "resolution=9600" << 1 << fine;
    QTest::newRow("width 0") << "width=0" << 7 << tooLarge;
    QTest::newRow("width 30001") << "width=30001" << 7 << tooLarge;
    QTest::newRow("width 30000") << "width=30000" << 7 << fine;
    QTest::newRow("height 0") << "height=0" << 7 << tooLarge;
    QTest::newRow("height 30001") << "height=30001" << 7 << tooLarge;
    QTest::newRow("height 30000") << "height=30000" << 7 << fine;
    QTest::newRow("10001 layers") << "layers=10001" << 7 << tooLarge;
    QTest::newRow("10000 layers") << "layers=10000" << 7 << fine;
    QTest::newRow("opacity before version 3") << "opacity=0.5" << 2 << invalid;
    QTest::newRow("opacity at version 3") << "opacity=0.5" << 3 << fine;
    QTest::newRow("full opacity before version 3") << "opacity=1" << 2 << fine;
    QTest::newRow("opacity above one") << "opacity=1.5" << 7 << invalid;
    QTest::newRow("opacity below zero") << "opacity=-0.5" << 7 << invalid;
    QTest::newRow("a blend mode before version 3") << "blend" << 2 << invalid;
    QTest::newRow("a blend mode at version 3") << "blend" << 3 << fine;
    QTest::newRow("a mask before version 4") << "mask" << 3 << invalid;
    QTest::newRow("a mask at version 4") << "mask" << 4 << fine;
    QTest::newRow("a mask under another name") << "maskName" << 7 << invalid;
    QTest::newRow("mask enabled without a mask") << "maskEnabledAlone" << 7 << invalid;
    QTest::newRow("a placement without a mask") << "placementAlone" << 7 << invalid;
    QTest::newRow("a placement that is no transform") << "placementInvalid" << 7 << invalid;
    QTest::newRow("a placed mask") << "placement" << 7 << fine;
    QTest::newRow("a clipping link before version 5") << "link" << 4 << invalid;
    QTest::newRow("a clipping link at version 5") << "link" << 5 << fine;
    QTest::newRow("a clipping link to nothing") << "linkNowhere" << 7 << invalid;
    QTest::newRow("a folder at version 1") << "folder" << 1 << invalid;
    QTest::newRow("a folder at version 2") << "folder" << 2 << fine;
    QTest::newRow("an empty folder at version 1") << "folderEmpty" << 1 << invalid;
    QTest::newRow("an empty folder at version 2") << "folderEmpty" << 2 << fine;
    QTest::newRow("a folder that blends") << "folderBlend" << 7 << invalid;
    QTest::newRow("a folder that fades before version 8") << "folderOpacity" << 7 << invalid;
    QTest::newRow("a folder that fades at version 8") << "folderOpacity" << 8 << fine;
    QTest::newRow("a folder that blends at version 8") << "folderBlend" << 8 << invalid;
    QTest::newRow("a folder with pixels") << "folderPixels" << 7 << invalid;
    QTest::newRow("a folder mask before version 6") << "folderMask" << 5 << invalid;
    QTest::newRow("a folder mask at version 6") << "folderMask" << 6 << fine;
    QTest::newRow("a parent that is no folder") << "parentLayer" << 7 << invalid;
    QTest::newRow("the same id twice") << "duplicate" << 7 << invalid;
    QTest::newRow("a transform of no size") << "transform" << 7 << invalid;
    QTest::newRow("a blank name") << "name= \n\t" << 7 << invalid;
    QTest::newRow("a name over 16384 bytes") << "nameLong" << 7 << invalid;
    QTest::newRow("a name of 16384 bytes") << "nameFits" << 7 << fine;
    QTest::newRow("a shorter name of 16386 bytes") << "nameWide" << 7 << invalid;
    QTest::newRow("an image under another name") << "imageName" << 7 << invalid;
    QTest::newRow("an image named in lower case") << "imageLower" << 7 << invalid;
    QTest::newRow("an active layer that is not there") << "active" << 7 << invalid;
}

void ProjectManifestTests::validationFollowsTheFormatsVersions()
{
    QFETCH(QString, change);
    QFETCH(int, version);
    QFETCH(std::optional<ProjectError::Kind>, refused);
    ProjectSnapshot snapshot = twoLayers();
    ProjectManifest &manifest = snapshot.manifest;
    manifest.version = version;
    manifest.resolution = std::nullopt;
    ProjectLayerRecord &image = manifest.layers[0], &blank = manifest.layers[1];
    const QString value = change.section('=', 1);
    const auto folder = [&] {
        blank.isGroup = true;
        image.parentID = blank.id;
        std::swap(manifest.layers[0], manifest.layers[1]);
    };
    if (change == "format")
        manifest.format = "com.example.project";
    else if (change == "colorSpace")
        manifest.colorSpace = "Display P3";
    else if (change.startsWith("resolution="))
        manifest.resolution = value.toDouble();
    else if (change.startsWith("width="))
        manifest.width = value.toInt();
    else if (change.startsWith("height="))
        manifest.height = value.toInt();
    else if (change.startsWith("layers=")) {
        while (int(manifest.layers.size()) < value.toInt())
            manifest.layers.push_back({.id = QUuid::createUuid(), .name = "n", .isVisible = true,
                                       .transform = {.origin = {0, 0}, .size = {1, 1}}, .imageFile = std::nullopt});
    } else if (change.startsWith("opacity="))
        blank.opacity = value.toDouble();
    else if (change == "blend")
        blank.blendMode = LayerBlendMode::multiply;
    else if (change == "mask" || change == "placement" || change == "placementInvalid") {
        image.maskFile = uuidString(image.id) + ".mask.png";
        image.maskEnabled = true;
        snapshot.masks.insert({image.id, asset(gray(4, 4, 255), "mask")});
        if (change != "mask")
            image.maskPlacement = LayerTransform{.origin = {0, 0}, .size = {change == "placement" ? 5.0 : 0.0, 5}};
    } else if (change == "maskName") {
        image.maskFile = "mask.png";
        snapshot.masks.insert({image.id, asset(gray(4, 4, 255), "mask")});
    } else if (change == "maskEnabledAlone")
        image.maskEnabled = true;
    else if (change == "placementAlone")
        image.maskPlacement = LayerTransform{.origin = {0, 0}, .size = {5, 5}};
    else if (change == "link")
        blank.maskSourceID = image.id;
    else if (change == "linkNowhere")
        blank.maskSourceID = QUuid::createUuid();
    else if (change == "folder")
        folder();
    else if (change == "folderEmpty")
        blank.isGroup = true;
    else if (change == "folderBlend") {
        folder();
        manifest.layers[0].blendMode = LayerBlendMode::screen;
    } else if (change == "folderOpacity") {
        folder();
        manifest.layers[0].opacity = 0.5;
    } else if (change == "folderPixels") {
        folder();
        manifest.layers[0].imageFile = uuidString(manifest.layers[0].id) + ".png";
        snapshot.images.insert({manifest.layers[0].id, asset(halfRed(), "folder")});
    } else if (change == "folderMask") {
        folder();
        manifest.layers[0].maskFile = uuidString(manifest.layers[0].id) + ".mask.png";
        snapshot.masks.insert({manifest.layers[0].id, asset(gray(4, 4, 255), "mask")});
    } else if (change == "parentLayer")
        blank.parentID = image.id;
    else if (change == "duplicate")
        blank.id = image.id;
    else if (change == "transform")
        blank.transform.size = QSizeF(0, 5);
    else if (change.startsWith("name="))
        blank.name = value;
    else if (change == "nameLong")
        blank.name = QString(16'385, 'n');
    else if (change == "nameFits")
        blank.name = QString(8'192, QChar(0x00E9));
    else if (change == "nameWide")
        blank.name = QString(8'193, QChar(0x00E9));
    else if (change == "imageName")
        image.imageFile = "image.png";
    else if (change == "imageLower")
        image.imageFile = image.id.toString(QUuid::WithoutBraces) + ".png";
    else if (change == "active")
        manifest.activeLayerID = QUuid::createUuid();
    else
        QVERIFY(change.isEmpty());
    // The same rules guard saving and loading.
    QTemporaryDir root;
    QCOMPARE(projectError([&] { ProjectStore::save(snapshot, root.filePath("Rules.comp")); }), refused);
    ProjectSnapshot plain = twoLayers();
    ProjectStore::save(plain, root.filePath("Read.comp"));
    for (const auto &[id, mask] : snapshot.masks)
        QVERIFY(mask.image().save(root.filePath("Read.comp/images/") + uuidString(id) + ".mask.png"));
    for (const auto &[id, pixels] : snapshot.images)
        QVERIFY(pixels.image().save(root.filePath("Read.comp/images/") + uuidString(id) + ".png"));
    overwrite(root.filePath("Read.comp/manifest.json"), manifest.encoded());
    QCOMPARE(projectError([&] { ProjectStore::load(root.filePath("Read.comp")); }), refused);
}

QTEST_MAIN(ProjectManifestTests)
#include "ProjectManifestTests.moc"
