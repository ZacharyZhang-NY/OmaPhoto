#include "SelectionFixtures.h"
#include "SessionRecord.h"

// The signal contract of every selection call.
class SelectionSignalTests : public QObject {
    Q_OBJECT
private slots:
    void selectionCallsAnnounceAndRefuseInSilence();
    void theLastSignalOfEverySelectionChangeSeesWhatItLeaves();
};

void SelectionSignalTests::selectionCallsAnnounceAndRefuseInSilence()
{
    const auto session = selectionSession();
    QSignalSpy changes(session.get(), &EditorSession::changed);
    const auto announced = [&](const std::function<void()> &change) {
        const qsizetype before = changes.count();
        change();
        return changes.count() > before;
    };
    QVERIFY(!announced([&] { session->deselect(); }));
    QVERIFY(!announced([&] { session->invertSelection(); }));
    QVERIFY(!announced([&] { session->expandSelection(1); }));
    QVERIFY(!announced([&] { session->cancelLasso(); }));
    QVERIFY(!announced([&] { session->extendLasso(QPointF(1, 1)); }));
    QVERIFY(!announced([&] { session->moveLassoCursor(QPointF(1, 1)); }));
    QVERIFY(!announced([&] { session->removeLastLassoPoint(); }));
    QVERIFY(!announced([&] { session->finishLasso(); }));
    QVERIFY(!announced([&] { session->dragMarquee(QPointF(1, 1), false, false); }));
    QVERIFY(!announced([&] { QVERIFY(!session->beginSelectionMove()); }));
    QVERIFY(!announced([&] { session->moveSelection(QSizeF(1, 1)); }));
    QVERIFY(!announced([&] { session->endSelectionMove(); }));
    QVERIFY(!announced([&] { session->nudgeSelection(1, 1); }));
    QVERIFY(!announced([&] { session->applySelection(rectPath(QRectF(0, 0, 10, 10)), SelectionMode::subtract, "Subtract"); }));
    QVERIFY(!announced([&] { session->updateHeldSelectionKeys(false, false); }));
    QVERIFY(!announced([&] { session->loadMaskSelection(session->activeLayerID().value()); }));
    QVERIFY(!announced([&] { session->loadLayerSelection(session->activeLayerID().value()); }));
    QVERIFY(announced([&] { session->updateHeldSelectionKeys(true, false); }));
    QVERIFY(announced([&] { session->setSelectionModeChoice(SelectionMode::add); }));
    QVERIFY(announced([&] { session->setSelectionAntialiased(false); }));
    QVERIFY(announced([&] { session->setSelectionExpandAmount(3); }));
    QVERIFY(announced([&] { session->setSelectionContractAmount(2); }));
    QVERIFY(announced([&] { session->setWandSettings(WandSettings{.tolerance = 5}); }));
    QVERIFY(!announced([&] { session->magicWand(QPointF(-1, -1), SelectionMode::replace); }));
    QVERIFY(announced([&] { session->beginLasso(QPointF(1, 1), SelectionMode::replace); }));
    QVERIFY(announced([&] { session->extendLasso(QPointF(50, 1)); }));
    QVERIFY(!announced([&] { session->extendLasso(QPointF(50.1, 1)); }));
    QVERIFY(announced([&] { session->moveLassoCursor(QPointF(50, 50)); }));
    QVERIFY(announced([&] { session->removeLastLassoPoint(); }));
    QVERIFY(announced([&] { session->extendLasso(QPointF(50, 1)); }));
    QVERIFY(announced([&] { session->extendLasso(QPointF(50, 50)); }));
    QVERIFY(announced([&] { session->finishLasso(); }));
    QVERIFY(session->selection().has_value());
    QVERIFY(announced([&] { session->selectAll(); }));
    QVERIFY(!announced([&] { session->selectAll(); }));
    // Inverse of everything leaves nothing to deselect.
    QVERIFY(announced([&] { session->invertSelection(); }));
    QVERIFY(!session->selection().has_value());
    QVERIFY(!announced([&] { session->deselect(); }));
    QVERIFY(announced([&] { session->selectAll(); }));
    QVERIFY(announced([&] { session->deselect(); }));
    QVERIFY(announced([&] { session->toggleMarqueeKind(); }));
    QVERIFY(announced([&] { session->toggleLassoKind(); }));
    QVERIFY(announced([&] { session->selectTool(NavigationTool::marquee); }));
    QVERIFY(announced([&] { session->beginLasso(QPointF(1, 1), SelectionMode::replace); }));
    QVERIFY(announced([&] { session->dragMarquee(QPointF(20, 20), false, false); }));
    QVERIFY(announced([&] { session->cancelLasso(); }));
    QVERIFY(announced([&] { session->applySelection(rectPath(QRectF(10, 10, 20, 20)), SelectionMode::replace, "Select"); }));
    QVERIFY(announced([&] { QVERIFY(session->beginSelectionMove()); }));
    QVERIFY(announced([&] { session->moveSelection(QSizeF(1, 1)); }));
    QVERIFY(announced([&] { session->endSelectionMove(); }));
    QVERIFY(announced([&] { session->nudgeSelection(1, 1); }));
    QVERIFY(announced([&] { session->expandSelection(1); }));
    QVERIFY(announced([&] { session->contractSelection(1); }));
    QVERIFY(announced([&] { session->addMask(); }));
    QVERIFY(announced([&] { session->deleteLayerOrMask(); }));
    QVERIFY(announced([&] { session->deleteLayerOrMask(); }));
    QVERIFY(session->document().value().layers.empty());
    QVERIFY(!announced([&] { session->deleteLayerOrMask(); }));
}

