#include "MenuFixtures.h"
#include "InlineTextDrawFixtures.h"
#include <QClipboard>
#include <QElapsedTimer>
#include <QAction>
#include <QLineEdit>
#include <QPushButton>
#include <QStyleHints>

// The editor on screen: its text, frame, caret, selection, focus.
class InlineTextDrawTests : public QObject {
    Q_OBJECT
private slots:
    void theEditorShowsTheDraftInsteadOfItsLayer();
    void theFrameAndHandlesSitOnTheBox();
    void aTurnedFlippedLayerIsEditedInPlace();
    void theCaretShowsWhileTheCanvasHasFocus();
    void aSelectionShowsBehindItsText();
    void textPastTheBoxShowsAPlus();
    void aFreshDraftTakesTheKeysButSparesAField();
    void theDraftTakesItsKeysAheadOfShortcuts();
    void theEditMenuReachesOpenText();
    void editsRepaintTheEditor();
    void openingTextRepaintsItsWholeLayer();
};

void InlineTextDrawTests::theEditorShowsTheDraftInsteadOfItsLayer()
{
    DrawnText shown;
    shown.text(QPointF(20, 30), QStringLiteral("Hide"));
    QVERIFY(where(shown.grab(), reddish).isValid());
    shown.session.editActiveText();
    shown.session.changeTextStyle([](LayerTextStyle &style) {
        style.content = QStringLiteral("Hi");
        style.red = 0;
        style.green = 1;
    });
    // The layer's red is gone; the draft's green shows.
    const QImage editing = shown.grab();
    QVERIFY(!where(editing, reddish).isValid());
    QVERIFY(where(editing, greenish).isValid());
    shown.session.cancelText();
    QVERIFY(where(shown.grab(), reddish).isValid());
}

void InlineTextDrawTests::theFrameAndHandlesSitOnTheBox()
{
    DrawnText shown;
    shown.session.beginText(QRectF(100, 80, 200, 120));
    const QImage image = shown.grab();
    const QColor accent = shown.canvas->palette().color(QPalette::Highlight);
    const QSizeF size = shown.editor().logicalSize();
    for (const QPointF &unit : LayerTransform::handles)
        QCOMPARE(image.pixelColor(shown.onScreen(QPointF(unit.x() * size.width(), unit.y() * size.height()))), QColor(Qt::white));
    // The frame: an accent line, a sixth of a handle.
    const QColor frame = image.pixelColor(shown.onScreen(QPointF(size.width() / 4, 0.25)));
    QVERIFY2(std::abs(frame.red() - accent.red()) < 40 && std::abs(frame.blue() - accent.blue()) < 40, qPrintable(frame.name()));
    QVERIFY(image.pixelColor(shown.onScreen(QPointF(size.width() / 4, 4))) != frame);
    // No plus without overflow.
    QVERIFY(image.pixelColor(shown.onScreen(QPointF(size.width(), size.height()))) == QColor(Qt::white));
    // Text draws over the frame, a subview over its view.
    shown.session.changeTextStyle([](LayerTextStyle &style) {
        style.content = QStringLiteral("H");
        style.leading = 30;
    });
    QVERIFY(reddish(shown.grab().pixelColor(shown.onScreen(QPointF(24, 0.25)))));
}

void InlineTextDrawTests::aTurnedFlippedLayerIsEditedInPlace()
{
    DrawnText shown;
    const QUuid id = shown.text(QPointF(150, 100), QStringLiteral("Turn"));
    rewrite(shown.session, [&](ProjectSnapshot &snapshot) {
        LayerTransform &transform = record(snapshot, id).transform;
        transform.rotation = 90;
        transform.flipX = true;
    });
    const QRect drawn = where(shown.grab(), reddish);
    // A field keeps the keys, so no caret draws.
    auto *field = new QLineEdit(&shown.window);
    field->show();
    field->setFocus();
    QTRY_VERIFY(field->hasFocus());
    shown.session.editActiveText();
    QTest::qWait(20);
    QVERIFY(!shown.editor().caretShown());
    const QRect edited = where(shown.grab(), reddish);
    QVERIFY2(std::abs(edited.left() - drawn.left()) <= 2 && std::abs(edited.top() - drawn.top()) <= 2 && std::abs(edited.right() - drawn.right()) <= 2
                 && std::abs(edited.bottom() - drawn.bottom()) <= 2,
             qPrintable(QString("%1,%2 %3x%4 against %5,%6 %7x%8")
                            .arg(edited.x()).arg(edited.y()).arg(edited.width()).arg(edited.height())
                            .arg(drawn.x()).arg(drawn.y()).arg(drawn.width()).arg(drawn.height())));
    // Turned, the text runs down the screen.
    QVERIFY(edited.height() > edited.width());
}

