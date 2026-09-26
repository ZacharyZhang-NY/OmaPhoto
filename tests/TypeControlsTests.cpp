#include "TypeControlsFixtures.h"
#include "Rendering/TextLayout.h"
#include "UI/LayerIcons.h"
#include "UI/NativeLayerList.h"
#include "UI/ToolIcons.h"
#include "UI/TypeControls.h"
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QtTest>

// The Type bar and the panel's text rows.
namespace {
// The first and last inked columns of an icon's row.
std::pair<int, int> inkedColumns(const QToolButton &button, int row)
{
    const QImage image = button.icon().pixmap(QSize(18, 18), 1).toImage();
    int first = -1, last = -1;
    for (int x = 0; x < image.width(); ++x) {
        if (qAlpha(image.pixel(x, row)) > 64) {
            first = first < 0 ? x : first;
            last = x;
        }
    }
    return {first, last};
}
}

class TypeControlsTests : public QObject {
    Q_OBJECT
private slots:
    void theBarShowsTheStyleItEdits();
    void sizeAndTrackingTakeWhatTheyCanRead();
    void sizeAndTrackingFollowTheLocale();
    void leadingIsAutoWhenEmptyAndClamps();
    void fieldsKeepTypingAndTheFocus();
    void alignmentChangesTheDraft();
    void theButtonsFollowTheDraft();
    void theBarRestsWithoutADocumentOrWhileBusy();
    void aTextRowShowsAndOpensItsText();
};

