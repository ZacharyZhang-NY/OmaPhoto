#include "AddressSpaceLimit.h"
#include "RenderFixtures.h"
#include "SelectionFixtures.h"

// The Marquee's boxes and thumbnails loaded as selections.
namespace {
void marquee(EditorSession &session, QPointF start, QPointF end, SelectionMode mode = SelectionMode::replace, bool square = false, bool fromCenter = false)
{
    session.beginLasso(start, mode);
    session.dragMarquee(end, square, fromCenter);
    session.finishLasso();
}

// A mask: a black square with a white hole.
QUuid maskedLayer(EditorSession &session)
{
    const QUuid id = session.activeLayerID().value();
    QImage mask = BrushRaster::context(100, 100, true);
    mask.fill(Qt::white);
    QPainter painter(&mask);
    painter.fillRect(QRect(20, 30, 40, 40), Qt::black);
    painter.fillRect(QRect(30, 40, 10, 10), Qt::white);
    painter.end();
    rewrite(session, [&](ProjectSnapshot &snapshot) { setMask(snapshot, id, LayerMask::assetFrom(mask)); });
    return id;
}
}

class SelectionSourceTests : public QObject {
    Q_OBJECT
private slots:
    void marqueeDrawsWholePixelRectanglesInAnyDirection();
    void marqueeShiftMakesSquaresAndCenteredDragsGrowFromTheAnchor();
    void marqueeEllipseSelectsAnOvalInItsBoxAndShiftMakesACircle();
    void ctrlClickingAMaskSelectsItsBlackAreas();
    void maskSelectionFollowsTheLayerTransformAndIgnoresAllWhiteMasks();
    void ctrlClickingALayerSelectsItsOpaquePixels();
    void paintedPixelsThatCannotFlattenLoadNothing();
};

void SelectionSourceTests::marqueeDrawsWholePixelRectanglesInAnyDirection()
{
    const auto session = selectionSession();
    session->selectTool(NavigationTool::marquee);
    marquee(*session, QPointF(60.4, 70.6), QPointF(20.2, 30.3));
    QCOMPARE(session->history.undoName(), QString("Rectangular Marquee"));
    QCOMPARE(bounds(*session), QRectF(20, 30, 40, 41));
    QCOMPARE(coverage(*session, 20, 30), 255);
    QCOMPARE(coverage(*session, 19, 30), 0);
    marquee(*session, QPointF(80, 80), QPointF(90, 90), SelectionMode::add);
    QCOMPARE(coverage(*session, 85, 85), 255);
    QCOMPARE(coverage(*session, 40, 50), 255);
    marquee(*session, QPointF(30, 40), QPointF(50, 60), SelectionMode::subtract);
    QCOMPARE(coverage(*session, 40, 50), 0);
    QCOMPARE(coverage(*session, 25, 35), 255);
    // A click deselects; a wild drag point is ignored.
    session->beginLasso(QPointF(5, 5), SelectionMode::replace);
    session->dragMarquee(QPointF(std::nan(""), 5), false, false);
    QCOMPARE(session->lassoDraft().value().points.size(), size_t(1));
    session->finishLasso();
    QVERIFY(!session->selection().has_value());
    // A lasso draft takes no marquee drag.
    session->selectTool(NavigationTool::lasso);
    session->beginLasso(QPointF(5, 5), SelectionMode::replace);
    session->dragMarquee(QPointF(50, 50), false, false);
    QCOMPARE(session->lassoDraft().value().points.size(), size_t(1));
    QVERIFY(!session->lassoDraft().value().anchor.has_value());
    session->cancelLasso();
}

void SelectionSourceTests::marqueeShiftMakesSquaresAndCenteredDragsGrowFromTheAnchor()
{
    const auto session = selectionSession();
    session->selectTool(NavigationTool::marquee);
    marquee(*session, QPointF(10, 10), QPointF(40, 20), SelectionMode::replace, true);
    QCOMPARE(bounds(*session), QRectF(10, 10, 30, 30));
    marquee(*session, QPointF(50, 50), QPointF(60, 55), SelectionMode::replace, false, true);
    QCOMPARE(bounds(*session), QRectF(40, 45, 20, 10));
    marquee(*session, QPointF(50, 50), QPointF(45, 58), SelectionMode::replace, true, true);
    QCOMPARE(bounds(*session), QRectF(42, 42, 16, 16));
    QVERIFY(isSelectionTool(NavigationTool::marquee) && !isSelectionTool(NavigationTool::brush));
    // The anchor rounds; the box follows the drag's sign.
    QCOMPARE(DragBox::rect(QPointF(10, 10), QPointF(3.4, 15.6), false, false), QRectF(3, 10, 7, 6));
    QCOMPARE(DragBox::rect(QPointF(10, 10), QPointF(3, 16), true, false), QRectF(3, 10, 7, 7));
}