void InlineTextDrawTests::theCaretShowsWhileTheCanvasHasFocus()
{
    DrawnText shown;
    auto *field = new QLineEdit(&shown.window);
    field->setGeometry(0, 0, 10, 10);
    field->show();
    shown.canvas->setFocus();
    QTRY_VERIFY(shown.canvas->hasFocus());
    beginTextAt(shown.session, QPointF(20, 30));
    QTest::keyClicks(shown.canvas, QStringLiteral("ab"));
    const QRectF caret = shown.editor().textTransform().mapRect(
        QRectF(QPointF(LayerTextStyle::padding, LayerTextStyle::padding), QSizeF(1, shown.session.textDraft().value().style.lineHeight()))
            .translated(QFontMetricsF(TextLayout::font(shown.session.textDraft().value().style)).horizontalAdvance(QStringLiteral("ab")), 0));
    const QPoint middle = caret.center().toPoint();
    QVERIFY(shown.editor().caretShown());
    QVERIFY2(reddish(shown.grab().pixelColor(middle)), qPrintable(shown.grab().pixelColor(middle).name()));
    // Input methods see the caret where it draws.
    const QRectF asked = static_cast<const QWidget *>(shown.canvas)->inputMethodQuery(Qt::ImCursorRectangle).toRectF();
    QVERIFY(asked.adjusted(-1, -1, 1, 1).contains(middle));
    // It blinks at half the system's flash time, repainting.
    QTest::keyClick(shown.canvas, Qt::Key_End);
    QElapsedTimer clock;
    clock.start();
    // The key's own repaint lands first; then only the blink's.
    QTest::qWait(50);
    PaintSpy spy(*shown.canvas);
    QTRY_VERIFY(!shown.editor().caretShown());
    QVERIFY(clock.elapsed() >= QGuiApplication::styleHints()->cursorFlashTime() / 2 - 50);
    QTRY_VERIFY(spy.painted.contains(middle));
    QTRY_VERIFY(shown.editor().caretShown());
    // Without the keys it stops; with them, shows at once.
    const QTimer *blink = shown.canvas->findChild<QTimer *>(QStringLiteral("caretBlink"));
    QVERIFY(blink && blink->isActive());
    field->setFocus();
    QVERIFY(!shown.editor().caretShown() && !blink->isActive());
    QVERIFY(!reddish(shown.grab().pixelColor(middle)));
    shown.canvas->setFocus();
    QVERIFY(shown.editor().caretShown() && blink->isActive());
    // A move or an edit shows the caret at once.
    QTRY_VERIFY(!shown.editor().caretShown());
    QTest::keyClick(shown.canvas, Qt::Key_Left);
    QVERIFY(shown.editor().caretShown());
    QTRY_VERIFY(!shown.editor().caretShown());
    QTest::keyClicks(shown.canvas, QStringLiteral("c"));
    QVERIFY(shown.editor().caretShown());
    // The blink ends with its editor.
    shown.session.cancelText();
    QVERIFY(!shown.canvas->findChild<QTimer *>(QStringLiteral("caretBlink")));
}

