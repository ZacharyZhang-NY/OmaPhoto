#include "BrushFixtures.h"
#include "CanvasFixtures.h"
#include "ContentView.h"
#include "UI/LayerMaskMenu.h"
#include "UI/NativeLayerList.h"
#include <QLabel>
#include <QToolButton>

// Swift's MaskAloneTests: Alt-click shows a mask by itself.
class MaskAloneTests : public QObject {
    Q_OBJECT
private slots:
    void optionClickTogglesTheMaskViewAndTargetingPixelsEndsIt();
    void aLayerWithoutAMaskHasNothingToShow();
    void anotherLayerOrAnEndedViewShowsTheImage();
    void theCanvasShowsTheMaskInGrayInsteadOfTheImage();
    void aStrokeOrAGradientShowsInTheMaskAsItIsLaid();
    void aPlacedOrDisabledMaskShowsAlone();
    void theBadgeNamesTheMaskAndTakesItsOwnClicks();
    void altClickOnTheThumbnailTogglesItAndOutlinesItWhite();
    void theViewOutlastsUndoRedoAndTargetingTheMaskAgain();
    void aPendingGradientElsewhereStaysOutOfTheView();
};

namespace {
// A red layer, 200 by 100, its right half masked.
QUuid masked(EditorSession &session)
{
    session.insert(filled(200, 100, qRgba(255, 0, 0, 255), QStringLiteral("Red")));
    const QUuid id = session.activeLayerID().value();
    QImage mask(200, 100, QImage::Format_Grayscale8);
    mask.fill(0);
    for (int y = 0; y < 100; ++y)
        std::fill_n(mask.scanLine(y), 100, uchar(255));
    rewrite(session, [&](ProjectSnapshot &snapshot) {
        record(snapshot, id).transform = LayerTransform{.origin = {0, 0}, .size = {200, 100}};
        setMask(snapshot, id, LayerMask::assetFrom(mask));
    });
    return id;
}

std::unique_ptr<EditorSession> maskedSession(QUuid &id)
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(200, 100);
    id = masked(*session);
    return session;
}

// The canvas at 1:1: view points are document pixels.
struct Canvas : Shown {
    QUuid id;
    Canvas() : Shown(QSize(200, 100), QSize(400, 300))
    {
        settle();
        id = masked(session);
        session.zoom(1);
        canvas->synchronizeDisplay();
    }
    QColor at(QPointF point)
    {
        canvas->synchronizeDisplay();
        const QPointF view = session.viewport.viewPoint(point, documentSize());
        return canvas->grab().toImage().pixelColor(view.toPoint());
    }
};

bool red(const QColor &colour)
{
    return colour.red() > 204 && colour.blue() < 102 && colour.green() < 102;
}

bool gray(const QColor &colour, int value)
{
    return std::abs(colour.red() - value) <= 2 && colour.red() == colour.green() && colour.green() == colour.blue();
}
}

void MaskAloneTests::optionClickTogglesTheMaskViewAndTargetingPixelsEndsIt()
{
    QUuid id;
    const auto session = maskedSession(id);
    session->toggleMaskAlone(id);
    QVERIFY(session->maskAloneLayer().value().id == id && session->isMaskSelected() && session->viewsMaskAlone());
    session->toggleMaskAlone(id);
    QVERIFY(!session->maskAloneLayer() && session->isMaskSelected());
    session->toggleMaskAlone(id);
    session->selectLayerTarget(id, false);
    QVERIFY(!session->maskAloneLayer() && !session->viewsMaskAlone());
    session->toggleMaskAlone(id);
    session->deleteLayerMask();
    QVERIFY(!session->maskAloneLayer() && !session->viewsMaskAlone());
    session->undo();
    QVERIFY(!session->maskAloneLayer());
}

void MaskAloneTests::aLayerWithoutAMaskHasNothingToShow()
{
    QUuid id;
    const auto session = maskedSession(id);
    session->deleteLayerMask();
    session->toggleMaskAlone(id);
    QVERIFY(!session->maskAloneLayer() && !session->isMaskSelected() && !session->viewsMaskAlone());
    // A busy project leaves the target, so the view too.
    session->addLayerMask();
    session->selectLayerTarget(id, false);
    session->setIsProjectBusy(true);
    session->toggleMaskAlone(id);
    QVERIFY(!session->viewsMaskAlone());
    // Busy, another layer's targeted mask stays unshown too.
    session->setIsProjectBusy(false);
    session->addBlankLayer();
    const QUuid other = session->activeLayerID().value();
    session->addLayerMask();
    QVERIFY(session->isMaskSelected());
    session->setIsProjectBusy(true);
    session->toggleMaskAlone(id);
    QVERIFY(session->activeLayerID() == other && !session->viewsMaskAlone());
}

