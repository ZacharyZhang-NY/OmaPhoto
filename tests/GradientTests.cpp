#include "BrushFixtures.h"
#include "Document/EditorSession.h"
#include "Document/ProjectWorkspace.h"
#include "IO/ImageExporter.h"
#include <QSignalSpy>

// Swift's GradientTests, and the palette the gradient reads.
namespace {
std::unique_ptr<EditorSession> makeSession(int width = 101, int height = 4)
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(width, height);
    session->addBlankLayer();
    session->selectTool(NavigationTool::gradient);
    GradientSettings settings = session->gradientSettings();
    settings.style = GradientStyle::foregroundToBackground;
    session->setGradientSettings(settings);
    return session;
}

std::vector<int> exported(const EditorSession &session, int x, int y)
{
    return pixel(ImageExporter::render(session.projectSnapshot().value()).image, x, y);
}

void drag(EditorSession &session, QPointF start, QPointF end)
{
    session.beginGradient(start);
    session.moveGradient(std::nullopt, end);
    session.endGradientDrag();
}

// Swift's `await commitGradient()`: done once the step lands.
bool commit(EditorSession &session)
{
    bool finished = false;
    session.commitGradient([&] { finished = true; });
    return QTest::qWaitFor([&] { return finished; }, 5000);
}

// Core Graphics quantises ramps: ends may fall levels short.
bool near(int value, int target, int tolerance = 3)
{
    return std::abs(value - target) <= tolerance;
}

bool near(const std::vector<int> &value, const std::vector<int> &target)
{
    for (size_t i = 0; i < value.size(); ++i) {
        if (!near(value[i], target[i]))
            return false;
    }
    return true;
}

void setting(EditorSession &session, const std::function<void(GradientSettings &)> &change)
{
    GradientSettings settings = session.gradientSettings();
    change(settings);
    session.setGradientSettings(settings);
}
}

class GradientTests : public QObject {
    Q_OBJECT
private slots:
    void foregroundToBackgroundFillsCanvasAndCommitsOneUndo();
    void radialSpreadsFromStartToRimInEveryDirection();
    void reverseOpacityAndDirectionFollowSettings();
    void foregroundToTransparentPreservesUnderlyingPixelsAndAlpha();
    void cancelUndoAndClicksLeaveDocumentUntouched();
    void redraggingReplacesPendingLineWithoutAccumulating();
    void maskGradientWritesCoverageInsideLayerBounds();
    void paletteChangesUpdatePendingPreview();
    void switchingToolLayerOrTargetAppliesIt();
    void aPendingGradientHoldsLayerEdits();
    void invertAndFiltersApplyItFirst();
    void digitsAndTabSetOpacityAndShape();
    void thePaletteIsTheBrushsColourAndABackground();
    void aMasksPaletteIsItsPaint();
    void aPendingGradientKeepsItsTarget();
    void revisionsBusyAndLinelessCommits();
};

void GradientTests::foregroundToBackgroundFillsCanvasAndCommitsOneUndo()
{
    const auto session = makeSession();
    const int count = session->history.undoCount();
    drag(*session, QPointF(0.5, 2), QPointF(100.5, 2));
    QVERIFY(session->gradientEdit());
    QVERIFY(!session->activeLayer().value().asset && session->history.undoCount() == count);
    QVERIFY(commit(*session));
    QVERIFY(!session->gradientEdit() && !session->brushError());
    QCOMPARE(session->history.undoCount(), count + 1);
    QCOMPARE(session->history.undoName(), QString("Gradient"));
    QVERIFY(near(exported(*session, 0, 0), {0, 0, 0, 255}));
    QVERIFY(near(exported(*session, 100, 3), {255, 255, 255, 255}));
    const std::vector<int> middle = exported(*session, 50, 1);
    QVERIFY(near(middle[0], 128) && middle[0] == middle[1] && middle[3] == 255);
    session->undo();
    QVERIFY(!session->activeLayer().value().asset);
}

