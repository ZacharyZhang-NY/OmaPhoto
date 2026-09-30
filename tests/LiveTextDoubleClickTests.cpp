#include "SelectionCanvasFixtures.h"
#include "Rendering/TextLayout.h"
#include <QFontMetricsF>

// Swift 1.2.10 (a5721b2): the Move tool's double click opens text.
namespace {
// Live text whose box covers `box`, made in one step.
QUuid addText(EditorSession &session, const QString &content, QRectF box)
{
    session.selectTool(NavigationTool::type);
    session.beginText(box);
    TextDraft draft = session.textDraft().value();
    draft.style.content = content;
    if (!session.applyText(draft))
        throw std::runtime_error("the text was refused");
    return session.activeLayerID().value();
}

// Opaque red pixels, as a layer's.
QImage redPixels(int width, int height)
{
    QImage image = BrushRaster::context(width, height, false);
    image.fill(Qt::red);
    return image;
}

// A document point on the canvas's view.
QPointF viewPoint(Canvas &shown, QPointF pixel)
{
    return shown.session.viewport.viewPoint(pixel, shown.documentSize());
}
}

class LiveTextDoubleClickTests : public QObject {
    Q_OBJECT
private slots:
    void theTopmostShownTextOpens();
    void otherPressesKeepTheMoveTool();
    void anOpenDragAndPreviewEndFirst();
    void theCaretWaitsAfterTheText();
    void zoomAndTurnsPlaceThePress();
};

void LiveTextDoubleClickTests::theTopmostShownTextOpens()
{
    // No display sync: the gesture opens the editor itself.
    Canvas shown;
    const QUuid lower = addText(shown.session, QStringLiteral("Lower"), QRectF(20, 20, 200, 100));
    const QUuid upper = addText(shown.session, QStringLiteral("Upper"), QRectF(100, 60, 200, 100));
    shown.session.addBlankLayer();
    const QUuid blank = shown.session.activeLayerID().value();
    shown.session.selectTool(NavigationTool::move);
    // Where both lie, the upper opens, in the Type tool.
    QTest::mouseDClick(shown.canvas, Qt::LeftButton, Qt::NoModifier, viewPoint(shown, QPointF(150, 90)).toPoint());
    QCOMPARE(shown.session.tool(), NavigationTool::type);
    QCOMPARE(shown.session.activeLayerID(), std::optional(upper));
    QCOMPARE(shown.session.textDraft().value().layerID, std::optional(upper));
    QVERIFY(shown.canvas->inlineTextEditor());
    shown.session.cancelText();
    // A hidden text is passed over for the one below.
    shown.session.selectTool(NavigationTool::move);
    shown.session.toggleLayerVisibility(upper);
    shown.session.selectLayer(blank);
    QTest::mouseDClick(shown.canvas, Qt::LeftButton, Qt::NoModifier, viewPoint(shown, QPointF(150, 90)).toPoint());
    QCOMPARE(shown.session.textDraft().value().layerID, std::optional(lower));
    QCOMPARE(shown.session.activeLayerID(), std::optional(lower));
    shown.session.cancelText();
    // A text in a hidden folder is passed over too.
    shown.session.selectTool(NavigationTool::move);
    shown.session.toggleLayerVisibility(upper);
    shown.session.selectLayer(upper);
    shown.session.groupSelectedLayers();
    const QUuid folder = shown.session.activeLayerID().value();
    QCOMPARE(layerWith(shown.session, upper).parentID, std::optional(folder));
    shown.session.toggleLayerVisibility(folder);
    QTest::mouseDClick(shown.canvas, Qt::LeftButton, Qt::NoModifier, viewPoint(shown, QPointF(150, 90)).toPoint());
    QCOMPARE(shown.session.textDraft().value().layerID, std::optional(lower));
}

void LiveTextDoubleClickTests::anOpenDragAndPreviewEndFirst()
{
    // On the text already active, Swift's commitTransform still cleans up.
    Canvas shown;
    const QUuid text = addText(shown.session, QStringLiteral("Words"), QRectF(20, 20, 200, 100));
    shown.session.selectTool(NavigationTool::move);
    shown.session.beginOpacityEdit();
    shown.session.setLayerOpacity(0.5);
    shown.session.previewBlendMode(LayerBlendMode::multiply, text);
    QVERIFY(!shown.session.canUndo());
    QTest::mouseDClick(shown.canvas, Qt::LeftButton, Qt::NoModifier, viewPoint(shown, QPointF(60, 60)).toPoint());
    QCOMPARE(shown.session.textDraft().value().layerID, std::optional(text));
    QVERIFY(shown.session.displayedBlendMode(layerWith(shown.session, text)) == LayerBlendMode::normal);
    shown.session.cancelText();
    QVERIFY(shown.session.canUndo());
}

