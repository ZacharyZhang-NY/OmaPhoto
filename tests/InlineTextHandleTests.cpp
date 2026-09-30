#include "InlineTextFixtures.h"
#include <QLineEdit>
#include <array>

// Open text's handles: cursors, resizing, limits, the padding.
namespace {
// A point of the box, in its own pixels.
QPointF at(TextCanvas &shown, double x, double y)
{
    return shown.editor().boxTransform().map(QPointF(x, y));
}

Qt::CursorShape hovered(TextCanvas &shown, QPointF point)
{
    shown.hover(point);
    return shown.canvas->cursor().shape();
}
}

class InlineTextHandleTests : public QObject {
    Q_OBJECT
private slots:
    void handlesFollowTheBoxEdges();
    void handlesResizeTheBox();
    void aTurnedBoxResizesAlongItsEdges();
    void aScaledLayerKeepsItsScale();
    void thePaddingTakesNoClicks();
    void aLostReleaseEndsAResize();
    void anEditEndsAResize();
    void aSquashedLayerTakesFarDrags();
    void theIBeamHoldsAcrossTheCanvas();
};

void InlineTextHandleTests::handlesFollowTheBoxEdges()
{
    TextCanvas shown;
    shown.type(QStringLiteral("Hi"));
    const QSizeF size = shown.editor().logicalSize();
    // Swift's frame positions, corner by corner round the box.
    const std::array<Qt::CursorShape, 8> shapes{Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor, Qt::SizeHorCursor,
                                                Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor, Qt::SizeHorCursor};
    for (size_t handle = 0; handle < shapes.size(); ++handle) {
        const QPointF unit = LayerTransform::handles[handle];
        QCOMPARE(hovered(shown, at(shown, unit.x() * size.width(), unit.y() * size.height())), shapes[handle]);
    }
    // Bands ten points either side of each edge; inside, text.
    const double width = size.width(), middle = size.height() / 2;
    QCOMPARE(hovered(shown, at(shown, width / 2, middle)), Qt::IBeamCursor);
    QCOMPARE(hovered(shown, at(shown, width - 9, middle)), Qt::SizeHorCursor);
    QCOMPARE(hovered(shown, at(shown, width - 11, middle)), Qt::IBeamCursor);
    QCOMPARE(hovered(shown, at(shown, width + 9, middle)), Qt::SizeHorCursor);
    QCOMPARE(hovered(shown, at(shown, width + 11, middle)), Qt::IBeamCursor);
    QCOMPARE(hovered(shown, at(shown, width / 2, 9)), Qt::SizeVerCursor);
    QCOMPARE(hovered(shown, at(shown, width / 2, -9)), Qt::SizeVerCursor);
    QCOMPARE(hovered(shown, at(shown, width / 2, -11)), Qt::IBeamCursor);
    // A press shows its handle's cursor before any hover.
    QCOMPARE(hovered(shown, at(shown, width / 2, middle)), Qt::IBeamCursor);
    const QPointF right = at(shown, width, middle);
    shown.press(right);
    QCOMPARE(shown.canvas->cursor().shape(), Qt::SizeHorCursor);
    // The release shows what lies under the pointer.
    shown.move(right - QPointF(500, 0));
    shown.release(right - QPointF(500, 0));
    QCOMPARE(shown.canvas->cursor().shape(), Qt::IBeamCursor);
    // A narrow box keeps a third of itself for text.
    QCOMPARE(shown.editor().logicalSize().width(), 16.0);
    QCOMPARE(hovered(shown, at(shown, 8, 0)), Qt::SizeVerCursor);
    QCOMPARE(hovered(shown, at(shown, 8, middle)), Qt::IBeamCursor);
}