void MaskAloneTests::anotherLayerOrAnEndedViewShowsTheImage()
{
    QUuid id;
    const auto session = maskedSession(id);
    session->addBlankLayer();
    const QUuid other = session->activeLayerID().value();
    session->toggleMaskAlone(id);
    QVERIFY(session->maskAloneLayer().has_value());
    // Another layer targets its pixels: the image again.
    session->selectLayer(other);
    QVERIFY(!session->maskAloneLayer() && !session->viewsMaskAlone());
    // Alt-clicking another layer's mask shows that one.
    session->addLayerMask(false);
    session->toggleMaskAlone(id);
    session->toggleMaskAlone(other);
    QCOMPARE(session->maskAloneLayer().value().id, other);
    // The badge's button: each change is announced.
    QSignalSpy changed(session.get(), &EditorSession::changed);
    session->setViewsMaskAlone(false);
    QVERIFY(!session->maskAloneLayer() && session->isMaskSelected());
    QCOMPARE(changed.count(), 1);
    session->setViewsMaskAlone(false);
    QCOMPARE(changed.count(), 1);
}

void MaskAloneTests::theCanvasShowsTheMaskInGrayInsteadOfTheImage()
{
    Canvas shown;
    QVERIFY(red(shown.at(QPointF(50, 50))));
    shown.session.toggleMaskAlone(shown.id);
    QVERIFY(shown.canvas->synchronizeDisplay());
    QVERIFY(gray(shown.at(QPointF(50, 50)), 255));
    QVERIFY(gray(shown.at(QPointF(150, 50)), 0));
    shown.session.toggleMaskAlone(shown.id);
    QVERIFY(shown.canvas->synchronizeDisplay());
    QVERIFY(red(shown.at(QPointF(50, 50))));
    // At a crisp zoom too, past the halving.
    shown.session.toggleMaskAlone(shown.id);
    shown.session.zoom(8);
    QVERIFY(gray(shown.at(QPointF(80.5, 50.5)), 255));
    QVERIFY(gray(shown.at(QPointF(120.5, 50.5)), 0));
}

void MaskAloneTests::aStrokeOrAGradientShowsInTheMaskAsItIsLaid()
{
    Canvas shown;
    EditorSession &session = shown.session;
    session.toggleMaskAlone(shown.id);
    session.selectTool(NavigationTool::brush);
    session.setMaskPaintWhite(false);
    session.setBrushSettings(brush(20, 1, 0, 0, 0));
    session.beginBrush(QPointF(50, 50));
    session.continueBrush(QPointF(60, 50));
    QVERIFY(gray(shown.at(QPointF(55, 50)), 0));
    QVERIFY(gray(shown.at(QPointF(55, 80)), 255));
    QVERIFY(session.finishBrushImmediately());
    QVERIFY(gray(shown.at(QPointF(55, 50)), 0));
    QVERIFY(session.maskAloneLayer().has_value());
    // A gradient shows as it is dragged, off its line.
    session.selectTool(NavigationTool::gradient);
    session.setMaskPaintWhite(true);
    session.beginGradient(QPointF(0, 50));
    session.moveGradient(std::nullopt, QPointF(200, 50));
    QVERIFY(session.gradientEdit().has_value());
    const QColor left = shown.at(QPointF(10, 80)), right = shown.at(QPointF(190, 80));
    // Before it, the right half was black.
    QVERIFY(gray(right, right.red()) && right.red() > 6 && right.red() < 128);
    // What shows is what the gradient commits.
    bool done = false;
    session.commitGradient([&] { done = true; });
    QTRY_VERIFY(done);
    const QImage committed = session.activeLayer().value().mask.value().asset.image();
    QVERIFY(gray(left, committed.pixelColor(10, 80).red()) && gray(right, committed.pixelColor(190, 80).red()));
    QVERIFY(gray(shown.at(QPointF(10, 80)), left.red()));
}

