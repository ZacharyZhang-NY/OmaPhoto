#include "BrushFixtures.h"
#include "Document/EditorSession.h"
#include "IO/ImageExporter.h"
#include <QSignalSpy>
#include <cmath>
#include <numbers>

// Swift's ShapeToolTests, and the draft's own rules.
namespace {
std::unique_ptr<EditorSession> makeSession()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(100, 80, true);
    session->selectTool(NavigationTool::shape);
    session->setForegroundColor(PaletteColor{1, 0, 0});
    return session;
}

void drag(EditorSession &session, QPointF start, QPointF end, bool square = false, bool fromCenter = false)
{
    session.beginShape(start);
    session.dragShape(end, square, fromCenter);
    session.finishShape();
}

// One exported pixel's red and alpha, as Swift's reader.
std::pair<int, int> redAlpha(const EditorSession &session, int x, int y)
{
    const std::vector<int> at = pixel(ImageExporter::render(session.projectSnapshot().value()).image, x, y);
    return {at[0], at[3]};
}

QStringList names(const EditorSession &session)
{
    QStringList list;
    for (const ImageLayer &layer : session.document().value().layers)
        list << layer.name;
    return list;
}

const std::pair<int, int> solid{255, 255};
}

class ShapeToolTests : public QObject {
    Q_OBJECT
private slots:
    void rectangleFillsANewLayerWithTheForegroundColorAsOneUndoStep();
    void ellipseLeavesItsCornersClearWithShiftCircleAndOptionFromCenter();
    void aClickEscapeOrToolSwitchMakesNoLayer();
    void roundedRectanglesFollowTheRadiusAndClampToAPill();
    void aLineKeepsItsEndsThicknessAndRoundCaps();
    void shiftSnapsALineToEighthsOfATurn();
    void aShapeTooLargeIsRefused();
    void namesSkipThoseInUse();
    void theDraftWaitsForEditableLayers();
    void tabAndTheKindsCancelTheDraft();
    void everyChangeIsAnnounced();
    void pathsFollowSwift();
    void flatDragsMakeNothing();
    void edgesAreSmoothAndLinesAPixelWide();
};

void ShapeToolTests::rectangleFillsANewLayerWithTheForegroundColorAsOneUndoStep()
{
    const auto session = makeSession();
    session->selectAll();
    const int count = session->history.undoCount();
    drag(*session, QPointF(10, 10), QPointF(40, 30));
    QCOMPARE(names(*session), (QStringList{"Layer 1", "Rectangle 1"}));
    QCOMPARE(session->activeLayer().value().name, QString("Rectangle 1"));
    QCOMPARE(session->history.undoCount(), count + 1);
    QCOMPARE(session->history.undoName(), QString("Rectangle"));
    QCOMPARE(session->activeLayer().value().transform.origin, QPointF(10, 10));
    QCOMPARE(session->activeLayer().value().transform.size, QSizeF(30, 20));
    // Unlike Paste, drawing a shape keeps the selection.
    QVERIFY(session->selection());
    QCOMPARE(redAlpha(*session, 25, 20), solid);
    QCOMPARE(redAlpha(*session, 10, 10), solid);
    QCOMPARE(redAlpha(*session, 39, 29), solid);
    QCOMPARE(redAlpha(*session, 9, 20).second, 0);
    QCOMPARE(redAlpha(*session, 40, 20).second, 0);
    QCOMPARE(redAlpha(*session, 25, 30).second, 0);
    drag(*session, QPointF(60, 10), QPointF(70, 20));
    QCOMPARE(session->activeLayer().value().name, QString("Rectangle 2"));
    session->undo();
    session->undo();
    QCOMPARE(names(*session), (QStringList{"Layer 1"}));
}