void SelectionSignalTests::theLastSignalOfEverySelectionChangeSeesWhatItLeaves()
{
    const auto session = selectionSession();
    QStringList seen;
    connect(session.get(), &EditorSession::changed, this, [&] { seen = described(*session); });
    const auto stale = [&](const std::function<void()> &change) {
        seen.clear();
        change();
        const QStringList left = described(*session);
        return seen == left ? QString() : seen.join("; ") + " != " + left.join("; ");
    };
    QCOMPARE(stale([&] { session->updateHeldSelectionKeys(false, true); }), QString());
    // A click with nothing selected still ends the draft.
    QCOMPARE(stale([&] { session->beginLasso(QPointF(1, 1), SelectionMode::replace); }), QString());
    QCOMPARE(stale([&] { session->finishLasso(); }), QString());
    QVERIFY(seen.filter("lasso ").isEmpty());
    QCOMPARE(stale([&] { session->setSelectionModeChoice(SelectionMode::add); }), QString());
    QCOMPARE(stale([&] { session->setSelectionAntialiased(false); }), QString());
    QCOMPARE(stale([&] { session->setSelectionExpandAmount(4); }), QString());
    QCOMPARE(stale([&] { session->setSelectionContractAmount(5); }), QString());
    QCOMPARE(stale([&] { session->setWandSettings(WandSettings{.tolerance = 5, .sampleAllLayers = true}); }), QString());
    QCOMPARE(stale([&] { session->beginLasso(QPointF(1, 1), SelectionMode::replace); }), QString());
    QCOMPARE(stale([&] { session->extendLasso(QPointF(60, 1)); }), QString());
    QCOMPARE(stale([&] { session->moveLassoCursor(QPointF(60, 60)); }), QString());
    QCOMPARE(stale([&] { session->removeLastLassoPoint(); }), QString());
    QCOMPARE(stale([&] { session->extendLasso(QPointF(60, 1)); }), QString());
    QCOMPARE(stale([&] { session->extendLasso(QPointF(60, 60)); }), QString());
    QCOMPARE(stale([&] { session->finishLasso(); }), QString());
    QVERIFY(seen.contains("names Lasso/") && seen.filter("outline ").size() == 1);
    QCOMPARE(stale([&] { session->beginLasso(QPointF(1, 1), SelectionMode::replace); }), QString());
    QCOMPARE(stale([&] { session->cancelLasso(); }), QString());
    QCOMPARE(stale([&] { session->beginLasso(QPointF(1, 1), SelectionMode::replace); }), QString());
    QCOMPARE(stale([&] { session->selectTool(NavigationTool::marquee); }), QString());
    QVERIFY(seen.filter("lasso ").isEmpty());
    QCOMPARE(stale([&] { session->toggleMarqueeKind(); }), QString());
    QCOMPARE(stale([&] { session->beginLasso(QPointF(1, 1), SelectionMode::add); }), QString());
    QCOMPARE(stale([&] { session->dragMarquee(QPointF(30, 30), true, false); }), QString());
    QCOMPARE(stale([&] { session->finishLasso(); }), QString());
    QCOMPARE(stale([&] { session->toggleLassoKind(); }), QString());
    QCOMPARE(stale([&] { QVERIFY(session->beginSelectionMove()); }), QString());
    QVERIFY(seen.contains("history 00"));
    QCOMPARE(stale([&] { session->moveSelection(QSizeF(3, 4)); }), QString());
    QCOMPARE(stale([&] { session->endSelectionMove(); }), QString());
    QVERIFY(seen.contains("history 10") && seen.contains("names Move Selection/"));
    QCOMPARE(stale([&] { session->nudgeSelection(1, 0); }), QString());
    QCOMPARE(stale([&] { session->expandSelection(2); }), QString());
    QCOMPARE(stale([&] { session->contractSelection(1); }), QString());
    QCOMPARE(stale([&] { session->invertSelection(); }), QString());
    QCOMPARE(stale([&] { session->selectAll(); }), QString());
    QCOMPARE(stale([&] { session->applySelection(rectPath(QRectF(10, 10, 20, 20)), SelectionMode::subtract, "Subtract"); }), QString());
    QCOMPARE(stale([&] { session->deselect(); }), QString());
    QCOMPARE(stale([&] { session->undo(); }), QString());
    QVERIFY(seen.filter("outline ").size() == 1);
    QCOMPARE(stale([&] { session->setSelection(std::nullopt, "Clear"); }), QString());
    // The panel's Ctrl-clicks and the mask from a selection.
    QCOMPARE(stale([&] { session->applySelection(rectPath(QRectF(10, 10, 20, 20)), SelectionMode::replace, "Select"); }), QString());
    QCOMPARE(stale([&] { session->addMask(); }), QString());
    QVERIFY(seen.filter("outline ").isEmpty() && seen.contains("mask target 1"));
    QCOMPARE(stale([&] { session->loadMaskSelection(session->activeLayerID().value()); }), QString());
    QVERIFY(seen.contains("names Load Mask Selection/"));
    QCOMPARE(stale([&] { session->deleteLayerOrMask(); }), QString());
    QVERIFY(seen.contains("mask target 0"));
    QCOMPARE(stale([&] { session->insert(filledRed()); }), QString());
    QCOMPARE(stale([&] { session->loadLayerSelection(session->activeLayerID().value(), SelectionMode::add); }), QString());
    QVERIFY(seen.contains("names Load Layer Selection/"));
    // An invert: busy while running, a step when it lands.
    bool done = false;
    QCOMPARE(stale([&] { session->invertPixels([&] { done = true; }); }), QString());
    QVERIFY(seen.contains("flags 1000"));
    QTRY_VERIFY(done);
    QVERIFY(seen.contains("flags 0000") && seen.contains("names Invert/"));
    QCOMPARE(seen, described(*session));
    QCOMPARE(stale([&] { session->deleteLayerOrMask(); }), QString());
    // A wand click: busy while running, a step when landing.
    done = false;
    QCOMPARE(stale([&] { session->magicWand(QPointF(1, 1), SelectionMode::replace, [&] { done = true; }); }), QString());
    QVERIFY(seen.contains("flags 1000"));
    QTRY_VERIFY(done);
    QVERIFY(seen.contains("flags 0000") && seen.contains("names Magic Wand/"));
    QCOMPARE(seen, described(*session));
}

QTEST_GUILESS_MAIN(SelectionSignalTests)
#include "SelectionSignalTests.moc"
