#include "SelectionFixtures.h"

// Outlines, modes, moves and resizes of the selection.
class SelectionTests : public QObject {
    Q_OBJECT
private slots:
    void replaceAddAndSubtractCombineOutlines();
    void modifiersPickModeAndSelectionIsClippedToCanvas();
    void emptySelectionIsDistinctFromNoSelection();
    void clickDeselectsAndSelectionStepsUndo();
    void polygonalCornersCanBeRemovedAndClosed();
    void antialiasingControlsEdgeCoverage();
    void selectAllInverseAndToolSwitchCancelsDraft();
    void inverseOfEverythingDeselects();
    void cursorBadgeFollowsModifiersButKeepsAnOutlinesStartingMode();
    void draggingMovesTheOutlineInWholePixelsAsOneUndo();
    void movingOffCanvasAndBackKeepsTheWholeShape();
    void arrowNudgesAndMoveIsOnlyForNewModeOnARealSelection();
    void expandAndContractGrowAndShrinkTheOutline();
    void expandStaysOnCanvasAndContractCanEmptyTheSelection();
    void theClipCoversTheOutlinesRegionAndAnEmptyOneNothing();
    void draftsRefuseWildPointsAndOtherToolsAndKeepTheirCursor();
};

void SelectionTests::replaceAddAndSubtractCombineOutlines()
{
    const auto session = selectionSession();
    lasso(*session, square(10, 10, 40));
    QCOMPARE(coverage(*session, 30, 30), 255);
    QCOMPARE(coverage(*session, 70, 70), 0);
    lasso(*session, square(50, 50, 40), SelectionMode::add);
    QCOMPARE(coverage(*session, 30, 30), 255);
    QCOMPARE(coverage(*session, 70, 70), 255);
    lasso(*session, square(20, 20, 20), SelectionMode::subtract);
    QCOMPARE(coverage(*session, 30, 30), 0);
    QCOMPARE(coverage(*session, 15, 15), 255);
    lasso(*session, square(60, 10, 20));
    QCOMPARE(coverage(*session, 70, 20), 255);
    QCOMPARE(coverage(*session, 70, 70), 0);
    QCOMPARE(coverage(*session, 15, 15), 0);
}

void SelectionTests::modifiersPickModeAndSelectionIsClippedToCanvas()
{
    const auto session = selectionSession();
    QVERIFY(session->selectionMode(false, false) == SelectionMode::replace);
    QVERIFY(session->selectionMode(true, false) == SelectionMode::add);
    QVERIFY(session->selectionMode(true, true) == SelectionMode::subtract);
    QVERIFY(session->selectionMode(false, true) == SelectionMode::subtract);
    lasso(*session, square(-50, -50, 100));
    const QRectF box = bounds(*session);
    QVERIFY(box.left() >= 0 && box.top() >= 0 && box.right() <= 50.001 && box.bottom() <= 50.001);
    QCOMPARE(coverage(*session, 49, 49), 255);
    QCOMPARE(coverage(*session, 50, 50), 0);
}

void SelectionTests::emptySelectionIsDistinctFromNoSelection()
{
    const auto session = selectionSession();
    // Nothing to subtract from.
    lasso(*session, square(0, 0, 50), SelectionMode::subtract);
    QVERIFY(!session->selection().has_value());
    lasso(*session, square(10, 10, 20));
    lasso(*session, square(0, 0, 60), SelectionMode::subtract);
    QVERIFY(session->selection().value().isEmpty());
    QCOMPARE(coverage(*session, 20, 20), 0);
    session->deselect();
    QVERIFY(!session->selection().has_value());
    // Deselecting nothing is refused and records nothing.
    const int count = session->history.undoCount();
    session->deselect();
    QCOMPARE(session->history.undoCount(), count);
}