void GradientTests::radialSpreadsFromStartToRimInEveryDirection()
{
    const auto session = makeSession(101, 101);
    setting(*session, [](GradientSettings &settings) { settings.shape = GradientShape::radial; });
    drag(*session, QPointF(50.5, 50.5), QPointF(90.5, 50.5));
    QVERIFY(commit(*session));
    QVERIFY(near(exported(*session, 50, 50), {0, 0, 0, 255}));
    // Equal distances get equal values; past the rim, background.
    std::vector<int> halfway;
    for (const QPoint point : {QPoint(70, 50), QPoint(30, 50), QPoint(50, 70), QPoint(50, 30)})
        halfway.push_back(exported(*session, point.x(), point.y())[0]);
    for (const int value : halfway)
        QVERIFY2(near(value, 128, 5) && near(value, halfway[0], 1), qPrintable(QString::number(value)));
    QVERIFY(near(exported(*session, 100, 50), {255, 255, 255, 255}));
    QVERIFY(near(exported(*session, 0, 0), {255, 255, 255, 255}));
}

void GradientTests::reverseOpacityAndDirectionFollowSettings()
{
    const auto session = makeSession();
    setting(*session, [](GradientSettings &settings) {
        settings.reversed = true;
        settings.opacity = 0.5;
    });
    drag(*session, QPointF(0.5, 2), QPointF(100.5, 2));
    QVERIFY(commit(*session));
    // White at the start, half clear on a blank layer.
    const std::vector<int> start = exported(*session, 0, 2);
    QVERIFY(near(start[3], 128) && near(start[0], start[3]));
    const std::vector<int> end = exported(*session, 100, 2);
    QVERIFY(near(end[0], 0) && near(end[3], 128));
}

void GradientTests::foregroundToTransparentPreservesUnderlyingPixelsAndAlpha()
{
    const auto session = makeSession();
    session->setPaletteColor(PaletteColor{1, 0, 0}, false);
    // Everything before the start is foreground: solid red.
    drag(*session, QPointF(100.5, 2), QPointF(101, 2));
    QVERIFY(commit(*session));
    QVERIFY(near(exported(*session, 50, 2), {255, 0, 0, 255}));
    session->setPaletteColor(PaletteColor::black(), false);
    setting(*session, [](GradientSettings &settings) { settings.style = GradientStyle::foregroundToTransparent; });
    drag(*session, QPointF(0.5, 2), QPointF(100.5, 2));
    QVERIFY(commit(*session));
    QVERIFY(near(exported(*session, 0, 2), {0, 0, 0, 255}));
    QVERIFY(near(exported(*session, 100, 2), {255, 0, 0, 255}));
    const std::vector<int> middle = exported(*session, 50, 2);
    QVERIFY(near(middle[0], 128) && middle[1] == 0 && middle[3] == 255);
}

void GradientTests::cancelUndoAndClicksLeaveDocumentUntouched()
{
    // Nothing else to undo: a pending gradient alone enables Undo.
    EditorSession fresh;
    fresh.createDocument(20, 4, true);
    fresh.history.reset();
    fresh.selectTool(NavigationTool::gradient);
    QVERIFY(!fresh.canUndo());
    drag(fresh, QPointF(0.5, 2), QPointF(10.5, 2));
    QVERIFY(fresh.canUndo());
    fresh.undo();
    QVERIFY(!fresh.gradientEdit() && !fresh.canUndo());
    const auto session = makeSession();
    const std::vector<ImageLayer> before = session->document().value().layers;
    session->beginGradient(QPointF(10, 2));
    session->endGradientDrag();
    QVERIFY(!session->gradientEdit());
    drag(*session, QPointF(0, 2), QPointF(100, 2));
    session->cancelGradient();
    QVERIFY(!session->gradientEdit() && session->document().value().layers == before);
    // The first Undo discards a pending gradient, as Photoshop.
    drag(*session, QPointF(0, 2), QPointF(100, 2));
    QVERIFY(session->canUndo());
    const int count = session->history.undoCount();
    session->undo();
    QVERIFY(!session->gradientEdit() && session->document().value().layers == before);
    QCOMPARE(session->history.undoCount(), count);
}

void GradientTests::redraggingReplacesPendingLineWithoutAccumulating()
{
    const auto session = makeSession();
    drag(*session, QPointF(0.5, 2), QPointF(100.5, 2));
    drag(*session, QPointF(100.5, 2), QPointF(0.5, 2));
    QVERIFY(commit(*session));
    QVERIFY(near(exported(*session, 0, 2), {255, 255, 255, 255}));
    QVERIFY(near(exported(*session, 100, 2), {0, 0, 0, 255}));
}