void ShapeToolTests::ellipseLeavesItsCornersClearWithShiftCircleAndOptionFromCenter()
{
    const auto session = makeSession();
    session->toggleShapeKind();
    QCOMPARE(session->shapeKind(), ShapeKind::ellipse);
    drag(*session, QPointF(50, 40), QPointF(60, 45), true, true);
    QCOMPARE(session->activeLayer().value().name, QString("Ellipse 1"));
    QCOMPARE(session->activeLayer().value().transform.origin, QPointF(40, 30));
    QCOMPARE(session->activeLayer().value().transform.size, QSizeF(20, 20));
    QCOMPARE(redAlpha(*session, 50, 40), solid);
    QVERIFY(redAlpha(*session, 41, 40).second > 0 && redAlpha(*session, 50, 31).second > 0);
    // Outside the circle, inside its box.
    QCOMPARE(redAlpha(*session, 40, 30).second, 0);
    QCOMPARE(redAlpha(*session, 59, 49).second, 0);
}

void ShapeToolTests::aClickEscapeOrToolSwitchMakesNoLayer()
{
    const auto session = makeSession();
    const int count = session->history.undoCount();
    session->beginShape(QPointF(20, 20));
    session->finishShape();
    session->beginShape(QPointF(20, 20));
    session->dragShape(QPointF(50, 50), false, false);
    QCOMPARE(session->shapeDraft().value().rect, QRectF(20, 20, 30, 30));
    session->cancelShape();
    session->beginShape(QPointF(20, 20));
    session->dragShape(QPointF(50, 50), false, false);
    session->selectTool(NavigationTool::brush);
    QVERIFY(!session->shapeDraft());
    QCOMPARE(session->history.undoCount(), count);
    QCOMPARE(session->document().value().layers.size(), size_t(1));
}

void ShapeToolTests::roundedRectanglesFollowTheRadiusAndClampToAPill()
{
    const auto session = makeSession();
    session->setShapeCornerRadius(8);
    drag(*session, QPointF(10, 10), QPointF(50, 40));
    session->setShapeCornerRadius(500);
    // 40 × 20: a radius of 10.
    drag(*session, QPointF(55, 50), QPointF(95, 70));
    QCOMPARE(redAlpha(*session, 10, 10).second, 0);
    QCOMPARE(redAlpha(*session, 11, 11).second, 0);
    QCOMPARE(redAlpha(*session, 13, 13).second, 255);
    QCOMPARE(redAlpha(*session, 30, 10).second, 255);
    QCOMPARE(redAlpha(*session, 30, 25), solid);
    QCOMPARE(redAlpha(*session, 55, 50).second, 0);
    QCOMPARE(redAlpha(*session, 75, 60), solid);
    QCOMPARE(session->document().value().layers.size(), size_t(3));
    QCOMPARE(session->activeLayer().value().shape.value().style.cornerRadius, 500.0);
    session->toggleShapeKind();
    session->beginShape(QPointF(5, 5));
    QCOMPARE(session->shapeDraft().value().cornerRadius, 0.0);
    session->cancelShape();
}

