#include "SelectionCanvasFixtures.h"
#include "Document/BrushStroke.h"
#include "Document/MagicWand.h"
#include "Document/ObjectSelection.h"
#include "Rendering/SelectionIcons.h"
#include "UI/NativeLayerList.h"
#include <QPainter>

// The Magic tool's Object mode: regions, edges, outlines, clicks.
namespace {
// Red discs on white, or flat white.
QImage discs(const std::vector<std::pair<QPointF, double>> &shapes, QSize size = QSize(400, 300))
{
    QImage image = BrushRaster::context(size.width(), size.height(), false);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(217, 26, 26));
    for (const auto &[centre, radius] : shapes)
        painter.drawEllipse(centre, radius, radius);
    return image;
}

// Pixels an outline covers at half coverage or more.
int covered(const std::optional<QPainterPath> &path, QSize size)
{
    const QImage coverage = DocumentSelection{path.value(), false}.coverage(size.width(), size.height());
    int count = 0;
    for (int y = 0; y < size.height(); ++y) {
        for (int x = 0; x < size.width(); ++x)
            count += coverage.constScanLine(y)[x] >= 128;
    }
    return count;
}

bool inside(const std::optional<QPainterPath> &path, QPointF point)
{
    return path.value().contains(point);
}

std::unique_ptr<EditorSession> withDisc()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(400, 300);
    const QImage image = discs({{QPointF(200, 150), 80}});
    session->insert(ImportedImage(image, image, QStringLiteral("Disc")));
    session->selectTool(NavigationTool::wand);
    session->setWandMode(WandMode::object);
    return session;
}

void selectObject(EditorSession &session, QPointF point, SelectionMode mode)
{
    bool done = false;
    session.selectObject(point, mode, [&done] { done = true; });
    QVERIFY(QTest::qWaitFor([&done] { return done; }, 20000));
}

// Points of the path, a subpath's close included.
int points(const QPainterPath &path)
{
    return path.elementCount();
}
}

class ObjectSelectionTests : public QObject {
    Q_OBJECT
private slots:
    void aRegionTakesTheEightConnectedCellsUnderTheClick();
    void edgesStepARingAPixelAndStopAtTen();
    void smoothingRoundsStairsAndKeepsSubpathsAndTheirRule();
    void theObjectUnderTheClickIsTracedAndTheBackgroundIsNone();
    void theEdgeOffsetTightensAndLoosensTheOutline();
    void theEdgeFollowsTheImageNotTheModelsGrid();
    void theSessionSelectsCombinesAndNamesTheStep();
    void theSessionRefusesAndReadsTheLayerItIsTold();
    void theModeSwitchesByTabAndWKeepsIt();
    void theCanvasClicksPicksAgainInsideAndShowsTheObjectCursor();
};

void ObjectSelectionTests::aRegionTakesTheEightConnectedCellsUnderTheClick()
{
    const std::vector<uchar> mask{
        1, 1, 0, 0, 1,
        0, 1, 0, 0, 1,
        0, 0, 1, 0, 0,
        1, 0, 0, 0, 1,
    };
    const std::vector<uchar> first = ObjectSelection::region(mask, 5, 4, 0, 0).value();
    QCOMPARE(first, (std::vector<uchar>{
        1, 1, 0, 0, 0,
        0, 1, 0, 0, 0,
        0, 0, 1, 0, 0,
        0, 0, 0, 0, 0,
    }));
    // One region from any cell, the diagonal included.
    QCOMPARE(ObjectSelection::region(mask, 5, 4, 2, 2).value(), first);
    QCOMPARE(ObjectSelection::region(mask, 5, 4, 4, 1).value(), (std::vector<uchar>{
        0, 0, 0, 0, 1,
        0, 0, 0, 0, 1,
        0, 0, 0, 0, 0,
        0, 0, 0, 0, 0,
    }));
    QCOMPARE(ObjectSelection::region(mask, 5, 4, 4, 3).value(), (std::vector<uchar>{
        0, 0, 0, 0, 0,
        0, 0, 0, 0, 0,
        0, 0, 0, 0, 0,
        0, 0, 0, 0, 1,
    }));
    // Background holds no region.
    QVERIFY(!ObjectSelection::region(mask, 5, 4, 3, 0));
}

