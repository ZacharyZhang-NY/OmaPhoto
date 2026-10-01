#include "Document/EditorSession.h"
#include "IO/ProjectStore.h"
#include "Rendering/TextLayout.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFontInfo>
#include <QFontMetricsF>
#include <limits>
#include <QTemporaryDir>
#include <QtTest>

// Swift 1.3.4: a font change applies to the selected letters.
namespace {
const QString sans = QStringLiteral("DejaVuSans");
const QString mono = QStringLiteral("DejaVuSansMono");

std::unique_ptr<EditorSession> makeSession()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(800, 600, true);
    session->selectTool(NavigationTool::type);
    return session;
}

LayerTextStyle hello()
{
    LayerTextStyle style;
    style.content = QStringLiteral("Hello");
    style.fontName = sans;
    return style;
}

// A saved text layer whose first two letters are mono.
struct SavedFonts {
    QTemporaryDir root;
    QString path = root.filePath(QStringLiteral("Fonts.comp"));
    SavedFonts()
    {
        const auto session = makeSession();
        session->beginText(QPointF(30, 40));
        TextDraft draft = session->textDraft().value();
        draft.style.content = QStringLiteral("Hello");
        draft.style.fontName = sans;
        draft.selection = {0, 2};
        session->setTextDraft(draft);
        session->changeTextStyle([](LayerTextStyle &style) { style.setFont(mono, {0, 2}); });
        if (!session->finishText())
            throw std::runtime_error("the text was refused");
        ProjectStore::save(session->projectSnapshot().value(), path);
    }
    // The manifest with its text's runs and version rewritten.
    std::optional<ProjectError::Kind> load(qint64 version, const QJsonValue &fontRuns) const
    {
        QFile file(path + QStringLiteral("/manifest.json"));
        if (!file.open(QIODevice::ReadOnly))
            throw std::runtime_error("no manifest");
        QJsonObject manifest = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        manifest["version"] = version;
        QJsonArray layers = manifest["layers"].toArray();
        const qsizetype index = layers.size() - 1;
        QJsonObject layer = layers[index].toObject();
        if (!layer.contains("text"))
            throw std::runtime_error("the top layer holds no text");
        QJsonObject text = layer["text"].toObject();
        text["fontRuns"] = fontRuns;
        layer["text"] = text;
        layers[index] = layer;
        manifest["layers"] = layers;
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(QJsonDocument(manifest).toJson()) < 0)
            throw std::runtime_error("the manifest could not be written");
        file.close();
        try {
            ProjectStore::load(path);
            return std::nullopt;
        } catch (const ProjectError &error) {
            return error.kind;
        }
    }
};

QJsonObject run(qint64 location, qint64 length, const QString &face)
{
    return {{"location", location}, {"length", length}, {"fontName", face}};
}
}

class TextFontRunsTests : public QObject {
    Q_OBJECT
private slots:
    void fontAppliesToTheSelectionOnly();
    void selectedFontSurvivesReopening();
    void runsNeedVersionElevenAndAValidFace();
    void eachRunLaysOutInItsFace();
    void newTextStartsInOneFace();
};

