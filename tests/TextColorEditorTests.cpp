#include "InlineTextDrawFixtures.h"
#include "Rendering/TextLayout.h"
#include "UI/ColorPaletteControls.h"
#include "UI/TypeControls.h"
#include <QApplication>
#include <QClipboard>
#include <QLineEdit>
#include <QInputMethodEvent>
#include <QPainter>
#include <QFontMetricsF>

// Swift 1.3.2 on the canvas: colours keep to their letters.
namespace {
const PaletteColor green{0, 1, 0};

std::optional<std::vector<LayerTextColorRun>> runs(const DrawnText &shown)
{
    return shown.session.textDraft().value().style.colorRuns;
}

LayerTextColorRun greenRun(qint64 location, qint64 length)
{
    return {location, length, 0, 1, 0};
}

// The caret's middle on screen, `advance` along the first line.
QPoint caretAt(const DrawnText &shown, const QString &before)
{
    const LayerTextStyle &style = shown.session.textDraft().value().style;
    const QRectF caret = shown.editor().textTransform().mapRect(
        QRectF(QPointF(LayerTextStyle::padding, LayerTextStyle::padding), QSizeF(1, style.lineHeight()))
            .translated(QFontMetricsF(TextLayout::font(style)).horizontalAdvance(before), 0));
    return caret.center().toPoint();
}

// A caret a pixel wide may straddle two pixels.
bool inked(const QImage &image, QPoint at, bool (*test)(QColor))
{
    return test(image.pixelColor(at)) || test(image.pixelColor(at - QPoint(1, 0))) || test(image.pixelColor(at + QPoint(1, 0)));
}
}

class TextColorEditorTests : public QObject {
    Q_OBJECT
private slots:
    void theSelectionReachesTheDraft();
    void coloursFollowTypingDeletingAndUndo();
    void pastingKeepsTheColoursAround();
    void aLetterTypedBetweenTwinsTakesTheOneBeforeIt();
    void theCaretTakesTheLetterBeforeIt();
    void theSwatchKeepsTheSelectionItPaints();
    void theLayerDrawsEachRunInItsColour();
    void theEditorDrawsNoGlyphsOfItsOwn();
    void twinsKeepTheirColoursOnEveryPath_data();
    void twinsKeepTheirColoursOnEveryPath();
    void aColouredTailOutlastsEveryStep_data();
    void aColouredTailOutlastsEveryStep();
    void aPreeditOverTheFirstLetterTakesItsColour();
    void atTheStartTheFirstLetterGivesTheColour();
};

void TextColorEditorTests::theSelectionReachesTheDraft()
{
    DrawnText shown;
    shown.canvas->setFocus();
    beginTextAt(shown.session, QPointF(20, 30));
    QTest::keyClicks(shown.canvas, QStringLiteral("abcd"));
    QCOMPARE(shown.session.textDraft().value().selection, (TextSpan{4, 0}));
    QTest::keyClick(shown.canvas, Qt::Key_Left, Qt::ShiftModifier);
    QTest::keyClick(shown.canvas, Qt::Key_Left, Qt::ShiftModifier);
    QCOMPARE(shown.session.textDraft().value().selection, (TextSpan{2, 2}));
    QTest::keyClick(shown.canvas, Qt::Key_Home);
    QCOMPARE(shown.session.textDraft().value().selection, (TextSpan{0, 0}));
    // Select All reaches it too.
    QTest::keyClick(shown.canvas, Qt::Key_A, Qt::ControlModifier);
    QCOMPARE(shown.session.textDraft().value().selection, (TextSpan{0, 4}));
}

