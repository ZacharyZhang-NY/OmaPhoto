#include "Document/LayerAdjustment.h"
#include "Document/LayerAppearance.h"
#include "Document/LayerTransform.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectStore.h"
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

// docs/writing-comp-files.md: its examples load, its names are ours.
namespace {
QString guide()
{
    QFile file(QStringLiteral(QT_TESTCASE_SOURCEDIR "/docs/writing-comp-files.md"));
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("the guide is missing");
    return QString::fromUtf8(file.readAll());
}

// The guide's ```json blocks, in order.
QList<QJsonObject> examples()
{
    QList<QJsonObject> found;
    static const QRegularExpression block(QStringLiteral("```json\\n(.*?)```"), QRegularExpression::DotMatchesEverythingOption);
    for (auto match = block.globalMatch(guide()); match.hasNext();) {
        QJsonParseError error;
        const QJsonDocument parsed = QJsonDocument::fromJson(match.next().captured(1).toUtf8(), &error);
        if (error.error != QJsonParseError::NoError)
            throw std::runtime_error("a guide example is no JSON: " + error.errorString().toStdString());
        found << parsed.object();
    }
    return found;
}

// The backticked names on the line that begins with `lead`.
QSet<QString> named(const QString &lead)
{
    static const QRegularExpression ticked(QStringLiteral("`([^`]+)`"));
    for (const QString &line : guide().split(QLatin1Char('\n'))) {
        if (!line.startsWith(lead))
            continue;
        QSet<QString> names;
        for (auto match = ticked.globalMatch(line.mid(lead.size())); match.hasNext();)
            names.insert(match.next().captured(1).remove(QLatin1Char('"')));
        return names;
    }
    throw std::runtime_error("no guide line begins " + lead.toStdString());
}

// A project folder: `manifest`, and a PNG per image.
QString project(const QTemporaryDir &root, const QJsonObject &manifest)
{
    const QString path = root.filePath(QStringLiteral("Guide.comp"));
    QDir().mkpath(path + QStringLiteral("/images"));
    QFile out(path + QStringLiteral("/manifest.json"));
    if (!out.open(QIODevice::WriteOnly) || out.write(QJsonDocument(manifest).toJson()) < 0)
        throw std::runtime_error("the manifest was not written");
    out.close();
    QImage image(1920, 1080, QImage::Format_RGBA8888);
    image.fill(QColor(20, 30, 60));
    for (const QJsonValue &layer : manifest.value(QStringLiteral("layers")).toArray()) {
        const QString file = layer.toObject().value(QStringLiteral("imageFile")).toString();
        if (!file.isEmpty() && !image.save(path + QStringLiteral("/images/") + file, "PNG"))
            throw std::runtime_error("an image was not written");
    }
    return path;
}
// The guide's two examples as one project, Curves on top.
QJsonObject withCurves()
{
    const QList<QJsonObject> blocks = examples();
    if (blocks.size() != 2)
        throw std::runtime_error("the guide should hold two examples");
    QJsonObject manifest = blocks[0];
    QJsonArray layers = manifest.value(QStringLiteral("layers")).toArray();
    layers.append(blocks[1]);
    manifest.insert(QStringLiteral("layers"), layers);
    return manifest;
}

// The Curves layer turned `kind`, with `drop` taken out.
QJsonObject adjusted(const QString &kind, const QString &drop = QString())
{
    QJsonObject manifest = withCurves();
    QJsonArray layers = manifest.value(QStringLiteral("layers")).toArray();
    QJsonObject layer = layers[1].toObject();
    QJsonObject adjustment = layer.value(QStringLiteral("adjustment")).toObject();
    adjustment.insert(QStringLiteral("kind"), kind);
    adjustment.remove(drop);
    layer.insert(QStringLiteral("adjustment"), adjustment);
    layers[1] = layer;
    manifest.insert(QStringLiteral("layers"), layers);
    return manifest;
}
}