void InlineTextHandleTests::handlesResizeTheBox()
{
    TextCanvas shown;
    shown.type(QStringLiteral("Hi"));
    const QSizeF size = shown.editor().logicalSize();
    const QPointF right = at(shown, size.width(), size.height() / 2);
    // A press alone makes point text a box its size.
    shown.click(right);
    QCOMPARE(shown.session.textDraft().value().style.boxSize, std::optional(size));
    QCOMPARE(shown.session.textDraft().value().transform, std::optional(LayerTransform{.origin = QPointF(20, 30), .size = size}));
    // Dragged, the edge follows and the far edge stays.
    shown.drag(right, right + QPointF(40, 0));
    const QSizeF wider(size.width() + 40, size.height());
    QCOMPARE(shown.session.textDraft().value().style.boxSize, std::optional(wider));
    QCOMPARE(shown.session.textDraft().value().transform, std::optional(LayerTransform{.origin = QPointF(20, 30), .size = wider}));
    QCOMPARE(shown.session.textDraft().value().style.content, QString("Hi"));
    // Past the far edge a side stops at 16 pixels.
    const QPointF left = at(shown, 0, size.height() / 2);
    shown.drag(left, left + QPointF(500, 0));
    QCOMPARE(shown.session.textDraft().value().style.boxSize, std::optional(QSizeF(16, size.height())));
    QCOMPARE(shown.session.textDraft().value().transform.value().origin, QPointF(20 + wider.width() - 16, 30));
    const QPointF top = at(shown, 8, 0);
    shown.drag(top, top + QPointF(0, 500));
    QCOMPARE(shown.session.textDraft().value().style.boxSize, std::optional(QSizeF(16, 16)));
    QCOMPARE(shown.session.textDraft().value().transform.value().origin, QPointF(20 + wider.width() - 16, 30 + size.height() - 16));
    const QPointF bottom = at(shown, 8, 16);
    shown.drag(bottom, bottom + QPointF(0, 40));
    QCOMPARE(shown.session.textDraft().value().style.boxSize, std::optional(QSizeF(16, 56)));
    shown.drag(bottom + QPointF(0, 40), bottom - QPointF(0, 500));
    QCOMPARE(shown.session.textDraft().value().style.boxSize, std::optional(QSizeF(16, 16)));
    // Past 30,000 pixels the box stays as it was.
    const QPointF edge = at(shown, 16, 8);
    shown.drag(edge, edge + QPointF(40'000, 0));
    QCOMPARE(shown.session.textDraft().value().style.boxSize, std::optional(QSizeF(16, 16)));
}

void InlineTextHandleTests::aTurnedBoxResizesAlongItsEdges()
{
    TextCanvas shown;
    shown.type(QStringLiteral("Hi"));
    QVERIFY(shown.session.finishText());
    const QUuid id = shown.session.activeLayerID().value();
    rewrite(shown.session, [&](ProjectSnapshot &snapshot) { record(snapshot, id).transform.rotation = 90; });
    // The install fits the view; one point a pixel again.
    shown.session.zoom(1);
    shown.session.editActiveText();
    const QSizeF size = shown.editor().logicalSize();
    // Turned a quarter, the right edge faces down.
    const QPointF right = at(shown, size.width(), size.height() / 2);
    QCOMPARE(hovered(shown, right), Qt::SizeVerCursor);
    shown.drag(right, right + QPointF(0, 40));
    QCOMPARE(shown.session.textDraft().value().style.boxSize, std::optional(QSizeF(size.width() + 40, size.height())));
    // And the bottom edge faces left.
    const QPointF bottom = at(shown, size.width() / 2, size.height());
    QCOMPARE(hovered(shown, bottom), Qt::SizeHorCursor);
    shown.drag(bottom, bottom - QPointF(40, 0));
    QCOMPARE(shown.session.textDraft().value().style.boxSize, std::optional(QSizeF(size.width() + 40, size.height() + 40)));
    // A turn past int's range counts its rest: 280 degrees.
    QVERIFY(shown.session.finishText());
    rewrite(shown.session, [&](ProjectSnapshot &snapshot) { record(snapshot, id).transform.rotation = 1e12; });
    shown.session.zoom(1);
    shown.session.editActiveText();
    QCOMPARE(hovered(shown, at(shown, 0, 0)), Qt::SizeBDiagCursor);
}

void InlineTextHandleTests::aScaledLayerKeepsItsScale()
{
    TextCanvas shown;
    shown.type(QStringLiteral("Hi"));
    QVERIFY(shown.session.finishText());
    const QUuid id = shown.session.activeLayerID().value();
    rewrite(shown.session, [&](ProjectSnapshot &snapshot) { record(snapshot, id).transform.size *= 20; });
    shown.session.zoom(1);
    shown.session.editActiveText();
    shown.key(Qt::Key_End);
    shown.type(QStringLiteral("!"));
    // Point text grows at its layer's scale; presses keep it.
    const QSizeF size = shown.editor().logicalSize();
    const LayerTransform grown = shown.editor().shownTransform();
    QCOMPARE(grown.size, size * 20);
    const QPointF right = at(shown, size.width(), size.height() / 2);
    shown.press(right);
    QCOMPARE(shown.session.textDraft().value().transform, std::optional(grown));
    // A valid box whose layer would pass 300,000 pixels stays.
    shown.move(right + QPointF(300'000, 0));
    shown.release(right + QPointF(300'000, 0));
    QCOMPARE(shown.session.textDraft().value().style.boxSize, std::optional(size));
    QCOMPARE(shown.session.textDraft().value().transform, std::optional(grown));
}

void InlineTextHandleTests::thePaddingTakesNoClicks()
{
    TextCanvas shown;
    shown.session.cancelText();
    shown.session.zoom(2);
    shown.session.beginText(QPointF(190, 140), true);
    shown.type(QStringLiteral("Pad"));
    const double middle = shown.editor().logicalSize().height() / 2;
    const QPointF padding = at(shown, 6, middle), letters = at(shown, LayerTextStyle::padding + 1, middle);
    QVERIFY(shown.canvas->rect().contains(padding.toPoint()));
    // Zoomed in, a click in the padding does nothing.
    shown.click(padding);
    QCOMPARE(shown.caret(), 3);
    QCOMPARE(shown.session.textDraft().value().style.content, QString("Pad"));
    // Nor a drag from it, after a click on letters.
    shown.click(letters);
    QCOMPARE(shown.caret(), 0);
    shown.key(Qt::Key_End);
    shown.drag(padding, letters);
    QCOMPARE(shown.selection(), (TextRange{3, 3}));
}

void InlineTextHandleTests::aLostReleaseEndsAResize()
{
    TextCanvas shown;
    shown.type(QStringLiteral("Hi"));
    const QSizeF size = shown.editor().logicalSize();
    const QPointF right = at(shown, size.width(), size.height() / 2);
    // A hover without the button ends it, and its cursor.
    shown.press(right);
    shown.move(right + QPointF(40, 0));
    QCOMPARE(hovered(shown, at(shown, size.width() / 2, size.height() / 2)), Qt::IBeamCursor);
    // So does a press: a padding drag then does nothing.
    const QSizeF held = shown.session.textDraft().value().style.boxSize.value();
    const QPointF edge = at(shown, held.width(), size.height() / 2);
    shown.press(edge);
    shown.move(edge + QPointF(20, 0));
    const QSizeF wider = shown.session.textDraft().value().style.boxSize.value();
    QCOMPARE(wider, QSizeF(held.width() + 20, held.height()));
    const QPointF padding = at(shown, wider.width() / 2, 11);
    shown.press(padding);
    shown.move(padding + QPointF(30, 0));
    QCOMPARE(shown.session.textDraft().value().style.boxSize, std::optional(wider));
    shown.release(padding + QPointF(30, 0));
    // And a lost focus, the cursor at once.
    const QPointF end = at(shown, wider.width(), size.height() / 2);
    shown.press(end);
    shown.move(end - QPointF(500, 0));
    QLineEdit field(&shown.window);
    field.show();
    field.setFocus();
    QTRY_VERIFY(field.hasFocus());
    QCOMPARE(shown.canvas->cursor().shape(), Qt::IBeamCursor);
}

void InlineTextHandleTests::anEditEndsAResize()
{
    TextCanvas shown;
    shown.type(QStringLiteral("abc"));
    const QSizeF size = shown.editor().logicalSize();
    const QPointF right = at(shown, size.width(), size.height() / 2);
    // Its drag would write back the draft it began with.
    shown.press(right);
    shown.move(right + QPointF(20, 0));
    shown.type(QStringLiteral("X"));
    shown.move(right + QPointF(40, 0));
    QCOMPARE(shown.session.textDraft().value().style.content, QString("abcX"));
    QCOMPARE(shown.session.textDraft().value().style.boxSize, std::optional(QSizeF(size.width() + 20, size.height())));
    shown.release(right + QPointF(40, 0));
    // A spacing key ends it too.
    const QPointF edge = at(shown, size.width() + 20, size.height() / 2);
    shown.press(edge);
    shown.move(edge + QPointF(20, 0));
    shown.key(Qt::Key_Right, Qt::AltModifier);
    const double tracking = shown.session.textDraft().value().style.tracking;
    QVERIFY(tracking > 0);
    shown.move(edge + QPointF(40, 0));
    QCOMPARE(shown.session.textDraft().value().style.tracking, tracking);
    QCOMPARE(shown.session.textDraft().value().style.boxSize, std::optional(QSizeF(size.width() + 40, size.height())));
    shown.release(edge + QPointF(40, 0));
}

void InlineTextHandleTests::aSquashedLayerTakesFarDrags()
{
    TextCanvas shown;
    shown.session.cancelText();
    shown.drag(QPointF(20, 20), QPointF(120, 120));
    TextDraft draft = shown.session.textDraft().value();
    draft.style.fontSize = 1;
    draft.style.boxSize = QSizeF(100, 30'000);
    draft.style.content = QStringLiteral("abc");
    shown.session.setTextDraft(draft);
    QVERIFY(shown.session.finishText());
    const QUuid id = shown.session.activeLayerID().value();
    rewrite(shown.session, [&](ProjectSnapshot &snapshot) { record(snapshot, id).transform.size = QSizeF(100, 10); });
    shown.session.zoom(1);
    shown.session.editActiveText();
    // Rows past int's range still lie below the lines.
    const QPointF inside = shown.editor().textTransform().map(QPointF(LayerTextStyle::padding + 1, 15'000));
    shown.press(inside);
    shown.move(inside + QPointF(0, 1'000'000));
    QCOMPARE(shown.caret(), 3);
    shown.release(inside + QPointF(0, 1'000'000));
}

// Swift 221d6cc: away from the box, the Type I-beam.
void InlineTextHandleTests::theIBeamHoldsAcrossTheCanvas()
{
    TextCanvas shown;
    shown.type(QStringLiteral("Hi"));
    const QSizeF size = shown.editor().logicalSize();
    QCOMPARE(hovered(shown, at(shown, size.width(), size.height() / 2)), Qt::SizeHorCursor);
    const QPointF corner(shown.canvas->width() - 3, shown.canvas->height() - 3);
    QCOMPARE(hovered(shown, corner), Qt::IBeamCursor);
    QCOMPARE(hovered(shown, QPointF(3, 3)), Qt::IBeamCursor);
    // After a resize drag, the I-beam again, away from it.
    const QPointF edge = at(shown, size.width(), size.height() / 2);
    shown.drag(edge, edge + QPointF(30, 0));
    QCOMPARE(hovered(shown, corner), Qt::IBeamCursor);
    // Nothing outside the canvas takes the I-beam: window or sibling.
    QWidget sibling(&shown.window);
    QVERIFY(!shown.window.testAttribute(Qt::WA_SetCursor));
    QCOMPARE(sibling.cursor().shape(), Qt::ArrowCursor);
    // The canvas owns its cursor: leaving sets none elsewhere.
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(shown.canvas, &leave);
    QVERIFY(!QGuiApplication::overrideCursor());
    QVERIFY(!shown.window.testAttribute(Qt::WA_SetCursor));
    QCOMPARE(sibling.cursor().shape(), Qt::ArrowCursor);
    QCOMPARE(shown.canvas->cursor().shape(), Qt::IBeamCursor);
}

QTEST_MAIN(InlineTextHandleTests)
#include "InlineTextHandleTests.moc"