void TextColorEditorTests::coloursFollowTypingDeletingAndUndo()
{
    DrawnText shown;
    shown.canvas->setFocus();
    beginTextAt(shown.session, QPointF(20, 30));
    QTest::keyClicks(shown.canvas, QStringLiteral("abcd"));
    QTest::keyClick(shown.canvas, Qt::Key_Left);
    QTest::keyClick(shown.canvas, Qt::Key_Left, Qt::ShiftModifier);
    QTest::keyClick(shown.canvas, Qt::Key_Left, Qt::ShiftModifier);
    shown.session.setPaletteColor(green, false);
    QCOMPARE(runs(shown).value(), (std::vector{greenRun(1, 2)}));
    // After a green letter, typing is green; after red, red.
    QTest::keyClick(shown.canvas, Qt::Key_Right);
    QTest::keyClicks(shown.canvas, QStringLiteral("x"));
    QCOMPARE(shown.session.textDraft().value().style.content, QString("abcxd"));
    QCOMPARE(runs(shown).value(), (std::vector{greenRun(1, 3)}));
    QTest::keyClick(shown.canvas, Qt::Key_Home);
    QTest::keyClick(shown.canvas, Qt::Key_Right);
    QTest::keyClicks(shown.canvas, QStringLiteral("y"));
    QCOMPARE(runs(shown).value(), (std::vector{greenRun(2, 3)}));
    // A deletion takes its letters' colours away.
    QTest::keyClick(shown.canvas, Qt::Key_Right);
    QTest::keyClick(shown.canvas, Qt::Key_Backspace);
    QCOMPARE(shown.session.textDraft().value().style.content, QString("aycxd"));
    QCOMPARE(runs(shown).value(), (std::vector{greenRun(2, 2)}));
    // Undo puts the letter back in the colour before it.
    QTest::keyClick(shown.canvas, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(shown.session.textDraft().value().style.content, QString("aybcxd"));
    QCOMPARE(runs(shown).value(), (std::vector{greenRun(3, 2)}));
    // Deleting every green letter leaves one colour.
    QTest::keyClick(shown.canvas, Qt::Key_End);
    QTest::keyClick(shown.canvas, Qt::Key_Left);
    for (int step = 0; step < 3; ++step)
        QTest::keyClick(shown.canvas, Qt::Key_Left, Qt::ShiftModifier);
    QTest::keyClick(shown.canvas, Qt::Key_Delete);
    QCOMPARE(shown.session.textDraft().value().style.content, QString("ayd"));
    QVERIFY(!runs(shown));
}

void TextColorEditorTests::pastingKeepsTheColoursAround()
{
    DrawnText shown;
    shown.canvas->setFocus();
    beginTextAt(shown.session, QPointF(20, 30));
    QTest::keyClicks(shown.canvas, QStringLiteral("abcd"));
    QTest::keyClick(shown.canvas, Qt::Key_Left, Qt::ShiftModifier);
    shown.session.setPaletteColor(green, false);
    QCOMPARE(runs(shown).value(), (std::vector{greenRun(3, 1)}));
    // Pasted at the start, before every run: runs move on.
    QGuiApplication::clipboard()->setText(QStringLiteral("123"));
    QTest::keyClick(shown.canvas, Qt::Key_Home);
    shown.editor().paste();
    QCOMPARE(shown.session.textDraft().value().style.content, QString("123abcd"));
    QCOMPARE(runs(shown).value(), (std::vector{greenRun(6, 1)}));
    // Pasted after the green letter, green.
    QTest::keyClick(shown.canvas, Qt::Key_End);
    shown.editor().paste();
    QCOMPARE(runs(shown).value(), (std::vector{greenRun(6, 4)}));
}

void TextColorEditorTests::aLetterTypedBetweenTwinsTakesTheOneBeforeIt()
{
    // Typed after a green a, before a red one: green.
    DrawnText shown;
    shown.canvas->setFocus();
    beginTextAt(shown.session, QPointF(20, 30));
    QTest::keyClicks(shown.canvas, QStringLiteral("aa"));
    QTest::keyClick(shown.canvas, Qt::Key_Home);
    QTest::keyClick(shown.canvas, Qt::Key_Right, Qt::ShiftModifier);
    shown.session.setPaletteColor(green, false);
    QCOMPARE(runs(shown).value(), (std::vector{greenRun(0, 1)}));
    QTest::keyClick(shown.canvas, Qt::Key_Right);
    QCOMPARE(shown.editor().caretPosition(), 1);
    QTest::keyClicks(shown.canvas, QStringLiteral("a"));
    QCOMPARE(shown.session.textDraft().value().style.content, QString("aaa"));
    QCOMPARE(runs(shown).value(), (std::vector{greenRun(0, 2)}));
}

void TextColorEditorTests::theCaretTakesTheLetterBeforeIt()
{
    DrawnText shown;
    auto *field = new QLineEdit(&shown.window);
    field->setGeometry(0, 0, 10, 10);
    field->show();
    shown.canvas->setFocus();
    QTRY_VERIFY(shown.canvas->hasFocus());
    beginTextAt(shown.session, QPointF(20, 30));
    QTest::keyClicks(shown.canvas, QStringLiteral("ab"));
    QTest::keyClick(shown.canvas, Qt::Key_Left, Qt::ShiftModifier);
    shown.session.setPaletteColor(green, false);
    // After the green b: green; after the red a: red.
    QTest::keyClick(shown.canvas, Qt::Key_End);
    QVERIFY(shown.editor().caretShown());
    const QPoint afterB = caretAt(shown, QStringLiteral("ab"));
    QVERIFY(inked(shown.grab(), afterB, greenish));
    QTest::keyClick(shown.canvas, Qt::Key_Left);
    const QPoint afterA = caretAt(shown, QStringLiteral("a"));
    QTRY_VERIFY(shown.editor().caretShown());
    QVERIFY(inked(shown.grab(), afterA, reddish));
    QVERIFY(!inked(shown.grab(), afterA, greenish));
}

void TextColorEditorTests::theSwatchKeepsTheSelectionItPaints()
{
    // Swift's 4503998: the Type bar's swatch leaves the letters selected.
    DrawnText shown;
    TypeControls bar(shown.session, &shown.window);
    bar.setGeometry(0, 0, 400, 30);
    bar.show();
    shown.canvas->setFocus();
    QTRY_VERIFY(shown.canvas->hasFocus());
    beginTextAt(shown.session, QPointF(20, 60));
    QTest::keyClicks(shown.canvas, QStringLiteral("AAAA BBBB"));
    for (int step = 0; step < 4; ++step)
        QTest::keyClick(shown.canvas, Qt::Key_Left, Qt::ShiftModifier);
    auto &swatch = *bar.findChild<SwatchButton *>(QStringLiteral("typeColor"));
    QTest::mouseClick(&swatch, Qt::LeftButton);
    QVERIFY(shown.session.colorPicker().has_value());
    QCOMPARE(shown.session.textDraft().value().selection, (TextSpan{5, 4}));
    QVERIFY(shown.editor().anchor() == 9 && shown.editor().caretPosition() == 5);
    PickerHSB hsb = shown.session.colorPicker().value().hsb;
    hsb.setRGB(green);
    shown.session.setColorPickerHSB(hsb);
    shown.session.closeColorPicker(true);
    QCOMPARE(runs(shown).value(), (std::vector{greenRun(5, 4)}));
    QCOMPARE(shown.session.textDraft().value().style.red, 1.0);
}

void TextColorEditorTests::theLayerDrawsEachRunInItsColour()
{
    DrawnText shown;
    shown.canvas->setFocus();
    beginTextAt(shown.session, QPointF(20, 30));
    QTest::keyClicks(shown.canvas, QStringLiteral("MMMM MMMM"));
    for (int step = 0; step < 4; ++step)
        QTest::keyClick(shown.canvas, Qt::Key_Left, Qt::ShiftModifier);
    shown.session.setPaletteColor(green, false);
    QVERIFY(shown.session.finishText());
    const QImage image = shown.session.activeLayer().value().asset.value().image();
    // Red on the left, green on the right, no fringe.
    int reds = 0, greens = 0, dark = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor colour = image.pixelColor(x, y);
            if (colour.alphaF() < 0.05f)
                continue;
            const double alpha = colour.alphaF();
            reds += colour.redF() > 0.8 * alpha && x < image.width() / 2;
            greens += colour.greenF() > 0.8 * alpha && x > image.width() / 2;
            // A glyph drawn twice leaves red under green edges.
            dark += colour.redF() > 0.05 && colour.greenF() > 0.05;
        }
    }
    QVERIFY2(reds > 100 && greens > 100 && dark == 0, qPrintable(QStringLiteral("%1 %2 %3").arg(reds).arg(greens).arg(dark)));
}