void MaskAloneTests::aPlacedOrDisabledMaskShowsAlone()
{
    Canvas shown;
    EditorSession &session = shown.session;
    // Turned off, the composite ignores it; alone, it shows.
    session.selectLayerTarget(shown.id, true);
    session.toggleLayerMask();
    QVERIFY(red(shown.at(QPointF(150, 50))));
    session.toggleMaskAlone(shown.id);
    QVERIFY(gray(shown.at(QPointF(150, 50)), 0));
    QVERIFY(gray(shown.at(QPointF(50, 50)), 255));
    // Placed apart, at its placement; its edge tone past it.
    rewrite(session, [&](ProjectSnapshot &snapshot) {
        record(snapshot, shown.id).maskLinked = false;
        record(snapshot, shown.id).maskPlacement = LayerTransform{.origin = {50, 0}, .size = {100, 50}};
    });
    session.toggleMaskAlone(shown.id);
    QVERIFY(session.maskAloneLayer().has_value());
    QVERIFY(gray(shown.at(QPointF(60, 20)), 255));
    QVERIFY(gray(shown.at(QPointF(140, 20)), 0));
    const int edge = qRound(LayerMask::background(session.activeLayer().value().mask.value().asset.thumbnail) * 255);
    QVERIFY(gray(shown.at(QPointF(20, 80)), edge));
    QVERIFY(edge > 0 && gray(shown.at(QPointF(120, 70)), edge));
}

void MaskAloneTests::theBadgeNamesTheMaskAndTakesItsOwnClicks()
{
    EditorSession session;
    ContentView view(session);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    session.createDocument(200, 100);
    const QUuid id = masked(session);
    QVERIFY(!view.findChild<MaskAloneBadge *>()->isVisible());
    session.toggleMaskAlone(id);
    MaskAloneBadge *badge = nullptr;
    QTRY_VERIFY((badge = view.findChild<MaskAloneBadge *>()) && badge->isVisible());
    QCOMPARE(badge->findChild<QLabel *>("maskAloneTitle")->text(), QString("Layer Mask"));
    QCOMPARE(badge->findChild<QLabel *>("maskAloneName")->text(), QString("Red"));
    QCOMPARE(badge->height(), 26);
    QCOMPARE(badge->layout()->contentsMargins(), QMargins(11, 0, 8, 0));
    QCOMPARE(badge->layout()->spacing(), 7);
    // At the canvas's foot, 14 points up, in the middle.
    auto *canvas = view.findChild<CanvasView *>();
    const QRect onView(badge->mapTo(&view, QPoint(0, 0)), badge->size()), canvasOnView(canvas->mapTo(&view, QPoint(0, 0)), canvas->size());
    QCOMPARE(canvasOnView.bottom() - onView.bottom(), 14);
    QVERIFY(std::abs(onView.center().x() - canvasOnView.center().x()) <= 1);
    // A long name ends in an ellipsis within 220 points.
    rewrite(session, [&](ProjectSnapshot &snapshot) { record(snapshot, id).name = QString(80, QLatin1Char('W')); });
    session.toggleMaskAlone(id);
    QTRY_VERIFY((badge = view.findChild<MaskAloneBadge *>()) && badge->isVisible());
    QLabel &name = *badge->findChild<QLabel *>("maskAloneName");
    QVERIFY(name.width() <= 220 && name.text().endsWith(QChar(0x2026)));
    // Its button, not the canvas under it, takes the click.
    auto &close = *badge->findChild<QToolButton *>("maskAloneClose");
    QCOMPARE(close.toolTip(), QString("Show the image again (or Alt-click the mask thumbnail)"));
    QCOMPARE(close.accessibleName(), QString("Stop viewing the mask"));
    const QPoint middle = close.mapTo(view.window(), close.rect().center());
    QCOMPARE(view.window()->childAt(middle), &close);
    QTest::mouseClick(view.window()->windowHandle(), Qt::LeftButton, Qt::NoModifier, middle);
    QVERIFY(!session.maskAloneLayer() && session.isMaskSelected());
    QTRY_VERIFY(!badge->isVisible());
    // The canvas under the badge's foot still takes clicks.
    session.toggleMaskAlone(id);
    QTRY_VERIFY((badge = view.findChild<MaskAloneBadge *>()) && badge->isVisible());
    const QPoint below = badge->mapTo(view.window(), QPoint(badge->width() / 2, badge->height() + 7));
    QCOMPARE(view.window()->childAt(below), canvas);
}