class WritingProjectsGuideTests : public QObject {
    Q_OBJECT
private slots:
    void theMinimalManifestLoadsAsItSays();
    void theCurvesLayerWarmsWhatLiesBelow();
    void theCommonAdjustmentFieldsAreRequired();
    void aFolderPassesAnAdjustmentThrough();
    void theBlendModesAndKindsAreOurs();
    void hsvSettingsWinOverTheLegacyFields();
};

void WritingProjectsGuideTests::theMinimalManifestLoadsAsItSays()
{
    QTemporaryDir root;
    const ProjectSnapshot snapshot = ProjectStore::load(project(root, examples().value(0)));
    QCOMPARE(snapshot.manifest.width, qint64(1920));
    QCOMPARE(snapshot.manifest.height, qint64(1080));
    QCOMPARE(snapshot.manifest.layers.size(), size_t(1));
    const ProjectLayerRecord &layer = snapshot.manifest.layers[0];
    QCOMPARE(layer.name, QString("Background"));
    QVERIFY(layer.isVisible);
    QCOMPARE(layer.opacity.value(), 1.0);
    QVERIFY(layer.blendMode.value() == LayerBlendMode::normal);
    QCOMPARE(layer.transform.origin, QPointF(0, 0));
    QCOMPARE(layer.transform.size, QSizeF(1920, 1080));
    QCOMPARE(layer.transform.rotation, 0.0);
    QVERIFY(layer.transform.sampling == LayerSampling::high);
    // Named after its layer, as the rules say.
    QCOMPARE(layer.imageFile.value(), layer.id.toString(QUuid::WithoutBraces).toUpper() + QStringLiteral(".png"));
    QVERIFY(snapshot.images.count(layer.id) == 1);
    QCOMPARE(snapshot.manifest.activeLayerID.value(), layer.id);
}

void WritingProjectsGuideTests::theCurvesLayerWarmsWhatLiesBelow()
{
    QTemporaryDir root;
    const ProjectSnapshot snapshot = ProjectStore::load(project(root, withCurves()));
    QCOMPARE(snapshot.manifest.layers.size(), size_t(2));
    QCOMPARE(snapshot.manifest.layers[1].name, QString("Warm Grade"));
    const LayerAdjustment &adjustment = snapshot.manifest.layers[1].adjustment.value();
    QVERIFY(adjustment.kind == AdjustmentKind::curves);
    const std::vector<std::vector<CurvePoint>> points{{{0, 0}, {255, 255}},
                                                   {{0, 0}, {120, 147}, {255, 255}},
                                                   {{0, 0}, {100, 114}, {255, 255}},
                                                   {{0, 0}, {115, 97}, {255, 238}}};
    QCOMPARE(adjustment.curves.channels.size(), points.size());
    for (size_t channel = 0; channel < points.size(); ++channel)
        QCOMPARE(adjustment.curves.channels[channel], points[channel]);
    // The composite is the background through those curves.
    QImage below(1, 1, QImage::Format_RGBA8888_Premultiplied);
    below.fill(QColor(20, 30, 60));
    const QColor expected = adjustment.curves.apply(below).pixelColor(0, 0);
    const QColor rendered = ImageExporter::render(snapshot).image.pixelColor(960, 540);
    QCOMPARE(rendered, expected);
    QVERIFY(rendered.red() > 20 && rendered.blue() < 60);
}

void WritingProjectsGuideTests::theCommonAdjustmentFieldsAreRequired()
{
    for (const QString &field : {QStringLiteral("hue"), QStringLiteral("saturation"), QStringLiteral("lightness"), QStringLiteral("colorize"),
                                 QStringLiteral("levels"), QStringLiteral("curves")}) {
        QTemporaryDir root;
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, ProjectStore::load(project(root, adjusted(QStringLiteral("Curves"), field))));
    }
    // A kind's own settings may be missing: its defaults.
    QTemporaryDir root;
    const ProjectSnapshot blur = ProjectStore::load(project(root, adjusted(QStringLiteral("Gaussian Blur"))));
    const LayerAdjustment &adjustment = blur.manifest.layers[1].adjustment.value();
    QVERIFY(adjustment.kind == AdjustmentKind::gaussianBlur);
    QCOMPARE(adjustment.gaussianRadius(), 10.0);
}