void InlineTextDrawTests::aSelectionShowsBehindItsText()
{
    DrawnText shown;
    shown.canvas->setFocus();
    QTRY_VERIFY(shown.canvas->hasFocus());
    beginTextAt(shown.session, QPointF(20, 30));
    QTest::keyClicks(shown.canvas, QStringLiteral("iiii"));
    QTest::keyClick(shown.canvas, Qt::Key_A, Qt::ControlModifier);
    // Above the letters, the line's height is highlighted.
    const QPoint above = shown.editor().textTransform().map(QPointF(LayerTextStyle::padding + 3, LayerTextStyle::padding + 3)).toPoint();
    QCOMPARE(shown.grab().pixelColor(above), shown.canvas->palette().color(QPalette::Active, QPalette::Highlight));
    const double line = shown.session.textDraft().value().style.lineHeight();
    const QPoint below = shown.editor().textTransform().map(QPointF(LayerTextStyle::padding + 3, LayerTextStyle::padding + line - 3)).toPoint();
    QCOMPARE(shown.grab().pixelColor(below), shown.canvas->palette().color(QPalette::Active, QPalette::Highlight));
    // To the last selected letter's edge.
    const double selected = QFontMetricsF(TextLayout::font(shown.session.textDraft().value().style)).horizontalAdvance(QStringLiteral("iiii"));
    const QPoint last = shown.editor().textTransform().map(QPointF(LayerTextStyle::padding + selected - 3, LayerTextStyle::padding + 3)).toPoint();
    QCOMPARE(shown.grab().pixelColor(last), shown.canvas->palette().color(QPalette::Active, QPalette::Highlight));
    // No caret while something is selected.
    const QPointF mapped = shown.editor().textTransform().map(
        QPointF(LayerTextStyle::padding + QFontMetricsF(TextLayout::font(shown.session.textDraft().value().style)).horizontalAdvance(QStringLiteral("iiii")) + 0.25,
                LayerTextStyle::padding + 40));
    const QPoint end(int(std::floor(mapped.x())), int(std::floor(mapped.y())));
    QVERIFY(!reddish(shown.grab().pixelColor(end)));
    QTest::keyClick(shown.canvas, Qt::Key_End);
    QVERIFY(reddish(shown.grab().pixelColor(end)));
    QVERIFY(shown.grab().pixelColor(above) != shown.canvas->palette().color(QPalette::Active, QPalette::Highlight));
    // Unfocused, a selection shows in the inactive colour.
    QPalette palette = shown.canvas->palette();
    palette.setColor(QPalette::Inactive, QPalette::Highlight, QColor(40, 200, 40));
    shown.canvas->setPalette(palette);
    QTest::keyClick(shown.canvas, Qt::Key_A, Qt::ControlModifier);
    auto *field = new QLineEdit(&shown.window);
    field->show();
    field->setFocus();
    QTRY_VERIFY(!shown.canvas->hasFocus());
    QCOMPARE(shown.grab().pixelColor(above), QColor(40, 200, 40));
}

void InlineTextDrawTests::textPastTheBoxShowsAPlus()
{
    DrawnText shown;
    shown.session.beginText(QRectF(100, 80, 120, 60));
    const QPointF corner(120, 60);
    QCOMPARE(shown.grab().pixelColor(shown.onScreen(corner)), QColor(Qt::white));
    QTest::keyClicks(shown.canvas, QStringLiteral("more words than the box holds"));
    QVERIFY(shown.grab().pixelColor(shown.onScreen(corner)).lightness() < 100);
}

void InlineTextDrawTests::aFreshDraftTakesTheKeysButSparesAField()
{
    DrawnText shown;
    auto *field = new QLineEdit(&shown.window);
    auto *button = new QPushButton(&shown.window);
    field->setGeometry(0, 0, 10, 10);
    button->setGeometry(20, 0, 10, 10);
    field->show();
    button->show();
    field->setFocus();
    QTRY_VERIFY(field->hasFocus());
    beginTextAt(shown.session, QPointF(20, 30));
    QTest::qWait(50);
    QVERIFY(field->hasFocus());
    shown.session.cancelText();
    button->setFocus();
    QTRY_VERIFY(button->hasFocus());
    beginTextAt(shown.session, QPointF(20, 30));
    QTRY_VERIFY(shown.canvas->hasFocus());
    // A focus request leaves open text's keys alone.
    button->setFocus();
    QTRY_VERIFY(button->hasFocus());
    shown.canvas->consumeFocusRequest(7);
    QTest::qWait(50);
    QVERIFY(button->hasFocus());
    shown.session.cancelText();
    shown.canvas->consumeFocusRequest(8);
    QTRY_VERIFY(shown.canvas->hasFocus());
}