void ObjectSelectionTests::edgesStepARingAPixelAndStopAtTen()
{
    std::vector<uchar> block(49);
    for (int y = 2; y <= 4; ++y) {
        for (int x = 2; x <= 4; ++x)
            block[size_t(y * 7 + x)] = 255;
    }
    QCOMPARE(ObjectSelection::adjusted(block, 7, 7, 0), block);
    std::vector<uchar> centre(49);
    centre[24] = 255;
    QCOMPARE(ObjectSelection::adjusted(block, 7, 7, 1), centre);
    QCOMPARE(ObjectSelection::adjusted(block, 7, 7, 2), std::vector<uchar>(49));
    std::vector<uchar> grown(49);
    for (int y = 1; y <= 5; ++y) {
        for (int x = 1; x <= 5; ++x)
            grown[size_t(y * 7 + x)] = 255;
    }
    QCOMPARE(ObjectSelection::adjusted(block, 7, 7, -1), grown);
    // Past ten steps either way, ten.
    std::vector<uchar> dot(25 * 25);
    dot[12 * 25 + 12] = 255;
    const std::vector<uchar> far = ObjectSelection::adjusted(dot, 25, 25, -20);
    QCOMPARE(far, ObjectSelection::adjusted(dot, 25, 25, -10));
    QCOMPARE(std::count(far.begin(), far.end(), uchar(255)), 21 * 21);
    QCOMPARE(far[2 * 25 + 2], uchar(255));
    QCOMPARE(far[1 * 25 + 12], uchar(0));
    std::vector<uchar> full(25 * 25, 255);
    QCOMPARE(ObjectSelection::adjusted(far, 25, 25, 20), ObjectSelection::adjusted(far, 25, 25, 10));
    // The image's edge counts as inside: a full mask keeps.
    QCOMPARE(ObjectSelection::adjusted(full, 25, 25, 3), full);
}

void ObjectSelectionTests::smoothingRoundsStairsAndKeepsSubpathsAndTheirRule()
{
    // A staircase triangle and a holed square, traced.
    std::vector<uchar> mask(40 * 40);
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 40; ++x)
            mask[size_t(y * 40 + x)] = (x <= y && y < 20) || (x >= 24 && x < 36 && y >= 24 && y < 36 && !(x >= 28 && x < 32 && y >= 28 && y < 32)) ? 255 : 0;
    }
    const QPainterPath traced = MagicWand::outline(mask, 40, 40).value();
    QVERIFY(points(traced) > 40);
    const QPainterPath smooth = ObjectSelection::smoothed(traced);
    QCOMPARE(smooth.fillRule(), traced.fillRule());
    // Three subpaths, each simplified, then cut three times.
    const QList<QPolygonF> parts = smooth.toSubpathPolygons();
    QCOMPARE(parts.size(), 3);
    QCOMPARE(parts[0].size(), 3 * 8 + 1);
    QCOMPARE(parts[1].size(), 4 * 8 + 1);
    QCOMPARE(parts[2].size(), 4 * 8 + 1);
    // The stair is gone: the hypotenuse is straight.
    QVERIFY(smooth.contains(QPointF(8, 12)) && !smooth.contains(QPointF(12, 8)));
    // Corners round off; the hole stays a hole.
    QVERIFY(smooth.contains(QPointF(30, 25)) && !smooth.contains(QPointF(30, 30)));
    QVERIFY(!smooth.contains(QPointF(24.3, 24.3)));
    QVERIFY(traced.contains(QPointF(24.3, 24.3)));
    // By hand: the extreme breaks the loop; curves keep ends.
    QPainterPath open;
    open.moveTo(0, 0);
    for (const QPointF point : {QPointF(5, -1.3), QPointF(10, 0), QPointF(10, 10), QPointF(0, 10), QPointF(0, 5)})
        open.lineTo(point);
    open.moveTo(20, 0);
    open.lineTo(30, 0);
    open.cubicTo(30, 3, 30, 7, 30, 10);
    open.lineTo(20, 10);
    const QList<QPolygonF> built = ObjectSelection::smoothed(open).toSubpathPolygons();
    QCOMPARE(built.size(), 2);
    QCOMPARE(built[0].size(), 33);
    QCOMPARE(built[0].front(), QPointF(4.375, 0));
    QCOMPARE(built[1].size(), 33);
    QCOMPARE(built[1].front(), QPointF(24.375, 0));
    QCOMPARE(built[1][8], QPointF(30, 4.375));
    // A flat loop keeps its points; a closed pair goes.
    QPainterPath flat;
    flat.moveTo(40, 0);
    for (const QPointF point : {QPointF(45, 0), QPointF(50, 0), QPointF(45, 0.5)})
        flat.lineTo(point);
    flat.closeSubpath();
    flat.moveTo(60, 0);
    flat.lineTo(70, 0);
    flat.closeSubpath();
    const QList<QPolygonF> kept = ObjectSelection::smoothed(flat).toSubpathPolygons();
    QCOMPARE(kept.size(), 1);
    QCOMPARE(kept[0].size(), 33);
    // A subpath under three points is dropped.
    QPainterPath thin;
    thin.moveTo(0, 0);
    thin.lineTo(5, 5);
    QVERIFY(ObjectSelection::smoothed(thin).isEmpty());
}