void WritingProjectsGuideTests::aFolderPassesAnAdjustmentThrough()
{
    // Red below, and a folder holding only an Invert layer.
    QJsonObject manifest = adjusted(QStringLiteral("Invert"));
    QJsonArray layers = manifest.value(QStringLiteral("layers")).toArray();
    const QString folder = QStringLiteral("B1C2D3E4-F5A6-4B7C-8D9E-0F1A2B3C4D5E");
    QJsonObject group = layers[1].toObject();
    group.remove(QStringLiteral("adjustment"));
    group.insert(QStringLiteral("id"), folder);
    group.insert(QStringLiteral("name"), QStringLiteral("Folder"));
    group.insert(QStringLiteral("isGroup"), true);
    QJsonObject invert = layers[1].toObject();
    invert.insert(QStringLiteral("parentID"), folder);
    manifest.insert(QStringLiteral("layers"), QJsonArray{layers[0], group, invert});
    QTemporaryDir root;
    const QString path = project(root, manifest);
    QImage red(1920, 1080, QImage::Format_RGBA8888);
    red.fill(Qt::red);
    QVERIFY(red.save(path + QStringLiteral("/images/") + layers[0].toObject().value(QStringLiteral("imageFile")).toString(), "PNG"));
    const QColor shown = ImageExporter::render(ProjectStore::load(path)).image.pixelColor(960, 540);
    QCOMPARE(shown, QColor(0, 255, 255));
}

void WritingProjectsGuideTests::theBlendModesAndKindsAreOurs()
{
    QSet<QString> modes, kinds;
    for (const LayerBlendMode mode : allLayerBlendModes)
        modes.insert(rawValue(mode));
    for (const AdjustmentKind kind : allAdjustmentKinds)
        kinds.insert(rawValue(kind));
    QCOMPARE(named(QStringLiteral("- **Blend modes are spelled exactly** as OmaPhoto names them:")), modes);
    QCOMPARE(named(QStringLiteral("- `kind` is one of")), kinds);
    QSet<QString> samplings;
    for (const LayerSampling sampling : {LayerSampling::high, LayerSampling::smooth, LayerSampling::nearest})
        samplings.insert(rawValue(sampling));
    QCOMPARE(named(QStringLiteral("- `sampling` is")), samplings);
}

void WritingProjectsGuideTests::hsvSettingsWinOverTheLegacyFields()
{
    // The background's colour, then through each Hue/Saturation.
    const auto shown = [](int legacyHue, std::optional<int> masterHue) {
        QJsonObject manifest = adjusted(QStringLiteral("Hue/Saturation"));
        QJsonArray layers = manifest.value(QStringLiteral("layers")).toArray();
        QJsonObject layer = layers[1].toObject();
        QJsonObject adjustment = layer.value(QStringLiteral("adjustment")).toObject();
        adjustment.insert(QStringLiteral("hue"), legacyHue);
        if (masterHue) {
            const QJsonObject values{{"hue", *masterHue}, {"saturation", 0}, {"lightness", 0}};
            adjustment.insert(QStringLiteral("hsvSettings"), QJsonObject{{"range", "Master"}, {"colorize", false}, {"invertRange", false},
                                                                         {"adjustments", QJsonArray{"Master", values}}, {"bands", QJsonArray{}}});
        }
        layer.insert(QStringLiteral("adjustment"), adjustment);
        layers[1] = layer;
        manifest.insert(QStringLiteral("layers"), layers);
        QTemporaryDir root;
        return ImageExporter::render(ProjectStore::load(project(root, manifest))).image.pixelColor(960, 540);
    };
    const QColor background(20, 30, 60);
    // No hsvSettings: the legacy hue turns the colour.
    QVERIFY(shown(120, std::nullopt) != background);
    // hsvSettings present: the legacy hue is ignored, its own applies.
    QCOMPARE(shown(120, 0), background);
    QCOMPARE(shown(0, 120), shown(120, std::nullopt));
}

QTEST_GUILESS_MAIN(WritingProjectsGuideTests)
#include "WritingProjectsGuideTests.moc"
