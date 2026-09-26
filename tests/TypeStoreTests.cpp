#include "BrushFixtures.h"
#include "SelectionFixtures.h"
#include "Document/EditorSession.h"
#include "IO/CanvasResizer.h"
#include "IO/ImageResizer.h"
#include "IO/ProjectStore.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

// Text layers kept and dropped: the manifest, rebuilds, resizes.
namespace {
// A blank layer, then text saying "Stored" at (20, 30).
std::unique_ptr<EditorSession> textSession()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(400, 300, true);
    session->selectTool(NavigationTool::type);
    session->beginText(QPointF(20, 30), true);
    TextDraft draft = session->textDraft().value();
    draft.style.content = QStringLiteral("Stored");
    session->applyText(draft);
    return session;
}

QJsonObject textOf(const ProjectSnapshot &snapshot, qsizetype index)
{
    return QJsonDocument::fromJson(snapshot.manifest.encoded()).object().value("layers").toArray().at(index).toObject().value("text").toObject();
}

QByteArray withText(const ProjectSnapshot &snapshot, const QJsonValue &text)
{
    QJsonObject object = QJsonDocument::fromJson(snapshot.manifest.encoded()).object();
    QJsonArray layers = object.value("layers").toArray();
    QJsonObject layer = layers.at(1).toObject();
    layer.insert("text", text);
    layers.replace(1, layer);
    object.insert("layers", layers);
    return QJsonDocument(object).toJson();
}

bool landed(const std::function<void(std::function<void()>)> &start)
{
    bool done = false;
    start([&] { done = true; });
    return QTest::qWaitFor([&] { return done; }, 20000);
}
}

class TypeStoreTests : public QObject {
    Q_OBJECT
private slots:
    void theManifestKeepsSwiftsKeys();
    void theManifestRefusesAWrongStyle();
    void anInvalidStyleLoadsAsPixels();
    void staleTextIsNeitherLiveNorSaved();
    void theStoreRefusesTextItCannotShow();
    void rebuildsDropTheText();
    void resizersKeepOrDropTheText();
};

void TypeStoreTests::theManifestKeepsSwiftsKeys()
{
    const auto session = textSession();
    const ProjectSnapshot point = session->projectSnapshot().value();
    QCOMPARE(textOf(point, 1).keys(),
             (QStringList{"alignment", "blue", "content", "fontName", "fontSize", "green", "leading", "red", "tracking"}));
    QCOMPARE(textOf(point, 1).value("alignment").toString(), QString("Left"));
    QCOMPARE(textOf(point, 1).value("fontName").toString(), QString("Helvetica"));
    session->changeTextStyle([](LayerTextStyle &style) {
        style.alignment = TextAlignment::center;
        style.boxSize = QSizeF(300, 150);
        style.red = 0.25;
        style.green = 0.5;
        style.blue = 0.75;
        style.tracking = -3;
        style.leading = 90;
    });
    QVERIFY(session->finishText());
    const ProjectSnapshot boxed = session->projectSnapshot().value();
    QCOMPARE(textOf(boxed, 1).value("boxSize").toArray(), (QJsonArray{300, 150}));
    const ProjectManifest decoded = ProjectManifest::decoded(boxed.manifest.encoded());
    QCOMPARE(decoded.layers[1].text, boxed.manifest.layers[1].text);
    QCOMPARE(decoded.layers[1].text.value().alignment, TextAlignment::center);
    QVERIFY(!decoded.layers[0].text);
}

void TypeStoreTests::theManifestRefusesAWrongStyle()
{
    const auto session = textSession();
    const ProjectSnapshot snapshot = session->projectSnapshot().value();
    const QJsonObject good = textOf(snapshot, 1);
    QVERIFY(!ProjectManifest::decoded(withText(snapshot, QJsonValue::Null)).layers[1].text);
    const auto refused = [&](const QJsonValue &text) {
        try {
            ProjectManifest::decoded(withText(snapshot, text));
        } catch (const ProjectError &error) {
            return error.kind == ProjectError::Kind::invalid;
        }
        return false;
    };
    for (const char *key : {"content", "fontName", "fontSize", "red", "green", "blue", "alignment", "tracking", "leading"}) {
        QJsonObject missing = good;
        missing.remove(key);
        QVERIFY2(refused(missing), key);
    }
    for (const auto &[key, value] : std::vector<std::pair<QString, QJsonValue>>{
             {"content", 1}, {"fontName", true}, {"fontSize", "72"}, {"red", "0"}, {"green", QJsonArray{}}, {"blue", "x"},
             {"alignment", "Justified"}, {"alignment", 0}, {"tracking", "0"}, {"leading", false}, {"boxSize", QJsonArray{300}}}) {
        QJsonObject wrong = good;
        wrong.insert(key, value);
        QVERIFY2(refused(wrong), qPrintable(key));
    }
    QVERIFY(refused(QJsonValue(3)));
}

void TypeStoreTests::anInvalidStyleLoadsAsPixels()
{
    const auto session = textSession();
    const QUuid id = session->activeLayerID().value();
    rewrite(*session, [&](ProjectSnapshot &snapshot) { record(snapshot, id).text.value().fontSize = 5000; });
    QVERIFY(!layerWith(*session, id).text);
    QVERIFY(layerWith(*session, id).asset);
}