void SelectionTests::clickDeselectsAndSelectionStepsUndo()
{
    const auto session = selectionSession();
    const int count = session->history.undoCount();
    lasso(*session, square(10, 10, 40));
    QCOMPARE(session->history.undoCount(), count + 1);
    QCOMPARE(session->history.undoName(), QString("Lasso"));
    lasso(*session, {QPointF(5, 5)});
    QVERIFY(!session->selection().has_value());
    QCOMPARE(session->history.undoName(), QString("Deselect"));
    session->undo();
    QVERIFY(!session->selection().value().isEmpty());
    session->undo();
    QVERIFY(!session->selection().has_value());
    session->redo();
    QCOMPARE(coverage(*session, 30, 30), 255);
    // The same square traced twice fills by winding, not odd-even.
    std::vector<QPointF> twice = square(20, 20, 30);
    twice.insert(twice.end(), {QPointF(20, 20), QPointF(50, 20), QPointF(50, 50), QPointF(20, 50)});
    lasso(*session, twice);
    QCOMPARE(coverage(*session, 35, 35), 255);
    // A click in Add mode keeps what there is.
    lasso(*session, {QPointF(5, 5)}, SelectionMode::add);
    QCOMPARE(coverage(*session, 30, 30), 255);
}

void SelectionTests::polygonalCornersCanBeRemovedAndClosed()
{
    const auto session = selectionSession();
    session->toggleLassoKind();
    QVERIFY(session->lassoKind() == LassoKind::polygonal);
    session->beginLasso(QPointF(10, 10), SelectionMode::replace);
    session->extendLasso(QPointF(90, 10));
    // A misplaced corner, taken back.
    session->extendLasso(QPointF(50, 50));
    session->removeLastLassoPoint();
    session->extendLasso(QPointF(90, 90));
    session->extendLasso(QPointF(10, 90));
    QCOMPARE(session->lassoDraft().value().points.size(), size_t(4));
    session->finishLasso();
    QCOMPARE(session->history.undoName(), QString("Polygonal Lasso"));
    QCOMPARE(coverage(*session, 80, 80), 255);
    QCOMPARE(coverage(*session, 5, 50), 0);
    session->beginLasso(QPointF(1, 1), SelectionMode::replace);
    session->cancelLasso();
    QVERIFY(!session->lassoDraft().has_value());
    QVERIFY(!session->selection().value().isEmpty());
    // Taking back the only point ends the draft.
    session->beginLasso(QPointF(1, 1), SelectionMode::replace);
    session->removeLastLassoPoint();
    QVERIFY(!session->lassoDraft().has_value());
    // A toggle ends a draft in progress.
    session->beginLasso(QPointF(1, 1), SelectionMode::replace);
    session->toggleLassoKind();
    QVERIFY(session->lassoKind() == LassoKind::freehand && !session->lassoDraft().has_value());
}

void SelectionTests::antialiasingControlsEdgeCoverage()
{
    const auto session = selectionSession();
    const std::vector<QPointF> triangle{QPointF(0, 0), QPointF(100, 0), QPointF(0, 100)};
    lasso(*session, triangle);
    // Pixels on x + y = 99 straddle the diagonal.
    int soft = 0, partial = 0;
    for (int x = 0; x < 100; ++x) {
        const int value = coverage(*session, x, 99 - x);
        soft += value > 0 && value < 255;
    }
    QVERIFY(soft > 0);
    QVERIFY(session->selectionAntialiased());
    session->setSelectionAntialiased(false);
    lasso(*session, triangle);
    QVERIFY(!session->selection().value().antialiased);
    for (int x = 0; x < 100; ++x) {
        const int value = coverage(*session, x, 99 - x);
        partial += value > 0 && value < 255;
    }
    QCOMPARE(partial, 0);
    // The hard edge survives Inverse, Expand and Contract.
    session->invertSelection();
    QVERIFY(!session->selection().value().antialiased);
    session->expandSelection(1);
    QVERIFY(!session->selection().value().antialiased);
    session->contractSelection(1);
    QVERIFY(!session->selection().value().antialiased);
}

void SelectionTests::selectAllInverseAndToolSwitchCancelsDraft()
{
    const auto session = selectionSession();
    session->selectAll();
    QCOMPARE(session->history.undoName(), QString("Select All"));
    QCOMPARE(coverage(*session, 0, 0), 255);
    QCOMPARE(coverage(*session, 99, 99), 255);
    lasso(*session, square(0, 0, 50));
    session->invertSelection();
    QCOMPARE(session->history.undoName(), QString("Inverse"));
    QCOMPARE(coverage(*session, 25, 25), 0);
    QCOMPARE(coverage(*session, 75, 75), 255);
    session->beginLasso(QPointF(5, 5), SelectionMode::replace);
    session->selectTool(NavigationTool::brush);
    QVERIFY(!session->lassoDraft().has_value());
    // Selecting all twice records one step.
    const int count = session->history.undoCount();
    session->selectAll();
    session->selectAll();
    QCOMPARE(session->history.undoCount(), count + 1);
}

