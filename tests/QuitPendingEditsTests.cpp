#include "DialogDesk.h"
#include "Document/ProjectWorkspace.h"
#include "SelectionFixtures.h"
#include "UI/ProjectWorkspaceView.h"
#include <QtTest>

// Swift 1.4.4: quit applies canvas edits and cancels dialogs.
namespace {
ImportedImage filled(const QColor &colour)
{
    QImage pixels(100, 20, QImage::Format_RGBA8888_Premultiplied);
    pixels.fill(colour);
    return ImportedImage(pixels, pixels, QStringLiteral("Fixture"));
}

// Swift's await: true once the call's callback has run.
bool awaited(const std::function<void(std::function<void()>)> &call)
{
    bool done = false;
    call([&done] { done = true; });
    return QTest::qWaitFor([&done] { return done; }, 10'000);
}

std::optional<bool> answer(ProjectWorkspace &workspace)
{
    std::optional<bool> result;
    workspace.confirmQuit([&](bool value) { result = value; });
    return QTest::qWaitFor([&] { return result.has_value(); }, 10'000) ? result : std::nullopt;
}

// Swift's test: a gradient waiting for Apply.
void pendGradient(EditorSession &session)
{
    session.createDocument(100, 20);
    session.addBlankLayer();
    session.selectTool(NavigationTool::gradient);
    session.beginGradient(QPointF(0, 10));
    session.moveGradient(std::nullopt, QPointF(100, 10));
    session.endGradientDrag();
}

ImageIdentity identity(const EditorSession &session)
{
    return session.activeLayer().value().asset.value().identity();
}
}

class QuitPendingEditsTests : public QObject {
    Q_OBJECT
private slots:
    void quittingAppliesAPendingGradient();
    void quittingAppliesAPixelMove();
    void quittingCancelsAnOpenDialog();
    void quittingCancelsTheOtherDialogs();
    void quittingCancelsAnAdjustmentsEditor();
    void aWorkspaceGoneMidSettleCallsNothing();
    void closingTheWindowAppliesTheGradientThenAsks();
    void aBusyProjectHoldsTheQuit();
    void aQuitWhileManagingIsRefusedAtOnce();
};

void QuitPendingEditsTests::quittingAppliesAPendingGradient()
{
    ProjectWorkspace workspace;
    EditorSession &session = workspace.current().session;
    pendGradient(session);
    QVERIFY(!workspace.canSwitch());
    const int count = session.history.undoCount();
    QVERIFY(awaited([&](std::function<void()> done) { workspace.settlePendingEdits(std::move(done)); }));
    QVERIFY(!session.gradientEdit());
    QCOMPARE(session.history.undoCount(), count + 1);
    QCOMPARE(session.history.undoName(), QString("Gradient"));
    QVERIFY(workspace.canSwitch());
}

void QuitPendingEditsTests::quittingAppliesAPixelMove()
{
    ProjectWorkspace workspace;
    EditorSession &session = workspace.current().session;
    session.createDocument(100, 20);
    session.insert(filled(QColor(200, 100, 50)));
    session.applySelection(rectPath(QRectF(10, 5, 10, 10)), SelectionMode::replace, "Select");
    QVERIFY(session.beginPixelMove());
    session.movePixels(QSizeF(30, 0));
    QVERIFY(!workspace.canSwitch());
    const int count = session.history.undoCount();
    QVERIFY(awaited([&](std::function<void()> done) { workspace.settlePendingEdits(std::move(done)); }));
    QVERIFY(!session.pixelMove());
    QCOMPARE(session.history.undoCount(), count + 1);
    QCOMPARE(session.history.undoName(), QString("Move Pixels"));
    QCOMPARE(session.selection().value().path.boundingRect(), QRectF(40, 5, 10, 10));
    QVERIFY(workspace.canSwitch());
}

void QuitPendingEditsTests::quittingCancelsAnOpenDialog()
{
    ProjectWorkspace workspace;
    EditorSession &session = workspace.current().session;
    session.createDocument(100, 20);
    session.insert(filled(QColor(200, 100, 50)));
    const ImageIdentity original = identity(session);
    const int count = session.history.undoCount();
    const std::vector<std::function<void()>> opens{[&] { session.beginFilter(FilterKind::gaussianBlur); },
                                                   [&] { session.beginHueSaturation(); }, [&] { session.beginLevels(); }};
    for (const std::function<void()> &open : opens) {
        open();
        QVERIFY(!workspace.canSwitch());
        QVERIFY(awaited([&](std::function<void()> done) { workspace.settlePendingEdits(std::move(done)); }));
        QVERIFY(workspace.canSwitch());
        QVERIFY(identity(session) == original);
    }
    QVERIFY(!session.filterEdit() && !session.hueSaturation() && !session.levels());
    QCOMPARE(session.history.undoCount(), count);
}

void QuitPendingEditsTests::quittingCancelsTheOtherDialogs()
{
    ProjectWorkspace workspace;
    EditorSession &session = workspace.current().session;
    session.createDocument(100, 20);
    session.insert(filled(QColor(200, 100, 50)));
    const QPainterPath before = rectPath(QRectF(10, 5, 10, 10));
    session.applySelection(before, SelectionMode::replace, "Select");
    const PaletteColor foreground = session.foregroundColor();
    const auto settle = [&] { return awaited([&](std::function<void()> done) { workspace.settlePendingEdits(std::move(done)); }); };
    // Color Range: the selection goes back as it was.
    session.beginColorRange();
    QVERIFY(session.colorRange() && !workspace.canSwitch());
    QVERIFY(settle());
    QVERIFY(!session.colorRange());
    QCOMPARE(session.selection().value().path.boundingRect(), before.boundingRect());
    // Expand, Contract or Feather: its panel goes.
    session.setSelectionAmountOperation(SelectionAmountOperation::feather);
    QVERIFY(!workspace.canSwitch());
    QVERIFY(settle());
    QVERIFY(!session.selectionAmountOperation());
    // The colour picker: closed, its colour not taken.
    session.openColorPicker(false);
    session.setColorPickerHSB(PickerHSB(120, 1, 1));
    QVERIFY(!workspace.canSwitch());
    QVERIFY(settle());
    QVERIFY(!session.colorPicker());
    QVERIFY(session.foregroundColor() == foreground);
    QVERIFY(workspace.canSwitch());
}

void QuitPendingEditsTests::quittingCancelsAnAdjustmentsEditor()
{
    ProjectWorkspace workspace;
    EditorSession &session = workspace.current().session;
    session.createDocument(100, 20);
    session.insert(filled(Qt::white));
    session.addAdjustment(AdjustmentKind::exposure);
    const QUuid id = session.adjustmentEditingID().value();
    const LayerAdjustment made = session.activeLayer().value().adjustment.value();
    QVERIFY(awaited([&](std::function<void()> done) { session.beginAdjustmentEditing(id, std::move(done)); }));
    FilterSettings settings = session.filterEdit().value().settings;
    settings.exposure.exposure = 2;
    session.updateFilter(settings, true);
    QVERIFY(!(session.activeLayer().value().adjustment.value() == made));
    QVERIFY(!workspace.canSwitch());
    QVERIFY(awaited([&](std::function<void()> done) { workspace.settlePendingEdits(std::move(done)); }));
    QVERIFY(!session.adjustmentEditingID() && !session.adjustmentOriginal() && !session.filterEdit());
    QVERIFY(session.activeLayer().value().adjustment.value() == made);
    QVERIFY(workspace.canSwitch());
}

void QuitPendingEditsTests::aWorkspaceGoneMidSettleCallsNothing()
{
    auto workspace = std::make_unique<ProjectWorkspace>();
    const std::shared_ptr<ProjectTab> tab = workspace->tabs().front();
    pendGradient(tab->session);
    bool called = false;
    workspace->settlePendingEdits([&called] { called = true; });
    workspace.reset();
    // The tab lives on and its gradient lands; no answer.
    QTRY_VERIFY(!tab->session.gradientEdit());
    QTest::qWait(100);
    QVERIFY(!called);
    QCOMPARE(tab->session.history.undoName(), QString("Gradient"));
}

void QuitPendingEditsTests::closingTheWindowAppliesTheGradientThenAsks()
{
    ProjectWorkspace workspace;
    ProjectWorkspaceView view(workspace);
    view.show();
    EditorSession &session = workspace.current().session;
    pendGradient(session);
    session.history.markSaved();
    DialogDesk desk;
    desk.replies = {"Cancel"};
    // The gradient is a step by the time it asks.
    desk.note = [&] { return QStringLiteral("pending=%1 step=%2").arg(session.gradientEdit().has_value()).arg(session.history.undoName()); };
    view.close();
    QTRY_COMPARE(desk.seen.size(), 1);
    QVERIFY2(desk.seen[0].startsWith("alert|2|Save changes to Untitled?") && desk.seen[0].endsWith("|pending=0 step=Gradient"),
             qPrintable(desk.seen[0]));
    QTRY_VERIFY(!workspace.isManaging());
    QVERIFY(view.isVisible());
}

void QuitPendingEditsTests::aBusyProjectHoldsTheQuit()
{
    ProjectWorkspace workspace;
    EditorSession &session = workspace.current().session;
    pendGradient(session);
    session.setIsProjectBusy(true);
    DialogDesk desk;
    QTest::ignoreMessage(QtWarningMsg, "Quit refused: the project is still busy");
    QCOMPARE(answer(workspace), std::optional(false));
    // Nothing asked, nothing applied: the gradient waits.
    QVERIFY(desk.seen.isEmpty());
    QVERIFY(session.gradientEdit());
    QVERIFY(!workspace.isManaging());
    session.setIsProjectBusy(false);
}

void QuitPendingEditsTests::aQuitWhileManagingIsRefusedAtOnce()
{
    ProjectWorkspace workspace;
    EditorSession &session = workspace.current().session;
    session.createDocument(100, 20);
    DialogDesk desk;
    desk.replies = {"Cancel"};
    std::optional<bool> second;
    bool settled = false;
    bool answered = false;
    // While the first quit asks, a second is refused unasked.
    desk.note = [&] {
        session.setSelectionAmountOperation(SelectionAmountOperation::feather);
        workspace.confirmQuit([&](bool value) { second = value; });
        answered = QTest::qWaitFor([&] { return second.has_value(); }, 5000);
        settled = !session.selectionAmountOperation();
        session.setSelectionAmountOperation(std::nullopt);
        return QString();
    };
    QCOMPARE(answer(workspace), std::optional(false));
    QCOMPARE(desk.seen.size(), 1);
    QVERIFY(answered);
    QCOMPARE(second, std::optional(false));
    QVERIFY(!settled);
}

QTEST_MAIN(QuitPendingEditsTests)
#include "QuitPendingEditsTests.moc"