void GradientTests::maskGradientWritesCoverageInsideLayerBounds()
{
    const auto session = makeSession();
    drag(*session, QPointF(0, 2), QPointF(0.6, 2));
    QVERIFY(commit(*session));
    session->addLayerMask(true);
    const QUuid id = session->activeLayerID().value();
    session->selectLayerTarget(id, true);
    QVERIFY(session->isMaskSelected());
    drag(*session, QPointF(0.5, 2), QPointF(100.5, 2));
    QVERIFY(commit(*session));
    QVERIFY(!session->brushError());
    QCOMPARE(session->history.undoName(), QString("Gradient Mask"));
    // The mask's palette paints black, to hide, then white.
    QVERIFY(near(exported(*session, 0, 2)[3], 0));
    QVERIFY(near(exported(*session, 100, 2)[3], 255));
    QVERIFY(near(exported(*session, 50, 2)[3], 128));
}

void GradientTests::paletteChangesUpdatePendingPreview()
{
    const auto session = makeSession();
    drag(*session, QPointF(0.5, 2), QPointF(100.5, 2));
    const int revision = session->brushRevision();
    session->swapPaletteColors();
    QVERIFY(session->brushRevision() > revision);
    QCOMPARE(session->gradientColors(false)[0].redF(), 1.0);
    session->cancelGradient();
}

void GradientTests::switchingToolLayerOrTargetAppliesIt()
{
    const auto session = makeSession();
    drag(*session, QPointF(0.5, 2), QPointF(100.5, 2));
    // Swift's Task: the step lands once the caller has returned.
    session->selectTool(NavigationTool::brush);
    QVERIFY(session->gradientEdit() && !session->isProjectBusy());
    QTRY_VERIFY(!session->gradientEdit() && !session->isProjectBusy());
    QCOMPARE(session->history.undoName(), QString("Gradient"));
    const QUuid first = session->activeLayerID().value();
    session->addBlankLayer();
    session->selectTool(NavigationTool::gradient);
    drag(*session, QPointF(0.5, 2), QPointF(50.5, 2));
    const int count = session->history.undoCount();
    session->selectLayer(first);
    QTRY_COMPARE(session->history.undoCount(), count + 1);
    QVERIFY(!session->gradientEdit());
    drag(*session, QPointF(0.5, 2), QPointF(50.5, 2));
    session->selectLayers({first}, first);
    QVERIFY(session->gradientEdit());
    // A new multi-selection applies it too.
    const QUuid top = session->document().value().layers.back().id;
    session->selectLayers({first, top}, first);
    QTRY_VERIFY(!session->gradientEdit());
    session->selectLayers({first}, first);
    drag(*session, QPointF(0.5, 2), QPointF(50.5, 2));
    session->addLayerMask(true);
    QVERIFY(session->gradientEdit());
    session->selectLayerTarget(first, true);
    QTRY_VERIFY(!session->gradientEdit());
    QCOMPARE(session->history.undoName(), QString("Gradient"));
}

void GradientTests::aPendingGradientHoldsLayerEdits()
{
    const auto session = makeSession();
    drag(*session, QPointF(0.5, 2), QPointF(100.5, 2));
    QVERIFY(!session->canEditLayers() && !session->canPaint() && session->canUseHistory());
    const int count = int(session->document().value().layers.size());
    session->addBlankLayer();
    QCOMPARE(int(session->document().value().layers.size()), count);
    // A step restored drops the pending gradient.
    QVERIFY(commit(*session));
    session->undo();
    drag(*session, QPointF(0.5, 2), QPointF(100.5, 2));
    QVERIFY(session->gradientEdit());
    session->redo();
    QVERIFY(!session->gradientEdit());
    QCOMPARE(session->history.undoName(), QString("Gradient"));
    // The window keeps its tab while one is pending.
    ProjectWorkspace workspace;
    QVERIFY(workspace.canSwitch());
    workspace.current().session.createDocument(8, 8, true);
    workspace.current().session.selectTool(NavigationTool::gradient);
    workspace.current().session.beginGradient(QPointF(1, 1));
    workspace.current().session.moveGradient(std::nullopt, QPointF(6, 1));
    QVERIFY(!workspace.canSwitch());
    workspace.current().session.cancelGradient();
    QVERIFY(workspace.canSwitch());
}