void ObjectSelectionTests::theObjectUnderTheClickIsTracedAndTheBackgroundIsNone()
{
    const QImage pair = discs({{QPointF(110, 150), 60}, {QPointF(290, 150), 60}});
    const std::optional<QPainterPath> left = ObjectSelection::select(pair, QPointF(110, 150), 0, false);
    QVERIFY(inside(left, QPointF(110, 150)) && inside(left, QPointF(160, 150)));
    QVERIFY(!inside(left, QPointF(290, 150)) && !inside(left, QPointF(10, 10)) && !inside(left, QPointF(178, 150)));
    const std::optional<QPainterPath> right = ObjectSelection::select(pair, QPointF(290.7, 150.2), 0, false);
    QVERIFY(inside(right, QPointF(290, 150)) && !inside(right, QPointF(110, 150)));
    // The raw mask hugs the disc, within two percent.
    const int area = covered(left, pair.size());
    QVERIFY2(std::abs(area - M_PI * 60 * 60) < 0.02 * M_PI * 60 * 60, qPrintable(QString::number(area)));
    // Background, off the image, no number, or no subject: none.
    QVERIFY(!ObjectSelection::select(pair, QPointF(10, 10), 0, false));
    QVERIFY(!ObjectSelection::select(pair, QPointF(200, 150), 0, false));
    for (const QPointF point : {QPointF(-0.5, 10), QPointF(400, 10), QPointF(10, 300), QPointF(std::nan(""), 10), QPointF(10, INFINITY)})
        QVERIFY(!ObjectSelection::select(pair, point, 0, false));
    QVERIFY(!ObjectSelection::select(discs({}), QPointF(200, 150), 0, false));
    // The point floors: the last half pixel is inside.
    const QImage edges = discs({{QPointF(20, 150), 60}, {QPointF(380, 150), 60}});
    QVERIFY(inside(ObjectSelection::select(edges, QPointF(399.5, 150), 0, false), QPointF(390, 150)));
    QVERIFY(inside(ObjectSelection::select(edges, QPointF(0, 150), 0, false), QPointF(10, 150)));
    QVERIFY(!ObjectSelection::select(edges, QPointF(-0.5, 150), 0, false));
    // The cell row comes from the height.
    const QImage low = discs({{QPointF(800, 1000), 150}}, QSize(1600, 1200));
    QVERIFY(inside(ObjectSelection::select(low, QPointF(800, 1000), 0, false), QPointF(800, 1000)));
    // Smoothing keeps the object and drops the stairs.
    const std::optional<QPainterPath> smooth = ObjectSelection::select(pair, QPointF(110, 150), 0, true);
    QVERIFY(inside(smooth, QPointF(110, 150)) && !inside(smooth, QPointF(290, 150)));
    QVERIFY(points(smooth.value()) < points(left.value()));
    // Cutting corners shaves a convex outline a little.
    const int rounded = covered(smooth, pair.size());
    QVERIFY2(rounded < area && rounded > 0.97 * area, qPrintable(QString::number(rounded)));
}

void ObjectSelectionTests::theEdgeOffsetTightensAndLoosensTheOutline()
{
    const QImage disc = discs({{QPointF(200, 150), 80}});
    const int plain = covered(ObjectSelection::select(disc, QPointF(200, 150), 0, false), disc.size());
    const int tight = covered(ObjectSelection::select(disc, QPointF(200, 150), 4, false), disc.size());
    const int loose = covered(ObjectSelection::select(disc, QPointF(200, 150), -4, false), disc.size());
    // Square steps widen a round rim: 8 r k.
    const double ring = 8 * 80 * 4;
    QVERIFY2(std::abs(plain - tight - ring) < 0.05 * ring, qPrintable(QString::number(plain - tight)));
    QVERIFY2(std::abs(loose - plain - ring - 4 * 4 * 4) < 0.05 * ring, qPrintable(QString::number(loose - plain)));
}

