#include "SelectionCanvasFixtures.h"

// Swift 1.3.5's snapping while drawing and moving a selection.
class SnapDrawingCanvasTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void aMarqueeSnapsWhereItStartsAndEnds();
    void aShapeSnapsItsCorners();
    void aMovedSelectionSnapsUnlessCtrl();
    void theReachIsTenViewPoints();
    void aMovedSelectionSnapsItsEdges();
};

namespace {
// Only the canvas's edges are targets: 0 to 400, 300.
struct Snapping : Canvas {
    Snapping()
    {
        session.setSnappingEnabled(true);
        session.setSnapEnabled(true);
        session.setSnapToDocumentBounds(true);
        session.setSnapToLayers(false);
        session.setSnapToGrid(false);
    }
    QRectF selected() const { return session.selection().value().path.boundingRect(); }
};
}

void SnapDrawingCanvasTests::aMarqueeSnapsWhereItStartsAndEnds()
{
    Snapping shown;
    // Within ten points, press and drag meet the edges.
    shown.press(QPointF(6, 8));
    shown.move(QPointF(393, 291));
    QVERIFY(shown.session.snapGuides.xs == std::vector<double>{400} && shown.session.snapGuides.ys == std::vector<double>{300});
    shown.release(QPointF(393, 291));
    QCOMPARE(shown.selected(), QRectF(0, 0, 400, 300));
    QVERIFY(shown.session.snapGuides.xs.empty() && shown.session.snapGuides.ys.empty());
    // Ctrl draws freely, both ends.
    shown.session.deselect();
    shown.drag(QPointF(6, 8), QPointF(393, 291), Qt::ControlModifier);
    QCOMPARE(shown.selected(), QRectF(6, 8, 387, 283));
    // A Lasso starts where it is pressed.
    shown.session.deselect();
    shown.session.selectTool(NavigationTool::lasso);
    shown.press(QPointF(6, 8));
    QCOMPARE(shown.session.lassoDraft().value().points.front(), QPointF(6, 8));
    shown.release(QPointF(6, 8));
}

void SnapDrawingCanvasTests::aShapeSnapsItsCorners()
{
    Snapping shown;
    shown.session.selectTool(NavigationTool::shape);
    shown.press(QPointF(6, 8));
    shown.move(QPointF(393, 291));
    QCOMPARE(shown.session.shapeDraft().value().rect, QRectF(0, 0, 400, 300));
    QVERIFY(shown.session.snapGuides.xs == std::vector<double>{400});
    shown.move(QPointF(393, 291), Qt::ControlModifier);
    QCOMPARE(shown.session.shapeDraft().value().rect, QRectF(0, 0, 393, 291));
    QVERIFY(shown.session.snapGuides.xs.empty());
    shown.session.cancelShape();
    shown.press(QPointF(6, 8), Qt::ControlModifier);
    QCOMPARE(shown.session.shapeDraft().value().anchor, QPointF(6, 8));
    shown.session.cancelShape();
}

void SnapDrawingCanvasTests::aMovedSelectionSnapsUnlessCtrl()
{
    Snapping shown;
    // Drawn freely, five in from the top left.
    shown.drag(QPointF(5, 5), QPointF(55, 35), Qt::ControlModifier);
    QCOMPARE(shown.selected(), QRectF(5, 5, 50, 30));
    // Twelve up: both near edges meet the canvas.
    shown.press(QPointF(30, 20));
    shown.move(QPointF(30, 8));
    QCOMPARE(shown.selected(), QRectF(0, 0, 50, 30));
    QVERIFY(shown.session.snapGuides.xs == std::vector<double>{0} && shown.session.snapGuides.ys == std::vector<double>{0});
    // Shift keeps one axis: the locked one never snaps.
    shown.move(QPointF(30, 8), Qt::ShiftModifier);
    QCOMPARE(shown.selected(), QRectF(5, 0, 50, 30));
    QVERIFY(shown.session.snapGuides.xs.empty() && shown.session.snapGuides.ys == std::vector<double>{0});
    shown.move(QPointF(18, 19), Qt::ShiftModifier);
    QCOMPARE(shown.selected(), QRectF(0, 5, 50, 30));
    QVERIFY(shown.session.snapGuides.xs == std::vector<double>{0} && shown.session.snapGuides.ys.empty());
    // Ctrl moves freely and shows no line.
    shown.move(QPointF(30, 8), Qt::ControlModifier);
    QCOMPARE(shown.selected(), QRectF(5, -7, 50, 30));
    QVERIFY(shown.session.snapGuides.xs.empty() && shown.session.snapGuides.ys.empty());
    shown.release(QPointF(30, 8), Qt::ControlModifier);
}

