#include "InlineTextFixtures.h"

// Typing on the canvas: keys, moves, undo, deletions, spacing.
class InlineTextEditorTests : public QObject {
    Q_OBJECT
private slots:
    void aDraftOpensTheEditorAndItsTextGrows();
    void typingIsOneUndoStep();
    void anotherDraftStartsAfresh();
    void arrowsHomeAndEndMoveAndShiftSelects();
    void upAndDownKeepTheirColumn();
    void deletionsReachCharactersWordsAndLines();
    void aWrappedParagraphMovesByItsLines();
    void aCaretPastTheBoxHasNoLine();
    void aFinalNewlineHasALineForTheCaret();
    void arrowsFollowARightToLeftParagraph();
    void returnTabEscapeAndFinishing();
    void altWithTheArrowsSetsSpacing();
};

void InlineTextEditorTests::aDraftOpensTheEditorAndItsTextGrows()
{
    TextCanvas shown;
    InlineTextEditor &editor = shown.editor();
    QCOMPARE(editor.draftID(), shown.session.textDraft().value().id);
    QCOMPARE(editor.logicalSize(), EditorSession::textBoxSize(shown.session.textDraft().value().style));
    QCOMPARE(editor.shownTransform(), (LayerTransform{.origin = {20, 30}, .size = editor.logicalSize()}));
    const double empty = editor.logicalSize().width();
    shown.type(QStringLiteral("Grow"));
    QCOMPARE(shown.content(), QString("Grow"));
    QCOMPARE(editor.logicalSize(), EditorSession::textBoxSize(shown.session.textDraft().value().style));
    QVERIFY(editor.logicalSize().width() > empty);
    // Six view points a handle at any zoom.
    QCOMPARE(editor.handleSize(), 6.0);
    shown.session.zoom(2);
    QCOMPARE(editor.handleSize(), 3.0);
    shown.session.zoom(4);
    QCOMPARE(editor.handleSize(), 2.0);
    shown.session.zoom(0.005);
    QCOMPARE(editor.handleSize(), 600.0);
    shown.session.zoom(1);
    // Content set elsewhere pulls the caret back inside.
    TextDraft shorter = shown.session.textDraft().value();
    shorter.style.content = QStringLiteral("Gr");
    shown.session.setTextDraft(shorter);
    QCOMPARE(editor.caretPosition(), 2);
    QCOMPARE(editor.anchor(), 2);
    // Point text on a scaled layer grows at that scale.
    QVERIFY(shown.session.finishText());
    const QUuid id = shown.session.activeLayerID().value();
    const QSize pixels = layerWith(shown.session, id).asset.value().size();
    rewrite(shown.session, [&](ProjectSnapshot &snapshot) {
        record(snapshot, id).transform = LayerTransform{.origin = {50, 40}, .size = QSizeF(pixels) * 2, .rotation = 30};
    });
    shown.session.editActiveText();
    InlineTextEditor &scaled = shown.editor();
    const QPointF corner = scaled.shownTransform().point(QPointF(0, 0));
    shown.key(Qt::Key_End);
    shown.type(QStringLiteral(" more"));
    QCOMPARE(scaled.shownTransform().size, scaled.logicalSize() * 2);
    QVERIFY((scaled.shownTransform().point(QPointF(0, 0)) - corner).manhattanLength() < 1e-9);
    QCOMPARE(scaled.shownTransform().rotation, 30.0);
    // Handles stay six points across a layer twice the size.
    shown.session.zoom(1);
    QCOMPARE(scaled.handleSize(), 3.0);
    // A box keeps its size however much is typed.
    shown.session.cancelText();
    shown.session.beginText(QRectF(10, 10, 200, 100));
    shown.type(QStringLiteral("Boxed words that go on"));
    QCOMPARE(shown.editor().logicalSize(), QSizeF(200, 100));
    // A box shows the transform its draft holds, never grown.
    QVERIFY(shown.session.finishText());
    shown.session.editActiveText();
    TextDraft boxed = shown.session.textDraft().value();
    boxed.style.boxSize = QSizeF(300, 100);
    boxed.transform = LayerTransform{.origin = {10, 10}, .size = {300, 100}};
    shown.session.setTextDraft(boxed);
    QCOMPARE(shown.editor().shownTransform(), boxed.transform.value());
}