// Swift's test, with faces of different widths kept.
void TextFontRunsTests::fontAppliesToTheSelectionOnly()
{
    LayerTextStyle style = hello();
    style.setFont(mono, {0, 2});
    QCOMPARE(style.fontName, sans);
    QCOMPARE(style.fontRuns.value(), (std::vector<LayerTextFontRun>{{0, 2, mono}}));
    QVERIFY(style.fontNameAt(0) == mono && style.fontNameAt(2) == sans);
    const double mixed = EditorSession::textBoxSize(style).width();
    style.setFont(mono, {0, 0});
    QVERIFY(!style.fontRuns && style.fontName == mono);
    QVERIFY(EditorSession::textBoxSize(style).width() > mixed);
    style.setFont(sans, {1, 3});
    style.replaceCharacters({5, 0}, 1);
    style.content += QStringLiteral("!");
    QVERIFY(style.isValid() && style.fontNameAt(5) == mono);
    QCOMPARE(style.uniformFontName({0, 2}), std::optional<QString>());
    QCOMPARE(style.uniformFontName({1, 3}), std::optional(sans));
    QCOMPARE(style.uniformFontName({4, 2}), std::optional(mono));
    QCOMPARE(style.uniformFontName({2, 0}), std::optional<QString>());
    style.setFont(mono, {0, 6});
    QVERIFY(!style.fontRuns && style.fontName == mono);
    style.setFont(sans, {0, 2});
    QCOMPARE(style.fontRuns.value(), (std::vector<LayerTextFontRun>{{0, 2, sans}}));
    // A name that is no face changes nothing.
    style.setFont(QString(), {2, 1});
    style.setFont(QStringLiteral("Bad\nName"), {2, 1});
    QCOMPARE(style.fontRuns.value(), (std::vector<LayerTextFontRun>{{0, 2, sans}}));
    // Same-face runs apart stay apart; a base gap mixes.
    LayerTextStyle apart = hello();
    apart.setFont(mono, {0, 1});
    apart.setFont(mono, {3, 1});
    QCOMPARE(apart.fontRuns.value(), (std::vector<LayerTextFontRun>{{0, 1, mono}, {3, 1, mono}}));
    QCOMPARE(apart.uniformFontName({0, 4}), std::optional<QString>());
    QCOMPARE(apart.uniformFontName({3, 1}), std::optional(mono));
    // All letters in the run's face: the text's face.
    LayerTextStyle kept = hello();
    kept.setFont(mono, {0, 2});
    kept.replaceCharacters({2, 3}, 0);
    kept.content.truncate(2);
    QVERIFY(!kept.fontRuns && kept.fontName == mono);
    // Neighbouring runs in two faces stay two.
    const QString bold = QStringLiteral("DejaVuSans-Bold");
    LayerTextStyle three = hello();
    three.setFont(mono, {0, 1});
    three.setFont(bold, {1, 1});
    QCOMPARE(three.fontRuns.value(), (std::vector<LayerTextFontRun>{{0, 1, mono}, {1, 1, bold}}));
    QVERIFY(three.fontNameAt(0) == mono && three.fontNameAt(1) == bold && three.fontNameAt(2) == sans);
    // A run starting at the span's end stays out.
    QCOMPARE(three.uniformFontName({0, 1}), std::optional(mono));
    QCOMPARE(three.uniformFontName({2, 3}), std::optional(sans));
    // A span past the end stops at it.
    three.setFont(mono, {3, 99});
    QCOMPARE(three.fontRuns.value(), (std::vector<LayerTextFontRun>{{0, 1, mono}, {1, 1, bold}, {3, 2, mono}}));
    QCOMPARE(three.uniformFontName({3, 99}), std::optional(mono));
    // Inserted at the start, letters take the first letter's face.
    LayerTextStyle first = hello();
    first.setFont(mono, {0, 1});
    first.replaceCharacters({0, 0}, 2);
    first.content.prepend(QStringLiteral("xy"));
    QCOMPARE(first.fontRuns.value(), (std::vector<LayerTextFontRun>{{0, 3, mono}}));
    first.replaceCharacters({0, 1}, 1);
    QCOMPARE(first.fontRuns.value(), (std::vector<LayerTextFontRun>{{0, 3, mono}}));
    // Deleting a run's letters drops the run.
    style.replaceCharacters({0, 2}, 0);
    style.content.remove(0, 2);
    QVERIFY(!style.fontRuns && style.fontName == mono);
}

void TextFontRunsTests::selectedFontSurvivesReopening()
{
    const SavedFonts saved;
    EditorSession reopened;
    reopened.installProject(ProjectStore::load(saved.path), saved.path);
    QCOMPARE(reopened.document().value().layers.back().liveText().value().style.fontRuns.value(), (std::vector<LayerTextFontRun>{{0, 2, mono}}));
    QCOMPARE(reopened.document().value().layers.back().liveText().value().style.fontName, sans);
    QCOMPARE(ProjectStore::load(saved.path).manifest.version, qint64(11));
}

