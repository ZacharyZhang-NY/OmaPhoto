#include "Document/EditorSession.h"
#include "IO/ProjectStore.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

// Swift 1.3.2: only the selected letters take a colour.
namespace {
const PaletteColor red{1, 0, 0};
const PaletteColor blue{0, 0, 1};

std::unique_ptr<EditorSession> makeSession()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(800, 600, true);
    session->selectTool(NavigationTool::type);
    return session;
}

// Swift writes the draft's content and selection in place.
void edit(EditorSession &session, const QString &content, TextSpan selection)
{
    TextDraft draft = session.textDraft().value();
    draft.style.content = content;
    draft.selection = selection;
    session.setTextDraft(draft);
}

void pick(EditorSession &session, const PaletteColor &color)
{
    PickerHSB hsb = session.colorPicker().value().hsb;
    hsb.setRGB(color);
    session.setColorPickerHSB(hsb);
}

// Opaque pixels mostly red, and those that are dark.
std::pair<int, int> redAndDark(const QImage &image)
{
    int reds = 0, darks = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor colour = image.pixelColor(x, y);
            if (colour.alphaF() <= 0.9f)
                continue;
            if (colour.redF() > 0.8f && colour.greenF() < 0.2f)
                ++reds;
            else if (colour.redF() < 0.2f)
                ++darks;
        }
    }
    return {reds, darks};
}

LayerTextColorRun run(qint64 location, qint64 length, const PaletteColor &color)
{
    return {location, length, color.red, color.green, color.blue};
}
}

class TextColorRunsTests : public QObject {
    Q_OBJECT
private slots:
    void selectedColorPaintsOnlyThoseLettersAndSurvivesReopening();
    void cancelingPickerRestoresSelectionColors();
    void theSwatchShowsTheSelectionOrTheLetterBeforeTheCaret();
    void theTypeBarsPickerPaintsTheSelectionAndCancelsBack();
    void fillAndNewTextStartInOneColour();
    void theManifestWritesRunsOnlyWhenThere();
    void aRunInALaterParagraphColoursItsOwnLetters();
    void runsDecodeAsSwiftsCodable();
    void cancelPutsBackMixedColoursForBothPickers();
};

namespace {
// A saved text layer with a run, and its manifest.
struct SavedRun {
    QTemporaryDir root;
    QString path = root.filePath(QStringLiteral("Run.comp"));
    QJsonObject manifest;
    SavedRun()
    {
        const auto session = makeSession();
        session->beginText(QPointF(30, 40));
        edit(*session, QStringLiteral("Abcd"), {1, 2});
        session->setPaletteColor(PaletteColor{0.25, 0.5, 0.75}, false);
        if (!session->finishText())
            throw std::runtime_error("the text was refused");
        ProjectStore::save(session->projectSnapshot().value(), path);
        QFile file(path + QStringLiteral("/manifest.json"));
        if (!file.open(QIODevice::ReadOnly))
            throw std::runtime_error("no manifest");
        manifest = QJsonDocument::fromJson(file.readAll()).object();
    }
    // Loads the manifest with its text's runs replaced.
    std::optional<ProjectError::Kind> load(const std::function<void(QJsonObject &)> &change) const
    {
        QJsonObject edited = manifest;
        QJsonArray layers = edited.value("layers").toArray();
        QJsonObject layer = layers.last().toObject();
        QJsonObject text = layer.value("text").toObject();
        change(text);
        layer.insert("text", text);
        layers.replace(layers.size() - 1, layer);
        edited.insert("layers", layers);
        QFile file(path + QStringLiteral("/manifest.json"));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            throw std::runtime_error("no manifest");
        file.write(QJsonDocument(edited).toJson());
        file.close();
        try {
            ProjectStore::load(path);
            return std::nullopt;
        } catch (const ProjectError &error) {
            return error.kind;
        }
    }
};
}