void SelectionSourceTests::marqueeEllipseSelectsAnOvalInItsBoxAndShiftMakesACircle()
{
    const auto session = selectionSession();
    session->selectTool(NavigationTool::marquee);
    session->toggleMarqueeKind();
    QVERIFY(session->marqueeKind() == LassoKind::ellipse);
    session->beginLasso(QPointF(10, 20), SelectionMode::replace);
    session->dragMarquee(QPointF(70, 60), false, false);
    session->finishLasso();
    QCOMPARE(session->history.undoName(), QString("Elliptical Marquee"));
    const QRectF box = bounds(*session);
    QVERIFY(std::abs(box.left() - 10) < 0.5 && std::abs(box.right() - 70) < 0.5 && std::abs(box.top() - 20) < 0.5 && std::abs(box.bottom() - 60) < 0.5);
    QCOMPARE(coverage(*session, 40, 40), 255);
    // The box's corner lies outside the oval.
    QCOMPARE(coverage(*session, 11, 21), 0);
    session->beginLasso(QPointF(5, 5), SelectionMode::replace);
    session->dragMarquee(QPointF(45, 25), true, false);
    session->finishLasso();
    const QRectF circle = bounds(*session);
    QVERIFY(std::abs(circle.width() - circle.height()) < 0.5);
    // A click with the ellipse deselects too.
    marquee(*session, QPointF(5, 5), QPointF(5, 5));
    QVERIFY(!session->selection().has_value());
    session->beginLasso(QPointF(1, 1), SelectionMode::replace);
    session->toggleMarqueeKind();
    QVERIFY(session->marqueeKind() == LassoKind::rectangle && !session->lassoDraft().has_value());
}

void SelectionSourceTests::ctrlClickingAMaskSelectsItsBlackAreas()
{
    const auto session = selectionSession();
    const QUuid id = maskedLayer(*session);
    session->loadMaskSelection(id);
    QCOMPARE(session->history.undoName(), QString("Load Mask Selection"));
    // Black is selected; the white hole and surroundings are not.
    QCOMPARE(coverage(*session, 25, 35), 255);
    QCOMPARE(coverage(*session, 35, 45), 0);
    QCOMPARE(coverage(*session, 80, 80), 0);
    // Exact pixel edges.
    QCOMPARE(coverage(*session, 59, 69), 255);
    QCOMPARE(coverage(*session, 60, 70), 0);
    // Shift adds and Alt subtracts.
    lasso(*session, square(80, 80, 10));
    session->loadMaskSelection(id, SelectionMode::add);
    QCOMPARE(coverage(*session, 85, 85), 255);
    QCOMPARE(coverage(*session, 25, 35), 255);
    session->loadMaskSelection(id, SelectionMode::subtract);
    QCOMPARE(coverage(*session, 85, 85), 255);
    QCOMPARE(coverage(*session, 25, 35), 0);
    // An unknown layer, or one without a mask, loads nothing.
    const int count = session->history.undoCount();
    EditorSession empty;
    empty.loadMaskSelection(id);
    empty.loadLayerSelection(id);
    QVERIFY(!empty.document().has_value());
    session->loadMaskSelection(QUuid::createUuid());
    session->addBlankLayer();
    session->loadMaskSelection(session->activeLayerID().value());
    QCOMPARE(session->history.undoCount(), count + 1);
}