void InlineTextDrawTests::theDraftTakesItsKeysAheadOfShortcuts()
{
    DrawnText shown;
    auto *selectAll = new QAction(&shown.window);
    selectAll->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_A));
    shown.window.addAction(selectAll);
    int triggered = 0;
    QObject::connect(selectAll, &QAction::triggered, [&] { ++triggered; });
    shown.canvas->setFocus();
    QTRY_VERIFY(shown.canvas->hasFocus());
    QTest::keyClick(shown.canvas, Qt::Key_A, Qt::ControlModifier);
    QCOMPARE(triggered, 1);
    beginTextAt(shown.session, QPointF(20, 30));
    QTest::keyClicks(shown.canvas, QStringLiteral("all"));
    QTest::keyClick(shown.canvas, Qt::Key_A, Qt::ControlModifier);
    QCOMPARE(triggered, 1);
    QCOMPARE(shown.editor().anchor(), 0);
    QCOMPARE(shown.editor().caretPosition(), 3);
    // Shift-Tab keeps the keys where the text is.
    auto *next = new QPushButton(&shown.window);
    next->show();
    QTest::keyClick(shown.canvas, Qt::Key_Backtab, Qt::ShiftModifier);
    QVERIFY(shown.canvas->hasFocus());
    QCOMPARE(shown.session.textDraft().value().style.content, QString("all"));
    // So do its other keys, and Backspace over the fills.
    int stolen = 0;
    const auto steal = [&](const QList<QKeySequence> &keys) {
        auto *action = new QAction(&shown.window);
        action->setShortcuts(keys);
        shown.window.addAction(action);
        QObject::connect(action, &QAction::triggered, [&] { ++stolen; });
    };
    for (const QKeySequence::StandardKey key : {QKeySequence::Undo, QKeySequence::Redo, QKeySequence::Copy, QKeySequence::Cut, QKeySequence::Paste})
        steal(QKeySequence::keyBindings(key));
    steal({QKeySequence(Qt::ALT | Qt::Key_Backspace), QKeySequence(Qt::CTRL | Qt::Key_Backspace), QKeySequence(Qt::SHIFT | Qt::Key_Backspace)});
    QTest::keyClick(shown.canvas, Qt::Key_End);
    QTest::keyClick(shown.canvas, Qt::Key_Left, Qt::ShiftModifier);
    QTest::keyClick(shown.canvas, Qt::Key_C, Qt::ControlModifier);
    QTest::keyClick(shown.canvas, Qt::Key_X, Qt::ControlModifier);
    QTest::keyClick(shown.canvas, Qt::Key_V, Qt::ControlModifier);
    QTest::keyClick(shown.canvas, Qt::Key_Z, Qt::ControlModifier);
    QTest::keyClick(shown.canvas, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(shown.session.textDraft().value().style.content, QString("all"));
    QTest::keyClick(shown.canvas, Qt::Key_Backspace, Qt::ShiftModifier);
    QTest::keyClick(shown.canvas, Qt::Key_Backspace, Qt::AltModifier);
    QTest::keyClick(shown.canvas, Qt::Key_Backspace, Qt::ControlModifier);
    QCOMPARE(shown.session.textDraft().value().style.content, QString());
    QCOMPARE(stolen, 0);
}

void InlineTextDrawTests::theEditMenuReachesOpenText()
{
    Bar bar;
    bar.window.show();
    QVERIFY(QTest::qWaitForWindowActive(&bar.window));
    EditorSession &session = bar.session();
    session.createDocument(400, 300, true);
    session.selectTool(NavigationTool::type);
    beginTextAt(session, QPointF(20, 30));
    auto *canvas = bar.window.findChild<CanvasView *>();
    QTRY_VERIFY(canvas->hasFocus());
    QTest::keyClicks(canvas, QStringLiteral("menu"));
    const auto content = [&] { return session.textDraft().value().style.content; };
    // Swift's text branch: bare entries, enabled, the text view's.
    QCOMPARE(bar.action("undo").text(), QString("Undo"));
    QVERIFY(bar.action("undo").isEnabled() && bar.action("redo").isEnabled());
    bar.action("undo").trigger();
    QCOMPARE(content(), QString());
    bar.action("redo").trigger();
    QCOMPARE(content(), QString("menu"));
    bar.action("selectAll").trigger();
    QCOMPARE(canvas->inlineTextEditor()->anchor(), 0);
    QVERIFY(!session.selection());
    bar.action("copy").trigger();
    QCOMPARE(QGuiApplication::clipboard()->text(), QString("menu"));
    bar.action("cut").trigger();
    QCOMPARE(content(), QString());
    bar.action("paste").trigger();
    bar.action("paste").trigger();
    QCOMPARE(content(), QString("menumenu"));
    // Closed, the entries carry the step's name again.
    QVERIFY(session.finishText());
    QCOMPARE(bar.action("undo").text(), QString("Undo New Text Layer"));
}