void TextFontRunsTests::runsNeedVersionElevenAndAValidFace()
{
    const SavedFonts saved;
    QCOMPARE(saved.load(11, QJsonArray{run(0, 2, mono)}), std::optional<ProjectError::Kind>());
    // Version 10 cannot hold faces.
    QCOMPARE(saved.load(10, QJsonArray{run(0, 2, mono)}), std::optional(ProjectError::Kind::invalid));
    // Overlapping, empty, past the end, nameless, newline, long, none.
    for (const QJsonArray &runs : {QJsonArray{run(0, 2, mono), run(1, 2, mono)}, QJsonArray{run(0, 0, mono)}, QJsonArray{run(4, 2, mono)},
                                   QJsonArray{run(0, 2, QString())}, QJsonArray{run(0, 2, QStringLiteral("A B"))},
                                   QJsonArray{run(0, 2, QString(201, u'x'))}, QJsonArray{}})
        QCOMPARE(saved.load(11, runs), std::optional(ProjectError::Kind::invalid));
    // Two hundred characters, a decomposed letter counted once, pass.
    QString name;
    for (int index = 0; index < 200; ++index)
        name += QStringLiteral("é");
    QCOMPARE(saved.load(11, QJsonArray{run(0, 2, name)}), std::optional<ProjectError::Kind>());
    // A negative start, a fractional place or length.
    QCOMPARE(saved.load(11, QJsonArray{run(-1, 2, mono)}), std::optional(ProjectError::Kind::invalid));
    QCOMPARE(saved.load(11, QJsonArray{QJsonObject{{"location", 0.5}, {"length", 2}, {"fontName", mono}}}), std::optional(ProjectError::Kind::invalid));
    QCOMPARE(saved.load(11, QJsonArray{QJsonObject{{"location", 0}, {"length", 2.5}, {"fontName", mono}}}), std::optional(ProjectError::Kind::invalid));
    // Every newline character, and a run past Int's end.
    for (const QChar newline : {QChar(u'\n'), QChar(u'\r'), QChar(u'\v'), QChar(u'\f'), QChar(0x85), QChar(QChar::LineSeparator), QChar(QChar::ParagraphSeparator)})
        QCOMPARE(saved.load(11, QJsonArray{run(0, 2, QStringLiteral("A") + newline + QStringLiteral("B"))}), std::optional(ProjectError::Kind::invalid));
    QCOMPARE(saved.load(11, QJsonArray{run(std::numeric_limits<qint64>::max(), 1, mono)}), std::optional(ProjectError::Kind::invalid));
    // A run missing a key, or no array, fails.
    QCOMPARE(saved.load(11, QJsonArray{QJsonObject{{"location", 0}, {"length", 2}}}), std::optional(ProjectError::Kind::invalid));
    QCOMPARE(saved.load(11, QJsonArray{QJsonObject{{"length", 2}, {"fontName", mono}}}), std::optional(ProjectError::Kind::invalid));
    QCOMPARE(saved.load(11, QJsonArray{QJsonObject{{"location", 0}, {"fontName", mono}}}), std::optional(ProjectError::Kind::invalid));
    QCOMPARE(saved.load(11, QJsonValue(3)), std::optional(ProjectError::Kind::invalid));
}

