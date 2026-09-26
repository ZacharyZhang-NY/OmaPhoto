#include "InlineTextFixtures.h"
#include <QLineEdit>
#include <array>

// The Type tool's pointer: new text, clicks, drags, selections.
namespace {
// Where a prefix of a line ends, plus a nudge.
QPointF after(TextCanvas &shown, const QString &prefix, double nudge = 1, int line = 0)
{
    const LayerTextStyle style = shown.session.textDraft().value().style;
    const double padding = LayerTextStyle::padding;
    const double x = QFontMetricsF(TextLayout::font(style)).horizontalAdvance(prefix);
    return shown.editor().textTransform().map(QPointF(padding + x + nudge, padding + style.lineHeight() * (line + 0.5)));
}

// The bounds of the pixels two grabs disagree on.
QRect changed(const QImage &from, const QImage &to)
{
    QRect found;
    for (int y = 0; y < to.height(); ++y) {
        for (int x = 0; x < to.width(); ++x) {
            if (to.pixel(x, y) != from.pixel(x, y))
                found |= QRect(x, y, 1, 1);
        }
    }
    return found;
}
}

class InlineTextPointerTests : public QObject {
    Q_OBJECT
private slots:
    void aClickOrADragBeginsText();
    void aClickOpensTheTextUnderIt();
    void clicksPlaceTheCaretAndDragsSelect();
    void doubleAndTripleClicksTakeWordsAndParagraphs();
    void aClickLandsAPreedit();
    void anUndoneCanvasLeavesTheBoxInert();
    void aLostReleaseDropsTheBox();
    void shiftExtendsByTheLastClicksUnit();
    void blankSpaceAndEmptyLinesTakeClicks();
};

