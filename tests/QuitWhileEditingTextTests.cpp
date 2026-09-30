#include "DialogDesk.h"
#include "Document/ProjectWorkspace.h"
#include "UI/ProjectWorkspaceView.h"
#include <QtTest>

// Swift 1.3.1: close or quit mid-text applies the text first.
namespace {
// A saved canvas with `content` typed under the Type tool.
void editText(EditorSession &session, const QString &content)
{
    session.createDocument(800, 600, true);
    session.history.markSaved();
    session.selectTool(NavigationTool::type);
    session.beginText(QPointF(100, 100));
    TextDraft draft = session.textDraft().value();
    draft.style.content = content;
    session.setTextDraft(draft);
}

// The callback's answer; empty if it never came.
std::optional<bool> answer(const std::function<void(std::function<void(bool)>)> &call)
{
    std::optional<bool> result;
    call([&](bool value) { result = value; });
    return QTest::qWaitFor([&] { return result.has_value(); }, 10'000) ? result : std::nullopt;
}

QString topText(const EditorSession &session)
{
    return session.document().value().layers.back().text.value().style.content;
}
}

class QuitWhileEditingTextTests : public QObject {
    Q_OBJECT
private slots:
    void closingTheWindowAppliesTheTextThenAsks();
    void quittingAppliesTextInQuitOrderAsOneStepEach();
    void aRefusedDraftStopsTheQuitWhereItStands();
    void textThatCannotBeAppliedHoldsTheQuit();
    void editedExistingTextIsOneEditStep();
    void unchangedAndBlankDraftsCloseQuietly();
};

void QuitWhileEditingTextTests::closingTheWindowAppliesTheTextThenAsks()
{
    ProjectWorkspace workspace;
    ProjectWorkspaceView view(workspace);
    view.show();
    const std::shared_ptr<ProjectTab> tab = workspace.tabs().front();
    editText(tab->session, QStringLiteral("Typed"));
    DialogDesk desk;
    desk.replies = {"Cancel", "Don’t Save"};
    // The text is a layer by the time it asks.
    desk.note = [&] { return QStringLiteral("draft=%1 text=%2").arg(tab->session.textDraft().has_value()).arg(topText(tab->session)); };
    // Cancelled: the window stays, the text a step.
    view.close();
    QTRY_COMPARE(desk.seen.size(), 1);
    QVERIFY2(desk.seen[0].startsWith("alert|2|Save changes to Untitled?") && desk.seen[0].endsWith("|draft=0 text=Typed"), qPrintable(desk.seen[0]));
    QTRY_VERIFY(!workspace.isManaging());
    QVERIFY(view.isVisible());
    QCOMPARE(tab->session.history.undoName(), QString("New Text Layer"));
    QVERIFY(tab->session.isModified());
    // Asked again, Don't Save closes it.
    view.close();
    QTRY_VERIFY(!view.isVisible());
    QCOMPARE(desk.seen.size(), 2);
}

void QuitWhileEditingTextTests::quittingAppliesTextInQuitOrderAsOneStepEach()
{
    ProjectWorkspace workspace;
    const std::shared_ptr<ProjectTab> first = workspace.tabs().front();
    workspace.newCanvas();
    const std::shared_ptr<ProjectTab> second = workspace.tabs().back();
    QVERIFY(first != second);
    editText(first->session, QStringLiteral("First"));
    editText(second->session, QStringLiteral("Second"));
    // The tab on screen first; Cancel stops there.
    DialogDesk desk;
    desk.replies = {"Cancel"};
    desk.note = [&] { return workspace.current().session.document().value().layers.back().text.value().style.content; };
    QCOMPARE(answer([&](auto done) { workspace.confirmQuit(done); }), std::optional<bool>(false));
    QCOMPARE(desk.seen.size(), 1);
    QVERIFY2(desk.seen[0].endsWith("|Second"), qPrintable(desk.seen[0]));
    for (const auto &[tab, text] : {std::pair(first, QStringLiteral("First")), std::pair(second, QStringLiteral("Second"))}) {
        QVERIFY(!tab->session.textDraft());
        QCOMPARE(topText(tab->session), text);
        QCOMPARE(tab->session.history.undoName(), QString("New Text Layer"));
        QVERIFY(tab->session.isModified());
        // One step: undo removes the text, redo restores it.
        const size_t layers = tab->session.document().value().layers.size();
        tab->session.undo();
        QCOMPARE(tab->session.document().value().layers.size(), layers - 1);
        QVERIFY(!tab->session.isModified());
        tab->session.redo();
        QCOMPARE(topText(tab->session), text);
    }
}

void QuitWhileEditingTextTests::aRefusedDraftStopsTheQuitWhereItStands()
{
    ProjectWorkspace workspace;
    const std::shared_ptr<ProjectTab> a = workspace.tabs().front();
    workspace.newCanvas();
    const std::shared_ptr<ProjectTab> b = workspace.tabs().back();
    workspace.newCanvas();
    const std::shared_ptr<ProjectTab> c = workspace.tabs().back();
    // Stored A, B, C; C shown: asked C, A, B.
    QCOMPARE(&workspace.current(), c.get());
    editText(a->session, QStringLiteral("A"));
    editText(b->session, QStringLiteral("B"));
    editText(c->session, QStringLiteral("C"));
    a->session.setIsProjectBusy(true);
    DialogDesk desk;
    QCOMPARE(answer([&](auto done) { workspace.confirmQuit(done); }), std::optional<bool>(false));
    // C applied; A refused; B never reached; nothing asked.
    QVERIFY(!c->session.textDraft());
    QCOMPARE(topText(c->session), QString("C"));
    QCOMPARE(a->session.textDraft().value().style.content, QString("A"));
    QCOMPARE(b->session.textDraft().value().style.content, QString("B"));
    QVERIFY(!b->session.isModified());
    QVERIFY(desk.seen.isEmpty());
    QVERIFY(!workspace.isManaging());
    a->session.setIsProjectBusy(false);
}

void QuitWhileEditingTextTests::textThatCannotBeAppliedHoldsTheQuit()
{
    ProjectWorkspace workspace;
    const std::shared_ptr<ProjectTab> tab = workspace.tabs().front();
    editText(tab->session, QStringLiteral("Held"));
    // A busy project refuses the text; the quit stops there.
    tab->session.setIsProjectBusy(true);
    DialogDesk desk;
    QCOMPARE(answer([&](auto done) { workspace.confirmQuit(done); }), std::optional<bool>(false));
    QVERIFY(desk.seen.isEmpty());
    QCOMPARE(tab->session.textDraft().value().style.content, QString("Held"));
    QVERIFY(!workspace.isManaging());
    tab->session.setIsProjectBusy(false);
}

void QuitWhileEditingTextTests::editedExistingTextIsOneEditStep()
{
    ProjectWorkspace workspace;
    const std::shared_ptr<ProjectTab> tab = workspace.tabs().front();
    EditorSession &session = tab->session;
    editText(session, QStringLiteral("Original"));
    QVERIFY(session.finishText());
    session.history.markSaved();
    const QUuid layer = session.document().value().layers.back().id;
    const int steps = session.history.undoCount();
    // The layer's text opened again and changed.
    session.editActiveText();
    TextDraft draft = session.textDraft().value();
    QCOMPARE(draft.layerID.value(), layer);
    draft.style.content = QStringLiteral("Edited");
    session.setTextDraft(draft);
    DialogDesk desk;
    desk.replies = {"Cancel"};
    QCOMPARE(answer([&](auto done) { workspace.confirmQuit(done); }), std::optional<bool>(false));
    QCOMPARE(desk.seen.size(), 1);
    QCOMPARE(session.document().value().layers.back().id, layer);
    QCOMPARE(topText(session), QString("Edited"));
    QCOMPARE(session.history.undoCount(), steps + 1);
    QCOMPARE(session.history.undoName(), QString("Edit Text"));
    session.undo();
    QCOMPARE(topText(session), QString("Original"));
    QVERIFY(!session.isModified());
    session.redo();
    QCOMPARE(topText(session), QString("Edited"));
}

void QuitWhileEditingTextTests::unchangedAndBlankDraftsCloseQuietly()
{
    ProjectWorkspace workspace;
    const std::shared_ptr<ProjectTab> first = workspace.tabs().front();
    workspace.newCanvas();
    const std::shared_ptr<ProjectTab> second = workspace.tabs().back();
    // First: saved text opened and left as it was.
    editText(first->session, QStringLiteral("Kept"));
    QVERIFY(first->session.finishText());
    first->session.history.markSaved();
    first->session.editActiveText();
    QVERIFY(first->session.textDraft().has_value());
    // Second: new text with nothing typed.
    editText(second->session, QString());
    const size_t layers = second->session.document().value().layers.size();
    const int firstSteps = first->session.history.undoCount(), secondSteps = second->session.history.undoCount();
    DialogDesk desk;
    QCOMPARE(answer([&](auto done) { workspace.confirmQuit(done); }), std::optional<bool>(true));
    QVERIFY(desk.seen.isEmpty());
    QVERIFY(!first->session.textDraft() && !second->session.textDraft());
    QCOMPARE(first->session.history.undoCount(), firstSteps);
    QCOMPARE(second->session.history.undoCount(), secondSteps);
    QCOMPARE(second->session.document().value().layers.size(), layers);
    QCOMPARE(topText(first->session), QString("Kept"));
}

QTEST_MAIN(QuitWhileEditingTextTests)
#include "QuitWhileEditingTextTests.moc"