void ShapeToolTests::aLineKeepsItsEndsThicknessAndRoundCaps()
{
    const auto session = makeSession();
    session->setForegroundColor(PaletteColor{1, 0.5, 0.25});
    session->setShapeKind(ShapeKind::line);
    session->beginShape(QPointF(10, 10));
    // Undragged, a line has no end yet.
    QVERIFY(!session->shapeLineEnds());
    session->dragShape(QPointF(50, 30), false, false);
    QCOMPARE(session->shapeLineEnds().value(), std::pair(QPointF(10, 10), QPointF(50, 30)));
    session->finishShape();
    // The ends' box, grown by half the default 4.
    const ImageLayer line = session->activeLayer().value();
    QCOMPARE(line.name, QString("Line 1"));
    QCOMPARE(line.transform.origin, QPointF(8, 8));
    QCOMPARE(line.transform.size, QSizeF(44, 24));
    const LayerShapeStyle style = line.liveShape().value().style;
    QCOMPARE(style.kind, ShapeKind::line);
    QCOMPARE(style.color(), (PaletteColor{1, 0.5, 0.25}));
    QCOMPARE(style.lineWidth.value(), 4.0);
    QCOMPARE(style.start.value(), QPointF(2.0 / 44, 2.0 / 24));
    QCOMPARE(style.end.value(), QPointF(42.0 / 44, 22.0 / 24));
    QCOMPARE(redAlpha(*session, 30, 20), solid);
    QCOMPARE(redAlpha(*session, 20, 25).second, 0);
    // Past its start, inside the round cap alone.
    QVERIFY(redAlpha(*session, 8, 9).second > 100);
    QCOMPARE(redAlpha(*session, 50, 30), solid);
    // Dragged back the other way: the same box, ends swapped.
    session->setShapeLineWidth(6);
    session->beginShape(QPointF(50, 30));
    session->dragShape(QPointF(10, 10), false, false);
    QCOMPARE(session->shapeLineEnds().value().first, QPointF(50, 30));
    session->finishShape();
    const ImageLayer back = session->activeLayer().value();
    QCOMPARE(back.transform.origin, QPointF(7, 7));
    QCOMPARE(back.transform.size, QSizeF(46, 26));
    QCOMPARE(back.shape.value().style.start.value(), QPointF(43.0 / 46, 23.0 / 26));
    QCOMPARE(back.shape.value().style.lineWidth.value(), 6.0);
    // A click leaves a dot: the box grows by thickness.
    session->beginShape(QPointF(70, 60));
    session->finishShape();
    const ImageLayer dot = session->activeLayer().value();
    QCOMPARE(dot.name, QString("Line 3"));
    QCOMPARE(dot.transform.origin, QPointF(67, 57));
    QCOMPARE(dot.transform.size, QSizeF(6, 6));
    QCOMPARE(dot.shape.value().style.start.value(), QPointF(0.5, 0.5));
    QCOMPARE(redAlpha(*session, 70, 60), solid);
    // A disc: its box's corners barely touched, its edges full.
    QVERIFY(redAlpha(*session, 67, 57).second < 20 && redAlpha(*session, 67, 60).second > 150);
    // A box that ends between pixels keeps its whole pixels.
    session->beginShape(QPointF(10, 40));
    session->dragShape(QPointF(50.5, 60), false, false);
    session->finishShape();
    QCOMPARE(session->activeLayer().value().asset.value().size(), QSize(46, 26));
    QCOMPARE(session->activeLayer().value().transform.origin, QPointF(7, 37));
}

void ShapeToolTests::shiftSnapsALineToEighthsOfATurn()
{
    const auto session = makeSession();
    session->setShapeKind(ShapeKind::line);
    session->beginShape(QPointF(10.6, 9.4));
    QCOMPARE(session->shapeDraft().value().anchor, QPointF(11, 9));
    session->beginShape(QPointF(10.4, 9.6));
    QCOMPARE(session->shapeDraft().value().anchor, QPointF(10, 10));
    session->dragShape(QPointF(50, 33), true, false);
    const double length = std::hypot(40.0, 23.0), leg = length * std::cos(std::numbers::pi / 4);
    const QPointF end = session->shapeLineEnds().value().second;
    QVERIFY(std::abs(end.x() - (10 + leg)) < 1e-9 && std::abs(end.y() - (10 + leg)) < 1e-9);
    QCOMPARE(session->shapeDraft().value().rect, DragBox::rect(QPointF(10, 10), end, false, false));
    // Flat stays flat; Alt grows the box both ways.
    session->dragShape(QPointF(50, 14), true, true);
    QVERIFY(std::abs(session->shapeLineEnds().value().second.y() - 10) < 1e-9);
    QCOMPARE(session->shapeDraft().value().rect, DragBox::rect(QPointF(10, 10), session->shapeLineEnds().value().second, false, true));
    // Without Shift a line ends where the pointer is.
    session->dragShape(QPointF(33.3, 21.7), false, false);
    QCOMPARE(session->shapeLineEnds().value().second, QPointF(33.3, 21.7));
    QCOMPARE(session->shapeDraft().value().rect, QRectF(10, 10, 23, 12));
}

void ShapeToolTests::aShapeTooLargeIsRefused()
{
    EditorSession session;
    session.createDocument(20'000, 12'000, true);
    session.selectTool(NavigationTool::shape);
    // Each side truncated first: 10,001 × 10,000 is over.
    session.beginShape(QPointF(0, 0));
    session.dragShape(QPointF(10'001, 10'000), false, false);
    session.finishShape();
    QCOMPARE(session.brushError().value(), QString("That shape is too large. A shape can cover up to 100 megapixels."));
    QCOMPARE(session.document().value().layers.size(), size_t(1));
    QVERIFY(!session.shapeDraft());
}