// Swift's: Select All then Inverse leaves nothing selected.
void SelectionTests::inverseOfEverythingDeselects()
{
    const auto session = selectionSession();
    session->selectAll();
    session->invertSelection();
    QVERIFY(!session->selection().has_value());
    QCOMPARE(session->history.undoName(), QString("Inverse"));
    session->undo();
    QVERIFY(!session->selection().value().isEmpty());
    // Everything but a corner inverts to that corner.
    lasso(*session, square(0, 0, 10));
    session->invertSelection();
    session->invertSelection();
    QCOMPARE(bounds(*session), QRectF(0, 0, 10, 10));
}

void SelectionTests::cursorBadgeFollowsModifiersButKeepsAnOutlinesStartingMode()
{
    const auto session = selectionSession();
    QVERIFY(session->lassoCursorMode(false, false) == SelectionMode::replace);
    QVERIFY(session->lassoCursorMode(true, false) == SelectionMode::add);
    QVERIFY(session->lassoCursorMode(false, true) == SelectionMode::subtract);
    session->setSelectionModeChoice(SelectionMode::add);
    QVERIFY(session->lassoCursorMode(false, false) == SelectionMode::add);
    session->beginLasso(QPointF(5, 5), SelectionMode::subtract);
    QVERIFY(session->lassoCursorMode(false, false) == SelectionMode::subtract);
    QVERIFY(session->lassoCursorMode(true, false) == SelectionMode::subtract);
    QVERIFY(session->displayedSelectionMode() == SelectionMode::subtract);
    session->cancelLasso();
    session->setSelectionModeChoice(SelectionMode::replace);
    session->updateHeldSelectionKeys(true, false);
    QVERIFY(session->displayedSelectionMode() == SelectionMode::add);
    QVERIFY(session->selectionModeChoice() == SelectionMode::replace);
    QVERIFY(session->heldSelectionMode() == std::optional(SelectionMode::add));
    session->updateHeldSelectionKeys(true, true);
    QVERIFY(session->displayedSelectionMode() == SelectionMode::subtract);
    session->updateHeldSelectionKeys(false, false);
    QVERIFY(session->displayedSelectionMode() == SelectionMode::replace);
    QVERIFY(!session->heldSelectionMode().has_value());
}

void SelectionTests::draggingMovesTheOutlineInWholePixelsAsOneUndo()
{
    const auto session = selectionSession();
    lasso(*session, square(10, 10, 20));
    QVERIFY(session->canMoveSelection(QPointF(20, 20)));
    QVERIFY(!session->canMoveSelection(QPointF(60, 60)));
    const int count = session->history.undoCount();
    QVERIFY(session->beginSelectionMove());
    // A second begin waits for the first to end.
    QVERIFY(!session->beginSelectionMove());
    session->moveSelection(QSizeF(10.4, 29.6));
    session->moveSelection(QSizeF(40.2, 40.4));
    session->endSelectionMove();
    QCOMPARE(session->history.undoCount(), count + 1);
    QCOMPARE(session->history.undoName(), QString("Move Selection"));
    QCOMPARE(bounds(*session), QRectF(50, 50, 20, 20));
    QCOMPARE(coverage(*session, 55, 55), 255);
    QCOMPARE(coverage(*session, 15, 15), 0);
    session->undo();
    QCOMPARE(bounds(*session), QRectF(10, 10, 20, 20));
    // Ended without a begin: nothing happens.
    session->endSelectionMove();
    session->moveSelection(QSizeF(5, 5));
    QCOMPARE(bounds(*session), QRectF(10, 10, 20, 20));
}

void SelectionTests::movingOffCanvasAndBackKeepsTheWholeShape()
{
    const auto session = selectionSession();
    lasso(*session, square(10, 10, 20));
    QVERIFY(session->beginSelectionMove());
    session->moveSelection(QSizeF(-25, 0));
    session->endSelectionMove();
    QCOMPARE(coverage(*session, 0, 20), 255);
    QVERIFY(session->beginSelectionMove());
    session->moveSelection(QSizeF(25, 0));
    session->endSelectionMove();
    QCOMPARE(bounds(*session), QRectF(10, 10, 20, 20));
}