void MaskAloneTests::altClickOnTheThumbnailTogglesItAndOutlinesItWhite()
{
    EditorSession session;
    NativeLayerList list(session);
    list.resize(300, 400);
    list.show();
    QVERIFY(QTest::qWaitForWindowExposed(&list));
    session.createDocument(200, 100);
    const QUuid id = masked(session);
    LayerCell &cell = *list.cells().at(0);
    auto *thumbnail = cell.findChild<LayerThumbnailButton *>("maskThumbnail");
    QVERIFY(thumbnail);
    QCOMPARE(thumbnail->toolTip(), QString("Select layer mask; Alt-click to view it alone; Shift-click to enable/disable; Ctrl-click to select its black "
                                           "areas (Ctrl-Shift adds, Ctrl-Alt subtracts)"));
    session.selectLayerTarget(id, false);
    const QPoint middle = thumbnail->rect().center();
    QTest::mouseClick(thumbnail, Qt::LeftButton, Qt::AltModifier, middle);
    QCOMPARE(session.maskAloneLayer().value().id, id);
    // Outlined in white rather than the accent.
    const QColor ring = thumbnail->grab().toImage().pixelColor(1, middle.y());
    QCOMPARE(ring, QColor(Qt::white));
    // Released off the thumbnail, the press does nothing.
    QTest::mousePress(thumbnail, Qt::LeftButton, Qt::AltModifier, middle);
    QTest::mouseRelease(thumbnail, Qt::LeftButton, Qt::AltModifier, QPoint(-40, middle.y()));
    QCOMPARE(session.maskAloneLayer().value().id, id);
    // Shift no longer toggles the mask on an Alt-click.
    QTest::mouseClick(thumbnail, Qt::LeftButton, Qt::AltModifier | Qt::ShiftModifier, middle);
    QVERIFY(!session.maskAloneLayer() && session.activeLayer().value().mask.value().isEnabled);
    const QColor accent = thumbnail->grab().toImage().pixelColor(1, middle.y());
    QCOMPARE(accent, thumbnail->palette().color(QPalette::Highlight));
}

void MaskAloneTests::theViewOutlastsUndoRedoAndTargetingTheMaskAgain()
{
    QUuid id;
    const auto session = maskedSession(id);
    session->toggleMaskAlone(id);
    // A plain click on the shown mask targets it again.
    session->selectLayerTarget(id, true);
    QCOMPARE(session->maskAloneLayer().value().id, id);
    // A stroke on it, undone and redone, keeps the view.
    session->selectTool(NavigationTool::brush);
    session->setBrushSettings(brush(20, 1, 0, 0, 0));
    session->beginBrush(QPointF(50, 50));
    QVERIFY(session->finishBrushImmediately());
    QCOMPARE(session->history.undoName(), QString("Paint Mask"));
    session->undo();
    QCOMPARE(session->maskAloneLayer().value().id, id);
    session->redo();
    QCOMPARE(session->maskAloneLayer().value().id, id);
}

void MaskAloneTests::aPendingGradientElsewhereStaysOutOfTheView()
{
    Canvas shown;
    EditorSession &session = shown.session;
    session.setForegroundColor(PaletteColor{0, 0, 1});
    session.selectTool(NavigationTool::gradient);
    // The layer's own pixels take a blue gradient, still pending.
    session.selectLayerTarget(shown.id, false);
    session.beginGradient(QPointF(0, 80));
    session.moveGradient(std::nullopt, QPointF(200, 80));
    QVERIFY(session.gradientEdit().has_value() && !session.gradientEdit()->raster->isMask);
    session.toggleMaskAlone(shown.id);
    // The commit is posted: the pixels' raster lingers, unshown.
    QVERIFY(session.maskAloneLayer().has_value() && session.gradientEdit().has_value());
    QVERIFY(gray(shown.at(QPointF(50, 50)), 255) && gray(shown.at(QPointF(150, 50)), 0));
    QTRY_VERIFY(!session.gradientEdit().has_value());
    // Another layer's mask gradient stays out of this view.
    session.addBlankLayer();
    const QUuid other = session.activeLayerID().value();
    session.addLayerMask();
    session.beginGradient(QPointF(0, 80));
    session.moveGradient(std::nullopt, QPointF(200, 80));
    QVERIFY(session.gradientEdit().has_value() && session.gradientEdit()->raster->isMask && session.gradientEdit()->raster->layer.id == other);
    session.toggleMaskAlone(shown.id);
    QCOMPARE(session.maskAloneLayer().value().id, shown.id);
    QVERIFY(session.gradientEdit().has_value());
    QVERIFY(gray(shown.at(QPointF(50, 50)), 255) && gray(shown.at(QPointF(150, 50)), 0));
    QTRY_VERIFY(!session.gradientEdit().has_value());
}

QTEST_MAIN(MaskAloneTests)
#include "MaskAloneTests.moc"
