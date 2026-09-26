#include "SelectionFixtures.h"

// Feather softens the selection itself, so everything clipped fades.
namespace {
// Values strictly between clear and full along one row.
int fading(const QImage &image, int row, int channel, int stride)
{
    int count = 0;
    for (int x = 0; x < image.width(); ++x) {
        const int value = image.constScanLine(row)[x * stride + channel];
        count += value > 8 && value < 247;
    }
    return count;
}
}

class SelectionFeatherTests : public QObject {
    Q_OBJECT
private slots:
    void featherSoftensTheSelectionAndWhatItClips();
    void feathersAddAsBlursAndStopAtTheirLimit();
    void featherTravelsWithTheOutline();
    void theClipGrowsByTheFalloff();
    void theAmountSheetAsksThenApplies();
    void aMaskFromAFeatheredSelectionFades();
};

void SelectionFeatherTests::featherSoftensTheSelectionAndWhatItClips()
{
    EditorSession session;
    session.createDocument(60, 20, true);
    session.addBlankLayer();
    session.selectAll();
    session.setSelectionFeatherAmount(6);
    session.featherSelection(6);
    QVERIFY(session.canModifySelection());
    QCOMPARE(session.selection().value().feather, 6.0);
    QCOMPARE(session.history.undoName(), QString("Feather Selection"));
    session.setSelection(DocumentSelection{rectPath(QRectF(20, 0, 20, 20)), true, 6}, "probe");
    const SelectionClip clip = session.selection().value().clip(QSizeF(60, 20));
    const QImage coverage = clip.coverage.value();
    QVERIFY2(fading(coverage, coverage.height() / 2, 0, 1) >= 4, "the clip has no soft edge");
    // A copy takes the fade too, within the canvas.
    QCOMPARE(session.selectionCopyRegion().value(), QRectF(8, 0, 44, 20));
    session.setForegroundColor(PaletteColor{1, 0, 0});
    bool done = false;
    session.fillSelection(EditorSession::FillSource::foreground, [&] { done = true; });
    QTRY_VERIFY(done);
    const QImage filled = session.activeLayer().value().asset.value().image().convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    QVERIFY2(fading(filled, filled.height() / 2, 3, 4) >= 4, "the fill has a hard edge");
    // Trimmed to the fade: whole inside, faint at the ends.
    QCOMPARE(session.activeLayer().value().transform.origin, QPointF(12, 0));
    QCOMPARE(filled.width(), 36);
    QCOMPARE(int(filled.constScanLine(10)[18 * 4 + 3]), 255);
    QCOMPARE(int(filled.constScanLine(10)[3]), 1);
}

void SelectionFeatherTests::feathersAddAsBlursAndStopAtTheirLimit()
{
    const auto session = selectionSession();
    QSignalSpy changes(session.get(), &EditorSession::changed);
    session->featherSelection(4);
    QVERIFY(!session->selection());
    QCOMPARE(int(changes.count()), 0);
    lasso(*session, square(10, 10, 40));
    session->featherSelection(3);
    session->featherSelection(4);
    QCOMPARE(session->selection().value().feather, 5.0);
    session->featherSelection(0);
    session->featherSelection(-3);
    QCOMPARE(session->selection().value().feather, 5.0);
    session->featherSelection(250);
    QCOMPARE(session->selection().value().feather, 250.0);
    session->undo();
    QCOMPARE(session->selection().value().feather, 5.0);
    // A hard edge stays hard: clear or full, nothing between.
    session->setSelectionAntialiased(false);
    lasso(*session, {QPointF(10, 10), QPointF(60, 20), QPointF(20, 70)});
    const QImage hard = session->selection().value().coverage(100, 100);
    for (int y = 0; y < 100; ++y)
        QCOMPARE(fading(hard, y, 0, 1), 0);
}

void SelectionFeatherTests::featherTravelsWithTheOutline()
{
    const auto session = selectionSession();
    lasso(*session, square(20, 20, 40));
    session->featherSelection(4);
    session->expandSelection(2);
    QCOMPARE(session->selection().value().feather, 4.0);
    session->contractSelection(1);
    QCOMPARE(session->selection().value().feather, 4.0);
    session->invertSelection();
    QCOMPARE(session->selection().value().feather, 4.0);
    session->invertSelection();
    QVERIFY(session->beginSelectionMove());
    session->moveSelection(QSizeF(5, 0));
    QCOMPARE(session->selection().value().feather, 4.0);
    QCOMPARE(bounds(*session).left(), 24.0);
}