void ObjectSelectionTests::theEdgeFollowsTheImageNotTheModelsGrid()
{
    // Grid cells five pixels wide; the image decides the edge.
    const QSize size(1600, 1200);
    const QImage disc = discs({{QPointF(800, 600), 320}}, size);
    const QImage coverage = DocumentSelection{ObjectSelection::select(disc, QPointF(800, 600), 0, false).value(), false}.coverage(size.width(), size.height());
    int wrong = 0, outside = 0;
    for (int y = 0; y < size.height(); ++y) {
        for (int x = 0; x < size.width(); ++x) {
            const bool truth = std::hypot(x + 0.5 - 800, y + 0.5 - 600) < 320;
            const bool chosen = coverage.constScanLine(y)[x] >= 128;
            wrong += truth != chosen;
            outside += chosen && !truth;
        }
    }
    // Measured: 190 wrong, 141 outside; the grid alone, 1682.
    QVERIFY2(wrong < 230 && outside > 70 && wrong - outside < 100, qPrintable(QStringLiteral("%1 %2").arg(wrong).arg(outside)));
}

void ObjectSelectionTests::theSessionSelectsCombinesAndNamesTheStep()
{
    const auto session = withDisc();
    bool done = false;
    session->selectObject(QPointF(200, 150), SelectionMode::replace, [&done] { done = true; });
    QVERIFY(session->isProjectBusy());
    QVERIFY(QTest::qWaitFor([&done] { return done; }, 20000));
    QVERIFY(!session->isProjectBusy());
    QCOMPARE(session->history.undoName(), QString("Object Selection"));
    QCOMPARE(coverage(*session, 200, 150), 255);
    QCOMPARE(coverage(*session, 10, 10), 0);
    // A traced outline is kept as traced, antialiased as set.
    QVERIFY(session->selection().value().antialiased);
    // Add and Subtract combine with what is there.
    session->applySelection(rectPath(QRectF(10, 10, 30, 30)), SelectionMode::replace, "Select");
    selectObject(*session, QPointF(200, 150), SelectionMode::add);
    QCOMPARE(session->history.undoName(), QString("Object Selection"));
    QCOMPARE(coverage(*session, 20, 20), 255);
    QCOMPARE(coverage(*session, 200, 150), 255);
    selectObject(*session, QPointF(200, 150), SelectionMode::subtract);
    QCOMPARE(coverage(*session, 20, 20), 255);
    QCOMPARE(coverage(*session, 200, 150), 0);
    // Background adds nothing, and New there deselects.
    const int steps = session->history.undoCount();
    selectObject(*session, QPointF(10, 290), SelectionMode::add);
    QCOMPARE(session->history.undoCount(), steps);
    QCOMPARE(coverage(*session, 20, 20), 255);
    selectObject(*session, QPointF(10, 290), SelectionMode::replace);
    QVERIFY(!session->selection());
    // The anti-alias setting reaches the selection.
    const auto whole = [](const QPainterPath &path) {
        for (int index = 0; index < path.elementCount(); ++index) {
            const QPainterPath::Element element = path.elementAt(index);
            if (element.x != std::round(element.x) || element.y != std::round(element.y))
                return false;
        }
        return true;
    };
    selectObject(*session, QPointF(200, 150), SelectionMode::replace);
    QVERIFY(!whole(session->selection().value().path));
    session->setSelectionAntialiased(false);
    selectObject(*session, QPointF(200, 150), SelectionMode::replace);
    QVERIFY(!session->selection().value().antialiased);
    // Without anti-alias the traced pixel edges stay.
    QVERIFY(whole(session->selection().value().path));
    // The document gone mid-run: the outline is dropped.
    done = false;
    session->selectObject(QPointF(200, 150), SelectionMode::replace, [&done] { done = true; });
    session->clearProject();
    QVERIFY(QTest::qWaitFor([&done] { return done; }, 20000));
    QVERIFY(!session->document() && !session->isProjectBusy());
}