void GradientTests::invertAndFiltersApplyItFirst()
{
    const auto session = makeSession();
    // Invert needs pixels: a first gradient lands, a second waits.
    drag(*session, QPointF(0.5, 2), QPointF(100.5, 2));
    QVERIFY(commit(*session));
    drag(*session, QPointF(0.5, 2), QPointF(100.5, 2));
    const int count = session->history.undoCount();
    bool inverted = false;
    session->invertPixels([&] { inverted = true; });
    QTRY_VERIFY(inverted);
    QCOMPARE(session->history.undoCount(), count + 2);
    QVERIFY(!session->gradientEdit());
    // Inverted black to white: the start is white now.
    QVERIFY(near(exported(*session, 0, 2), {255, 255, 255, 255}));
    DocumentSelection selection;
    selection.path.addRect(QRectF(10, 0, 20, 4));
    session->setSelection(selection, QStringLiteral("Marquee"));
    drag(*session, QPointF(0.5, 2), QPointF(100.5, 2));
    QVERIFY(session->gradientEdit() && session->canContentAwareFill());
    session->beginFilter(FilterKind::contentAwareFill);
    QTRY_VERIFY(session->filterEdit().has_value());
    QVERIFY(!session->gradientEdit());
    session->cancelFilter();
}

void GradientTests::digitsAndTabSetOpacityAndShape()
{
    const auto session = makeSession();
    QVERIFY(session->usesOpacityKeys());
    session->typeOpacityDigit(4, 0.0);
    session->typeOpacityDigit(5, 0.1);
    QCOMPARE(session->gradientSettings().opacity, 0.45);
    QCOMPARE(session->brushSettings().opacity, 1.0);
    setting(*session, [](GradientSettings &settings) {
        settings.style = GradientStyle::foregroundToTransparent;
        settings.reversed = true;
    });
    session->cycleToolMode();
    QCOMPARE(session->gradientSettings(), (GradientSettings{GradientShape::radial, GradientStyle::foregroundToTransparent, true, 0.45}));
    session->cycleToolMode();
    QCOMPARE(session->gradientSettings().shape, GradientShape::linear);
    QCOMPARE(rawValue(GradientShape::linear), QString("Linear"));
    QCOMPARE(rawValue(GradientShape::radial), QString("Radial"));
    QCOMPARE(rawValue(GradientStyle::foregroundToBackground), QString("Foreground to Background"));
    QCOMPARE(rawValue(GradientStyle::foregroundToTransparent), QString("Foreground to Transparent"));
    // Settings redraw the pending preview.
    drag(*session, QPointF(0.5, 2), QPointF(100.5, 2));
    for (const auto &change : std::initializer_list<std::function<void()>>{
             [&] { session->setBackgroundColor(PaletteColor{0, 0, 1}); }, [&] { session->setMaskPaintWhite(true); },
             [&] { session->setBrushSettings(brush(12, 1, 0, 1, 0)); }, [&] { setting(*session, [](GradientSettings &s) { s.reversed = true; }); }}) {
        const int revision = session->brushRevision();
        change();
        QVERIFY(session->brushRevision() > revision);
    }
}

void GradientTests::thePaletteIsTheBrushsColourAndABackground()
{
    EditorSession session;
    session.createDocument(20, 10, true);
    QCOMPARE(session.foregroundColor(), PaletteColor::black());
    QCOMPARE(session.backgroundColor(), PaletteColor::white());
    session.setPaletteColor(PaletteColor{0.2, 0.4, 0.6}, false);
    QCOMPARE(session.brushSettings().red, 0.2);
    QCOMPARE(session.brushSettings().blue, 0.6);
    session.setPaletteColor(PaletteColor{1, 0, 0}, true);
    QCOMPARE(session.paletteColor(true), (PaletteColor{1, 0, 0}));
    session.swapPaletteColors();
    QCOMPARE(session.foregroundColor(), (PaletteColor{1, 0, 0}));
    QCOMPARE(session.backgroundColor(), (PaletteColor{0.2, 0.4, 0.6}));
    // Fill takes the palette's colour.
    DocumentSelection selection;
    selection.path.addRect(QRectF(0, 0, 10, 10));
    session.setSelection(selection, QStringLiteral("Marquee"));
    bool filled = false;
    session.fillSelection(EditorSession::FillSource::foreground, [&] { filled = true; });
    QTRY_VERIFY(filled);
    QCOMPARE(exported(session, 5, 5), (std::vector<int>{255, 0, 0, 255}));
    session.resetPaletteColors();
    QCOMPARE(session.foregroundColor(), PaletteColor::black());
    QCOMPARE(session.backgroundColor(), PaletteColor::white());
    // Busy, the palette stays.
    session.setIsProjectBusy(true);
    QVERIFY(!session.canEditPalette());
    session.setPaletteColor(PaletteColor::white(), false);
    session.swapPaletteColors();
    QCOMPARE(session.foregroundColor(), PaletteColor::black());
    session.setIsProjectBusy(false);
}