void InlineTextPointerTests::aClickOrADragBeginsText()
{
    TextCanvas shown;
    shown.session.cancelText();
    // A click is Swift's small box: point text there.
    shown.click(QPointF(50, 60));
    QCOMPARE(shown.session.textDraft().value().origin, QPointF(50, 60));
    QVERIFY(!shown.session.textDraft().value().style.boxSize);
    shown.session.cancelText();
    // A drag draws a box, shown in the accent meanwhile.
    const QImage before = shown.canvas->grab().toImage();
    shown.press(QPointF(40, 50));
    shown.move(QPointF(140, 110));
    const QImage drawing = shown.canvas->grab().toImage();
    const QRect drawn = changed(before, drawing);
    QVERIFY2(std::abs(drawn.left() - 40) <= 1 && std::abs(drawn.right() - 140) <= 1 && std::abs(drawn.top() - 50) <= 1 && std::abs(drawn.bottom() - 110) <= 1,
             qPrintable(QString("%1 %2 %3 %4").arg(drawn.left()).arg(drawn.top()).arg(drawn.right()).arg(drawn.bottom())));
    QCOMPARE(drawing.pixel(90, 80), before.pixel(90, 80));
    // One point wide on whole points: two rows, half each.
    const QColor accent = shown.canvas->palette().color(QPalette::Highlight);
    const auto half = [](int line, int ground, int shown) { return std::abs(shown - (line + ground) / 2) <= 2; };
    for (const int row : {49, 50}) {
        const QColor ground = before.pixelColor(90, row), edge = drawing.pixelColor(90, row);
        QVERIFY2(half(accent.red(), ground.red(), edge.red()) && half(accent.green(), ground.green(), edge.green())
                     && half(accent.blue(), ground.blue(), edge.blue()),
                 qPrintable(edge.name()));
    }
    shown.release(QPointF(140, 110));
    QCOMPARE(shown.session.textDraft().value().style.boxSize, std::optional(QSizeF(100, 60)));
    QCOMPARE(shown.session.textDraft().value().origin, QPointF(40, 50));
    // Under four pixels both ways: a click; else a box.
    const std::array<std::pair<QPointF, std::optional<QSizeF>>, 3> drags{
        {{QPointF(43, 53), std::nullopt}, {QPointF(44, 54), QSizeF(16, 16)}, {QPointF(43, 100), QSizeF(16, 50)}}};
    for (const auto &[to, box] : drags) {
        shown.session.cancelText();
        shown.drag(QPointF(40, 50), to);
        QCOMPARE(shown.session.textDraft().value().style.boxSize, box);
    }
    shown.session.cancelText();
    // Escape, or another tool, drops the box being drawn.
    shown.press(QPointF(40, 50));
    shown.move(QPointF(140, 110));
    shown.key(Qt::Key_Escape);
    shown.release(QPointF(140, 110));
    QVERIFY(!shown.session.textDraft());
    QCOMPARE(shown.canvas->grab().toImage().pixel(90, 50), before.pixel(90, 50));
    shown.press(QPointF(40, 50));
    shown.move(QPointF(140, 110));
    shown.session.selectTool(NavigationTool::marquee);
    shown.session.selectTool(NavigationTool::type);
    shown.release(QPointF(140, 110));
    QVERIFY(!shown.session.textDraft());
    // A box past the limits is refused; its outline goes.
    shown.press(QPointF(40, 50));
    shown.move(QPointF(40'040, 110));
    QCoreApplication::processEvents();
    PaintSpy spy(*shown.canvas);
    shown.release(QPointF(40'040, 110));
    QVERIFY(!shown.session.textDraft());
    QTRY_VERIFY(spy.painted.contains(QPoint(40, 80)));
    QCOMPARE(shown.canvas->grab().toImage().pixel(40, 80), before.pixel(40, 80));
}

void InlineTextPointerTests::aClickOpensTheTextUnderIt()
{
    TextCanvas shown;
    // A click beside open text keeps it, then begins anew.
    shown.type(QStringLiteral("Kept"));
    const size_t count = shown.session.document().value().layers.size();
    shown.click(QPointF(300, 250));
    QCOMPARE(shown.session.document().value().layers.size(), count + 1);
    QCOMPARE(shown.session.textDraft().value().origin, QPointF(300, 250));
    shown.session.cancelText();
    // A click opens text, active or not, the caret there.
    const QUuid kept = shown.session.document().value().layers.back().id;
    shown.session.selectLayer(shown.session.document().value().layers.front().id);
    const QPointF on(20 + LayerTextStyle::padding + 1, 80);
    shown.click(on);
    QCOMPARE(shown.session.textDraft().value().layerID, std::optional(kept));
    QCOMPARE(shown.caret(), 0);
    shown.session.cancelText();
    // Hidden text stays shut: the click begins new text.
    shown.session.toggleLayerVisibility(kept);
    shown.click(on);
    QVERIFY(!shown.session.textDraft().value().layerID);
    shown.session.cancelText();
    shown.session.toggleLayerVisibility(kept);
    // A new box widened leftwards lands where it shows.
    shown.drag(QPointF(40, 150), QPointF(240, 250));
    shown.type(QStringLiteral("Box"));
    const QPointF left = shown.editor().boxTransform().map(QPointF(0, 50));
    shown.drag(left, left - QPointF(20, 0));
    QVERIFY(shown.session.finishText());
    QCOMPARE(shown.session.activeLayer().value().transform.origin, QPointF(20, 150));
}

void InlineTextPointerTests::clicksPlaceTheCaretAndDragsSelect()
{
    TextCanvas shown;
    shown.type(QStringLiteral("alpha beta"));
    shown.click(after(shown, QStringLiteral("alp")));
    QCOMPARE(shown.selection(), (TextRange{3, 3}));
    // A drag selects; Shift-click takes the selection on.
    shown.drag(after(shown, QStringLiteral("al")), after(shown, QStringLiteral("alpha b")));
    QCOMPARE(shown.selection(), (TextRange{2, 7}));
    shown.click(after(shown, QStringLiteral("alpha be")), Qt::ShiftModifier);
    QCOMPARE(shown.selection(), (TextRange{2, 8}));
    // Shift's drag keeps that anchor, past it both ways.
    shown.press(after(shown, QStringLiteral("alpha ")), Qt::ShiftModifier);
    QCOMPARE(shown.selection(), (TextRange{2, 6}));
    shown.move(after(shown, QStringLiteral("a")), Qt::ShiftModifier);
    QCOMPARE(shown.editor().anchor(), 2);
    QCOMPARE(shown.caret(), 1);
    shown.release(after(shown, QStringLiteral("a")), Qt::ShiftModifier);
    // Without the button held a move drags nothing.
    shown.press(after(shown, QStringLiteral("alp")));
    shown.hover(after(shown, QStringLiteral("alpha b")));
    QCOMPARE(shown.selection(), (TextRange{3, 3}));
    shown.release(after(shown, QStringLiteral("alp")));
    // A click on a later line finds that line.
    shown.key(Qt::Key_End);
    shown.key(Qt::Key_Return);
    shown.type(QStringLiteral("gamma"));
    shown.click(after(shown, QStringLiteral("ga"), 1, 1));
    QCOMPARE(shown.caret(), 13);
    // Clicks go through a flipped layer's mirror, as it shows.
    QVERIFY(shown.session.finishText());
    const QUuid id = shown.session.activeLayerID().value();
    rewrite(shown.session, [&](ProjectSnapshot &snapshot) { record(snapshot, id).transform.flipX = true; });
    shown.session.editActiveText();
    shown.click(after(shown, QStringLiteral("al")));
    QCOMPARE(shown.caret(), 2);
}

void InlineTextPointerTests::doubleAndTripleClicksTakeWordsAndParagraphs()
{
    TextCanvas shown;
    shown.type(QStringLiteral("alpha beta"));
    // Qt sends a double click's second press by itself.
    const auto doubleClick = [&](QPointF at) {
        shown.click(at);
        QTest::mouseDClick(shown.canvas, Qt::LeftButton, Qt::NoModifier, at.toPoint());
    };
    // The word under the pointer, from any of its letters.
    const QPointF word = after(shown, QStringLiteral("alpha be"));
    doubleClick(word);
    QCOMPARE(shown.selection(), (TextRange{6, 10}));
    shown.release(word);
    doubleClick(after(shown, QStringLiteral("alpha "), 2));
    QCOMPARE(shown.selection(), (TextRange{6, 10}));
    shown.release(word);
    doubleClick(after(shown, QStringLiteral("alpha"), -2));
    QCOMPARE(shown.selection(), (TextRange{0, 5}));
    shown.release(word);
    // Its drag goes by words, forwards and back past it.
    doubleClick(word);
    shown.move(after(shown, QStringLiteral("alpha bet")));
    QCOMPARE(shown.editor().anchor(), 6);
    QCOMPARE(shown.caret(), 10);
    shown.move(after(shown, QStringLiteral("a")));
    QCOMPARE(shown.editor().anchor(), 10);
    QCOMPARE(shown.caret(), 0);
    shown.move(after(shown, QStringLiteral("alpha bet")));
    QCOMPARE(shown.editor().anchor(), 6);
    QCOMPARE(shown.caret(), 10);
    shown.release(word);
    // A third click soon after takes the paragraph and break.
    TextDraft draft = shown.session.textDraft().value();
    draft.style.content = QStringLiteral("alpha beta\r\ngamma");
    shown.session.setTextDraft(draft);
    doubleClick(word);
    shown.release(word);
    shown.click(word);
    QCOMPARE(shown.selection(), (TextRange{0, 12}));
    // Fourth clicks and far third clicks are single again.
    shown.click(word);
    QCOMPARE(shown.selection(), (TextRange{8, 8}));
    doubleClick(word);
    shown.release(word);
    shown.click(after(shown, QStringLiteral("al")));
    QCOMPARE(shown.selection(), (TextRange{2, 2}));
}

void InlineTextPointerTests::aClickLandsAPreedit()
{
    TextCanvas shown;
    shown.type(QStringLiteral("ab"));
    shown.compose(QStringLiteral("ni"));
    // Qt asks the input method to commit; else it lands.
    shown.click(after(shown, QStringLiteral("a")));
    QVERIFY(!shown.editor().marked());
    QCOMPARE(shown.content(), QString("abni"));
    QCOMPARE(shown.caret(), 1);
    shown.key(Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(shown.content(), QString());
    // A handle's press lands it too.
    shown.type(QStringLiteral("ab"));
    shown.compose(QStringLiteral("ni"));
    const QSizeF size = shown.editor().logicalSize();
    const QPointF right = shown.editor().boxTransform().map(QPointF(size.width(), size.height() / 2));
    shown.press(right);
    QVERIFY(!shown.editor().marked());
    QCOMPARE(shown.content(), QString("abni"));
    shown.release(right);
}

void InlineTextPointerTests::anUndoneCanvasLeavesTheBoxInert()
{
    TextCanvas shown;
    shown.session.cancelText();
    // Undo takes the canvas mid-drag: the box makes nothing.
    shown.press(QPointF(40, 50));
    shown.move(QPointF(140, 110));
    shown.session.undo();
    shown.session.undo();
    QVERIFY(!shown.session.document());
    shown.move(QPointF(160, 130));
    const QImage blank = shown.canvas->grab().toImage();
    QCOMPARE(blank.pixel(40, 80), blank.pixel(300, 250));
    shown.release(QPointF(160, 130));
    QVERIFY(!shown.session.textDraft());
}

void InlineTextPointerTests::aLostReleaseDropsTheBox()
{
    TextCanvas shown;
    shown.session.cancelText();
    const QImage before = shown.canvas->grab().toImage();
    // A move without the button: its release was lost.
    shown.press(QPointF(40, 50));
    shown.move(QPointF(140, 110));
    QCoreApplication::processEvents();
    PaintSpy hover(*shown.canvas);
    shown.hover(QPointF(200, 150));
    QTRY_VERIFY(hover.painted.contains(QPoint(40, 80)));
    QCOMPARE(shown.canvas->grab().toImage().pixel(40, 80), before.pixel(40, 80));
    shown.release(QPointF(200, 150));
    QVERIFY(!shown.session.textDraft());
    // A press after a lost release starts afresh.
    shown.press(QPointF(40, 50));
    shown.move(QPointF(140, 110));
    QCoreApplication::processEvents();
    PaintSpy press(*shown.canvas);
    shown.press(QPointF(300, 250));
    QTRY_VERIFY(press.painted.contains(QPoint(40, 80)));
    shown.release(QPointF(300, 250));
    QCOMPARE(shown.session.textDraft().value().origin, QPointF(300, 250));
    QVERIFY(!shown.session.textDraft().value().style.boxSize);
    shown.session.cancelText();
    // So does a lost focus.
    shown.press(QPointF(40, 50));
    shown.move(QPointF(140, 110));
    QLineEdit field(&shown.window);
    field.show();
    field.setFocus();
    QTRY_VERIFY(field.hasFocus());
    shown.release(QPointF(140, 110));
    QVERIFY(!shown.session.textDraft());
    QCOMPARE(shown.canvas->grab().toImage().pixel(40, 80), before.pixel(40, 80));
}

void InlineTextPointerTests::shiftExtendsByTheLastClicksUnit()
{
    TextCanvas shown;
    shown.type(QStringLiteral("alpha beta gamma"));
    const auto doubleClick = [&](QPointF at) {
        shown.click(at);
        QTest::mouseDClick(shown.canvas, Qt::LeftButton, Qt::NoModifier, at.toPoint());
        shown.release(at);
    };
    // Before any click, Shift extends from the caret.
    shown.click(after(shown, QStringLiteral("alpha beta ga")), Qt::ShiftModifier);
    QCOMPARE(shown.editor().anchor(), 16);
    QCOMPARE(shown.caret(), 13);
    // After a double click, by whole words either way.
    doubleClick(after(shown, QStringLiteral("al")));
    QCOMPARE(shown.selection(), (TextRange{0, 5}));
    shown.click(after(shown, QStringLiteral("alpha be")), Qt::ShiftModifier);
    QCOMPARE(shown.selection(), (TextRange{0, 10}));
    shown.click(after(shown, QStringLiteral("alpha beta ga")), Qt::ShiftModifier);
    QCOMPARE(shown.selection(), (TextRange{0, 16}));
    doubleClick(after(shown, QStringLiteral("alpha be")));
    shown.click(after(shown, QStringLiteral("al")), Qt::ShiftModifier);
    QCOMPARE(shown.editor().anchor(), 10);
    QCOMPARE(shown.caret(), 0);
    shown.press(after(shown, QStringLiteral("alpha beta ga")), Qt::ShiftModifier);
    QCOMPARE(shown.selection(), (TextRange{6, 16}));
    shown.move(after(shown, QStringLiteral("al")), Qt::ShiftModifier);
    QCOMPARE(shown.editor().anchor(), 10);
    QCOMPARE(shown.caret(), 0);
    shown.release(after(shown, QStringLiteral("al")), Qt::ShiftModifier);
    // A key's selection brings characters back.
    shown.key(Qt::Key_Right, Qt::ShiftModifier);
    shown.click(after(shown, QStringLiteral("alpha beta ga")), Qt::ShiftModifier);
    QCOMPARE(shown.selection(), (TextRange{10, 13}));
    doubleClick(after(shown, QStringLiteral("alpha be")));
    shown.click(after(shown, QStringLiteral("al")), Qt::ShiftModifier);
    shown.key(Qt::Key_Left);
    shown.click(after(shown, QStringLiteral("alpha beta ga")), Qt::ShiftModifier);
    QCOMPARE(shown.selection(), (TextRange{0, 13}));
    // So does other text, or a style from anywhere.
    doubleClick(after(shown, QStringLiteral("al")));
    TextDraft draft = shown.session.textDraft().value();
    draft.style.content = QStringLiteral("omega beta gamma");
    shown.session.setTextDraft(draft);
    shown.click(after(shown, QStringLiteral("omega be")), Qt::ShiftModifier);
    QCOMPARE(shown.selection(), (TextRange{0, 8}));
    doubleClick(after(shown, QStringLiteral("om")));
    shown.key(Qt::Key_Right, Qt::AltModifier);
    shown.key(Qt::Key_Left, Qt::AltModifier);
    shown.click(after(shown, QStringLiteral("omega be")), Qt::ShiftModifier);
    QCOMPARE(shown.selection(), (TextRange{0, 8}));
    doubleClick(after(shown, QStringLiteral("om")));
    shown.session.changeTextStyle([](LayerTextStyle &style) { style.fontSize = 60; });
    shown.click(after(shown, QStringLiteral("omega be")), Qt::ShiftModifier);
    QCOMPARE(shown.selection(), (TextRange{0, 8}));
    // A style set mid-drag ends the drag; so does typing.
    shown.press(after(shown, QStringLiteral("omega b")));
    shown.session.changeTextStyle([](LayerTextStyle &style) { style.fontSize = 72; });
    shown.move(after(shown, QStringLiteral("om")));
    QCOMPARE(shown.selection(), (TextRange{7, 7}));
    shown.release(after(shown, QStringLiteral("om")));
    shown.press(after(shown, QStringLiteral("om")));
    shown.type(QStringLiteral("X"));
    shown.move(after(shown, QStringLiteral("omega b")));
    QCOMPARE(shown.selection(), (TextRange{3, 3}));
    shown.release(after(shown, QStringLiteral("omega b")));
    // Even the same letter typed over the letter selected.
    shown.press(after(shown, QStringLiteral("om")));
    shown.move(after(shown, QStringLiteral("omX")));
    QCOMPARE(shown.selection(), (TextRange{2, 3}));
    shown.type(QStringLiteral("X"));
    shown.move(after(shown, QStringLiteral("omXeg")));
    QCOMPARE(shown.selection(), (TextRange{3, 3}));
    shown.release(after(shown, QStringLiteral("omXeg")));
}

void InlineTextPointerTests::blankSpaceAndEmptyLinesTakeClicks()
{
    TextCanvas shown;
    shown.session.cancelText();
    shown.drag(QPointF(20, 20), QPointF(380, 280));
    TextDraft draft = shown.session.textDraft().value();
    draft.style.fontSize = 24;
    draft.style.content = QStringLiteral("abc\ndef");
    shown.session.setTextDraft(draft);
    // Below the lines is the end; above them, the start.
    shown.click(after(shown, QString(), 1, 2));
    QCOMPARE(shown.caret(), 7);
    shown.press(after(shown, QStringLiteral("d"), 1, 1));
    shown.move(after(shown, QStringLiteral("d"), 1, -1));
    QCOMPARE(shown.editor().anchor(), 5);
    QCOMPARE(shown.caret(), 0);
    shown.release(after(shown, QStringLiteral("d"), 1, -1));
    // An empty last line has no word or paragraph.
    draft = shown.session.textDraft().value();
    draft.style.content = QStringLiteral("abc\n\ndef\n");
    shown.session.setTextDraft(draft);
    const auto clicks = [&](QPointF at, int count) {
        shown.click(at);
        QTest::mouseDClick(shown.canvas, Qt::LeftButton, Qt::NoModifier, at.toPoint());
        shown.release(at);
        if (count == 3)
            shown.click(at);
    };
    clicks(after(shown, QString(), 1, 3), 3);
    QCOMPARE(shown.selection(), (TextRange{9, 9}));
    clicks(after(shown, QString(), 1, 3), 2);
    QCOMPARE(shown.selection(), (TextRange{9, 9}));
    clicks(after(shown, QStringLiteral("d"), 1, 2), 2);
    QCOMPARE(shown.selection(), (TextRange{5, 8}));
    // An empty line between others is its break.
    clicks(after(shown, QString(), 1, 1), 3);
    QCOMPARE(shown.selection(), (TextRange{4, 5}));
    // Past the text, a double click takes its last word.
    draft = shown.session.textDraft().value();
    draft.style.content = QStringLiteral("abc def");
    shown.session.setTextDraft(draft);
    clicks(after(shown, QStringLiteral("abc def"), 20), 2);
    QCOMPARE(shown.selection(), (TextRange{4, 7}));
    // A final line separator leaves no word either.
    draft = shown.session.textDraft().value();
    draft.style.content = QStringLiteral("abc") + QChar(QChar::LineSeparator);
    shown.session.setTextDraft(draft);
    clicks(after(shown, QString(), 1, 1), 2);
    QCOMPARE(shown.selection(), (TextRange{4, 4}));
    // Below an overfull box's lines: the end of what shows.
    draft = shown.session.textDraft().value();
    draft.style.boxSize = QSizeF(150, 100);
    draft.style.content = QStringLiteral("aaa bbb ccc ddd eee fff");
    shown.session.setTextDraft(draft);
    shown.click(after(shown, QStringLiteral("ccc"), 1, 1));
    shown.key(Qt::Key_End);
    const int shows = shown.caret();
    QVERIFY(shows < 23);
    shown.click(after(shown, QString(), 1, 2));
    QCOMPARE(shown.caret(), shows);
}

QTEST_MAIN(InlineTextPointerTests)
#include "InlineTextPointerTests.moc"
