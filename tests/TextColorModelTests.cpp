#include "Document/EditorSession.h"
#include "IO/ProjectStore.h"
#include <QTemporaryDir>
#include <QtTest>

// Swift's colour runs in a text style: arithmetic and validity.
namespace {
const PaletteColor red{1, 0, 0};
const PaletteColor blue{0, 0, 1};
const PaletteColor green{0, 1, 0};

LayerTextColorRun run(qint64 location, qint64 length, const PaletteColor &color)
{
    return {location, length, color.red, color.green, color.blue};
}

std::unique_ptr<EditorSession> makeSession()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(800, 600, true);
    session->selectTool(NavigationTool::type);
    return session;
}

void edit(EditorSession &session, const QString &content, TextSpan selection)
{
    TextDraft draft = session.textDraft().value();
    draft.style.content = content;
    draft.selection = selection;
    session.setTextDraft(draft);
}
}

class TextColorModelTests : public QObject {
    Q_OBJECT
private slots:
    void colorAppliesToSelectionAndFollowsEdits();
    void invalidColorRunsAreRejected();
    void everyChannelOfARunIsChecked();
    void runsOfOneColourJoinOnlyWhereTheyMeet();
};

void TextColorModelTests::colorAppliesToSelectionAndFollowsEdits()
{
    LayerTextStyle style;
    style.content = QStringLiteral("Hello world");
    style.setColor(red, {6, 5});
    QCOMPARE(style.colorRuns.value(), (std::vector{run(6, 5, red)}));
    QVERIFY(style.color(5) == PaletteColor::black() && style.color(6) == red);
    // Painting beside a run in its colour joins it.
    style.setColor(red, {5, 1});
    QVERIFY(style.colorRuns.value().size() == 1 && style.colorRuns.value().front().location == 5);
    // Typed letters take the colour before them; deleted ones go.
    style.replaceCharacters({11, 0}, 1);
    style.content += u'!';
    QVERIFY(style.isValid() && style.color(11) == red);
    style.replaceCharacters({0, 2}, 0);
    style.content.remove(0, 2);
    QVERIFY(style.isValid() && style.colorRuns.value().front().location == 3 && style.colorRuns.value().front().length == 7);
    // Typed at the start: the first letter's colour.
    style.replaceCharacters({0, 0}, 2);
    style.content.prepend(QStringLiteral("ab"));
    QVERIFY(style.color(0) == PaletteColor::black() && style.colorRuns.value().front().location == 5);
    // No selection, or all of it, recolours the whole text.
    style.setColor(red, {4, 0});
    QVERIFY(!style.colorRuns && style.red == 1);
    style.setColor(blue, {0, style.content.size()});
    QVERIFY(!style.colorRuns && style.blue == 1 && style.red == 0);
    // Past the end, a span is clamped to the text.
    style.setColor(red, {3, 900});
    QCOMPARE(style.colorRuns.value(), (std::vector{run(3, style.content.size() - 3, red)}));
    // Runs matching the base colour vanish.
    style.setColor(blue, {3, 900});
    QVERIFY(!style.colorRuns);
    // Without runs, an edit keeps none.
    style.replaceCharacters({0, 0}, 3);
    QVERIFY(!style.colorRuns);
}


void TextColorModelTests::invalidColorRunsAreRejected()
{
    LayerTextStyle style;
    style.content = QStringLiteral("Text");
    style.colorRuns = std::vector{run(2, 3, red)};
    QVERIFY(!style.isValid());
    style.colorRuns = std::vector{run(0, 2, red), run(1, 2, PaletteColor{0, 1, 0})};
    QVERIFY(!style.isValid());
    style.colorRuns = std::vector{LayerTextColorRun{0, 1, 2, 0, 0}};
    QVERIFY(!style.isValid());
    style.colorRuns = std::vector{run(1, 0, red)};
    QVERIFY(!style.isValid());
    style.colorRuns = std::vector{run(-1, 2, red)};
    QVERIFY(!style.isValid());
    style.colorRuns = std::vector{run(1, std::numeric_limits<qint64>::max(), red)};
    QVERIFY(!style.isValid());
    style.colorRuns = std::vector<LayerTextColorRun>();
    QVERIFY(!style.isValid());
    style.colorRuns = std::vector{run(0, 1, red), run(3, 1, blue)};
    QVERIFY(style.isValid());
}


void TextColorModelTests::everyChannelOfARunIsChecked()
{
    LayerTextStyle style;
    style.content = QStringLiteral("Text");
    for (int channel = 0; channel < 3; ++channel) {
        for (const double value : {-0.01, 1.01, std::nan(""), std::numeric_limits<double>::infinity(), 0.0, 1.0}) {
            LayerTextColorRun run{0, 1, 0.5, 0.5, 0.5};
            (channel == 0 ? run.red : channel == 1 ? run.green : run.blue) = value;
            style.colorRuns = std::vector{run};
            const bool valid = value == 0 || value == 1;
            QVERIFY2(style.isValid() == valid, qPrintable(QStringLiteral("channel %1 at %2").arg(channel).arg(value)));
        }
    }
    // The store refuses an invalid run on saving too.
    const auto session = makeSession();
    session->beginText(QPointF(30, 40));
    edit(*session, QStringLiteral("Text"), {0, 1});
    session->setPaletteColor(red, false);
    QVERIFY(session->finishText());
    ProjectSnapshot snapshot = session->projectSnapshot().value();
    snapshot.manifest.layers.back().text->colorRuns->front().blue = 2;
    QTemporaryDir root;
    try {
        ProjectStore::save(snapshot, root.filePath(QStringLiteral("Bad.comp")));
        QFAIL("an invalid run saved");
    } catch (const ProjectError &error) {
        QCOMPARE(error.kind, ProjectError::Kind::invalid);
    }
}


void TextColorModelTests::runsOfOneColourJoinOnlyWhereTheyMeet()
{
    LayerTextStyle style;
    style.content = QStringLiteral("abcde");
    style.setColor(green, {0, 1});
    style.setColor(green, {4, 1});
    // A base-coloured gap keeps two runs apart.
    QCOMPARE(style.colorRuns.value(), (std::vector{run(0, 1, green), run(4, 1, green)}));
    QVERIFY(style.color(2) == PaletteColor::black());
    // Split by the base colour, then repainted: one run again.
    style.setColor(green, {0, 5});
    style.setColor(red, {0, 5});
    style.setColor(green, {1, 3});
    style.setColor(red, {2, 1});
    QCOMPARE(style.colorRuns.value(), (std::vector{run(1, 1, green), run(3, 1, green)}));
    style.setColor(green, {2, 1});
    QCOMPARE(style.colorRuns.value(), (std::vector{run(1, 3, green)}));
}

QTEST_MAIN(TextColorModelTests)
#include "TextColorModelTests.moc"