void SelectionTests::arrowNudgesAndMoveIsOnlyForNewModeOnARealSelection()
{
    const auto session = selectionSession();
    // Nothing selected.
    QVERIFY(!session->beginSelectionMove());
    lasso(*session, square(10, 10, 20));
    const int count = session->history.undoCount();
    session->nudgeSelection(1, 0);
    session->nudgeSelection(0, -10);
    QCOMPARE(bounds(*session), QRectF(11, 0, 20, 20));
    QCOMPARE(session->history.undoCount(), count + 2);
    // Shift draws an added outline instead of moving.
    QVERIFY(session->selectionMode(true, false) != SelectionMode::replace);
    session->beginLasso(QPointF(5, 5), SelectionMode::add);
    QVERIFY(!session->canMoveSelection(QPointF(20, 10)));
    session->cancelLasso();
    lasso(*session, square(0, 0, 60), SelectionMode::subtract);
    QVERIFY(session->selection().value().isEmpty());
    QVERIFY(!session->beginSelectionMove());
    session->nudgeSelection(1, 0);
    QCOMPARE(session->history.undoCount(), count + 3);
}

void SelectionTests::expandAndContractGrowAndShrinkTheOutline()
{
    const auto session = selectionSession();
    QVERIFY(!session->canModifySelection());
    lasso(*session, square(40, 40, 20));
    QVERIFY(session->canModifySelection());
    session->expandSelection(5);
    QCOMPARE(session->history.undoName(), QString("Expand Selection"));
    const QRectF grown = bounds(*session);
    QVERIFY(std::abs(grown.left() - 35) < 0.01 && std::abs(grown.width() - 30) < 0.01);
    QCOMPARE(coverage(*session, 37, 50), 255);
    QCOMPARE(coverage(*session, 33, 50), 0);
    // Rounded corners: the grown box's corner pixel is outside.
    QCOMPARE(coverage(*session, 35, 35), 0);
    QVERIFY(coverage(*session, 36, 36) > 100);
    session->contractSelection(8);
    QCOMPARE(session->history.undoName(), QString("Contract Selection"));
    const QRectF shrunk = bounds(*session);
    QVERIFY(std::abs(shrunk.left() - 43) < 0.01 && std::abs(shrunk.width() - 14) < 0.01);
    session->undo();
    QVERIFY(std::abs(bounds(*session).width() - 30) < 0.01);
    // Nothing, a wild amount, or a draft in progress: refused.
    const int count = session->history.undoCount();
    session->expandSelection(0);
    session->expandSelection(501);
    session->beginLasso(QPointF(1, 1), SelectionMode::replace);
    QVERIFY(!session->canModifySelection());
    session->contractSelection(1);
    session->cancelLasso();
    QCOMPARE(session->history.undoCount(), count);
}

void SelectionTests::expandStaysOnCanvasAndContractCanEmptyTheSelection()
{
    const auto session = selectionSession();
    session->selectAll();
    session->expandSelection(10);
    QCOMPARE(bounds(*session), QRectF(0, 0, 100, 100));
    // Pulls in from the canvas edges too.
    session->contractSelection(10);
    QCOMPARE(coverage(*session, 5, 50), 0);
    QCOMPARE(coverage(*session, 50, 50), 255);
    session->contractSelection(45);
    QVERIFY(session->selection().value().isEmpty());
    QVERIFY(!session->canModifySelection());
}