void TextColorRunsTests::selectedColorPaintsOnlyThoseLettersAndSurvivesReopening()
{
    const auto session = makeSession();
    session->beginText(QPointF(30, 40));
    edit(*session, QStringLiteral("AAAA BBBB"), {5, 4});
    session->openTextColorPicker();
    pick(*session, red);
    session->previewTextColor();
    session->closeColorPicker(true);
    QCOMPARE(session->textDraft().value().style.red, 0.0);
    QVERIFY(session->textDraft().value().style.color(5) == red);
    QVERIFY(session->finishText());
    const auto pixels = redAndDark(session->activeLayer().value().asset.value().image());
    QVERIFY2(pixels.first > 50 && pixels.second > 50, qPrintable(QStringLiteral("%1 %2").arg(pixels.first).arg(pixels.second)));
    const ProjectSnapshot snapshot = session->projectSnapshot().value();
    QCOMPARE(snapshot.manifest.version, qint64(10));
    QTemporaryDir root;
    const QString path = root.filePath(QStringLiteral("TextColors.comp"));
    ProjectStore::save(snapshot, path);
    EditorSession reopened;
    reopened.installProject(ProjectStore::load(path), path);
    const LayerText text = reopened.document().value().layers.back().liveText().value();
    QVERIFY(text.style == session->activeLayer().value().liveText().value().style);
    QCOMPARE(redAndDark(reopened.document().value().layers.back().asset.value().image()), pixels);
    // Version 9 cannot hold runs.
    QFile manifest(path + QStringLiteral("/manifest.json"));
    QVERIFY(manifest.open(QIODevice::ReadOnly));
    QJsonObject object = QJsonDocument::fromJson(manifest.readAll()).object();
    manifest.close();
    object.insert(QStringLiteral("version"), 9);
    QVERIFY(manifest.open(QIODevice::WriteOnly | QIODevice::Truncate));
    manifest.write(QJsonDocument(object).toJson());
    manifest.close();
    try {
        ProjectStore::load(path);
        QFAIL("version 9 with colour runs loaded");
    } catch (const ProjectError &error) {
        QCOMPARE(error.kind, ProjectError::Kind::invalid);
    }
    session->undo();
    QVERIFY(!session->activeLayer() || !session->activeLayer().value().liveText());
}

void TextColorRunsTests::cancelingPickerRestoresSelectionColors()
{
    const auto session = makeSession();
    session->beginText(QPointF(30, 40));
    edit(*session, QStringLiteral("Two words"), {0, 3});
    session->setPaletteColor(red, false);
    const LayerTextStyle colored = session->textDraft().value().style;
    QCOMPARE(colored.colorRuns.value().size(), size_t(1));
    session->openColorPicker(false);
    pick(*session, blue);
    session->previewTextColor();
    QCOMPARE(session->textDraft().value().style.color(0).blue, 1.0);
    session->closeColorPicker(false);
    QVERIFY(session->textDraft().value().style == colored);
    // Retyped meanwhile, the runs cannot come back: one colour.
    session->openColorPicker(false);
    pick(*session, blue);
    session->previewTextColor();
    edit(*session, QStringLiteral("Other words"), {0, 0});
    session->closeColorPicker(false);
    const LayerTextStyle retyped = session->textDraft().value().style;
    QVERIFY(!retyped.colorRuns && retyped.red == colored.red && retyped.blue == colored.blue);
    // Nothing selected: the whole text previews, Cancel restores it.
    edit(*session, QStringLiteral("Other words"), {3, 0});
    session->openColorPicker(false);
    pick(*session, red);
    session->previewTextColor();
    QVERIFY(session->textDraft().value().style.red == 1 && !session->textDraft().value().style.colorRuns);
    session->closeColorPicker(false);
    QVERIFY(session->textDraft().value().style.red == 0 && session->textDraft().value().style.blue == 0);
}