void TextColorEditorTests::theEditorDrawsNoGlyphsOfItsOwn()
{
    // The runs belong to the layer: overlay glyphs stay clear.
    DrawnText shown;
    beginTextAt(shown.session, QPointF(20, 30));
    shown.session.changeTextStyle([](LayerTextStyle &style) {
        style.content = QStringLiteral("MMMM");
        style.setColor(PaletteColor{0, 1, 0}, {0, 4});
        style.setColor(PaletteColor{1, 0, 0}, {0, 2});
    });
    QVERIFY(runs(shown).has_value());
    const auto overlay = [&shown] {
        QImage drawn(shown.canvas->size(), QImage::Format_ARGB32_Premultiplied);
        drawn.fill(Qt::transparent);
        QPainter painter(&drawn);
        shown.editor().draw(painter);
        painter.end();
        return drawn;
    };
    // Only the frame, handles and translucent highlight may show.
    const auto glyphs = [](const QImage &drawn) {
        int count = 0;
        for (int y = 0; y < drawn.height(); ++y)
            for (int x = 0; x < drawn.width(); ++x)
                count += drawn.pixelColor(x, y).alpha() > 128 && (greenish(drawn.pixelColor(x, y)) || reddish(drawn.pixelColor(x, y)));
        return count;
    };
    shown.canvas->clearFocus();
    QTRY_VERIFY(!shown.editor().caretShown());
    QCOMPARE(glyphs(overlay()), 0);
    // Selected, with and without the keys, as under the picker.
    shown.canvas->setFocus();
    QTRY_VERIFY(shown.canvas->hasFocus());
    QTest::keyClick(shown.canvas, Qt::Key_A, Qt::ControlModifier);
    for (const bool focused : {true, false}) {
        if (!focused) {
            shown.canvas->clearFocus();
            QTRY_VERIFY(!shown.canvas->hasFocus());
        }
        const QImage drawn = overlay();
        QCOMPARE(glyphs(drawn), 0);
        // The highlight at 45% over the letters' place.
        const QPoint middle = shown.editor().textTransform().map(QPointF(LayerTextStyle::padding + 10, LayerTextStyle::padding + 20)).toPoint();
        QVERIFY2(std::abs(drawn.pixelColor(middle).alpha() - 115) <= 2, qPrintable(QString::number(drawn.pixelColor(middle).alpha())));
    }
}