void SelectionSourceTests::maskSelectionFollowsTheLayerTransformAndIgnoresAllWhiteMasks()
{
    const auto session = selectionSession(200, 200);
    const QUuid id = maskedLayer(*session);
    // The layer and its mask stretched 2× from the origin.
    rewrite(*session, [&](ProjectSnapshot &snapshot) {
        record(snapshot, id).transform.origin = QPointF(0, 0);
        record(snapshot, id).transform.size = QSizeF(200, 200);
    });
    session->loadMaskSelection(id);
    QCOMPARE(bounds(*session), QRectF(40, 60, 80, 80));
    // A mask placed apart loads where it shows.
    rewrite(*session, [&](ProjectSnapshot &snapshot) {
        record(snapshot, id).maskPlacement = LayerTransform{.origin = {10, 0}, .size = {100, 100}};
        record(snapshot, id).maskLinked = false;
    });
    session->loadMaskSelection(id);
    QCOMPARE(bounds(*session), QRectF(30, 30, 40, 40));
    session->deselect();
    session->addBlankLayer();
    session->addLayerMask(true);
    // No black anywhere: nothing to select.
    session->loadMaskSelection(session->activeLayerID().value());
    QVERIFY(!session->selection().has_value());
}

void SelectionSourceTests::ctrlClickingALayerSelectsItsOpaquePixels()
{
    const auto session = selectionSession(200, 200);
    // An opaque ring, a clear centre, a faint corner pixel.
    QImage image = BrushRaster::context(50, 50, false);
    QPainter painter(&image);
    painter.fillRect(QRect(10, 10, 30, 30), Qt::red);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(QRect(20, 20, 10, 10), Qt::transparent);
    painter.fillRect(QRect(0, 0, 1, 1), QColor(255, 0, 0, 64));
    painter.end();
    session->insert(ImportedImage(image, image, "Ring"));
    const QUuid id = session->activeLayerID().value();
    rewrite(*session, [&](ProjectSnapshot &snapshot) {
        record(snapshot, id).transform.origin = QPointF(50, 50);
        record(snapshot, id).transform.size = QSizeF(100, 100);
    });
    session->loadLayerSelection(id);
    QCOMPARE(session->history.undoName(), QString("Load Layer Selection"));
    // Twice the size: the ring's box doubles.
    QCOMPARE(bounds(*session), QRectF(70, 70, 60, 60));
    QCOMPARE(coverage(*session, 75, 75), 255);
    QCOMPARE(coverage(*session, 100, 100), 0);
    // The 25% pixel is under the threshold.
    QCOMPARE(coverage(*session, 50, 50), 0);
    lasso(*session, square(0, 0, 20));
    session->loadLayerSelection(id, SelectionMode::add);
    QCOMPARE(coverage(*session, 10, 10), 255);
    QCOMPARE(coverage(*session, 75, 75), 255);
    // An empty layer selects nothing; nor does a folder.
    session->addBlankLayer();
    const std::optional<DocumentSelection> before = session->selection();
    session->loadLayerSelection(session->activeLayerID().value());
    QVERIFY(session->selection() == before);
    session->addGroup();
    session->loadLayerSelection(session->activeLayerID().value());
    QVERIFY(session->selection() == before);
    session->loadLayerSelection(QUuid::createUuid());
    QVERIFY(session->selection() == before);
}

void SelectionSourceTests::paintedPixelsThatCannotFlattenLoadNothing()
{
    // Two painted bases flatten on first use.
    const auto session = selectionSession(4000, 3000);
    const auto pixels = std::make_shared<const RasterSnapshot>(4000, 3000, solid(2, 2, qRgba(255, 0, 0, 255)), QRectF(0, 0, 2, 2), std::vector<BrushPatch>{});
    session->insert(ImportedImage(pixels, QImage(), "Painted"));
    const QUuid id = session->activeLayerID().value();
    QImage gray(2, 2, QImage::Format_Grayscale8);
    gray.fill(Qt::black);
    const auto mask = std::make_shared<const RasterSnapshot>(4000, 3000, gray, QRectF(0, 0, 2, 2), std::vector<BrushPatch>{}, true);
    rewrite(*session, [&](ProjectSnapshot &snapshot) { setMask(snapshot, id, ImportedImage(mask, gray, "Mask")); });
    const int count = session->history.undoCount();
    {
        const AddressSpaceLimit limit(8 * 1024 * 1024);
        session->loadLayerSelection(id);
        session->loadMaskSelection(id);
    }
    QVERIFY(!session->selection().has_value());
    QCOMPARE(session->history.undoCount(), count);
    QVERIFY(!pixels->hasMaterializedPixels() && !mask->hasMaterializedPixels());
    // With room, the mask loads: its black base, white around.
    session->loadMaskSelection(id);
    QCOMPARE(bounds(*session), QRectF(0, 0, 2, 2));
}

QTEST_GUILESS_MAIN(SelectionSourceTests)
#include "SelectionSourceTests.moc"