void TypeControlsTests::theBarShowsTheStyleItEdits()
{
    Bar shown;
    QCOMPARE(shown.bar.title->text(), QString("Type"));
    auto &font = find<QComboBox>(shown.bar, "typeFont");
    QCOMPARE(font.width(), 210);
    QCOMPARE(font.toolTip(), QString("Font face, including bold and italic variants"));
    QCOMPARE(font.accessibleName(), QString("Font"));
    // The catalog waits for the menu: the style's face alone.
    QCOMPARE(font.count(), 1);
    QCOMPARE(font.currentText(), QString("Helvetica"));
    shown.restyle([](LayerTextStyle &style) { style.fontName = QStringLiteral("Aachen"); });
    QCOMPARE(font.count(), 2);
    font.showPopup();
    font.hidePopup();
    // Every face, and the style's own though missing, sorted.
    QStringList names = TextLayout::availableFonts();
    names << QStringLiteral("Aachen");
    names.sort();
    QStringList listed;
    for (int index = 0; index < font.count(); ++index)
        listed << font.itemText(index);
    QCOMPARE(listed, names);
    QCOMPARE(font.currentText(), QString("Aachen"));
    QCOMPARE(font.itemText(0), QString("Aachen"));
    shown.restyle([](LayerTextStyle &style) { style.fontName = QStringLiteral("Helvetica"); });
    TextStyleField &size = shown.field("typeSize"), &tracking = shown.field("typeTracking"), &leading = shown.field("typeLeading");
    QVERIFY(size.width() == 52 && tracking.width() == 45 && leading.width() == 52);
    QCOMPARE(size.text(), QString("72"));
    QCOMPARE(tracking.text(), QString("0"));
    QCOMPARE(leading.text(), QString());
    QCOMPARE(leading.placeholderText(), QString("Auto"));
    QCOMPARE(leading.toolTip(), QString("Line height, baseline to baseline. Empty or 0 is Auto: 120% of the font size."));
    QVERIFY(size.alignment().testFlag(Qt::AlignRight));
    QStringList labels;
    for (const QLabel *label : shown.bar.findChildren<QLabel *>())
        labels << label->text();
    labels.sort();
    QCOMPARE(labels, (QStringList{"Leading", "Tracking", "Type", "px"}));
    QToolButton &left = shown.align("typeAlignLeft"), &center = shown.align("typeAlignCenter"), &right = shown.align("typeAlignRight");
    QCOMPARE(left.toolTip(), QString("Align left"));
    QCOMPARE(center.accessibleName(), QString("Align center"));
    QCOMPARE(right.toolTip(), QString("Align right"));
    QVERIFY(left.size() == QSize(30, 26) && left.autoRaise() && left.parentWidget()->layout()->spacing() == 2);
    QVERIFY(left.isChecked() && !center.isChecked() && !right.isChecked());
    // Four lines each; the short ones show the side.
    const auto [leftStart, leftEnd] = inkedColumns(left, 7);
    const auto [centreStart, centreEnd] = inkedColumns(center, 7);
    const auto [rightStart, rightEnd] = inkedColumns(right, 7);
    QVERIFY(leftStart == 1 && leftEnd == 11 && centreStart == 4 && centreEnd == 13 && rightStart == 6 && rightEnd == 16);
    QCOMPARE(inkedColumns(center, 4), (std::pair{1, 16}));
    QCOMPARE(inkedColumns(left, 14), (std::pair{1, 11}));
    QCOMPARE(inkedColumns(left, 12), (std::pair{-1, -1}));
    const QColor glyph = left.icon().pixmap(QSize(18, 18), 1).toImage().pixelColor(9, 4);
    const QColor ink = shown.bar.palette().color(QPalette::WindowText);
    QVERIFY(std::abs(glyph.red() - ink.red()) <= 2 && std::abs(glyph.green() - ink.green()) <= 2 && std::abs(glyph.blue() - ink.blue()) <= 2);
    // A new theme redraws them in its own ink.
    const QPalette before = QApplication::palette();
    QPalette themed = before;
    themed.setColor(QPalette::WindowText, QColor(200, 10, 30));
    QApplication::setPalette(themed);
    const auto inked = [&] {
        const QColor now = center.icon().pixmap(QSize(18, 18), 1).toImage().pixelColor(9, 4);
        return std::abs(now.red() - 200) <= 2 && std::abs(now.green() - 10) <= 2 && std::abs(now.blue() - 30) <= 2;
    };
    QTRY_VERIFY(inked());
    QApplication::setPalette(before);
    // Swift's order and spacing, scrolling without bars.
    auto &scroll = find<QScrollArea>(shown.bar, "typeScroll");
    QWidget &fields = find<QWidget>(shown.bar, "typeFields");
    QVERIFY(scroll.widget() == &fields && scroll.widgetResizable() && scroll.frameShape() == QFrame::NoFrame);
    QVERIFY(scroll.horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff && scroll.verticalScrollBarPolicy() == Qt::ScrollBarAlwaysOff);
    QCOMPARE(scroll.height(), fields.sizeHint().height());
    QCOMPARE(fields.layout()->spacing(), 10);
    QVERIFY(font.x() < size.x() && size.x() < left.parentWidget()->x() && left.parentWidget()->x() < tracking.x() && tracking.x() < leading.x());
    // The defaults, then a text layer's own style.
    const QUuid blank = shown.session.activeLayerID().value();
    shown.restyle([](LayerTextStyle &style) {
        style.fontSize = 30;
        style.alignment = TextAlignment::right;
    });
    QCOMPARE(size.text(), QString("30"));
    QVERIFY(right.isChecked() && !left.isChecked());
    const QUuid text = addText(shown.session, QStringLiteral("Styled"));
    shown.session.selectLayer(blank);
    shown.restyle([](LayerTextStyle &style) {
        style.fontSize = 50;
        style.tracking = 2.5;
        style.leading = 90.4;
        style.alignment = TextAlignment::center;
    });
    QVERIFY(size.text() == QString("50") && tracking.text() == QString("2.5") && leading.text() == QString("90"));
    QVERIFY(center.isChecked());
    shown.session.selectLayer(text);
    QVERIFY(size.text() == QString("30") && tracking.text() == QString("0") && leading.text() == QString());
    QVERIFY(right.isChecked() && !center.isChecked());
    // A draft shows its own style.
    shown.session.editActiveText();
    shown.restyle([](LayerTextStyle &style) { style.fontSize = 44.25; });
    QCOMPARE(size.text(), QString("44.25"));
    shown.restyle([](LayerTextStyle &style) { style.fontSize = 1234.5678; });
    QCOMPARE(size.text(), QString("1234.568"));
    shown.restyle([](LayerTextStyle &style) { style.fontSize = 40.0004; });
    QCOMPARE(size.text(), QString("40"));
    QCOMPARE(shown.session.activeLayer().value().liveText().value().style.fontSize, 30.0);
}