void TextColorRunsTests::theSwatchShowsTheSelectionOrTheLetterBeforeTheCaret()
{
    const auto session = makeSession();
    QVERIFY(session->typeColor() == session->foregroundColor());
    session->beginText(QPointF(30, 40));
    edit(*session, QStringLiteral("abcdef"), {2, 2});
    session->setPaletteColor(red, false);
    // Selected: the first selected letter's colour.
    QVERIFY(session->typeColor() == red);
    edit(*session, QStringLiteral("abcdef"), {1, 3});
    QVERIFY(session->typeColor() == PaletteColor::black());
    // The Type bar's picker previews the selection; Cancel undoes.
    edit(*session, QStringLiteral("abcdef"), {4, 0});
    QVERIFY(session->typeColor() == red);
    edit(*session, QStringLiteral("abcdef"), {2, 0});
    QVERIFY(session->typeColor() == PaletteColor::black());
    edit(*session, QStringLiteral("abcdef"), {0, 0});
    QVERIFY(session->typeColor() == PaletteColor::black());
}

void TextColorRunsTests::theTypeBarsPickerPaintsTheSelectionAndCancelsBack()
{
    const auto session = makeSession();
    session->beginText(QPointF(30, 40));
    edit(*session, QStringLiteral("AAAA BBBB"), {0, 4});
    const LayerTextStyle before = session->textDraft().value().style;
    // Fill recolours every letter, runs gone, even unchanged.
    session->openTextColorPicker();
    QVERIFY(session->colorPicker().value().editedText.has_value());
    pick(*session, blue);
    session->previewTextColor();
    QVERIFY(session->textDraft().value().style.color(0) == blue && session->textDraft().value().style.color(5) == PaletteColor::black());
    session->closeColorPicker(false);
    QVERIFY(session->textDraft().value().style == before);
    // OK paints the selection and moves the foreground.
    session->openTextColorPicker();
    pick(*session, blue);
    session->closeColorPicker(true);
    QVERIFY(session->textDraft().value().style.color(3) == blue && session->textDraft().value().style.color(4) == PaletteColor::black());
    QVERIFY(session->foregroundColor() == blue);
}

void TextColorRunsTests::fillAndNewTextStartInOneColour()
{
    const auto session = makeSession();
    session->beginText(QPointF(30, 40));
    edit(*session, QStringLiteral("Two words"), {4, 5});
    session->setPaletteColor(red, false);
    QVERIFY(session->finishText());
    const QUuid id = session->activeLayerID().value();
    // The defaults keep the style, never the runs.
    QVERIFY(!session->textDefaults().colorRuns);
    // The next text holds no runs of the last.
    session->beginText(QPointF(300, 300), true);
    QVERIFY(!session->textDraft().value().style.colorRuns);
    session->cancelText();
    // A run's keys, as Swift's Codable writes them.
    QVERIFY(session->recolorText(id, PaletteColor::black()));
    const LayerTextStyle filled = session->activeLayer().value().liveText().value().style;
    QVERIFY(!filled.colorRuns && filled.red == 0);
    QCOMPARE(session->history.undoName(), QString("Fill Text"));
    QCOMPARE(redAndDark(session->activeLayer().value().asset.value().image()).first, 0);
}

void TextColorRunsTests::theManifestWritesRunsOnlyWhenThere()
{
    const auto session = makeSession();
    session->beginText(QPointF(30, 40));
    edit(*session, QStringLiteral("Plain"), {0, 0});
    QVERIFY(session->finishText());
    QTemporaryDir root;
    const QString path = root.filePath(QStringLiteral("Plain.comp"));
    ProjectStore::save(session->projectSnapshot().value(), path);
    QFile manifest(path + QStringLiteral("/manifest.json"));
    QVERIFY(manifest.open(QIODevice::ReadOnly));
    const QByteArray bytes = manifest.readAll();
    QVERIFY(!bytes.contains("colorRuns"));
    // A run's keys, as Swift's Codable writes them.
    session->editActiveText();
    edit(*session, QStringLiteral("Plain"), {1, 2});
    session->setPaletteColor(red, false);
    QVERIFY(session->finishText());
    ProjectStore::save(session->projectSnapshot().value(), path);
    manifest.close();
    QVERIFY(manifest.open(QIODevice::ReadOnly));
    const QJsonObject object = QJsonDocument::fromJson(manifest.readAll()).object();
    const QJsonObject text = object.value("layers").toArray().last().toObject().value("text").toObject();
    QCOMPARE(text.value("colorRuns").toArray(), (QJsonArray{QJsonObject{{"location", 1}, {"length", 2}, {"red", 1}, {"green", 0}, {"blue", 0}}}));
}