void TextColorEditorTests::twinsKeepTheirColoursOnEveryPath_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<QString>("content");
    QTest::addColumn<qint64>("greens");
    for (const char *path : {"paste", "commit", "preedit"})
        QTest::newRow(path) << path << "aaa" << qint64(2);
    QTest::newRow("cut") << "cut" << "a" << qint64(0);
    QTest::newRow("undo") << "undo" << "aa" << qint64(1);
    QTest::newRow("redo") << "redo" << "aaa" << qint64(2);
    QTest::newRow("replaced by itself") << "same" << "aa" << qint64(2);
    // Around a preedit: it sits green between the twins.
    QTest::newRow("paste while composing") << "pasteComposing" << "aqxa" << qint64(3);
    QTest::newRow("commit over a preedit") << "commitOverPreedit" << "aya" << qint64(2);
    QTest::newRow("commit over a selection") << "commitOverSelection" << "zza" << qint64(2);
    QTest::newRow("undo of two typed") << "undoTwo" << "aa" << qint64(1);
    QTest::newRow("a preedit typed over, undone") << "typedOverPreedit" << "aa" << qint64(1);
    QTest::newRow("undo while composing") << "undoComposing" << "aa" << qint64(1);
}

void TextColorEditorTests::twinsKeepTheirColoursOnEveryPath()
{
    // A green a, then a red one, the caret between.
    QFETCH(QString, path);
    QFETCH(QString, content);
    QFETCH(qint64, greens);
    DrawnText shown;
    shown.canvas->setFocus();
    beginTextAt(shown.session, QPointF(20, 30));
    QTest::keyClicks(shown.canvas, QStringLiteral("aa"));
    QTest::keyClick(shown.canvas, Qt::Key_Home);
    QTest::keyClick(shown.canvas, Qt::Key_Right, Qt::ShiftModifier);
    shown.session.setPaletteColor(green, false);
    // A plain arrow collapses the selection to its own side.
    QTest::keyClick(shown.canvas, Qt::Key_Right);
    QCOMPARE(shown.editor().caretPosition(), 1);
    const auto input = [&](const QString &commit, const QString &preedit) {
        QInputMethodEvent event(preedit, {});
        event.setCommitString(commit);
        QApplication::sendEvent(shown.canvas, &event);
    };
    if (path == "paste") {
        QGuiApplication::clipboard()->setText(QStringLiteral("a"));
        shown.editor().paste();
    } else if (path == "commit") {
        input(QStringLiteral("a"), QString());
    } else if (path == "preedit") {
        input(QString(), QStringLiteral("a"));
    } else if (path == "cut") {
        QTest::keyClick(shown.canvas, Qt::Key_Left, Qt::ShiftModifier);
        shown.editor().cut();
    } else if (path == "undo" || path == "redo") {
        QTest::keyClicks(shown.canvas, QStringLiteral("a"));
        QTest::keyClick(shown.canvas, Qt::Key_Z, Qt::ControlModifier);
        if (path == "redo")
            QTest::keyClick(shown.canvas, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    } else if (path == "pasteComposing") {
        input(QString(), QStringLiteral("x"));
        QGuiApplication::clipboard()->setText(QStringLiteral("q"));
        shown.editor().paste();
    } else if (path == "commitOverPreedit") {
        input(QString(), QStringLiteral("x"));
        input(QStringLiteral("y"), QString());
    } else if (path == "commitOverSelection") {
        QTest::keyClick(shown.canvas, Qt::Key_Home);
        QTest::keyClick(shown.canvas, Qt::Key_Right, Qt::ShiftModifier);
        input(QStringLiteral("zz"), QString());
    } else if (path == "undoTwo") {
        QTest::keyClicks(shown.canvas, QStringLiteral("bc"));
        QTest::keyClick(shown.canvas, Qt::Key_Z, Qt::ControlModifier);
    } else if (path == "typedOverPreedit") {
        input(QString(), QStringLiteral("x"));
        QTest::keyClicks(shown.canvas, QStringLiteral("b"));
        QTest::keyClick(shown.canvas, Qt::Key_Z, Qt::ControlModifier);
    } else if (path == "undoComposing") {
        QTest::keyClicks(shown.canvas, QStringLiteral("b"));
        input(QString(), QStringLiteral("x"));
        QTest::keyClick(shown.canvas, Qt::Key_Z, Qt::ControlModifier);
    } else {
        // The red letter replaced by itself takes the green before.
        QTest::keyClick(shown.canvas, Qt::Key_End);
        QTest::keyClick(shown.canvas, Qt::Key_Left, Qt::ShiftModifier);
        input(QStringLiteral("a"), QString());
    }
    QCOMPARE(shown.session.textDraft().value().style.content, content);
    if (greens == 0)
        QVERIFY(!runs(shown));
    else
        QCOMPARE(runs(shown).value(), (std::vector{greenRun(0, greens)}));
}

void TextColorEditorTests::aColouredTailOutlastsEveryStep_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<QString>("content");
    QTest::newRow("typing undone and redone") << "redo" << "axybcd";
    QTest::newRow("deletions undone") << "undoDeletes" << "abcd";
    QTest::newRow("commit with a preedit") << "commitAndPreedit" << "axyzbcd";
    QTest::newRow("paste around a preedit") << "pasteComposing" << "aqqxbcd";
}