void TypeStoreTests::theStoreRefusesTextItCannotShow()
{
    const auto session = textSession();
    const ProjectSnapshot good = session->projectSnapshot().value();
    const QUuid id = good.manifest.layers[1].id;
    QTemporaryDir root;
    const QString path = root.filePath("Text.comp");
    ProjectStore::save(good, path);
    QCOMPARE(ProjectStore::load(path).manifest.layers[1].text, good.manifest.layers[1].text);
    const auto refused = [&](const std::function<void()> &body) -> std::optional<ProjectError::Kind> {
        try {
            body();
        } catch (const ProjectError &error) {
            return error.kind;
        }
        return std::nullopt;
    };
    // A style past its bounds refuses the project, both ways.
    ProjectSnapshot wild = good;
    record(wild, id).text.value().fontSize = 5000;
    QCOMPARE(refused([&] { ProjectStore::save(wild, path); }), std::optional(ProjectError::Kind::invalid));
    QFile manifest(path + "/manifest.json");
    QVERIFY(manifest.open(QIODevice::WriteOnly | QIODevice::Truncate));
    manifest.write(wild.manifest.encoded());
    manifest.close();
    QCOMPARE(refused([&] { ProjectStore::load(path); }), std::optional(ProjectError::Kind::invalid));
    // Text needs pixels: a blank layer cannot carry it.
    ProjectSnapshot bare = good;
    record(bare, id).imageFile = std::nullopt;
    bare.images.erase(id);
    QCOMPARE(refused([&] { ProjectStore::save(bare, path); }), std::optional(ProjectError::Kind::invalid));
    record(bare, id).text = std::nullopt;
    QVERIFY(!refused([&] { ProjectStore::save(bare, path); }));
}

void TypeStoreTests::staleTextIsNeitherLiveNorSaved()
{
    QImage pixels(20, 10, QImage::Format_RGBA8888_Premultiplied);
    pixels.fill(Qt::transparent);
    ImageLayer layer(ImportedImage(pixels, pixels, QStringLiteral("Pixels")), QPointF());
    LayerTextStyle style;
    style.content = QStringLiteral("Stale");
    layer.text = LayerText{style, layer.asset.value().identity()};
    QVERIFY(layer.liveText());
    QCOMPARE(layer.hierarchyRecord().text, std::optional(style));
    // Other pixels, as a bake leaves them: text no more.
    layer.text.value().image = ImageIdentity{};
    QVERIFY(!layer.liveText() && layer.text);
    QVERIFY(!layer.hierarchyRecord().text);
}

void TypeStoreTests::rebuildsDropTheText()
{
    const std::vector<std::pair<QString, std::function<bool(EditorSession &)>>> edits = {
        {"Brush Stroke", [](EditorSession &session) {
             session.selectTool(NavigationTool::brush);
             session.beginBrush(QPointF(40, 60));
             session.finishBrush();
             return true;
         }},
        {"Fill", [](EditorSession &session) {
             session.applySelection(rectPath(QRectF(30, 40, 20, 20)), SelectionMode::replace, "Select");
             return landed([&](std::function<void()> done) { session.fillSelection(EditorSession::FillSource::foreground, done); });
         }},
        {"Invert", [](EditorSession &session) { return landed([&](std::function<void()> done) { session.invertPixels(done); }); }},
        {"Content-Aware Fill", [](EditorSession &session) {
             session.applySelection(rectPath(QRectF(30, 40, 20, 20)), SelectionMode::replace, "Select");
             session.beginFilter(FilterKind::contentAwareFill);
             return landed([&](std::function<void()> done) { session.commitFilter(done); });
         }},
        {"Transform Selection", [](EditorSession &session) {
             session.applySelection(rectPath(QRectF(300, 250, 10, 10)), SelectionMode::replace, "Select");
             if (!landed([&](std::function<void()> done) { session.beginSelectionTransform(done); }))
                 return false;
             LayerTransform draft = session.transformEdit().value().draft;
             draft.origin += QPointF(3, 2);
             session.previewTransform(draft);
             session.commitTransform();
             return true;
         }},
    };
    for (const auto &[name, edit] : edits) {
        const auto session = textSession();
        const QUuid id = session->activeLayerID().value();
        QVERIFY2(edit(*session), qPrintable(name));
        QCOMPARE(session->history.undoName(), name);
        QVERIFY2(!layerWith(*session, id).text, qPrintable(name));
    }
    // A mask stroke leaves the text as it was.
    const auto session = textSession();
    const QUuid id = session->activeLayerID().value();
    session->addLayerMask(true);
    session->selectLayerTarget(id, true);
    session->selectTool(NavigationTool::brush);
    session->beginBrush(QPointF(40, 60));
    session->finishBrush();
    QVERIFY(layerWith(*session, id).liveText());
}

void TypeStoreTests::resizersKeepOrDropTheText()
{
    const auto session = textSession();
    const ProjectSnapshot snapshot = session->projectSnapshot().value();
    const ProjectSnapshot canvas = CanvasResizer::resize(snapshot, {.width = 500, .height = 400});
    QCOMPARE(canvas.manifest.layers[1].text, snapshot.manifest.layers[1].text);
    QVERIFY(!ImageResizer::resize(snapshot, {.width = 800, .height = 600, .resolution = 72}).manifest.layers[1].text);
    // Swift's rebuild after a resize leaves the text out.
    session->applyDocumentSize(canvas, QStringLiteral("Canvas Size"));
    QVERIFY(!session->document().value().layers[1].text);
}

QTEST_MAIN(TypeStoreTests)
#include "TypeStoreTests.moc"
