#include "InlineTextDrawFixtures.h"
#include "UI/TypeControls.h"
#include <QAbstractItemView>
#include <QFontMetricsF>
#include "Rendering/TextLayout.h"

// Swift 1.3.4's font menu: the selection's face, or (Multiple).
namespace {
const QString sans = QStringLiteral("DejaVuSans");
const QString mono = QStringLiteral("DejaVuSansMono");

// Typed text with a menu beside the canvas.
struct Typed : DrawnText {
    TypeControls *const bar = new TypeControls(session, &window);
    TypeFontPicker &menu() const { return *bar->findChild<TypeFontPicker *>(QStringLiteral("typeFont")); }
    Typed()
    {
        bar->setGeometry(0, 300, 400, 40);
        bar->show();
        session.changeTextStyle([](LayerTextStyle &style) { style.fontName = sans; });
        canvas->setFocus();
        beginTextAt(session, QPointF(20, 30));
        QTest::keyClicks(canvas, QStringLiteral("Hello"));
    }
    const LayerTextStyle &style() const { return session.textDraft().value().style; }
    // The menu chooses a face, as a click does.
    void choose(const QString &face)
    {
        if (menu().findText(face) < 0) {
            menu().showPopup();
            menu().hidePopup();
        }
        emit menu().activated(menu().findText(face));
    }
};
}

class TextFontControlsTests : public QObject {
    Q_OBJECT
private slots:
    void theMenuShowsTheCaretsFaceOrMultiple();
    void aChosenFaceTakesTheSelectionTheMenuKept();
    void typingAndUndoKeepTheFaces();
    void theLoadedMenuKeepsMultipleOnTop();
    void deletingToOneFaceKeepsItsLetters();
    void theEditorPlacesTheCaretByTheFaces();
};

void TextFontControlsTests::theMenuShowsTheCaretsFaceOrMultiple()
{
    Typed typed;
    typed.session.changeTextStyle([](LayerTextStyle &style) { style.setFont(mono, {0, 2}); });
    // The caret shows the letter before it.
    QTest::keyClick(typed.canvas, Qt::Key_Home);
    QTest::keyClick(typed.canvas, Qt::Key_Right);
    QCOMPARE(typed.menu().currentText(), mono);
    QTest::keyClick(typed.canvas, Qt::Key_Right);
    QCOMPARE(typed.menu().currentText(), mono);
    QTest::keyClick(typed.canvas, Qt::Key_Right);
    QCOMPARE(typed.menu().currentText(), sans);
    QTest::keyClick(typed.canvas, Qt::Key_End);
    QCOMPARE(typed.menu().currentText(), sans);
    // At the start, the first letter.
    QTest::keyClick(typed.canvas, Qt::Key_Home);
    QCOMPARE(typed.menu().currentText(), mono);
    // Across both faces: (Multiple), on top, no font.
    QTest::keyClick(typed.canvas, Qt::Key_Right);
    QTest::keyClick(typed.canvas, Qt::Key_Right, Qt::ShiftModifier);
    QTest::keyClick(typed.canvas, Qt::Key_Right, Qt::ShiftModifier);
    QCOMPARE(typed.menu().currentIndex(), 0);
    QCOMPARE(typed.menu().currentText(), QStringLiteral("(Multiple)"));
    QVERIFY(typed.menu().isMultiple(0));
    // Still mixed: the item stays one.
    QTest::keyClick(typed.canvas, Qt::Key_Right, Qt::ShiftModifier);
    QCOMPARE(typed.menu().currentText(), QStringLiteral("(Multiple)"));
    QVERIFY(!typed.menu().isMultiple(1));
    // One face again: the item goes.
    QTest::keyClick(typed.canvas, Qt::Key_End);
    QTest::keyClick(typed.canvas, Qt::Key_Left, Qt::ShiftModifier);
    QCOMPARE(typed.menu().currentText(), sans);
    QVERIFY(!typed.menu().isMultiple(0) && typed.menu().findText(QStringLiteral("(Multiple)")) < 0);
}

void TextFontControlsTests::aChosenFaceTakesTheSelectionTheMenuKept()
{
    Typed typed;
    QTest::keyClick(typed.canvas, Qt::Key_Left, Qt::ShiftModifier);
    QTest::keyClick(typed.canvas, Qt::Key_Left, Qt::ShiftModifier);
    // The menu takes the keys; the letters stay selected.
    typed.menu().setFocus(Qt::MouseFocusReason);
    QTRY_VERIFY(!typed.canvas->hasFocus());
    QCOMPARE(typed.session.textDraft().value().selection, (TextSpan{3, 2}));
    QVERIFY(typed.editor().anchor() == 5 && typed.editor().caretPosition() == 3);
    typed.choose(mono);
    QCOMPARE(typed.style().fontRuns.value(), (std::vector<LayerTextFontRun>{{3, 2, mono}}));
    QCOMPARE(typed.style().fontName, sans);
    // The face shown, or (Multiple), changes nothing.
    QSignalSpy changed(&typed.session, &EditorSession::changed);
    typed.choose(mono);
    QCOMPARE(int(changed.count()), 0);
    typed.canvas->setFocus();
    QTest::keyClick(typed.canvas, Qt::Key_Left, Qt::ShiftModifier);
    QCOMPARE(typed.menu().currentText(), QStringLiteral("(Multiple)"));
    const int heard = int(changed.count());
    emit typed.menu().activated(0);
    QCOMPARE(int(changed.count()), heard);
    // From (Multiple), any face takes the whole selection.
    typed.choose(sans);
    QVERIFY(!typed.style().fontRuns && typed.style().fontName == sans);
    // A caret alone sets the whole text's face.
    QTest::keyClick(typed.canvas, Qt::Key_End);
    typed.choose(mono);
    QVERIFY(!typed.style().fontRuns && typed.style().fontName == mono);
}