void ObjectSelectionTests::theSessionRefusesAndReadsTheLayerItIsTold()
{
    const auto session = withDisc();
    session->selectAll();
    const int steps = session->history.undoCount();
    for (const QPointF point : {QPointF(-1, 10), QPointF(400, 10), QPointF(10, 300), QPointF(std::nan(""), 10)}) {
        selectObject(*session, point, SelectionMode::replace);
        QCOMPARE(coverage(*session, 10, 10), 255);
    }
    session->setIsProjectBusy(true);
    selectObject(*session, QPointF(200, 150), SelectionMode::replace);
    session->setIsProjectBusy(false);
    QVERIFY(session->beginSelectionMove());
    selectObject(*session, QPointF(200, 150), SelectionMode::replace);
    session->endSelectionMove();
    QCOMPARE(coverage(*session, 10, 10), 255);
    QCOMPARE(session->history.undoCount(), steps);
    // This Layer reads a blank layer: nothing there.
    session->addBlankLayer();
    ObjectSelectionSettings own = session->objectSelectionSettings();
    own.sampleAllLayers = false;
    session->setObjectSelectionSettings(own);
    selectObject(*session, QPointF(200, 150), SelectionMode::replace);
    QVERIFY(!session->selection());
    own.sampleAllLayers = true;
    session->setObjectSelectionSettings(own);
    selectObject(*session, QPointF(200, 150), SelectionMode::replace);
    QCOMPARE(coverage(*session, 200, 150), 255);
    // The edge offset reaches the trace, clamped to ten.
    const int plain = covered(session->selection().value().path, QSize(400, 300));
    own.edgeOffset = 50;
    session->setObjectSelectionSettings(own);
    selectObject(*session, QPointF(200, 150), SelectionMode::replace);
    const int clamped = covered(session->selection().value().path, QSize(400, 300));
    QCOMPARE(session->objectSelectionSettings().edgeOffset, 50);
    const QImage disc = discs({{QPointF(200, 150), 80}});
    QCOMPARE(clamped, covered(ObjectSelection::select(disc, QPointF(200, 150), 10, true), QSize(400, 300)));
    QVERIFY(clamped < plain);
    own.edgeOffset = -50;
    session->setObjectSelectionSettings(own);
    selectObject(*session, QPointF(200, 150), SelectionMode::replace);
    QCOMPARE(covered(session->selection().value().path, QSize(400, 300)), covered(ObjectSelection::select(disc, QPointF(200, 150), -10, true), QSize(400, 300)));
}

void ObjectSelectionTests::theModeSwitchesByTabAndWKeepsIt()
{
    EditorSession session;
    session.createDocument(20, 20);
    QCOMPARE(session.wandMode(), WandMode::wand);
    QCOMPARE(rawValue(WandMode::wand), QString("Wand"));
    QCOMPARE(rawValue(WandMode::object), QString("Object"));
    QSignalSpy changed(&session, &EditorSession::changed);
    session.setWandMode(WandMode::object);
    QCOMPARE(changed.count(), 1);
    ObjectSelectionSettings settings;
    QVERIFY(settings.sampleAllLayers && settings.edgeOffset == 0);
    settings.edgeOffset = -3;
    session.setObjectSelectionSettings(settings);
    QCOMPARE(changed.count(), 2);
    QCOMPARE(session.objectSelectionSettings(), settings);
    // Tab cycles the mode only under the Magic tool.
    session.selectTool(NavigationTool::lasso);
    session.cycleToolMode();
    QCOMPARE(session.wandMode(), WandMode::object);
    session.pressWandKey();
    QCOMPARE(session.tool(), NavigationTool::wand);
    QCOMPARE(session.wandMode(), WandMode::object);
    session.cycleToolMode();
    QCOMPARE(session.wandMode(), WandMode::wand);
    session.cycleToolMode();
    QCOMPARE(session.wandMode(), WandMode::object);
    QCOMPARE(label(NavigationTool::wand), QString("Magic (W) · Tab switches Wand and Object"));
    // The list's W picks the tool; a repeat changes nothing.
    NativeLayerList list(session);
    list.show();
    QVERIFY(QTest::qWaitForWindowExposed(&list));
    list.setFocus();
    QTRY_VERIFY(list.hasFocus());
    session.selectTool(NavigationTool::brush);
    QKeyEvent repeat(QEvent::KeyPress, Qt::Key_W, Qt::NoModifier, QStringLiteral("w"), true);
    QApplication::sendEvent(&list, &repeat);
    QCOMPARE(session.tool(), NavigationTool::brush);
    QTest::keyClick(&list, Qt::Key_W);
    QCOMPARE(session.tool(), NavigationTool::wand);
    QCOMPARE(session.wandMode(), WandMode::object);
}