void InlineTextDrawTests::editsRepaintTheEditor()
{
    DrawnText shown;
    // A field keeps the keys: no blink repaints the edits.
    auto *field = new QLineEdit(&shown.window);
    field->show();
    field->setFocus();
    QTRY_VERIFY(field->hasFocus());
    beginTextAt(shown.session, QPointF(20, 30));
    QTest::qWait(50);
    PaintSpy spy(*shown.canvas);
    QTest::keyClicks(shown.canvas, QStringLiteral("Wide words"));
    QTRY_VERIFY(spy.painted.contains(shown.editor().drawnRect().toAlignedRect() & shown.canvas->rect()));
    // Closing new text repaints where it stood.
    const QRect stood = shown.editor().drawnRect().toAlignedRect() & shown.canvas->rect();
    spy.painted = QRect();
    shown.session.cancelText();
    QTRY_VERIFY(spy.painted.contains(stood));
    // A paste grows the box past its old margins.
    beginTextAt(shown.session, QPointF(20, 30));
    QTest::qWait(50);
    QVERIFY(!shown.canvas->findChild<QTimer *>(QStringLiteral("caretBlink"))->isActive());
    spy.painted = QRect();
    QGuiApplication::clipboard()->setText(QStringLiteral("iiiiiiiiiiiiiiii"));
    QTest::keyClick(shown.canvas, Qt::Key_V, Qt::ControlModifier);
    QTRY_VERIFY(spy.painted.contains(shown.editor().drawnRect().toAlignedRect() & shown.canvas->rect()));
    shown.session.cancelText();
    // A layer whose text shrank shows whole again when left.
    const QUuid id = shown.text(QPointF(20, 30), QStringLiteral("Hi you all"));
    const QPoint right = layerWith(shown.session, id).transform.point(QPointF(0.95, 0.5)).toPoint();
    const auto shrink = [&] {
        shown.session.editActiveText();
        QTest::keyClick(shown.canvas, Qt::Key_End);
        QTest::keyClick(shown.canvas, Qt::Key_Backspace, Qt::AltModifier);
        QTest::keyClick(shown.canvas, Qt::Key_Backspace, Qt::AltModifier);
        QVERIFY(!shown.editor().drawnRect().contains(right));
        QTest::qWait(50);
        spy.painted = QRect();
    };
    shrink();
    shown.session.cancelText();
    QTRY_VERIFY(spy.painted.contains(right));
    // So does one whose draft another replaces.
    shrink();
    TextDraft other = shown.session.textDraft().value();
    other.id = QUuid::createUuid();
    other.layerID = std::nullopt;
    other.transform = std::nullopt;
    other.origin = QPointF(20, 200);
    shown.session.setTextDraft(other);
    QTRY_VERIFY(spy.painted.contains(right));
    // The caret's line after a final newline falls far below.
    shown.session.cancelText();
    beginTextAt(shown.session, QPointF(20, 20));
    shown.session.changeTextStyle([](LayerTextStyle &style) {
        style.fontSize = 24;
        style.leading = 120;
    });
    QTest::keyClicks(shown.canvas, QStringLiteral("A"));
    QTest::qWait(50);
    spy.painted = QRect();
    QTest::keyClick(shown.canvas, Qt::Key_Return);
    const QPoint below = shown.editor().textTransform().map(QPointF(LayerTextStyle::padding, LayerTextStyle::padding + 180)).toPoint();
    QVERIFY(below.y() > shown.editor().textTransform().map(QPointF(0, shown.editor().logicalSize().height())).y() + 40);
    QTRY_VERIFY(spy.painted.contains(below));
}

void InlineTextDrawTests::openingTextRepaintsItsWholeLayer()
{
    DrawnText shown;
    beginTextAt(shown.session, QPointF(20, 20));
    shown.session.changeTextStyle([](LayerTextStyle &style) { style.fontSize = 24; });
    QTest::keyClicks(shown.canvas, QStringLiteral("Tall"));
    QVERIFY(shown.session.finishText());
    const QUuid id = shown.session.activeLayerID().value();
    const QSize pixels = layerWith(shown.session, id).asset.value().size();
    rewrite(shown.session, [&](ProjectSnapshot &snapshot) {
        record(snapshot, id).transform = LayerTransform{.origin = {20, 20}, .size = QSizeF(pixels.width(), pixels.height() * 5)};
    });
    // Point text shows at the width's scale; the rest goes.
    const QPoint low = layerWith(shown.session, id).transform.point(QPointF(0.5, 0.9)).toPoint();
    QTest::qWait(50);
    PaintSpy spy(*shown.canvas);
    shown.session.editActiveText();
    QVERIFY(!shown.editor().drawnRect().contains(low));
    QTRY_VERIFY(spy.painted.contains(low));
}

QTEST_MAIN(InlineTextDrawTests)
#include "InlineTextDrawTests.moc"