void TextColorRunsTests::aRunInALaterParagraphColoursItsOwnLetters()
{
    const auto session = makeSession();
    session->beginText(QPointF(30, 40));
    edit(*session, QStringLiteral("MM\nMM"), {3, 2});
    session->setPaletteColor(red, false);
    QCOMPARE(session->textDraft().value().style.colorRuns.value(), (std::vector{run(3, 2, red)}));
    QVERIFY(session->finishText());
    const QImage image = session->activeLayer().value().asset.value().image();
    // The first line dark, the second red.
    const auto [topRed, topDark] = redAndDark(image.copy(0, 0, image.width(), image.height() / 2));
    const auto [lowRed, lowDark] = redAndDark(image.copy(0, image.height() / 2, image.width(), image.height() - image.height() / 2));
    QVERIFY2(topRed == 0 && topDark > 50 && lowRed > 50 && lowDark == 0,
             qPrintable(QStringLiteral("%1 %2 %3 %4").arg(topRed).arg(topDark).arg(lowRed).arg(lowDark)));
    // A run across the break: each line its own share.
    session->editActiveText();
    edit(*session, QStringLiteral("MM\nMM"), {1, 3});
    session->setPaletteColor(PaletteColor{0, 1, 0}, false);
    QVERIFY(session->finishText());
    const QImage across = session->activeLayer().value().asset.value().image();
    const auto count = [&across](bool upper, int channel) {
        int found = 0;
        const int from = upper ? 0 : across.height() / 2, to = upper ? across.height() / 2 : across.height();
        for (int y = from; y < to; ++y) {
            for (int x = 0; x < across.width(); ++x) {
                const QColor colour = across.pixelColor(x, y);
                const std::array<float, 3> rgb{colour.redF(), colour.greenF(), colour.blueF()};
                const bool dark = channel < 0 && rgb[0] < 0.2f && rgb[1] < 0.2f;
                found += colour.alphaF() > 0.9f && (dark || (channel >= 0 && rgb[size_t(channel)] > 0.8f && rgb[size_t(1 - channel)] < 0.2f));
            }
        }
        return found;
    };
    // Upper: dark then green; lower: green then red.
    QVERIFY(count(true, -1) > 50 && count(true, 1) > 50 && count(true, 0) == 0);
    QVERIFY(count(false, -1) == 0 && count(false, 1) > 50 && count(false, 0) > 50);
}

