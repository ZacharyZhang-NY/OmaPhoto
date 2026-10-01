#include "BrushFixtures.h"
#include "SelectionFixtures.h"

// Why a stroke is refused, as Photoshop explains it.
class PaintRefusalTests : public QObject {
    Q_OBJECT
private slots:
    void eachRefusalSaysWhy();
    void theGradientAndTheSmearSayWhyToo();
    void aBusyEditorOrAnotherToolSaysNothing();
};

namespace {
std::unique_ptr<EditorSession> makeSession()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(100, 80, true);
    session->selectTool(NavigationTool::brush);
    return session;
}

// The press's error, cleared for the next.
std::optional<QString> pressed(EditorSession &session)
{
    session.beginBrush(QPointF(10, 10));
    const std::optional<QString> error = session.brushError();
    session.setBrushError(std::nullopt);
    return error;
}

void emptySelection(EditorSession &session)
{
    session.applySelection(rectPath(QRectF(0, 0, 10, 10)), SelectionMode::replace, "Select");
    session.applySelection(rectPath(QRectF(0, 0, 100, 80)), SelectionMode::subtract, "Subtract");
    QVERIFY(session.selection().value().isEmpty());
}
}

void PaintRefusalTests::eachRefusalSaysWhy()
{
    const auto session = makeSession();
    const QUuid layer = session->activeLayerID().value();
    QCOMPARE(session->paintRefusal(), std::nullopt);
    // Two layers, ahead of everything else.
    session->addGroup();
    const QUuid folder = session->activeLayerID().value();
    session->selectLayers({layer, folder}, folder);
    QCOMPARE(pressed(*session), std::optional<QString>("Several layers are selected. Select just one to paint on it."));
    session->selectLayer(folder);
    QCOMPARE(pressed(*session), std::optional<QString>("“Folder 1” is a folder, which has no pixels of its own. Paint on a layer inside it, or on the "
                                                       "folder’s mask."));
    // Its mask paints; turned off, the mask says so.
    session->addLayerMask();
    QVERIFY(session->isMaskSelected() && session->canPaint());
    session->toggleLayerMask();
    QCOMPARE(pressed(*session), std::optional<QString>("The layer mask is turned off. Shift-click its thumbnail to turn it on, then paint."));
    session->toggleLayerMask();
    session->selectLayer(layer);
    session->toggleLayerVisibility(layer);
    QCOMPARE(pressed(*session), std::optional<QString>("“Layer 1” is hidden, or inside a hidden folder. Show it to paint on it."));
    session->toggleLayerVisibility(layer);
    // Inside a hidden folder too.
    QVERIFY(session->placeLayer(layer, folder));
    session->toggleLayerVisibility(folder);
    session->selectLayer(layer);
    QCOMPARE(pressed(*session), std::optional<QString>("“Layer 1” is hidden, or inside a hidden folder. Show it to paint on it."));
    session->toggleLayerVisibility(folder);
    QVERIFY(session->canPaint());
    session->addLayerMask();
    QVERIFY(session->isMaskSelected());
    session->toggleLayerMask();
    QCOMPARE(pressed(*session), std::optional<QString>("The layer mask is turned off. Shift-click its thumbnail to turn it on, then paint."));
    // The pixels still paint under a mask turned off.
    session->selectLayerTarget(layer, false);
    QCOMPARE(session->paintRefusal(), std::nullopt);
    session->toggleLayerMask();
    session->addAdjustment(AdjustmentKind::levels);
    session->setAdjustmentEditingID(std::nullopt);
    const QUuid adjustment = session->activeLayerID().value();
    session->selectLayerTarget(adjustment, false);
    QVERIFY(!session->isMaskSelected());
    QCOMPARE(pressed(*session), std::optional<QString>("“Levels” is an adjustment layer, with no pixels to paint. Paint on its mask instead."));
    // Its mask paints, so the empty selection is why.
    if (!session->activeLayer().value().mask)
        session->addLayerMask();
    session->selectLayerTarget(adjustment, true);
    emptySelection(*session);
    QCOMPARE(pressed(*session),
             std::optional<QString>("Nothing is selected, so there’s nowhere to paint. Choose Select › Deselect (Ctrl+D) to paint anywhere."));
    session->deselect();
    session->selectLayer(layer);
    emptySelection(*session);
    QCOMPARE(pressed(*session),
             std::optional<QString>("Nothing is selected, so there’s nowhere to paint. Choose Select › Deselect (Ctrl+D) to paint anywhere."));
    QVERIFY(!session->brushStroke());
    session->deselect();
    session->beginBrush(QPointF(10, 10));
    QVERIFY(session->brushStroke() && !session->brushError());
    session->cancelBrush();
}

void PaintRefusalTests::theGradientAndTheSmearSayWhyToo()
{
    const auto session = makeSession();
    session->insert(filledRed());
    emptySelection(*session);
    const QString empty("Nothing is selected, so there’s nowhere to paint. Choose Select › Deselect (Ctrl+D) to paint anywhere.");
    session->selectTool(NavigationTool::gradient);
    session->beginGradient(QPointF(5, 5));
    QCOMPARE(session->brushError(), std::optional(empty));
    QVERIFY(!session->gradientEdit());
    session->setBrushError(std::nullopt);
    session->selectTool(NavigationTool::blur);
    session->setBlurMode(BlurToolMode::smudge);
    QCOMPARE(pressed(*session), std::optional(empty));
    // The mask's own words come first.
    session->deselect();
    session->addLayerMask();
    QCOMPARE(pressed(*session), std::optional<QString>("Smudge and Liquify work on a layer's pixels, not its mask."));
    session->toggleLayerMask();
    QCOMPARE(pressed(*session), std::optional<QString>("Smudge and Liquify work on a layer's pixels, not its mask."));
}

void PaintRefusalTests::aBusyEditorOrAnotherToolSaysNothing()
{
    const auto session = makeSession();
    emptySelection(*session);
    session->setIsProjectBusy(true);
    QCOMPARE(session->paintRefusal(), std::nullopt);
    session->setBrushError(QStringLiteral("Earlier"));
    session->beginBrush(QPointF(10, 10));
    QCOMPARE(session->brushError(), std::nullopt);
    session->setIsProjectBusy(false);
    // Spot Healing never paints a mask, and says nothing.
    session->deselect();
    session->addLayerMask();
    session->toggleLayerMask();
    session->selectTool(NavigationTool::spotHealing);
    session->setBrushError(QStringLiteral("Earlier"));
    session->beginBrush(QPointF(10, 10));
    QCOMPARE(session->brushError(), std::optional<QString>("Earlier"));
    session->selectTool(NavigationTool::move);
    session->beginBrush(QPointF(10, 10));
    QCOMPARE(session->brushError(), std::optional<QString>("Earlier"));
}

QTEST_GUILESS_MAIN(PaintRefusalTests)
#include "PaintRefusalTests.moc"