void TextColorEditorTests::aColouredTailOutlastsEveryStep()
{
    // setMarkedText replaces a selection whole: its first colour.
    QFETCH(QString, path);
    QFETCH(QString, content);
    DrawnText shown;
    shown.canvas->setFocus();
    beginTextAt(shown.session, QPointF(20, 30));
    QTest::keyClicks(shown.canvas, QStringLiteral("abcd"));
    QTest::keyClick(shown.canvas, Qt::Key_Left, Qt::ShiftModifier);
    shown.session.setPaletteColor(green, false);
    QTest::keyClick(shown.canvas, Qt::Key_Home);
    QTest::keyClick(shown.canvas, Qt::Key_Right);
    const auto input = [&](const QString &commit, const QString &preedit) {
        QInputMethodEvent event(preedit, {});
        event.setCommitString(commit);
        QApplication::sendEvent(shown.canvas, &event);
    };
    if (path == "redo") {
        QTest::keyClicks(shown.canvas, QStringLiteral("xy"));
        QTest::keyClick(shown.canvas, Qt::Key_Z, Qt::ControlModifier);
        QTest::keyClick(shown.canvas, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    } else if (path == "undoDeletes") {
        QTest::keyClick(shown.canvas, Qt::Key_Delete);
        QTest::keyClick(shown.canvas, Qt::Key_Delete);
        QTest::keyClick(shown.canvas, Qt::Key_Z, Qt::ControlModifier);
    } else if (path == "commitAndPreedit") {
        input(QStringLiteral("xy"), QStringLiteral("z"));
    } else {
        input(QString(), QStringLiteral("x"));
        QGuiApplication::clipboard()->setText(QStringLiteral("qq"));
        shown.editor().paste();
    }
    QCOMPARE(shown.session.textDraft().value().style.content, content);
    QCOMPARE(runs(shown).value(), (std::vector{greenRun(content.size() - 1, 1)}));
}

void TextColorEditorTests::aPreeditOverTheFirstLetterTakesItsColour()
{
    // setMarkedText replaces the selection whole: its first letter's colour.
    DrawnText shown;
    shown.canvas->setFocus();
    beginTextAt(shown.session, QPointF(20, 30));
    QTest::keyClicks(shown.canvas, QStringLiteral("abc"));
    QTest::keyClick(shown.canvas, Qt::Key_Home);
    QTest::keyClick(shown.canvas, Qt::Key_Right, Qt::ShiftModifier);
    shown.session.setPaletteColor(green, false);
    QTest::keyClick(shown.canvas, Qt::Key_End);
    QTest::keyClick(shown.canvas, Qt::Key_Left, Qt::ShiftModifier);
    shown.session.setPaletteColor(green, false);
    QTest::keyClick(shown.canvas, Qt::Key_Home);
    QTest::keyClick(shown.canvas, Qt::Key_Right, Qt::ShiftModifier);
    QInputMethodEvent event(QStringLiteral("xy"), {});
    QApplication::sendEvent(shown.canvas, &event);
    QCOMPARE(shown.session.textDraft().value().style.content, QString("xybc"));
    QCOMPARE(runs(shown).value(), (std::vector{greenRun(0, 2), greenRun(3, 1)}));
    // Updated, its cursor moved, then committed: the colour carries on.
    QInputMethodEvent updated(QStringLiteral("xyz"), {});
    QApplication::sendEvent(shown.canvas, &updated);
    QCOMPARE(runs(shown).value(), (std::vector{greenRun(0, 3), greenRun(4, 1)}));
    QInputMethodEvent moved(QStringLiteral("xyz"), {QInputMethodEvent::Attribute(QInputMethodEvent::Cursor, 0, 1)});
    QApplication::sendEvent(shown.canvas, &moved);
    QCOMPARE(runs(shown).value(), (std::vector{greenRun(0, 3), greenRun(4, 1)}));
    QInputMethodEvent committed;
    committed.setCommitString(QStringLiteral("q"));
    QApplication::sendEvent(shown.canvas, &committed);
    QCOMPARE(shown.session.textDraft().value().style.content, QString("qbc"));
    QCOMPARE(runs(shown).value(), (std::vector{greenRun(0, 1), greenRun(2, 1)}));
}

void TextColorEditorTests::atTheStartTheFirstLetterGivesTheColour()
{
    // No letter before the caret: the first one's colour.
    DrawnText shown;
    auto *field = new QLineEdit(&shown.window);
    field->setGeometry(0, 0, 10, 10);
    field->show();
    shown.canvas->setFocus();
    QTRY_VERIFY(shown.canvas->hasFocus());
    beginTextAt(shown.session, QPointF(20, 30));
    QTest::keyClicks(shown.canvas, QStringLiteral("ab"));
    QTest::keyClick(shown.canvas, Qt::Key_Home);
    QTest::keyClick(shown.canvas, Qt::Key_Right, Qt::ShiftModifier);
    shown.session.setPaletteColor(green, false);
    QTest::keyClick(shown.canvas, Qt::Key_Home);
    QCOMPARE(shown.editor().caretPosition(), 0);
    QVERIFY(shown.session.typeColor() == green);
    QTRY_VERIFY(shown.editor().caretShown());
    QVERIFY(inked(shown.grab(), caretAt(shown, QString()), greenish));
}

QTEST_MAIN(TextColorEditorTests)
#include "TextColorEditorTests.moc"