void TextFontControlsTests::typingAndUndoKeepTheFaces()
{
    Typed typed;
    QTest::keyClick(typed.canvas, Qt::Key_Left, Qt::ShiftModifier);
    QTest::keyClick(typed.canvas, Qt::Key_Left, Qt::ShiftModifier);
    typed.choose(mono);
    // Typed after mono, mono; after sans, sans.
    QTest::keyClick(typed.canvas, Qt::Key_End);
    QTest::keyClicks(typed.canvas, QStringLiteral("!"));
    QCOMPARE(typed.style().fontRuns.value(), (std::vector<LayerTextFontRun>{{3, 3, mono}}));
    QTest::keyClick(typed.canvas, Qt::Key_Home);
    QTest::keyClick(typed.canvas, Qt::Key_Right);
    QTest::keyClicks(typed.canvas, QStringLiteral("x"));
    QCOMPARE(typed.style().content, QStringLiteral("Hxello!"));
    QCOMPARE(typed.style().fontRuns.value(), (std::vector<LayerTextFontRun>{{4, 3, mono}}));
    // Undo takes the typing back, the faces with it.
    QTest::keyClick(typed.canvas, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(typed.style().content, QStringLiteral("Hello!"));
    QCOMPARE(typed.style().fontRuns.value(), (std::vector<LayerTextFontRun>{{3, 3, mono}}));
    // Applied, the layer keeps them.
    QVERIFY(typed.session.finishText());
    QCOMPARE(typed.session.activeLayer().value().liveText().value().style.fontRuns.value(), (std::vector<LayerTextFontRun>{{3, 3, mono}}));
}

void TextFontControlsTests::theLoadedMenuKeepsMultipleOnTop()
{
    Typed typed;
    typed.session.changeTextStyle([](LayerTextStyle &style) { style.setFont(mono, {0, 2}); });
    QTest::keyClick(typed.canvas, Qt::Key_A, Qt::ControlModifier);
    QCOMPARE(typed.menu().currentText(), QStringLiteral("(Multiple)"));
    typed.menu().showPopup();
    QVERIFY(typed.menu().isMultiple(0) && typed.menu().currentIndex() == 0);
    QVERIFY(typed.menu().findText(mono) > 0 && typed.menu().findText(sans) > 0);
    typed.menu().hidePopup();
    // A single face after loading drops the item again.
    QTest::keyClick(typed.canvas, Qt::Key_End);
    QTRY_COMPARE(typed.menu().currentText(), sans);
    QVERIFY(!typed.menu().isMultiple(0));
}

// A fix beyond Swift, whose draft drops the folded face.
void TextFontControlsTests::deletingToOneFaceKeepsItsLetters()
{
    Typed typed;
    QTest::keyClick(typed.canvas, Qt::Key_Home);
    QTest::keyClick(typed.canvas, Qt::Key_Right, Qt::ShiftModifier);
    QTest::keyClick(typed.canvas, Qt::Key_Right, Qt::ShiftModifier);
    typed.choose(mono);
    QTest::keyClick(typed.canvas, Qt::Key_End);
    for (int letter = 0; letter < 3; ++letter)
        QTest::keyClick(typed.canvas, Qt::Key_Backspace);
    QCOMPARE(typed.style().content, QStringLiteral("He"));
    QVERIFY(!typed.style().fontRuns && typed.style().fontName == mono);
    QVERIFY(typed.session.finishText());
    QCOMPARE(typed.session.activeLayer().value().liveText().value().style.fontName, mono);
}

// A click lands by the faces the letters show.
void TextFontControlsTests::theEditorPlacesTheCaretByTheFaces()
{
    Typed typed;
    QTest::keyClick(typed.canvas, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(typed.canvas, QStringLiteral("iiii"));
    QTest::keyClick(typed.canvas, Qt::Key_Left, Qt::ShiftModifier);
    QTest::keyClick(typed.canvas, Qt::Key_Left, Qt::ShiftModifier);
    typed.choose(mono);
    const double narrow = QFontMetricsF(TextLayout::font(typed.style(), sans)).horizontalAdvance(QLatin1Char('i'));
    const double wide = QFontMetricsF(TextLayout::font(typed.style(), mono)).horizontalAdvance(QLatin1Char('i'));
    QVERIFY(wide > 2 * narrow);
    // A fifth into the last letter: before it, in mono.
    const double x = LayerTextStyle::padding + 2 * narrow + wide + wide / 5;
    const QPointF at = typed.editor().textTransform().map(QPointF(x, LayerTextStyle::padding + typed.style().lineHeight() / 2));
    typed.click(at);
    QCOMPARE(typed.editor().caretPosition(), 3);
}

QTEST_MAIN(TextFontControlsTests)
#include "TextFontControlsTests.moc"