void InlineTextEditorTests::typingIsOneUndoStep()
{
    TextCanvas shown;
    shown.type(QStringLiteral("Hi you"));
    QCOMPARE(shown.editor().caretPosition(), 6);
    shown.key(Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(shown.content(), QString());
    shown.key(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(shown.content(), QString("Hi you"));
    QCOMPARE(shown.editor().caretPosition(), 6);
    // A move ends the step; deleting starts another.
    shown.key(Qt::Key_Left);
    shown.type(QStringLiteral("!"));
    shown.key(Qt::Key_Backspace);
    shown.key(Qt::Key_Backspace);
    QCOMPARE(shown.content(), QString("Hi yu"));
    shown.key(Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(shown.content(), QString("Hi yo!u"));
    shown.key(Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(shown.content(), QString("Hi you"));
    // Typing drops the redo; a move ends the step.
    shown.key(Qt::Key_End);
    shown.type(QStringLiteral("r"));
    shown.key(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(shown.content(), QString("Hi your"));
    shown.key(Qt::Key_Left);
    shown.type(QStringLiteral("s"));
    shown.key(Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(shown.content(), QString("Hi your"));
    // A fresh draft starts with nothing to undo or redo.
    shown.key(Qt::Key_Z, Qt::ControlModifier);
    shown.session.cancelText();
    shown.session.beginText(QPointF(100, 100), true);
    shown.key(Qt::Key_Z, Qt::ControlModifier);
    shown.key(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(shown.content(), QString());
    QVERIFY(shown.session.textDraft());
    shown.type(QStringLiteral("x"));
    shown.key(Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(shown.content(), QString());
}

void InlineTextEditorTests::anotherDraftStartsAfresh()
{
    TextCanvas shown;
    // A draft set over this drops its steps, as Swift's.
    shown.type(QStringLiteral("ab"));
    TextDraft other = shown.session.textDraft().value();
    other.id = QUuid::createUuid();
    other.style.content = QStringLiteral("new");
    shown.session.setTextDraft(other);
    QCOMPARE(shown.editor().draftID(), other.id);
    shown.type(QStringLiteral("s"));
    QCOMPARE(shown.content(), QString("nesw"));
    shown.key(Qt::Key_Z, Qt::ControlModifier);
    shown.key(Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(shown.content(), QString("new"));
    other.id = QUuid::createUuid();
    shown.session.setTextDraft(other);
    shown.key(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(shown.content(), QString("new"));
    // A composition in progress ends with its draft.
    shown.compose(QStringLiteral("ni"));
    other.id = QUuid::createUuid();
    other.style.content = QStringLiteral("NEW");
    shown.session.setTextDraft(other);
    QVERIFY(!shown.editor().marked());
    shown.commit(QStringLiteral("X"));
    QCOMPARE(shown.content(), QString("NEWX"));
    shown.key(Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(shown.content(), QString("NEW"));
}

void InlineTextEditorTests::arrowsHomeAndEndMoveAndShiftSelects()
{
    TextCanvas shown;
    shown.type(QStringLiteral("one two"));
    shown.key(Qt::Key_Return);
    shown.type(QStringLiteral("three"));
    shown.key(Qt::Key_Home);
    QCOMPARE(shown.editor().caretPosition(), 8);
    shown.key(Qt::Key_Up);
    QCOMPARE(shown.editor().caretPosition(), 0);
    shown.key(Qt::Key_Right, Qt::ControlModifier);
    QCOMPARE(shown.editor().caretPosition(), 3);
    shown.key(Qt::Key_Right, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(shown.selection(), (TextRange{3, 7}));
    // A plain arrow collapses a selection to its side.
    shown.key(Qt::Key_Left);
    QCOMPARE(shown.selection(), (TextRange{3, 3}));
    shown.key(Qt::Key_End, Qt::ShiftModifier);
    QCOMPARE(shown.selection(), (TextRange{3, 7}));
    shown.key(Qt::Key_Right);
    QCOMPARE(shown.editor().caretPosition(), 7);
    shown.key(Qt::Key_Left, Qt::ControlModifier);
    QCOMPARE(shown.editor().caretPosition(), 4);
    // Down keeps its first x; below the last, the end.
    shown.key(Qt::Key_Down);
    QVERIFY(shown.editor().caretPosition() > 8 && shown.editor().caretPosition() < 13);
    shown.key(Qt::Key_Down);
    QCOMPARE(shown.editor().caretPosition(), 13);
    shown.key(Qt::Key_Home, Qt::ControlModifier);
    QCOMPARE(shown.editor().caretPosition(), 0);
    shown.key(Qt::Key_End, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(shown.selection(), (TextRange{0, 13}));
    // A character a step; above the first line, its start.
    shown.key(Qt::Key_Home, Qt::ControlModifier);
    shown.key(Qt::Key_Right);
    shown.key(Qt::Key_Right);
    shown.key(Qt::Key_Left);
    QCOMPARE(shown.caret(), 1);
    shown.key(Qt::Key_Up);
    QCOMPARE(shown.caret(), 0);
    // Nothing lies past either end.
    shown.key(Qt::Key_Left);
    QCOMPARE(shown.caret(), 0);
    shown.key(Qt::Key_End, Qt::ControlModifier);
    shown.key(Qt::Key_Right);
    QCOMPARE(shown.caret(), 13);
}

void InlineTextEditorTests::upAndDownKeepTheirColumn()
{
    TextCanvas shown;
    shown.type(QStringLiteral("abcdef"));
    shown.key(Qt::Key_Return);
    shown.type(QStringLiteral("ab"));
    shown.key(Qt::Key_Return);
    shown.type(QStringLiteral("abcdef"));
    shown.key(Qt::Key_Home, Qt::ControlModifier);
    shown.key(Qt::Key_End);
    // Through a short line and back, as NSTextView.
    shown.key(Qt::Key_Down);
    QCOMPARE(shown.caret(), 9);
    shown.key(Qt::Key_Down);
    QCOMPARE(shown.caret(), 16);
    shown.key(Qt::Key_Up);
    shown.key(Qt::Key_Up);
    QCOMPARE(shown.caret(), 6);
    // An edit starts the column afresh.
    shown.key(Qt::Key_Down);
    shown.key(Qt::Key_Backspace);
    shown.key(Qt::Key_Down);
    QCOMPARE(shown.caret(), 10);
}

void InlineTextEditorTests::aWrappedParagraphMovesByItsLines()
{
    TextCanvas shown;
    shown.session.cancelText();
    shown.session.beginText(QRectF(10, 10, 300, 400));
    shown.type(QStringLiteral("alpha beta gamma"));
    // One word a line: Home keeps to its line.
    shown.key(Qt::Key_Home);
    QCOMPARE(shown.editor().caretPosition(), 11);
    shown.key(Qt::Key_Up);
    QCOMPARE(shown.editor().caretPosition(), 6);
    // A wrapped line's end stays before its breaking space.
    shown.key(Qt::Key_End);
    QCOMPARE(shown.editor().caretPosition(), 10);
    shown.key(Qt::Key_Up);
    QCOMPARE(shown.editor().caretPosition(), 4);
    shown.key(Qt::Key_Down);
    shown.key(Qt::Key_Down);
    QVERIFY(shown.editor().caretPosition() > 11 && shown.editor().caretPosition() <= 16);
    // The last line keeps its space: End goes past it.
    shown.key(Qt::Key_End, Qt::ControlModifier);
    shown.type(QStringLiteral(" "));
    shown.key(Qt::Key_Home);
    shown.key(Qt::Key_End);
    QCOMPARE(shown.editor().caretPosition(), 17);
}

void InlineTextEditorTests::aCaretPastTheBoxHasNoLine()
{
    TextCanvas shown;
    shown.session.cancelText();
    // Room for one line: "beta" falls past the bottom.
    shown.session.beginText(QRectF(10, 10, 300, 120));
    shown.type(QStringLiteral("alpha beta"));
    shown.key(Qt::Key_Up);
    QCOMPARE(shown.caret(), 10);
    shown.key(Qt::Key_Home);
    QCOMPARE(shown.caret(), 0);
    // The laid line still wraps before its space.
    shown.key(Qt::Key_End);
    QCOMPARE(shown.caret(), 5);
    shown.key(Qt::Key_Up);
    QCOMPARE(shown.caret(), 0);
    shown.key(Qt::Key_End);
    shown.key(Qt::Key_Right);
    shown.key(Qt::Key_End);
    QCOMPARE(shown.caret(), 10);
}

void InlineTextEditorTests::deletionsReachCharactersWordsAndLines()
{
    TextCanvas shown;
    shown.type(QStringLiteral("alpha beta gamma"));
    shown.key(Qt::Key_Backspace, Qt::AltModifier);
    QCOMPARE(shown.content(), QString("alpha beta "));
    shown.key(Qt::Key_Backspace, Qt::ControlModifier);
    QCOMPARE(shown.content(), QString());
    shown.type(QStringLiteral("alpha beta"));
    shown.key(Qt::Key_Home);
    shown.key(Qt::Key_Delete, Qt::AltModifier);
    QCOMPARE(shown.content(), QString(" beta"));
    shown.key(Qt::Key_Delete);
    QCOMPARE(shown.content(), QString("beta"));
    shown.key(Qt::Key_Delete, Qt::ControlModifier);
    QCOMPARE(shown.content(), QString());
    // A character as a reader counts it: the emoji whole.
    shown.type(QStringLiteral("a"));
    shown.typeText(QStringLiteral("😀"));
    QCOMPARE(shown.content(), QString("a😀"));
    shown.key(Qt::Key_Backspace);
    QCOMPARE(shown.content(), QString("a"));
    // A letter and its accent move and go as one.
    shown.typeText(QStringLiteral("e\u0301"));
    shown.key(Qt::Key_Left);
    QCOMPARE(shown.caret(), 1);
    shown.key(Qt::Key_Right);
    QCOMPARE(shown.caret(), 3);
    shown.key(Qt::Key_Backspace);
    QCOMPARE(shown.content(), QString("a"));
    // A selection goes first, whatever the key.
    shown.type(QStringLiteral("bc"));
    shown.key(Qt::Key_Left, Qt::ShiftModifier);
    shown.key(Qt::Key_Left, Qt::ShiftModifier);
    shown.key(Qt::Key_Delete, Qt::ControlModifier);
    QCOMPARE(shown.content(), QString("a"));
    // Shift adds nothing; the keypad's Delete deletes.
    shown.type(QStringLiteral("b cd"));
    shown.key(Qt::Key_Backspace, Qt::ShiftModifier);
    QCOMPARE(shown.content(), QString("ab c"));
    shown.key(Qt::Key_Backspace, Qt::ShiftModifier | Qt::AltModifier);
    QCOMPARE(shown.content(), QString("ab "));
    shown.key(Qt::Key_Home);
    shown.key(Qt::Key_Delete, Qt::KeypadModifier);
    QCOMPARE(shown.content(), QString("b "));
    // At either end nothing goes, and the redo lasts.
    shown.key(Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(shown.content(), QString("ab "));
    QCOMPARE(shown.caret(), 0);
    shown.key(Qt::Key_Backspace);
    shown.key(Qt::Key_End);
    shown.key(Qt::Key_Delete);
    shown.key(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(shown.content(), QString("b "));
}

void InlineTextEditorTests::returnTabEscapeAndFinishing()
{
    TextCanvas shown;
    shown.type(QStringLiteral("A"));
    shown.key(Qt::Key_Return);
    shown.key(Qt::Key_Tab);
    shown.key(Qt::Key_Space);
    shown.type(QStringLiteral("BM"));
    QCOMPARE(shown.content(), QString("A\n\t BM"));
    // Tool letters and Space type here: the canvas keeps none.
    QCOMPARE(shown.session.tool(), NavigationTool::type);
    QCOMPARE(shown.editor().logicalSize().height(), EditorSession::textBoxSize(shown.session.textDraft().value().style).height());
    const size_t count = shown.session.document().value().layers.size();
    shown.key(Qt::Key_Escape);
    QVERIFY(!shown.session.textDraft() && !shown.canvas->inlineTextEditor());
    QCOMPARE(shown.session.document().value().layers.size(), count);
    shown.session.beginText(QPointF(40, 40), true);
    shown.type(QStringLiteral("Kept"));
    shown.key(Qt::Key_Return, Qt::ControlModifier);
    QVERIFY(!shown.session.textDraft());
    QCOMPARE(shown.session.activeLayer().value().liveText().value().style.content, QString("Kept"));
    shown.session.editActiveText();
    shown.key(Qt::Key_End);
    shown.type(QStringLiteral("!"));
    shown.key(Qt::Key_Enter, Qt::ControlModifier);
    QCOMPARE(shown.session.activeLayer().value().liveText().value().style.content, QString("Kept!"));
}

void InlineTextEditorTests::altWithTheArrowsSetsSpacing()
{
    TextCanvas shown;
    shown.type(QStringLiteral("Spaced"));
    const auto style = [&] { return shown.session.textDraft().value().style; };
    shown.key(Qt::Key_Right, Qt::AltModifier);
    QCOMPARE(style().tracking, 1.0);
    shown.key(Qt::Key_Left, Qt::AltModifier | Qt::ShiftModifier);
    QCOMPARE(style().tracking, -9.0);
    // Up closes the lines from what Auto works out to.
    shown.key(Qt::Key_Up, Qt::AltModifier);
    QCOMPARE(style().leading, 72 * 1.2 - 1);
    shown.key(Qt::Key_Down, Qt::AltModifier | Qt::ShiftModifier);
    QCOMPARE(style().leading, 72 * 1.2 + 9);
    shown.session.changeTextStyle([](LayerTextStyle &changed) { changed.leading = 5; });
    shown.key(Qt::Key_Up, Qt::AltModifier | Qt::ShiftModifier);
    QCOMPARE(style().leading, 1.0);
    // Past the style's bounds nothing changes.
    shown.session.changeTextStyle([](LayerTextStyle &changed) { changed.tracking = 995; });
    shown.key(Qt::Key_Right, Qt::AltModifier | Qt::ShiftModifier);
    QCOMPARE(style().tracking, 995.0);
    QCOMPARE(shown.content(), QString("Spaced"));
    QCOMPARE(shown.editor().caretPosition(), 6);
}

void InlineTextEditorTests::aFinalNewlineHasALineForTheCaret()
{
    TextCanvas shown;
    shown.session.cancelText();
    // In a box, Return makes TextKit's extra line, the caret's.
    shown.session.beginText(QRectF(10, 10, 300, 300));
    shown.type(QStringLiteral("A"));
    shown.key(Qt::Key_Return);
    shown.key(Qt::Key_Home);
    shown.type(QStringLiteral("B"));
    QCOMPARE(shown.content(), QString("A\nB"));
    // Point text keeps its measure; that line lies past it.
    shown.session.cancelText();
    shown.session.beginText(QPointF(20, 30), true);
    shown.type(QStringLiteral("A"));
    const QSizeF one = shown.editor().logicalSize();
    shown.key(Qt::Key_Return);
    QCOMPARE(shown.editor().logicalSize(), one);
    shown.key(Qt::Key_Up);
    QCOMPARE(shown.caret(), 0);
    shown.key(Qt::Key_Down);
    QCOMPARE(shown.caret(), 2);
    shown.key(Qt::Key_Home);
    shown.type(QStringLiteral("B"));
    QCOMPARE(shown.content(), QString("A\nB"));
    // A final U+2028 breaks the line the same way.
    const QString separator(QChar::LineSeparator);
    shown.session.cancelText();
    shown.session.beginText(QRectF(10, 10, 300, 300));
    shown.typeText(QStringLiteral("A") + separator);
    shown.key(Qt::Key_Home);
    shown.type(QStringLiteral("B"));
    QCOMPARE(shown.content(), QStringLiteral("A") + separator + QStringLiteral("B"));
    // A line ends before the separator that breaks it.
    shown.key(Qt::Key_Home, Qt::ControlModifier);
    shown.key(Qt::Key_End);
    QCOMPARE(shown.caret(), 1);
}

void InlineTextEditorTests::arrowsFollowARightToLeftParagraph()
{
    TextCanvas shown;
    shown.typeText(QStringLiteral("אבג דה"));
    // Right goes back through Hebrew, as Qt's fields move.
    shown.key(Qt::Key_Home);
    shown.key(Qt::Key_Left);
    QCOMPARE(shown.caret(), 1);
    shown.key(Qt::Key_Right);
    QCOMPARE(shown.caret(), 0);
    // A plain arrow collapses to its own side.
    shown.key(Qt::Key_Left, Qt::ShiftModifier);
    shown.key(Qt::Key_Left, Qt::ShiftModifier);
    QCOMPARE(shown.selection(), (TextRange{0, 2}));
    shown.key(Qt::Key_Right);
    QCOMPARE(shown.selection(), (TextRange{0, 0}));
    shown.key(Qt::Key_Left, Qt::ShiftModifier);
    shown.key(Qt::Key_Left);
    QCOMPARE(shown.selection(), (TextRange{1, 1}));
    // Words go the same way.
    shown.key(Qt::Key_Left, Qt::ControlModifier);
    QCOMPARE(shown.caret(), 3);
    shown.key(Qt::Key_Right, Qt::ControlModifier);
    QCOMPARE(shown.caret(), 0);
    // Each paragraph reads its own way, whatever parts them.
    for (const QString &separator : {QStringLiteral("\n"), QStringLiteral("\r"), QString(QChar(0x2029))}) {
        TextDraft draft = shown.session.textDraft().value();
        draft.style.content = QStringLiteral("אב") + separator + QStringLiteral("ab");
        shown.session.setTextDraft(draft);
        shown.key(Qt::Key_End, Qt::ControlModifier);
        shown.key(Qt::Key_Home);
        shown.key(Qt::Key_Right);
        QCOMPARE(shown.caret(), 4);
    }
}

QTEST_MAIN(InlineTextEditorTests)
#include "InlineTextEditorTests.moc"