void SelectionFeatherTests::theClipGrowsByTheFalloff()
{
    const DocumentSelection soft{rectPath(QRectF(10, 10, 20, 20)), true, 3};
    QCOMPARE(soft.coverageBounds(), QRectF(4, 4, 32, 32));
    QCOMPARE(soft.clip(QSizeF(100, 100)).rect, QRectF(3, 3, 34, 34));
    QCOMPARE(soft.clip(QSizeF(20, 20)).rect, QRectF(3, 3, 17, 17));
    // A feathered edge is antialiased first, whatever the choice.
    QPainterPath triangle;
    triangle.addPolygon(QPolygonF{QPointF(5, 5), QPointF(35, 12), QPointF(12, 33)});
    QCOMPARE((DocumentSelection{triangle, false, 1}.coverage(40, 40)), (DocumentSelection{triangle, true, 1}.coverage(40, 40)));
    // The canvas edge repeats: a whole canvas stays whole there.
    const QImage whole = DocumentSelection{rectPath(QRectF(0, 0, 40, 40)), true, 6}.coverage(40, 40);
    QCOMPARE(int(whole.constScanLine(0)[0]), 255);
    QCOMPARE(int(whole.constScanLine(39)[20]), 255);
    const DocumentSelection hard{rectPath(QRectF(10, 10, 20, 20)), true, 0};
    QCOMPARE(hard.coverageBounds(), QRectF(10, 10, 20, 20));
    // Past the outline it fades, then nothing.
    const QImage image = soft.coverage(40, 40);
    QVERIFY(image.constScanLine(20)[8] > 0);
    QCOMPARE(int(image.constScanLine(20)[2]), 0);
    QCOMPARE(int(image.constScanLine(20)[20]), 255);
    // Either side of the outline mirrors the other.
    QCOMPARE(image.constScanLine(20)[9] + image.constScanLine(20)[10], 255);
}

void SelectionFeatherTests::theAmountSheetAsksThenApplies()
{
    const auto session = selectionSession();
    QSignalSpy changes(session.get(), &EditorSession::changed);
    session->promptSelectionAmount(SelectionAmountOperation::feather);
    QVERIFY(!session->selectionAmountOperation());
    QCOMPARE(int(changes.count()), 0);
    lasso(*session, square(20, 20, 40));
    session->promptSelectionAmount(SelectionAmountOperation::feather);
    QCOMPARE(session->selectionAmountOperation(), std::optional(SelectionAmountOperation::feather));
    QVERIFY(!session->canEditLayers());
    QVERIFY(!session->canUseHistory());
    QVERIFY(!session->canStartProjectOperation());
    session->confirmSelectionAmount(0);
    session->confirmSelectionAmount(251);
    QVERIFY(session->selectionAmountOperation().has_value());
    QCOMPARE(session->selection().value().feather, 0.0);
    session->confirmSelectionAmount(250);
    QVERIFY(!session->selectionAmountOperation());
    QCOMPARE(session->selectionFeatherAmount(), 250);
    QCOMPARE(session->selection().value().feather, 250.0);
    QVERIFY(session->canEditLayers());
    // A file request waits for the question, then runs.
    session->promptSelectionAmount(SelectionAmountOperation::expand);
    bool ran = false;
    session->waitForFileRequest([&ran] { ran = true; });
    QTest::qWait(10);
    QVERIFY(!ran);
    session->setSelectionAmountOperation(std::nullopt);
    QTRY_VERIFY(ran);
    session->promptSelectionAmount(SelectionAmountOperation::expand);
    session->confirmSelectionAmount(501);
    QVERIFY(session->selectionAmountOperation().has_value());
    session->confirmSelectionAmount(3);
    QCOMPARE(session->selectionExpandAmount(), 3);
    QCOMPARE(session->history.undoName(), QString("Expand Selection"));
    QCOMPARE(bounds(*session), QRectF(17, 17, 46, 46));
    session->promptSelectionAmount(SelectionAmountOperation::contract);
    session->confirmSelectionAmount(500);
    QCOMPARE(session->selectionContractAmount(), 500);
    QVERIFY(session->selection().value().isEmpty());
    // No question open: an amount does nothing.
    session->undo();
    session->confirmSelectionAmount(5);
    QCOMPARE(session->selectionContractAmount(), 500);
}

void SelectionFeatherTests::aMaskFromAFeatheredSelectionFades()
{
    EditorSession session;
    session.createDocument(60, 20, true);
    session.setSelection(DocumentSelection{rectPath(QRectF(20, 0, 20, 20)), true, 6}, "probe");
    session.addMask(false);
    const QImage mask = session.activeLayer().value().mask.value().asset.image();
    QCOMPARE(mask, session.document().value().layers.back().mask.value().asset.image());
    QVERIFY2(fading(mask, 10, 0, 1) >= 4, "the mask has a hard edge");
    QCOMPARE(int(mask.constScanLine(10)[30]), 255);
    QCOMPARE(int(mask.constScanLine(10)[2]), 0);
}

QTEST_GUILESS_MAIN(SelectionFeatherTests)
#include "SelectionFeatherTests.moc"