void TextFontRunsTests::eachRunLaysOutInItsFace()
{
    LayerTextStyle style = hello();
    style.content = QStringLiteral("iiii\niiii");
    const double plain = TextLines(style, {10'000, 10'000}).usedSize().width();
    // A run on the second paragraph widens that line alone.
    style.setFont(mono, {5, 4});
    const double wider = TextLines(style, {10'000, 10'000}).usedSize().width();
    QCOMPARE(wider, 4 * QFontMetricsF(TextLayout::font(style, mono)).horizontalAdvance(QLatin1Char('i')));
    QVERIFY(wider > plain);
    // A face named by PostScript keeps its style: bold.
    const QString bold = QStringLiteral("DejaVuSans-Bold");
    LayerTextStyle heavy = hello();
    heavy.content = QStringLiteral("iiiii");
    heavy.setFont(bold, {0, 4});
    QCOMPARE(QFontInfo(TextLayout::font(heavy, bold)).styleName(), QStringLiteral("Bold"));
    // An independent reference: DejaVu Sans Bold at 72 pixels.
    QFont reference(QStringLiteral("DejaVu Sans"));
    reference.setStyleName(QStringLiteral("Bold"));
    reference.setPixelSize(72);
    reference.setKerning(false);
    reference.setHintingPreference(QFont::PreferNoHinting);
    QFont regular = reference;
    regular.setStyleName(QStringLiteral("Book"));
    QCOMPARE(TextLines(heavy, {10'000, 10'000}).usedSize().width(),
             4 * QFontMetricsF(reference).horizontalAdvance(QLatin1Char('i')) + QFontMetricsF(regular).horizontalAdvance(QLatin1Char('i')));
    QVERIFY(QFontMetricsF(reference).horizontalAdvance(QLatin1Char('i')) > QFontMetricsF(regular).horizontalAdvance(QLatin1Char('i')));
    // A run across a break covers both lines.
    style.fontRuns = std::nullopt;
    style.setFont(mono, {2, 5});
    const TextLines across(style, {10'000, 10'000});
    const double narrow = QFontMetricsF(TextLayout::font(style, sans)).horizontalAdvance(QLatin1Char('i'));
    const double wide = QFontMetricsF(TextLayout::font(style, mono)).horizontalAdvance(QLatin1Char('i'));
    QCOMPARE(across.caret(4).value().left(), 2 * narrow + 2 * wide);
    QCOMPARE(across.caret(9).value().left(), 2 * wide + 2 * narrow);
    // At another size and tracking, against fonts built here.
    LayerTextStyle sized = hello();
    sized.content = QStringLiteral("iiiii");
    sized.fontSize = 40;
    sized.tracking = 3;
    sized.setFont(mono, {1, 2});
    const auto made = [](const QString &family) {
        QFont font(family);
        font.setPixelSize(40);
        font.setKerning(false);
        font.setLetterSpacing(QFont::AbsoluteSpacing, 3);
        font.setHintingPreference(QFont::PreferNoHinting);
        return QFontMetricsF(font).horizontalAdvance(QStringLiteral("i"));
    };
    const double expected = 3 * made(QStringLiteral("DejaVu Sans")) + 2 * made(QStringLiteral("DejaVu Sans Mono"));
    QVERIFY(qAbs(TextLines(sized, {10'000, 10'000}).usedSize().width() - expected) < 0.01);
    // Colouring the run's letters keeps their face.
    sized.setColor(PaletteColor{1, 0, 0}, {1, 2});
    QVERIFY(sized.colorRuns && sized.fontRuns);
    QVERIFY(qAbs(TextLines(sized, {10'000, 10'000}).usedSize().width() - expected) < 0.01);
    // The face keeps the style's size and spacing.
    style.tracking = 3;
    QCOMPARE(TextLayout::font(style, mono).letterSpacing(), 3.0);
    QCOMPARE(TextLayout::font(style, mono).pixelSize(), 72);
    QCOMPARE(TextLayout::font(style, mono).family(), QStringLiteral("DejaVu Sans Mono"));
    // An unlisted face asks Qt by its own name.
    QCOMPARE(TextLayout::font(style, QStringLiteral("Zapfino")).family(), QStringLiteral("Zapfino"));
}

void TextFontRunsTests::newTextStartsInOneFace()
{
    const auto session = makeSession();
    session->beginText(QPointF(30, 40));
    TextDraft draft = session->textDraft().value();
    draft.style.content = QStringLiteral("Hello");
    draft.style.fontName = sans;
    session->setTextDraft(draft);
    session->changeTextStyle([](LayerTextStyle &style) { style.setFont(mono, {0, 2}); });
    QVERIFY(session->finishText());
    session->beginText(QPointF(300, 300), true);
    QVERIFY(!session->textDraft().value().style.fontRuns);
    QCOMPARE(session->textDraft().value().style.fontName, sans);
}

QTEST_MAIN(TextFontRunsTests)
#include "TextFontRunsTests.moc"