void TypeControlsTests::sizeAndTrackingTakeWhatTheyCanRead()
{
    Bar shown;
    TextStyleField &size = shown.field("typeSize"), &tracking = shown.field("typeTracking");
    shown.type(size, "48");
    QCOMPARE(shown.session.currentTextStyle().fontSize, 48.0);
    // A size out of range, or no number, changes nothing.
    shown.type(size, "0");
    QCOMPARE(size.text(), QString("0"));
    QCOMPARE(shown.session.currentTextStyle().fontSize, 48.0);
    shown.type(size, "2500");
    QCOMPARE(shown.session.currentTextStyle().fontSize, 250.0);
    shown.type(size, "x");
    QCOMPARE(shown.session.currentTextStyle().fontSize, 250.0);
    shown.type(size, "12.5");
    QCOMPARE(shown.session.currentTextStyle().fontSize, 12.5);
    // Arrows step one, Shift ten, within 1 to 2000.
    QTest::keyClick(&size, Qt::Key_Up);
    QCOMPARE(shown.session.currentTextStyle().fontSize, 13.5);
    QCOMPARE(size.text(), QString("13.5"));
    QTest::keyClick(&size, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(shown.session.currentTextStyle().fontSize, 3.5);
    QTest::keyClick(&size, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(shown.session.currentTextStyle().fontSize, 1.0);
    QCOMPARE(size.text(), QString("1"));
    shown.restyle([](LayerTextStyle &style) { style.fontSize = 1995; });
    QTest::keyClick(&size, Qt::Key_Up, Qt::ShiftModifier);
    QCOMPARE(shown.session.currentTextStyle().fontSize, 2000.0);
    QCOMPARE(size.text(), QString("2000"));
    // Tracking takes any readable number the style allows.
    shown.type(tracking, "-3");
    QCOMPARE(shown.session.currentTextStyle().tracking, -3.0);
    QTest::keyClick(&tracking, Qt::Key_Up);
    QCOMPARE(shown.session.currentTextStyle().tracking, -2.0);
    QTest::keyClick(&tracking, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(shown.session.currentTextStyle().tracking, -12.0);
    QCOMPARE(tracking.text(), QString("-12"));
    shown.type(tracking, "-150");
    QCOMPARE(shown.session.currentTextStyle().tracking, -15.0);
    shown.type(tracking, "abc");
    QCOMPARE(shown.session.currentTextStyle().tracking, -15.0);
    shown.restyle([](LayerTextStyle &style) { style.tracking = 995; });
    QTest::keyClick(&tracking, Qt::Key_Up, Qt::ShiftModifier);
    QCOMPARE(shown.session.currentTextStyle().tracking, 995.0);
    QCOMPARE(tracking.text(), QString("995"));
}

void TypeControlsTests::sizeAndTrackingFollowTheLocale()
{
    // Swift's `.number` fields read and write the user's locale.
    Bar shown;
    shown.bar.setLocale(QLocale(QLocale::German, QLocale::Germany));
    shown.restyle([](LayerTextStyle &style) {
        style.fontSize = 1212.5;
        style.tracking = -0.25;
    });
    TextStyleField &size = shown.field("typeSize"), &tracking = shown.field("typeTracking"), &leading = shown.field("typeLeading");
    size.sync();
    tracking.sync();
    QCOMPARE(size.text(), QString("1212,5"));
    QCOMPARE(tracking.text(), QString("-0,25"));
    shown.restyle([](LayerTextStyle &style) { style.fontSize = 12; });
    size.sync();
    QCOMPARE(size.text(), QString("12"));
    shown.type(size, "14,25");
    QCOMPARE(shown.session.currentTextStyle().fontSize, 14.25);
    shown.type(tracking, "1,5");
    QCOMPARE(shown.session.currentTextStyle().tracking, 1.5);
    // Leading reads as Swift's Double(text), whatever the locale.
    shown.type(leading, "20,5");
    QCOMPARE(shown.session.currentTextStyle().leading, 0.0);
    shown.type(leading, "20.5");
    QCOMPARE(shown.session.currentTextStyle().leading, 20.5);
}

void TypeControlsTests::leadingIsAutoWhenEmptyAndClamps()
{
    Bar shown;
    TextStyleField &leading = shown.field("typeLeading");
    shown.type(leading, "90");
    QCOMPARE(shown.session.currentTextStyle().leading, 90.0);
    // Past the bounds it clamps; no number is Auto.
    shown.type(leading, "9000");
    QCOMPARE(shown.session.currentTextStyle().leading, 5000.0);
    // One edit makes "-7": below zero clamps to Auto.
    shown.type(leading, "7");
    QTest::keyClick(&leading, Qt::Key_Home);
    QTest::keyClicks(&leading, "-");
    QCOMPARE(shown.session.currentTextStyle().leading, 0.0);
    shown.type(leading, " 64 ");
    QCOMPARE(shown.session.currentTextStyle().leading, 64.0);
    shown.type(leading, "x");
    QCOMPARE(shown.session.currentTextStyle().leading, 0.0);
    // Steps count from the height Auto works out to.
    QTest::keyClick(&leading, Qt::Key_Up);
    QCOMPARE(shown.session.currentTextStyle().leading, 72 * 1.2 + 1);
    QCOMPARE(leading.text(), QString("87"));
    QTest::keyClick(&leading, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(shown.session.currentTextStyle().leading, 72 * 1.2 - 9);
    QCOMPARE(leading.text(), QString("77"));
    shown.restyle([](LayerTextStyle &style) { style.leading = 5; });
    QTest::keyClick(&leading, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(shown.session.currentTextStyle().leading, 0.0);
    QCOMPARE(leading.text(), QString());
    // Unfocused, it shows the leading rounded.
    leading.clearFocus();
    shown.restyle([](LayerTextStyle &style) { style.leading = 90.5; });
    QCOMPARE(leading.text(), QString("91"));
}

void TypeControlsTests::fieldsKeepTypingAndTheFocus()
{
    Bar shown;
    TextStyleField &size = shown.field("typeSize"), &tracking = shown.field("typeTracking");
    // Focused, a field keeps its typing through changes.
    shown.type(size, "4");
    QTest::keyClicks(&size, ".");
    shown.restyle([](LayerTextStyle &style) { style.tracking = 7; });
    QCOMPARE(size.text(), QString("4."));
    QCOMPARE(tracking.text(), QString("7"));
    // Leaving shows the session's number.
    tracking.setFocus(Qt::OtherFocusReason);
    QCOMPARE(size.text(), QString("4"));
    // A menu or popup borrows the focus: the typing stays.
    for (const Qt::FocusReason reason : {Qt::MenuBarFocusReason, Qt::PopupFocusReason}) {
        shown.type(size, "3.");
        tracking.setFocus(reason);
        QVERIFY(!size.hasFocus());
        shown.restyle([](LayerTextStyle &style) { style.fontSize = 40; });
        QCOMPARE(size.text(), QString("3."));
        size.setFocus();
        QTRY_VERIFY(size.hasFocus());
        tracking.setFocus(Qt::OtherFocusReason);
        QCOMPARE(size.text(), QString("40"));
    }
    // Return shows the number, selected; Swift's bar keeps the focus.
    for (const Qt::Key key : {Qt::Key_Return, Qt::Key_Enter}) {
        shown.type(size, "2.");
        const int request = shown.session.canvasFocusRequest();
        QTest::keyClick(&size, key);
        QVERIFY(size.hasFocus() && size.selectedText() == QString("2"));
        QCOMPARE(shown.session.canvasFocusRequest(), request);
    }
    // Escape is the field's own: the typing stays.
    shown.type(size, "3.");
    QTest::keyClick(&size, Qt::Key_Escape);
    QVERIFY(size.hasFocus() && size.text() == QString("3."));
    // Other keys stay the field's own.
    shown.type(size, "15");
    QTest::keyClick(&size, Qt::Key_Backspace);
    QCOMPARE(size.text(), QString("1"));
    QCOMPARE(shown.session.currentTextStyle().fontSize, 1.0);
}

void TypeControlsTests::alignmentChangesTheDraft()
{
    Bar shown;
    QToolButton &left = shown.align("typeAlignLeft"), &center = shown.align("typeAlignCenter");
    // Without a draft the defaults change.
    center.click();
    QCOMPARE(shown.session.currentTextStyle().alignment, TextAlignment::center);
    QVERIFY(!shown.session.textDraft());
    QVERIFY(center.isChecked() && !left.isChecked());
    shown.session.beginText(QPointF(10, 10), true);
    left.click();
    QCOMPARE(shown.session.textDraft().value().style.alignment, TextAlignment::left);
    QVERIFY(left.isChecked() && !center.isChecked());
    // Editing an existing text layer opens its draft.
    shown.session.cancelText();
    const QUuid text = addText(shown.session, QStringLiteral("Aligned"));
    QVERIFY(!shown.session.textDraft());
    center.click();
    QCOMPARE(shown.session.textDraft().value().layerID, std::optional(text));
    QCOMPARE(shown.session.textDraft().value().style.alignment, TextAlignment::center);
}

void TypeControlsTests::theButtonsFollowTheDraft()
{
    Bar shown;
    QPushButton &cancel = shown.button("typeCancel"), &done = shown.button("typeDone"), &edit = shown.button("typeEdit");
    QVERIFY(!cancel.isVisible() && !done.isVisible() && edit.isVisible());
    QCOMPARE(edit.text(), QString("Edit Text"));
    QVERIFY(!edit.isEnabled());
    // The fields take the room; the buttons keep the end.
    QLabel &unit = *shown.bar.findChild<QWidget *>("typeFields")->findChild<QLabel *>();
    const int natural = unit.sizeHint().width();
    shown.bar.resize(1400, shown.bar.height());
    QTRY_VERIFY(edit.geometry().right() > 1400 - 40);
    QCOMPARE(unit.width(), natural);
    // Done on blank new text just closes it.
    shown.session.beginText(QPointF(20, 30), true);
    done.click();
    QVERIFY(!shown.session.textDraft() && !cancel.isVisible() && edit.isVisible());
    QCOMPARE(shown.session.document().value().layers.size(), size_t(1));
    shown.session.beginText(QPointF(20, 30), true);
    QVERIFY(cancel.isVisible() && done.isVisible() && !edit.isVisible());
    QVERIFY(cancel.text() == QString("Cancel") && done.text() == QString("Done"));
    // Done applies the draft as a new layer.
    shown.restyle([](LayerTextStyle &style) { style.content = QStringLiteral("Done"); });
    done.click();
    QVERIFY(!shown.session.textDraft());
    QCOMPARE(shown.session.history.undoName(), QString("New Text Layer"));
    const QUuid text = shown.session.activeLayerID().value();
    QCOMPARE(shown.session.activeLayer().value().liveText().value().style.content, QString("Done"));
    QVERIFY(edit.isVisible() && edit.isEnabled());
    // Edit Text opens the active layer from any tool.
    shown.session.selectTool(NavigationTool::move);
    edit.click();
    QCOMPARE(shown.session.tool(), NavigationTool::type);
    QCOMPARE(shown.session.textDraft().value().layerID, std::optional(text));
    // Cancel drops the draft; the layer stays as it was.
    shown.restyle([](LayerTextStyle &style) { style.content = QStringLiteral("Changed"); });
    cancel.click();
    QVERIFY(!shown.session.textDraft());
    QCOMPARE(shown.session.activeLayer().value().liveText().value().style.content, QString("Done"));
    QCOMPARE(shown.session.history.undoName(), QString("New Text Layer"));
    // Pixels drop the text: Edit Text rests.
    shown.session.selectTool(NavigationTool::brush);
    shown.session.beginBrush(QPointF(40, 60));
    shown.session.finishBrush();
    QVERIFY(!shown.session.activeLayer().value().liveText());
    QVERIFY(edit.isVisible() && !edit.isEnabled());
}

void TypeControlsTests::theBarRestsWithoutADocumentOrWhileBusy()
{
    EditorSession empty;
    const TypeControls idle(empty);
    QVERIFY(!idle.isEnabled());
    empty.createDocument(40, 20, true);
    QVERIFY(idle.isEnabled());
    Bar shown;
    // Busy dims the bar only once the busy indicator shows.
    shown.session.setIsProjectBusy(true);
    QVERIFY(shown.bar.isEnabled());
    QTRY_VERIFY(!shown.bar.isEnabled());
    shown.session.setIsProjectBusy(false);
    QVERIFY(shown.bar.isEnabled());
}

void TypeControlsTests::aTextRowShowsAndOpensItsText()
{
    EditorSession session;
    session.createDocument(400, 300, true);
    const QUuid blank = session.activeLayerID().value();
    const QUuid text = addText(session, QStringLiteral("Row"));
    NativeLayerList list(session);
    list.resize(252, 400);
    list.show();
    QVERIFY(QTest::qWaitForWindowActive(&list));
    LayerCell &textRow = *list.cells().at(0), &blankRow = *list.cells().at(1);
    const auto dimensions = [](LayerCell &row) { return row.findChild<QLabel *>("layerDimensions")->text(); };
    QCOMPARE(dimensions(textRow), QString("Text · Double-click to edit"));
    QCOMPARE(dimensions(blankRow), QString("400 × 300 px"));
    QCOMPARE(textRow.thumbnail().toolTip(), QString("Editable text layer"));
    QCOMPARE(textRow.thumbnail().accessibleName(), QString("Select text: Row"));
    QCOMPARE(blankRow.thumbnail().toolTip(), QString("Select image pixels"));
    QCOMPARE(blankRow.thumbnail().accessibleName(), QString("Select image: Layer 1"));
    // A square text icon, where pixels show the canvas.
    const QColor ink = textRow.palette().color(QPalette::WindowText);
    QCOMPARE(textRow.thumbnail().size(), QSize(36, 36));
    QCOMPARE(textRow.thumbnail().icon().availableSizes(), QList<QSize>{QSize(36, 36)});
    const QImage expected = LayerIcons::pixmap(LayerIcon::text, 36, ink, 1).toImage();
    QCOMPARE(textRow.thumbnail().icon().pixmap(QSize(36, 36), 1).toImage().convertToFormat(expected.format()), expected);
    // The rail's Type glyph, 1.2 times its size, centred.
    QImage glyph(36, 36, expected.format());
    glyph.fill(Qt::transparent);
    QPainter painter(&glyph);
    ToolIcons::paint(painter, NavigationTool::type, QPointF(7.2, 7.2), 18 * 1.2, ink);
    painter.end();
    QCOMPARE(expected, glyph);
    QCOMPARE(blankRow.thumbnail().size(), QSize(36, 27));
    // A double click on the thumbnail opens the text.
    session.selectLayer(blank);
    QTest::mouseDClick(&textRow.thumbnail(), Qt::LeftButton);
    QCOMPARE(session.activeLayerID(), std::optional(text));
    QCOMPARE(session.textDraft().value().layerID, std::optional(text));
    QVERIFY(!session.renamingLayerID());
    // With a draft open, rows refuse the gesture.
    QTest::mouseDClick(&blankRow.thumbnail(), Qt::LeftButton);
    QCOMPARE(session.activeLayerID(), std::optional(text));
    session.cancelText();
    // On the name a text layer renames, as every layer.
    QTest::mouseDClick(&textRow, Qt::LeftButton, Qt::NoModifier, QPoint(textRow.width() - 20, 20));
    QCOMPARE(session.renamingLayerID(), std::optional(text));
    QVERIFY(!session.textDraft());
    session.setRenamingLayerID(std::nullopt);
    // Text that loads as pixels shows them, same image.
    const ImageIdentity before = layerWith(session, text).asset.value().identity();
    rewrite(session, [&](ProjectSnapshot &snapshot) { record(snapshot, text).text.value().fontSize = 5000; });
    QCOMPARE(layerWith(session, text).asset.value().identity(), before);
    QCOMPARE(list.cells().at(0), &textRow);
    QVERIFY(dimensions(textRow).endsWith(" px"));
    QCOMPARE(textRow.thumbnail().toolTip(), QString("Select image pixels"));
    QCOMPARE(textRow.thumbnail().size(), QSize(36, 27));
    QCOMPARE(textRow.thumbnail().icon().availableSizes(), QList<QSize>{QSize(72, 54)});
    QTest::mouseDClick(&textRow.thumbnail(), Qt::LeftButton);
    QVERIFY(!session.textDraft());
    QCOMPARE(session.renamingLayerID(), std::optional(text));
    QTest::keyClick(textRow.findChild<QLineEdit *>("layerNameEditor"), Qt::Key_Escape);
    QVERIFY(!session.renamingLayerID());
}

QTEST_MAIN(TypeControlsTests)
#include "TypeControlsTests.moc"