void SnapDrawingCanvasTests::theReachIsTenViewPoints()
{
    Snapping shown;
    shown.session.addGuide({QUuid::createUuid(), CanvasGuide::Axis::vertical, 200});
    const QSizeF size = shown.documentSize();
    // Seven pixels off the guide: snapped at 100%, not 200%.
    shown.drag(shown.session.viewport.viewPoint(QPointF(207, 150), size), shown.session.viewport.viewPoint(QPointF(250, 180), size));
    QCOMPARE(shown.selected(), QRectF(200, 150, 50, 30));
    shown.session.deselect();
    shown.session.zoom(2);
    shown.drag(shown.session.viewport.viewPoint(QPointF(207, 150), size), shown.session.viewport.viewPoint(QPointF(250, 180), size));
    QCOMPARE(shown.selected(), QRectF(207, 150, 43, 30));
    // A moved selection's reach is ten view points too.
    QPainterPath box;
    box.addRect(210, 150, 40, 20);
    shown.session.setSelection(DocumentSelection{box}, QStringLiteral("Marquee"));
    shown.press(shown.session.viewport.viewPoint(QPointF(230, 160), size));
    shown.move(shown.session.viewport.viewPoint(QPointF(227, 160), size));
    QCOMPARE(shown.selected(), QRectF(207, 150, 40, 20));
    shown.release(shown.session.viewport.viewPoint(QPointF(227, 160), size));
}

void SnapDrawingCanvasTests::aMovedSelectionSnapsItsEdges()
{
    Snapping shown;
    EditorSession *const session = &shown.session;
    QPainterPath box;
    box.addRect(10, 10, 50, 30);
    // No move begun: the offset as it came, no lines.
    session->snapGuides = {{1}, {}};
    QCOMPARE(session->snappedSelectionOffset(QSizeF(-8.6, 0), 3), QSizeF(-8.6, 0));
    QVERIFY(session->snapGuides.xs.empty());
    session->setSelection(DocumentSelection{box}, QStringLiteral("Marquee"));
    QVERIFY(session->beginSelectionMove());
    // Whole pixels first; the left edge then meets the canvas.
    QCOMPARE(session->snappedSelectionOffset(QSizeF(-8.6, 0.4), 3), QSizeF(-10, 0));
    QVERIFY(session->snapGuides.xs == std::vector<double>{0} && session->snapGuides.ys.empty());
    // The far edge meets the far side; beyond reach, nothing.
    QCOMPARE(session->snappedSelectionOffset(QSizeF(338.6, 258.6), 3), QSizeF(340, 260));
    QVERIFY(session->snapGuides.xs == std::vector<double>{400} && session->snapGuides.ys == std::vector<double>{300});
    QCOMPARE(session->snappedSelectionOffset(QSizeF(-5, 0), 3), QSizeF(-5, 0));
    // Rounded half away from zero; the middle is no target.
    QCOMPARE(session->snappedSelectionOffset(QSizeF(8.6, 0), 3), QSizeF(9, 0));
    QCOMPARE(session->snappedSelectionOffset(QSizeF(2.5, 0), 3), QSizeF(3, 0));
    QCOMPARE(session->snappedSelectionOffset(QSizeF(163, 0), 3), QSizeF(163, 0));
    // An axis Shift holds does not snap.
    QCOMPARE(session->snappedSelectionOffset(QSizeF(-8.6, -8.6), 3, true, false), QSizeF(-10, -9));
    QCOMPARE(session->snappedSelectionOffset(QSizeF(-8.6, -8.6), 3, false, true), QSizeF(-9, -10));
    QVERIFY(session->snapGuides.xs.empty() && session->snapGuides.ys == std::vector<double>{0});
    // A turned ellipse snaps its own box, not control points.
    session->endSelectionMove();
    QPainterPath ellipse;
    ellipse.addEllipse(100, 100, 200, 60);
    ellipse = QTransform().translate(200, 130).rotate(45).translate(-200, -130).map(ellipse);
    const double left = ellipse.boundingRect().left();
    QVERIFY(left - ellipse.controlPointRect().left() > 6);
    session->setSelection(DocumentSelection{ellipse}, QStringLiteral("Marquee"));
    QVERIFY(session->beginSelectionMove());
    const double whole = std::round(2 - left);
    QCOMPARE(session->snappedSelectionOffset(QSizeF(whole, 0), 3), QSizeF(whole - (left + whole), 0));
    session->setSnappingEnabled(false);
    QCOMPARE(session->snappedSelectionOffset(QSizeF(-8.6, 0), 3), QSizeF(-8.6, 0));
    QVERIFY(session->snapGuides.xs.empty());
}

QTEST_MAIN(SnapDrawingCanvasTests)
#include "SnapDrawingCanvasTests.moc"