void ShapeToolTests::namesSkipThoseInUse()
{
    EditorSession empty;
    QCOMPARE(empty.nextShapeName(ShapeKind::ellipse), QString("Ellipse 1"));
    const auto session = makeSession();
    session->renameLayer(session->activeLayerID().value(), QStringLiteral("Rectangle 1"));
    QCOMPARE(session->nextShapeName(ShapeKind::rectangle), QString("Rectangle 2"));
    QCOMPARE(session->nextShapeName(ShapeKind::line), QString("Line 1"));
    drag(*session, QPointF(1, 1), QPointF(5, 5));
    QCOMPARE(session->activeLayer().value().name, QString("Rectangle 2"));
}

void ShapeToolTests::theDraftWaitsForEditableLayers()
{
    const auto session = makeSession();
    // Only the Shape tool, a finite point, editable layers begin.
    session->selectTool(NavigationTool::brush);
    session->beginShape(QPointF(5, 5));
    QVERIFY(!session->shapeDraft());
    session->selectTool(NavigationTool::shape);
    session->beginShape(QPointF(NAN, 5));
    session->beginShape(QPointF(5, INFINITY));
    QVERIFY(!session->shapeDraft());
    session->setIsProjectBusy(true);
    session->beginShape(QPointF(5, 5));
    QVERIFY(!session->shapeDraft());
    session->setIsProjectBusy(false);
    session->beginShape(QPointF(5, 5));
    session->dragShape(QPointF(NAN, 20), false, false);
    session->dragShape(QPointF(20, -INFINITY), false, false);
    QCOMPARE(session->shapeDraft().value().rect, QRectF(5, 5, 0, 0));
    // A draft finished while busy makes nothing.
    session->dragShape(QPointF(20, 20), false, false);
    session->setIsProjectBusy(true);
    session->finishShape();
    QVERIFY(!session->shapeDraft());
    QCOMPARE(session->document().value().layers.size(), size_t(1));
    session->setIsProjectBusy(false);
    // Without a draft, a drag or finish changes nothing.
    session->dragShape(QPointF(30, 30), false, false);
    session->finishShape();
    QVERIFY(!session->shapeDraft());
    QCOMPARE(session->document().value().layers.size(), size_t(1));
}

void ShapeToolTests::tabAndTheKindsCancelTheDraft()
{
    const auto session = makeSession();
    session->beginShape(QPointF(5, 5));
    session->toggleShapeKind();
    QVERIFY(!session->shapeDraft());
    QCOMPARE(session->shapeKind(), ShapeKind::ellipse);
    session->toggleShapeKind();
    QCOMPARE(session->shapeKind(), ShapeKind::line);
    session->toggleShapeKind();
    QCOMPARE(session->shapeKind(), ShapeKind::rectangle);
    session->beginShape(QPointF(5, 5));
    session->cycleToolMode();
    QVERIFY(!session->shapeDraft());
    QCOMPARE(session->shapeKind(), ShapeKind::ellipse);
    // Choosing the tool it has keeps the draft.
    session->beginShape(QPointF(5, 5));
    session->selectTool(NavigationTool::shape);
    QVERIFY(session->shapeDraft());
    QCOMPARE(session->shapeDraft().value().kind, ShapeKind::ellipse);
}

void ShapeToolTests::everyChangeIsAnnounced()
{
    const auto session = makeSession();
    QSignalSpy spy(session.get(), &EditorSession::changed);
    session->setShapeKind(ShapeKind::line);
    session->setShapeLineWidth(9);
    session->setShapeCornerRadius(3);
    QCOMPARE(spy.count(), 3);
    QCOMPARE(session->shapeLineWidth(), 9.0);
    QCOMPARE(session->shapeCornerRadius(), 3.0);
    session->beginShape(QPointF(1, 1));
    session->dragShape(QPointF(9, 9), false, false);
    session->cancelShape();
    QCOMPARE(spy.count(), 6);
    session->cancelShape();
    QCOMPARE(spy.count(), 6);
    // A rectangle's click clears the draft, one signal.
    session->setShapeKind(ShapeKind::rectangle);
    session->beginShape(QPointF(1, 1));
    spy.clear();
    session->finishShape();
    QCOMPARE(spy.count(), 1);
    QVERIFY(!session->shapeDraft());
}