void ObjectSelectionTests::theCanvasClicksPicksAgainInsideAndShowsTheObjectCursor()
{
    Canvas shown;
    EditorSession &session = shown.session;
    const QImage image = discs({{QPointF(200, 150), 80}});
    session.insert(ImportedImage(image, image, QStringLiteral("Disc")));
    session.selectTool(NavigationTool::wand);
    shown.canvas->synchronizeDisplay();
    const double ratio = shown.canvas->devicePixelRatio();
    shown.hover(QPointF(20, 20));
    QVERIFY(shown.shows(CanvasView::wandCursor(SelectionMode::replace, ratio)));
    // Tab on the canvas switches to Object and its cursor.
    shown.canvas->setFocus();
    QTRY_VERIFY(shown.canvas->hasFocus());
    QTest::keyClick(shown.canvas, Qt::Key_Tab);
    QCOMPARE(session.wandMode(), WandMode::object);
    shown.hover(QPointF(20, 20));
    QVERIFY(shown.shows(CanvasView::selectionCursor(SelectionIcon::objectSelection, SelectionMode::replace, ratio)));
    shown.hover(QPointF(20, 20), Qt::ShiftModifier);
    QVERIFY(shown.shows(CanvasView::selectionCursor(SelectionIcon::objectSelection, SelectionMode::add, ratio)));
    // A click traces the object under the document point.
    session.zoom(2);
    shown.canvas->synchronizeDisplay();
    shown.click(session.viewport.viewPoint(QPointF(260, 150), shown.documentSize()));
    QTRY_VERIFY_WITH_TIMEOUT(!session.isProjectBusy() && session.selection().has_value(), 20000);
    QCOMPARE(session.history.undoName(), QString("Object Selection"));
    QCOMPARE(shown.coverage(200, 150), 255);
    QCOMPARE(shown.coverage(20, 20), 0);
    session.zoom(1);
    shown.canvas->synchronizeDisplay();
    // A click inside a selection picks afresh, never deselects.
    session.selectAll();
    shown.canvas->synchronizeDisplay();
    shown.click(QPointF(200, 150));
    QTRY_VERIFY_WITH_TIMEOUT(!session.isProjectBusy() && shown.coverage(20, 20) == 0, 20000);
    QCOMPARE(shown.coverage(200, 150), 255);
    QCOMPARE(session.history.undoName(), QString("Object Selection"));
    // Shift adds the object to a box elsewhere.
    session.applySelection(rectPath(QRectF(10, 10, 20, 20)), SelectionMode::replace, "Select");
    shown.canvas->synchronizeDisplay();
    shown.click(QPointF(200, 150), Qt::ShiftModifier);
    QTRY_VERIFY_WITH_TIMEOUT(!session.isProjectBusy() && shown.coverage(200, 150) == 255, 20000);
    QCOMPARE(shown.coverage(20, 20), 255);
    // Wand mode's click matches colour again.
    session.setWandMode(WandMode::wand);
    session.deselect();
    shown.canvas->synchronizeDisplay();
    shown.click(QPointF(20, 20));
    QTRY_VERIFY_WITH_TIMEOUT(!session.isProjectBusy() && session.selection().has_value(), 20000);
    QCOMPARE(session.history.undoName(), QString("Magic Wand"));
    QCOMPARE(shown.coverage(20, 20), 255);
    QCOMPARE(shown.coverage(200, 150), 0);
    // W picks Magic, keeps the mode, and ignores a repeat.
    session.setWandMode(WandMode::object);
    session.selectTool(NavigationTool::brush);
    QKeyEvent repeat(QEvent::KeyPress, Qt::Key_W, Qt::NoModifier, QStringLiteral("w"), true);
    QApplication::sendEvent(shown.canvas, &repeat);
    QCOMPARE(session.tool(), NavigationTool::brush);
    QTest::keyClick(shown.canvas, Qt::Key_W);
    QCOMPARE(session.tool(), NavigationTool::wand);
    QCOMPARE(session.wandMode(), WandMode::object);
}

QTEST_MAIN(ObjectSelectionTests)
#include "ObjectSelectionTests.moc"