void LiveTextDoubleClickTests::otherPressesKeepTheMoveTool()
{
    Canvas shown;
    QObject::connect(&shown.session, &EditorSession::changed, shown.canvas, [&shown] { shown.canvas->synchronizeDisplay(); });
    const QUuid text = addText(shown.session, QStringLiteral("Words"), QRectF(20, 20, 200, 100));
    shown.session.selectTool(NavigationTool::move);
    // One click moves; it opens nothing.
    shown.click(viewPoint(shown, QPointF(60, 60)));
    QVERIFY(!shown.session.textDraft());
    QCOMPARE(shown.session.tool(), NavigationTool::move);
    // Off the text, a double click is the Move tool's.
    shown.session.addPixelLayer(redPixels(60, 40), QPointF(260, 220), QStringLiteral("Red"), QStringLiteral("Add"));
    const QUuid red = shown.session.activeLayerID().value();
    shown.session.selectLayer(text);
    shown.session.setTransformAutoSelect(true);
    QTest::mouseDClick(shown.canvas, Qt::LeftButton, Qt::NoModifier, viewPoint(shown, QPointF(280, 240)).toPoint());
    QVERIFY(!shown.session.textDraft());
    QCOMPARE(shown.session.tool(), NavigationTool::move);
    QCOMPARE(shown.session.activeLayerID(), std::optional(red));
    // While a layer is renamed, layers rest: nothing opens.
    shown.session.setRenamingLayerID(red);
    QTest::mouseDClick(shown.canvas, Qt::LeftButton, Qt::NoModifier, viewPoint(shown, QPointF(60, 60)).toPoint());
    QVERIFY(!shown.session.textDraft());
    QCOMPARE(shown.session.activeLayerID(), std::optional(red));
    shown.session.setRenamingLayerID(std::nullopt);
    // A busy project opens nothing.
    shown.session.setIsProjectBusy(true);
    QTest::mouseDClick(shown.canvas, Qt::LeftButton, Qt::NoModifier, viewPoint(shown, QPointF(60, 60)).toPoint());
    QVERIFY(!shown.session.textDraft());
    shown.session.setIsProjectBusy(false);
    // Other tools' double clicks are theirs.
    shown.session.selectTool(NavigationTool::marquee);
    QTest::mouseDClick(shown.canvas, Qt::LeftButton, Qt::NoModifier, viewPoint(shown, QPointF(60, 60)).toPoint());
    QVERIFY(!shown.session.textDraft());
    QCOMPARE(shown.session.activeLayerID(), std::optional(red));
}

void LiveTextDoubleClickTests::theCaretWaitsAfterTheText()
{
    Canvas shown;
    QObject::connect(&shown.session, &EditorSession::changed, shown.canvas, [&shown] { shown.canvas->synchronizeDisplay(); });
    addText(shown.session, QStringLiteral("Word"), QRectF(20, 20, 300, 150));
    shown.session.selectTool(NavigationTool::move);
    QTest::mouseDClick(shown.canvas, Qt::LeftButton, Qt::NoModifier, viewPoint(shown, QPointF(60, 60)).toPoint());
    QTRY_COMPARE(shown.canvas->inlineTextEditor()->caretPosition(), 4);
    QCOMPARE(shown.canvas->inlineTextEditor()->anchor(), 4);
    QTest::keyClicks(shown.canvas, QStringLiteral("X"));
    QCOMPARE(shown.session.textDraft().value().style.content, QString("WordX"));
    // A caret a click placed stays where it was put.
    shown.session.cancelText();
    shown.session.selectTool(NavigationTool::type);
    const LayerTextStyle style = layerWith(shown.session, shown.session.activeLayerID().value()).liveText().value().style;
    const QFontMetricsF metrics(TextLayout::font(style));
    const double padding = LayerTextStyle::padding;
    const QPointF at = viewPoint(shown, QPointF(20 + padding + metrics.horizontalAdvance(QStringLiteral("Wo")), 20 + padding + style.lineHeight() / 2));
    shown.click(at);
    QCOMPARE(shown.canvas->inlineTextEditor()->caretPosition(), 2);
    QCoreApplication::processEvents();
    QCOMPARE(shown.canvas->inlineTextEditor()->caretPosition(), 2);
    QCOMPARE(shown.canvas->inlineTextEditor()->anchor(), 2);
    // New text keeps its caret, despite opened text's queued step.
    shown.session.cancelText();
    shown.session.editActiveText();
    shown.session.cancelText();
    TextDraft fresh{.documentID = shown.session.document().value().id, .layerID = std::nullopt, .origin = QPointF(20, 200), .style = style, .selection = TextSpan()};
    fresh.style.content = QStringLiteral("New");
    shown.session.setTextDraft(fresh);
    QCoreApplication::processEvents();
    QCOMPARE(shown.session.textDraft().value().id, fresh.id);
    QCOMPARE(shown.canvas->inlineTextEditor()->caretPosition(), 0);
    QCOMPARE(shown.canvas->inlineTextEditor()->anchor(), 0);
}

void LiveTextDoubleClickTests::zoomAndTurnsPlaceThePress()
{
    Canvas shown;
    const QUuid text = addText(shown.session, QStringLiteral("Turned"), QRectF(100, 100, 160, 60));
    rewrite(shown.session, [&](ProjectSnapshot &snapshot) { record(snapshot, text).transform.rotation = 90; });
    shown.session.selectTool(NavigationTool::move);
    shown.session.zoom(1.5);
    QVERIFY(shown.session.viewport.viewPoint(QPointF(180, 70), shown.documentSize()) != QPointF(180, 70));
    // Outside the turned box, inside the upright one: nothing.
    QTest::mouseDClick(shown.canvas, Qt::LeftButton, Qt::NoModifier, viewPoint(shown, QPointF(110, 130)).toPoint());
    QVERIFY(!shown.session.textDraft());
    shown.release(viewPoint(shown, QPointF(110, 130)));
    // Inside the turned box only: the text opens.
    QTest::mouseDClick(shown.canvas, Qt::LeftButton, Qt::NoModifier, viewPoint(shown, QPointF(180, 70)).toPoint());
    QCOMPARE(shown.session.textDraft().value().layerID, std::optional(text));
}

QTEST_MAIN(LiveTextDoubleClickTests)
#include "LiveTextDoubleClickTests.moc"