void ShapeToolTests::pathsFollowSwift()
{
    const QRectF tall(0, 0, 20, 40);
    QPainterPath expected;
    expected.addRoundedRect(tall, 10, 10);
    // The radius stops at half the shorter side, either way.
    QCOMPARE(path(ShapeKind::rectangle, tall, 500), expected);
    QCOMPARE(path(ShapeKind::rectangle, tall.transposed(), 500), [] {
        QPainterPath wide;
        wide.addRoundedRect(QRectF(0, 0, 40, 20), 10, 10);
        return wide;
    }());
    QPainterPath square;
    square.addRect(tall);
    QCOMPARE(path(ShapeKind::rectangle, tall, -5), square);
    QCOMPARE(path(ShapeKind::rectangle, tall), square);
    QPainterPath ellipse;
    ellipse.addEllipse(tall);
    QCOMPARE(path(ShapeKind::ellipse, tall, 8), ellipse);
    QCOMPARE(rawValue(ShapeKind::rectangle) + rawValue(ShapeKind::ellipse) + rawValue(ShapeKind::line), QString("RectangleEllipseLine"));
    QCOMPARE(shapeKind(QStringLiteral("Line")), std::optional(ShapeKind::line));
    QCOMPARE(shapeKind(QStringLiteral("Ellipse")), std::optional(ShapeKind::ellipse));
    QVERIFY(!shapeKind(QStringLiteral("line")));
}

void ShapeToolTests::flatDragsMakeNothing()
{
    const auto session = makeSession();
    drag(*session, QPointF(20, 20), QPointF(50, 20));
    drag(*session, QPointF(20, 20), QPointF(20, 50));
    QCOMPARE(session->document().value().layers.size(), size_t(1));
    QVERIFY(!session->brushError());
}

void ShapeToolTests::edgesAreSmoothAndLinesAPixelWide()
{
    const auto session = makeSession();
    session->setShapeKind(ShapeKind::ellipse);
    drag(*session, QPointF(40, 30), QPointF(60, 50));
    // The circle's rim crosses this pixel: part covered.
    const int rim = redAlpha(*session, 40, 38).second;
    QVERIFY2(rim > 40 && rim < 250, qPrintable(QString::number(rim)));
    // Thinner than a pixel, a line still draws one wide.
    session->setShapeKind(ShapeKind::line);
    session->setShapeLineWidth(0.4);
    drag(*session, QPointF(10, 10), QPointF(50, 30));
    const QImage line = session->activeLayer().value().asset.value().image();
    int strongest = 0;
    for (int y = 0; y < line.height(); ++y)
        strongest = std::max(strongest, alpha(line, 20, y));
    QVERIFY2(strongest > 150, qPrintable(QString::number(strongest)));
    // Pixel-wide is still an outline: 1 and 1.001 agree.
    session->setShapeLineWidth(1);
    drag(*session, QPointF(10, 40), QPointF(50, 60));
    const QImage one = session->activeLayer().value().asset.value().image();
    session->setShapeLineWidth(1.001);
    drag(*session, QPointF(10, 40), QPointF(50, 60));
    const QImage wider = session->activeLayer().value().asset.value().image();
    QCOMPARE(one.size(), wider.size());
    int apart = 0;
    for (int y = 0; y < one.height(); ++y) {
        for (int x = 0; x < one.width(); ++x)
            apart = std::max(apart, std::abs(alpha(one, x, y) - alpha(wider, x, y)));
    }
    QVERIFY2(apart <= 3, qPrintable(QString::number(apart)));
}

QTEST_GUILESS_MAIN(ShapeToolTests)
#include "ShapeToolTests.moc"