void TextColorRunsTests::runsDecodeAsSwiftsCodable()
{
    const SavedRun saved;
    const QJsonArray written = saved.manifest.value("layers").toArray().last().toObject().value("text").toObject().value("colorRuns").toArray();
    QCOMPARE(written, (QJsonArray{QJsonObject{{"location", 1}, {"length", 2}, {"red", 0.25}, {"green", 0.5}, {"blue", 0.75}}}));
    const LayerTextStyle loaded = ProjectStore::load(saved.path).manifest.layers.back().text.value();
    QCOMPARE(loaded.colorRuns.value(), (std::vector{LayerTextColorRun{1, 2, 0.25, 0.5, 0.75}}));
    const auto withRun = [&](const std::function<void(QJsonObject &)> &change) {
        return saved.load([&](QJsonObject &text) {
            QJsonObject run = written.first().toObject();
            change(run);
            text.insert("colorRuns", QJsonArray{run});
        });
    };
    const std::optional<ProjectError::Kind> invalid = ProjectError::Kind::invalid;
    // Each of the five keys required, each a number.
    for (const char *key : {"location", "length", "red", "green", "blue"}) {
        QCOMPARE(withRun([key](QJsonObject &run) { run.remove(key); }), invalid);
        QCOMPARE(withRun([key](QJsonObject &run) { run.insert(key, QStringLiteral("1")); }), invalid);
        QCOMPARE(withRun([key](QJsonObject &run) { run.insert(key, QJsonValue::Null); }), invalid);
        QCOMPARE(withRun([key](QJsonObject &run) { run.insert(key, true); }), invalid);
    }
    // Offsets are whole Int64s.
    QCOMPARE(withRun([](QJsonObject &run) { run.insert("location", 1.5); }), invalid);
    QCOMPARE(withRun([](QJsonObject &run) { run.insert("length", 1e19); }), invalid);
    // Unknown keys pass.
    QCOMPARE(withRun([](QJsonObject &run) { run.insert("alpha", 1); }), std::nullopt);
    // Not an array, or an entry no object: refused.
    QCOMPARE(saved.load([](QJsonObject &text) { text.insert("colorRuns", QJsonObject()); }), invalid);
    QCOMPARE(saved.load([](QJsonObject &text) { text.insert("colorRuns", QJsonArray{5}); }), invalid);
    // Absent or null: one colour.
    QCOMPARE(saved.load([](QJsonObject &text) { text.remove("colorRuns"); }), std::nullopt);
    QVERIFY(!ProjectStore::load(saved.path).manifest.layers.back().text.value().colorRuns);
    QCOMPARE(saved.load([](QJsonObject &text) { text.insert("colorRuns", QJsonValue::Null); }), std::nullopt);
    QVERIFY(!ProjectStore::load(saved.path).manifest.layers.back().text.value().colorRuns);
}

void TextColorRunsTests::cancelPutsBackMixedColoursForBothPickers()
{
    for (const bool typeBar : {false, true}) {
        const auto session = makeSession();
        session->beginText(QPointF(30, 40));
        // A blue base; red and green in the coming selection.
        edit(*session, QStringLiteral("abcdef"), {0, 0});
        session->setPaletteColor(blue, false);
        edit(*session, QStringLiteral("abcdef"), {1, 1});
        session->setPaletteColor(red, false);
        edit(*session, QStringLiteral("abcdef"), {2, 1});
        session->setPaletteColor(PaletteColor{0, 1, 0}, false);
        edit(*session, QStringLiteral("abcdef"), {1, 3});
        const LayerTextStyle original = session->textDraft().value().style;
        QCOMPARE(original.colorRuns.value().size(), size_t(2));
        const auto open = [&] { typeBar ? session->openTextColorPicker() : session->openColorPicker(false); };
        open();
        pick(*session, PaletteColor{1, 1, 0});
        session->previewTextColor();
        QVERIFY(session->textDraft().value().style.color(1) == (PaletteColor{1, 1, 0}));
        session->closeColorPicker(false);
        QVERIFY2(session->textDraft().value().style == original, typeBar ? "type bar" : "palette");
        // Retyped meanwhile: the new text stays, the base comes back.
        open();
        pick(*session, PaletteColor{1, 1, 0});
        session->previewTextColor();
        edit(*session, QStringLiteral("xyz"), {0, 0});
        session->closeColorPicker(false);
        const LayerTextStyle retyped = session->textDraft().value().style;
        QVERIFY(retyped.content == "xyz" && !retyped.colorRuns);
        QVERIFY((PaletteColor{retyped.red, retyped.green, retyped.blue}) == blue);
    }
}

QTEST_MAIN(TextColorRunsTests)
#include "TextColorRunsTests.moc"