void GradientTests::aMasksPaletteIsItsPaint()
{
    EditorSession session;
    session.createDocument(20, 10, true);
    session.addLayerMask(true);
    QVERIFY(session.isMaskSelected());
    QCOMPARE(session.paletteColor(false), PaletteColor::black());
    QCOMPARE(session.paletteColor(true), PaletteColor::white());
    session.setPaletteColor(PaletteColor::white(), false);
    QVERIFY(session.maskPaintWhite());
    session.setPaletteColor(PaletteColor::white(), true);
    QVERIFY(!session.maskPaintWhite());
    session.swapPaletteColors();
    QVERIFY(session.maskPaintWhite());
    session.resetPaletteColors();
    QVERIFY(!session.maskPaintWhite());
    // A mask's gradient colours are gray.
    const std::array<QColor, 2> colors = session.gradientColors(true);
    QCOMPARE(colors[0], QColor::fromRgbF(0, 0, 0, 1));
    QCOMPARE(colors[1].alphaF(), 0.0f);
}

void GradientTests::aPendingGradientKeepsItsTarget()
{
    const auto session = makeSession();
    // Another tool draws no gradient.
    session->selectTool(NavigationTool::brush);
    session->beginGradient(QPointF(1, 1));
    QVERIFY(!session->gradientEdit());
    session->selectTool(NavigationTool::gradient);
    const QUuid first = session->activeLayerID().value();
    drag(*session, QPointF(0.5, 2), QPointF(100.5, 2));
    const BrushStroke *raster = session->gradientEdit().value().raster.get();
    // Same target: a click restarts, the old pixels still showing.
    const std::vector<BrushPatch> shown = raster->patches();
    session->beginGradient(QPointF(40, 2));
    QCOMPARE(session->gradientEdit().value().start, QPointF(40, 2));
    QCOMPARE(session->gradientEdit().value().end, QPointF(40, 2));
    QCOMPARE(raster->patches().front().image, shown.front().image);
    session->endGradientDrag();
    QVERIFY(!session->gradientEdit());
    // Until a switch lands, other targets start no line.
    session->addBlankLayer();
    session->selectTool(NavigationTool::gradient);
    drag(*session, QPointF(0.5, 2), QPointF(100.5, 2));
    const BrushStroke *second = session->gradientEdit().value().raster.get();
    session->selectLayer(first);
    session->beginGradient(QPointF(10, 1));
    QCOMPARE(session->gradientEdit().value().raster.get(), second);
    QCOMPARE(session->gradientEdit().value().start, QPointF(0.5, 2));
    QTRY_VERIFY(!session->gradientEdit());
    session->addLayerMask(true);
    session->selectLayerTarget(first, false);
    drag(*session, QPointF(0.5, 2), QPointF(100.5, 2));
    const BrushStroke *pixels = session->gradientEdit().value().raster.get();
    session->selectLayerTarget(first, true);
    session->beginGradient(QPointF(10, 1));
    QCOMPARE(session->gradientEdit().value().raster.get(), pixels);
    QCOMPARE(session->gradientEdit().value().start, QPointF(0.5, 2));
    QVERIFY(!pixels->isMask);
    QTRY_VERIFY(!session->gradientEdit());
}

void GradientTests::revisionsBusyAndLinelessCommits()
{
    const auto session = makeSession();
    int revision = session->brushRevision();
    session->beginGradient(QPointF(1, 1));
    QVERIFY(session->brushRevision() > revision);
    // A line not yet drawn commits nothing and ends.
    const int count = session->history.undoCount();
    QVERIFY(commit(*session));
    QVERIFY(!session->gradientEdit());
    QCOMPARE(session->history.undoCount(), count);
    drag(*session, QPointF(0.5, 2), QPointF(100.5, 2));
    revision = session->brushRevision();
    session->cancelGradient();
    QVERIFY(session->brushRevision() > revision);
    // Busy, the commit waits: the gradient stays pending.
    drag(*session, QPointF(0.5, 2), QPointF(100.5, 2));
    session->setIsProjectBusy(true);
    QVERIFY(commit(*session));
    QVERIFY(session->gradientEdit());
    session->setIsProjectBusy(false);
    QCOMPARE(session->history.undoCount(), count);
}

QTEST_GUILESS_MAIN(GradientTests)
#include "GradientTests.moc"