void SelectionTests::theClipCoversTheOutlinesRegionAndAnEmptyOneNothing()
{
    const auto session = selectionSession();
    lasso(*session, square(10, 10, 20));
    const SelectionClip clip = session->selection().value().clip(QSizeF(100, 100));
    // One pixel around the outline, whole pixels.
    QCOMPARE(clip.rect, QRectF(9, 9, 22, 22));
    QCOMPARE(clip.coverage.value().size(), QSize(22, 22));
    QCOMPARE(int(clip.coverage.value().constScanLine(0)[0]), 0);
    QCOMPARE(int(clip.coverage.value().constScanLine(1)[1]), 255);
    QCOMPARE(int(clip.coverage.value().constScanLine(20)[20]), 255);
    QCOMPARE(int(clip.coverage.value().constScanLine(21)[21]), 0);
    // At the canvas edge the region stops with the canvas.
    lasso(*session, square(-5, -5, 20));
    const SelectionClip edge = session->selection().value().clip(QSizeF(100, 100));
    QCOMPARE(edge.rect, QRectF(0, 0, 16, 16));
    QCOMPARE(int(edge.coverage.value().constScanLine(14)[14]), 255);
    QCOMPARE(int(edge.coverage.value().constScanLine(15)[15]), 0);
    lasso(*session, square(0, 0, 60), SelectionMode::subtract);
    const SelectionClip empty = session->selection().value().clip(QSizeF(100, 100));
    QVERIFY(empty.rect.isNull() && !empty.coverage.has_value());
    // An outline without area is empty too.
    session->applySelection(rectPath(QRectF(10, 10, 0, 30)), SelectionMode::replace, "Select");
    QVERIFY(session->selection().value().isEmpty());
    QVERIFY(!session->selection().value().clip(QSizeF(100, 100)).coverage.has_value());
    QVERIFY(DocumentSelection{rectPath(QRectF(10, 10, 0, 30))}.isEmpty());
    QVERIFY(!DocumentSelection{rectPath(QRectF(10, 10, 1, 30))}.isEmpty());
    // A hard-edged clip is never soft.
    session->setSelectionAntialiased(false);
    lasso(*session, {QPointF(0, 0), QPointF(30, 0), QPointF(0, 30)});
    const SelectionClip hard = session->selection().value().clip(QSizeF(100, 100));
    // Pixel centres on the diagonal: soft would give 128.
    for (int x = 0; x < 30; ++x) {
        const int value = hard.coverage.value().constScanLine(29 - x)[x];
        QVERIFY2(value == 0 || value == 255, qPrintable(QString::number(value)));
    }
}

void SelectionTests::draftsRefuseWildPointsAndOtherToolsAndKeepTheirCursor()
{
    const auto session = selectionSession();
    // The Move tool and the Wand draw no outline.
    session->selectTool(NavigationTool::move);
    session->beginLasso(QPointF(1, 1), SelectionMode::replace);
    QVERIFY(!session->lassoDraft().has_value());
    session->selectTool(NavigationTool::wand);
    session->beginLasso(QPointF(1, 1), SelectionMode::replace);
    QVERIFY(!session->lassoDraft().has_value());
    session->selectTool(NavigationTool::lasso);
    session->beginLasso(QPointF(1, 1), SelectionMode::replace);
    session->extendLasso(QPointF(std::nan(""), 5));
    session->extendLasso(QPointF(1.1, 1.1));
    QCOMPARE(session->lassoDraft().value().points.size(), size_t(1));
    session->moveLassoCursor(QPointF(40, 40));
    QCOMPARE(session->lassoDraft().value().cursor, std::optional(QPointF(40, 40)));
    session->moveLassoCursor(std::nullopt);
    QVERIFY(!session->lassoDraft().value().cursor.has_value());
    // Busy, an outline can neither begin nor apply.
    session->cancelLasso();
    session->setIsProjectBusy(true);
    session->beginLasso(QPointF(1, 1), SelectionMode::replace);
    QVERIFY(!session->lassoDraft().has_value());
    session->applySelection(rectPath(QRectF(0, 0, 10, 10)), SelectionMode::replace, "Select");
    QVERIFY(!session->selection().has_value());
    session->setIsProjectBusy(false);
    // While the outline moves, no draft begins.
    lasso(*session, square(10, 10, 20));
    QVERIFY(session->beginSelectionMove());
    session->beginLasso(QPointF(1, 1), SelectionMode::replace);
    QVERIFY(!session->lassoDraft().has_value());
    session->endSelectionMove();
    // The project closed mid-drag: the move goes nowhere.
    QVERIFY(session->beginSelectionMove());
    session->clearProject();
    session->moveSelection(QSizeF(1, 1));
    session->endSelectionMove();
    QVERIFY(!session->document().has_value());
}

QTEST_GUILESS_MAIN(SelectionTests)
#include "SelectionTests.moc"
