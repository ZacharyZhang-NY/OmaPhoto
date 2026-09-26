#include "InlineTextFixtures.h"
#include <QClipboard>
#include <QLineEdit>

// What types, the cap, the clipboard and input methods.
class InlineTextInputTests : public QObject {
    Q_OBJECT
private slots:
    void keysTypeAsQtsTextFieldsJudge();
    void theTextHoldsAHundredThousandUnits();
    void theClipboardTakesPlainText();
    void inputMethodsComposeInTheDraft();
    void inputMethodsReplaceOnlyWhatTheyName();
    void inputMethodsFollowTheCaret();
    void aPreeditLeavesTheHistoryAlone();
    void theEditMenuWorksAroundAPreedit();
};

void InlineTextInputTests::keysTypeAsQtsTextFieldsJudge()
{
    TextCanvas shown;
    // A joiner types, even under Ctrl and Shift.
    const QString joiner(QChar(0x200D));
    shown.typeText(joiner, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(shown.content(), joiner);
    // Ctrl alone or with Shift commands; AltGr's Ctrl types.
    shown.key(Qt::Key_K, Qt::ControlModifier);
    shown.key(Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(shown.content(), joiner);
    shown.key(Qt::Key_K, Qt::ControlModifier | Qt::AltModifier);
    QCOMPARE(shown.content(), joiner + QStringLiteral("k"));
    // Shift-+ and Shift-− type here, as in any text view.
    QTRY_VERIFY(shown.canvas->hasFocus());
    shown.key(Qt::Key_Plus, Qt::ShiftModifier);
    shown.key(Qt::Key_Underscore, Qt::ShiftModifier);
    QCOMPARE(shown.content(), joiner + QStringLiteral("k+_"));
    QCOMPARE(shown.session.activeLayer().value().blendMode, LayerBlendMode::normal);
}

void InlineTextInputTests::theTextHoldsAHundredThousandUnits()
{
    TextCanvas shown;
    TextDraft draft = shown.session.textDraft().value();
    draft.style.content = QString(99'999, QLatin1Char('a'));
    shown.session.setTextDraft(draft);
    shown.key(Qt::Key_End, Qt::ControlModifier);
    shown.type(QStringLiteral("bc"));
    QCOMPARE(shown.content().size(), 100'000);
    QVERIFY(shown.content().endsWith(QLatin1Char('b')));
    // Replacing keeps within the cap too.
    QGuiApplication::clipboard()->setText(QStringLiteral("xyz"));
    shown.key(Qt::Key_Left, Qt::ShiftModifier);
    shown.key(Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(shown.content().size(), 100'000);
    QVERIFY(shown.content().endsWith(QLatin1Char('b')));
    // So does an input method's text.
    shown.commit(QStringLiteral("zz"));
    QCOMPARE(shown.content().size(), 100'000);
    QVERIFY(shown.content().endsWith(QLatin1Char('b')));
}

void InlineTextInputTests::theClipboardTakesPlainText()
{
    TextCanvas shown;
    shown.type(QStringLiteral("copy me"));
    shown.key(Qt::Key_A, Qt::ControlModifier);
    QCOMPARE(shown.selection(), (TextRange{0, 7}));
    shown.key(Qt::Key_C, Qt::ControlModifier);
    QCOMPARE(QGuiApplication::clipboard()->text(), QString("copy me"));
    shown.key(Qt::Key_End);
    shown.key(Qt::Key_Left, Qt::ControlModifier | Qt::ShiftModifier);
    shown.key(Qt::Key_X, Qt::ControlModifier);
    QCOMPARE(QGuiApplication::clipboard()->text(), QString("me"));
    QCOMPARE(shown.content(), QString("copy "));
    QMimeData *rich = new QMimeData;
    rich->setHtml(QStringLiteral("<b>bold</b>"));
    rich->setText(QStringLiteral("plain"));
    QGuiApplication::clipboard()->setMimeData(rich);
    shown.key(Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(shown.content(), QString("copy plain"));
    // Each paste is its own step.
    shown.key(Qt::Key_V, Qt::ControlModifier);
    shown.key(Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(shown.content(), QString("copy plain"));
    // Nothing selected, nothing cut, and the redo lasts.
    shown.key(Qt::Key_X, Qt::ControlModifier);
    QCOMPARE(shown.content(), QString("copy plain"));
    QCOMPARE(QGuiApplication::clipboard()->text(), QString("plain"));
    shown.key(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(shown.content(), QString("copy plainplain"));
    // Shift-Delete is the platform's Cut, never a deletion.
    QVERIFY(QKeySequence::keyBindings(QKeySequence::Cut).contains(QKeySequence(Qt::SHIFT | Qt::Key_Delete)));
    shown.key(Qt::Key_Home);
    shown.key(Qt::Key_Delete, Qt::ShiftModifier);
    QCOMPARE(shown.content(), QString("copy plainplain"));
    shown.key(Qt::Key_End);
    shown.key(Qt::Key_Left, Qt::ShiftModifier);
    shown.key(Qt::Key_Delete, Qt::ShiftModifier);
    QCOMPARE(QGuiApplication::clipboard()->text(), QString("n"));
    QCOMPARE(shown.content(), QString("copy plainplai"));
}

void InlineTextInputTests::inputMethodsComposeInTheDraft()
{
    TextCanvas shown;
    QVERIFY(shown.canvas->testAttribute(Qt::WA_InputMethodEnabled));
    shown.type(QStringLiteral("x"));
    shown.compose(QStringLiteral("ni"), {QInputMethodEvent::Attribute(QInputMethodEvent::Cursor, 1, 1)});
    // Marked text is in the draft, as in NSTextView's storage.
    QCOMPARE(shown.content(), QString("xni"));
    QCOMPARE(shown.editor().marked(), std::optional(TextRange{1, 3}));
    QCOMPARE(shown.caret(), 2);
    const QWidget &widget = *shown.canvas;
    // Input methods see the text around, without their own.
    QCOMPARE(widget.inputMethodQuery(Qt::ImSurroundingText).toString(), QString("x"));
    QCOMPARE(widget.inputMethodQuery(Qt::ImCursorPosition).toInt(), 1);
    shown.commit(QStringLiteral("你"));
    QCOMPARE(shown.content(), QString("x你"));
    QVERIFY(!shown.editor().marked());
    QCOMPARE(shown.caret(), 2);
    QCOMPARE(widget.inputMethodQuery(Qt::ImSurroundingText).toString(), QString("x你"));
    QCOMPARE(widget.inputMethodQuery(Qt::ImCursorPosition).toInt(), 2);
    QVERIFY(widget.inputMethodQuery(Qt::ImHints).toInt() & Qt::ImhMultiLine);
    const QRectF caret = widget.inputMethodQuery(Qt::ImCursorRectangle).toRectF();
    QVERIFY(caret.left() > 20 + 12 && caret.height() > 80);
    // The typing and the composed word undo together.
    shown.key(Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(shown.content(), QString());
    // After a move, composing starts a step of its own.
    shown.type(QStringLiteral("a"));
    shown.key(Qt::Key_Left);
    shown.compose(QStringLiteral("b"));
    shown.commit(QStringLiteral("b"));
    QCOMPARE(shown.content(), QString("ba"));
    shown.key(Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(shown.content(), QString("a"));
    // A paste lands before the preedit, which follows, as Qt's.
    shown.compose(QStringLiteral("ni"));
    QGuiApplication::clipboard()->setText(QStringLiteral("P"));
    shown.key(Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(shown.content(), QString("Pnia"));
    QCOMPARE(shown.editor().marked(), std::optional(TextRange{1, 3}));
    // An empty preedit ends the composing.
    shown.compose(QStringLiteral("ni"));
    shown.compose(QString());
    QCOMPARE(shown.content(), QString("Pa"));
    QVERIFY(!shown.editor().marked());
    // A preedit records nothing: undo takes the last step back.
    shown.key(Qt::Key_End);
    shown.compose(QStringLiteral("n"));
    shown.key(Qt::Key_Left);
    shown.compose(QStringLiteral("ni"));
    QCOMPARE(shown.content(), QString("Pani"));
    shown.key(Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(shown.content(), QString("a"));
    QVERIFY(!shown.editor().marked());
    shown.key(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(shown.content(), QString("Pa"));
    shown.session.cancelText();
    QVERIFY(!shown.canvas->testAttribute(Qt::WA_InputMethodEnabled));
}

void InlineTextInputTests::inputMethodsReplaceOnlyWhatTheyName()
{
    TextCanvas shown;
    // A commit alone types; one may replace text before.
    shown.commit(QStringLiteral("helo"));
    QCOMPARE(shown.content(), QString("helo"));
    shown.commit(QStringLiteral("hello"), -4, 4);
    QCOMPARE(shown.content(), QString("hello"));
    QCOMPARE(shown.caret(), 5);
    shown.commit(QString(), -1, 1);
    QCOMPARE(shown.content(), QString("hell"));
    shown.key(Qt::Key_Left);
    shown.commit(QString(), -1, 1);
    QCOMPARE(shown.content(), QString("hel"));
    QCOMPARE(shown.caret(), 2);
    // Qt removes a range whole or not at all.
    shown.commit(QString(), -1, 5);
    QCOMPARE(shown.content(), QString("hel"));
    shown.key(Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(shown.content(), QString("hell"));
    shown.key(Qt::Key_End);
    // One typing step, as keys make; input drops the redo.
    shown.key(Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(shown.content(), QString());
    shown.commit(QStringLiteral("h"));
    shown.key(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(shown.content(), QString("h"));
    shown.commit(QStringLiteral("ell"));
    // Nothing inserted or removed changes nothing.
    shown.key(Qt::Key_A, Qt::ControlModifier);
    shown.compose(QString());
    QCOMPARE(shown.content(), QString("hell"));
    QCOMPARE(shown.selection(), (TextRange{0, 4}));
    shown.key(Qt::Key_Home);
    shown.commit(QString(), 9, 1);
    QCOMPARE(shown.caret(), 0);
    QCOMPARE(shown.content(), QString("hell"));
    // While composing, a replacement counts from the preedit.
    shown.key(Qt::Key_End);
    shown.compose(QStringLiteral("ni"));
    shown.commit(QStringLiteral("X"), -2, 2);
    QCOMPARE(shown.content(), QString("heX"));
    QVERIFY(!shown.editor().marked());
    QCOMPARE(shown.caret(), 3);
    // A replacement of nothing still places what it inserts.
    shown.commit(QStringLiteral("Y"), -1, 0);
    QCOMPARE(shown.content(), QString("heYX"));
    QCOMPARE(shown.caret(), 3);
    // An input method may set the selection alone.
    shown.key(Qt::Key_Left, Qt::ShiftModifier);
    shown.compose(QString(), {QInputMethodEvent::Attribute(QInputMethodEvent::Selection, 1, 2, QVariant())});
    QCOMPARE(shown.selection(), (TextRange{1, 3}));
    QCOMPARE(shown.content(), QString("heYX"));
    // Input takes a selection first, then counts from there.
    shown.commit(QStringLiteral("Z"), 1, 0);
    QCOMPARE(shown.content(), QString("hXZ"));
    QCOMPARE(shown.selection(), (TextRange{3, 3}));
    // A selection beside a preedit goes with the next input.
    TextDraft draft = shown.session.textDraft().value();
    draft.style.content = QStringLiteral("abcdef");
    shown.session.setTextDraft(draft);
    shown.key(Qt::Key_End);
    shown.compose(QStringLiteral("ni"), {QInputMethodEvent::Attribute(QInputMethodEvent::Selection, 1, 2, QVariant())});
    QCOMPARE(shown.content(), QString("abcnidef"));
    QCOMPARE(shown.editor().inputMethodQuery(Qt::ImCurrentSelection).toString(), QString("bc"));
    QCOMPARE(shown.editor().inputMethodQuery(Qt::ImAnchorPosition).toInt(), 1);
    QCOMPARE(shown.editor().inputMethodQuery(Qt::ImCursorPosition).toInt(), 3);
    // Its updates move the caret and keep that selection.
    shown.compose(QStringLiteral("ni"), {QInputMethodEvent::Attribute(QInputMethodEvent::Cursor, 0, 1, QVariant())});
    QCOMPARE(shown.caret(), 3);
    QCOMPARE(shown.editor().inputMethodQuery(Qt::ImCurrentSelection).toString(), QString("bc"));
    shown.commit(QStringLiteral("X"));
    QCOMPARE(shown.content(), QString("aXdef"));
    // Its own move ends a typing run, as keys do.
    shown.commit(QStringLiteral("abc"));
    shown.compose(QString(), {QInputMethodEvent::Attribute(QInputMethodEvent::Selection, 0, 0, QVariant())});
    shown.commit(QStringLiteral("Q"));
    QCOMPARE(shown.content(), QString("QaXabcdef"));
    shown.key(Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(shown.content(), QString("aXabcdef"));
    // Queries read it in the text without the preedit.
    shown.key(Qt::Key_Home);
    shown.compose(QStringLiteral("ni"), {QInputMethodEvent::Attribute(QInputMethodEvent::Selection, 3, -2, QVariant())});
    QCOMPARE(shown.content(), QString("aniXabcdef"));
    QCOMPARE(shown.editor().inputMethodQuery(Qt::ImCurrentSelection).toString(), QString("Xa"));
}

void InlineTextInputTests::inputMethodsFollowTheCaret()
{
    TextCanvas shown;
    QTRY_VERIFY(shown.canvas->hasFocus());
    // Edits, moves and zooms move the candidates, as microfocus.
    QSignalSpy moved(QGuiApplication::inputMethod(), &QInputMethod::cursorRectangleChanged);
    shown.type(QStringLiteral("a"));
    QVERIFY(!moved.isEmpty());
    moved.clear();
    shown.key(Qt::Key_Left);
    QVERIFY(!moved.isEmpty());
    moved.clear();
    shown.session.zoom(2);
    QVERIFY(!moved.isEmpty());
    // Without the keys the canvas leaves input methods be.
    auto *field = new QLineEdit(&shown.window);
    field->show();
    field->setFocus();
    QTRY_VERIFY(field->hasFocus());
    moved.clear();
    shown.session.zoom(1);
    shown.editor().selectAll();
    QVERIFY(moved.isEmpty());
}

void InlineTextInputTests::aPreeditLeavesTheHistoryAlone()
{
    TextCanvas shown;
    // Qt's contract: a cancelled composition keeps the redo.
    shown.type(QStringLiteral("abc"));
    shown.key(Qt::Key_Z, Qt::ControlModifier);
    shown.compose(QStringLiteral("ni"));
    shown.compose(QString());
    QCOMPARE(shown.content(), QString());
    shown.key(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(shown.content(), QString("abc"));
    // A commit is one step, from the text before composing.
    shown.key(Qt::Key_End);
    shown.compose(QStringLiteral("x"));
    shown.compose(QStringLiteral("xy"));
    shown.commit(QStringLiteral("XY"));
    QCOMPARE(shown.content(), QString("abcXY"));
    shown.key(Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(shown.content(), QString("abc"));
    shown.key(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(shown.content(), QString("abcXY"));
    // A preedit takes a selection in a typing step.
    shown.key(Qt::Key_A, Qt::ControlModifier);
    shown.compose(QStringLiteral("q"));
    shown.compose(QString());
    QCOMPARE(shown.content(), QString());
    shown.key(Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(shown.content(), QString("abcXY"));
    // A partial commit lands; what follows counts from there.
    shown.key(Qt::Key_End);
    shown.compose(QStringLiteral("mn"));
    QInputMethodEvent partial(QStringLiteral("n"), {});
    partial.setCommitString(QStringLiteral("M"));
    QApplication::sendEvent(shown.canvas, &partial);
    shown.key(Qt::Key_Left);
    shown.commit(QStringLiteral("N"));
    QCOMPARE(shown.content(), QString("abcXYMN"));
    shown.key(Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(shown.content(), QString("abcXYM"));
}

void InlineTextInputTests::theEditMenuWorksAroundAPreedit()
{
    TextCanvas shown;
    const auto composing = [&] {
        TextDraft draft = shown.session.textDraft().value();
        draft.style.content = QStringLiteral("abcdef");
        shown.session.setTextDraft(draft);
        shown.key(Qt::Key_End);
        shown.compose(QStringLiteral("ni"), {QInputMethodEvent::Attribute(QInputMethodEvent::Selection, 1, 2, QVariant())});
    };
    // Copy and Cut take the selection beside it, as Qt's.
    composing();
    QGuiApplication::clipboard()->setText(QStringLiteral("sentinel"));
    shown.editor().copy();
    QCOMPARE(QGuiApplication::clipboard()->text(), QString("bc"));
    shown.editor().cut();
    QCOMPARE(shown.content(), QString("anidef"));
    QCOMPARE(shown.editor().marked(), std::optional(TextRange{1, 3}));
    QCOMPARE(shown.caret(), 3);
    shown.commit(QStringLiteral("Y"));
    QCOMPARE(shown.content(), QString("aYdef"));
    shown.key(Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(shown.content(), QString("adef"));
    shown.key(Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(shown.content(), QString("abcdef"));
    // Paste replaces it; the preedit follows the pasted text.
    composing();
    QGuiApplication::clipboard()->setText(QStringLiteral("P"));
    shown.editor().paste();
    QCOMPARE(shown.content(), QString("aPnidef"));
    QCOMPARE(shown.editor().marked(), std::optional(TextRange{2, 4}));
    shown.commit(QStringLiteral("X"));
    QCOMPARE(shown.content(), QString("aPXdef"));
    // Select All selects around it; the next input takes it.
    composing();
    shown.editor().selectAll();
    QCOMPARE(shown.content(), QString("abcdefni"));
    QCOMPARE(shown.editor().inputMethodQuery(Qt::ImCurrentSelection).toString(), QString("abcdef"));
    shown.commit(QStringLiteral("X"));
    QCOMPARE(shown.content(), QString("X"));
    // It ends a typing run, a preedit showing or not.
    shown.type(QStringLiteral("y"));
    shown.compose(QStringLiteral("ni"));
    shown.editor().selectAll();
    shown.commit(QStringLiteral("Z"));
    shown.key(Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(shown.content(), QString("Xy"));
    // A paste is its own step and drops the redo.
    shown.key(Qt::Key_End);
    shown.type(QStringLiteral("c"));
    QGuiApplication::clipboard()->setText(QStringLiteral("P"));
    shown.key(Qt::Key_V, Qt::ControlModifier);
    shown.type(QStringLiteral("b"));
    shown.key(Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(shown.content(), QString("XycP"));
    shown.key(Qt::Key_Z, Qt::ControlModifier);
    QGuiApplication::clipboard()->setText(QStringLiteral("Q"));
    shown.key(Qt::Key_V, Qt::ControlModifier);
    shown.key(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(shown.content(), QString("XycQ"));
}

QTEST_MAIN(InlineTextInputTests)
#include "InlineTextInputTests.moc"
