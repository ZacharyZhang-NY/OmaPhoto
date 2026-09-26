#include "IO/ProjectStore.h"
#include "SessionFixtures.h"
#include "UI/ProjectWorkspaceView.h"
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QtTest>

// A layer dragged to another tab, the canvas, or New.
namespace {
QMimeData *rowsOf(EditorSession &session)
{
    QStringList lines;
    for (const ImageLayer &layer : session.document().value().layers)
        lines << uuidString(layer.id);
    auto *data = new QMimeData;
    data->setData(ProjectWorkspace::layerType, lines.join('\n').toUtf8());
    return data;
}

// A drag entering a widget, then dropping or leaving.
bool enter(QWidget &target, QMimeData *data, QPoint at = QPoint(5, 5))
{
    // Shift makes Qt propose the move, as plain drags do.
    QDragEnterEvent event(at, Qt::MoveAction | Qt::CopyAction, data, Qt::LeftButton, Qt::ShiftModifier);
    QApplication::sendEvent(&target, &event);
    return event.proposedAction() == Qt::MoveAction && event.isAccepted() && event.dropAction() == Qt::CopyAction;
}

// A row drag proposes a move; project targets copy.
bool drop(QWidget &target, QMimeData *data, QPoint at = QPoint(5, 5))
{
    const bool entered = enter(target, data, at);
    QDropEvent event(at, Qt::MoveAction | Qt::CopyAction, data, Qt::LeftButton, Qt::ShiftModifier);
    QApplication::sendEvent(&target, &event);
    delete data;
    return entered && event.isAccepted() && event.dropAction() == Qt::CopyAction;
}

// A move after entering; only a valid one stays accepted.
bool move(QWidget &target, QMimeData *data, QPoint at = QPoint(5, 5))
{
    QDragMoveEvent event(at, Qt::MoveAction | Qt::CopyAction, data, Qt::LeftButton, Qt::ShiftModifier);
    QApplication::sendEvent(&target, &event);
    return event.isAccepted() && event.dropAction() == Qt::CopyAction;
}

void leave(QWidget &target)
{
    QDragLeaveEvent event;
    QApplication::sendEvent(&target, &event);
}

int layerCount(ProjectTab &tab)
{
    return tab.session.document() ? int(tab.session.document().value().layers.size()) : 0;
}

struct Desk {
    ProjectWorkspace workspace;
    ProjectWorkspaceView window{workspace};
    ProjectTab &first = workspace.current();
    ProjectTab &second = workspace.addTab(false);
    Desk()
    {
        first.session.createDocument(8, 8);
        first.session.addBlankLayer();
        first.session.addBlankLayer();
        second.session.createDocument(8, 8);
        workspace.select(first.id);
        window.show();
        if (!QTest::qWaitForWindowActive(&window))
            throw std::runtime_error("the window never became active");
    }
    ProjectTabButton &button(ProjectTab &tab)
    {
        for (ProjectTabButton *button : window.tabs()->buttons()) {
            if (button->tab->id == tab.id)
                return *button;
        }
        throw std::runtime_error("no button for the tab");
    }
};
}

class ProjectDropTests : public QObject {
    Q_OBJECT
private slots:
    void theWorkspaceKnowsADragsSourceTab();
    void aTabTakesAnotherTabsLayersAndRefusesItsOwn();
    void theCanvasTakesAnotherTabsLayerAtAPoint();
    void theNewButtonOpensATabForADroppedLayer();
    void altDragsAndBusyWorkspacesAreRefused();
};

void ProjectDropTests::theWorkspaceKnowsADragsSourceTab()
{
    Desk desk;
    const QMimeData *data = rowsOf(desk.first.session);
    QCOMPARE(desk.workspace.draggedLayerSource(*data), std::optional(desk.first.id));
    QVERIFY(!desk.workspace.canReceiveDrag(*data, desk.first.id));
    QVERIFY(desk.workspace.canReceiveDrag(*data, desk.second.id));
    QVERIFY(desk.workspace.canReceiveDrag(*data, std::nullopt));
    delete data;
    // A stranger's id has no source: every target takes it.
    QMimeData stranger;
    stranger.setData(ProjectWorkspace::layerType, uuidString(QUuid::createUuid()).toUtf8());
    QVERIFY(!desk.workspace.draggedLayerSource(stranger).has_value());
    QVERIFY(desk.workspace.canReceiveDrag(stranger, desk.first.id));
    QMimeData files;
    files.setText(QStringLiteral("not a layer"));
    QVERIFY(desk.workspace.canReceiveDrag(files, desk.first.id));
    // A line that is no id names nothing, zeros included.
    const QUuid zeroed = desk.first.session.activeLayerID().value();
    rewrite(desk.first.session, [&](ProjectSnapshot &snapshot) { record(snapshot, zeroed).id = QUuid(); });
    QVERIFY(indexOf(desk.first.session.document().value().layers, QUuid()) >= 0);
    QMimeData garbage;
    garbage.setData(ProjectWorkspace::layerType, "not an id");
    QVERIFY(!desk.workspace.draggedLayerSource(garbage).has_value());
    QMimeData zero;
    zero.setData(ProjectWorkspace::layerType, uuidString(QUuid()).toUtf8());
    QCOMPARE(desk.workspace.draggedLayerSource(zero), std::optional(desk.first.id));
}

void ProjectDropTests::aTabTakesAnotherTabsLayersAndRefusesItsOwn()
{
    Desk desk;
    ProjectTabButton &own = desk.button(desk.first), &other = desk.button(desk.second);
    QVERIFY(!enter(own, rowsOf(desk.first.session)));
    QCOMPARE(own.findChild<QToolButton *>("selectTab")->toolTip(), desk.first.title());
    // Targeted, the other tab says so; left, it stops.
    QMimeData *data = rowsOf(desk.first.session);
    QVERIFY(enter(other, data));
    QCOMPARE(other.findChild<QToolButton *>("selectTab")->toolTip(), QStringLiteral("Add to %1").arg(desk.second.title()));
    leave(other);
    QCOMPARE(other.findChild<QToolButton *>("selectTab")->toolTip(), desk.second.title());
    delete data;
    // Dropped, both layers are copied, one after the other.
    QVERIFY(drop(other, rowsOf(desk.first.session)));
    QTRY_COMPARE(layerCount(desk.second), 2);
    QCOMPARE(layerCount(desk.first), 2);
    QCOMPARE(desk.workspace.current().id, desk.second.id);
    QCOMPARE(desk.second.session.document().value().layers.front().name, QString("Layer 1"));
    QVERIFY(!drop(other, rowsOf(desk.second.session)));
}

void ProjectDropTests::theCanvasTakesAnotherTabsLayerAtAPoint()
{
    Desk desk;
    ContentView &editor = *desk.window.content();
    QVERIFY(!editor.acceptsDrop(*std::unique_ptr<QMimeData>(rowsOf(desk.first.session))));
    desk.workspace.select(desk.second.id);
    ContentView &target = *desk.window.content();
    // The canvas fits its view from the event loop first.
    QCoreApplication::processEvents();
    QVERIFY(target.acceptsDrop(*std::unique_ptr<QMimeData>(rowsOf(desk.first.session))));
    auto *ring = target.findChild<QWidget *>("dropRing");
    QVERIFY(ring != nullptr && !ring->isVisible());
    QMimeData *data = rowsOf(desk.first.session);
    QVERIFY(enter(target, data));
    QVERIFY(ring->isVisible());
    leave(target);
    QVERIFY(!ring->isVisible());
    delete data;
    // Dropped on the canvas, the copy lands at that point.
    CanvasView &canvas = *target.findChild<CanvasView *>("editorCanvas");
    const QPoint at = canvas.mapTo(&target, canvas.rect().center());
    const QPointF centre = desk.second.session.viewport.documentPoint(canvas.rect().center(), QSizeF(8, 8));
    QVERIFY(std::abs(centre.x() - 4) < 1 && std::abs(centre.y() - 4) < 1);
    QVERIFY(drop(target, rowsOf(desk.first.session), at));
    QTRY_COMPARE(layerCount(desk.second), 2);
    QVERIFY(!ring->isVisible());
    const ImageLayer copy = desk.second.session.document().value().layers.back();
    QCOMPARE(copy.origin() + QPointF(copy.size().width() / 2, copy.size().height() / 2), centre);
    // A rename open in the target refuses the drop.
    desk.second.session.setRenamingLayerID(copy.id);
    QVERIFY(!target.acceptsDrop(*std::unique_ptr<QMimeData>(rowsOf(desk.first.session))));
}

void ProjectDropTests::theNewButtonOpensATabForADroppedLayer()
{
    Desk desk;
    auto *button = desk.window.findChild<NewCanvasButton *>("newCanvasButton");
    QVERIFY(button != nullptr);
    const QString plain = button->toolTip();
    QMimeData *data = rowsOf(desk.first.session);
    QVERIFY(enter(*button, data));
    QCOMPARE(button->toolTip(), QString("Open in a new project tab"));
    leave(*button);
    QCOMPARE(button->toolTip(), plain);
    delete data;
    // Each dropped row opens its own tab, as Swift.
    QVERIFY(drop(*button, rowsOf(desk.first.session)));
    QTRY_COMPARE(int(desk.workspace.tabs().size()), 4);
    QTRY_COMPARE(layerCount(desk.workspace.current()), 1);
    QCOMPARE(layerCount(*desk.workspace.tabs().at(2)), 1);
    QVERIFY(desk.workspace.current().id != desk.first.id);
    // The action still makes a canvas on a click.
    QVERIFY(desk.window.findChild<QAction *>("newCanvasToolbar") == button->defaultAction());
}

void ProjectDropTests::altDragsAndBusyWorkspacesAreRefused()
{
    Desk desk;
    ProjectTabButton &other = desk.button(desk.second);
    auto *button = desk.window.findChild<NewCanvasButton *>("newCanvasButton");
    // Alt makes a layer drag a duplicate in its panel.
    QTest::keyPress(desk.window.windowHandle(), Qt::Key_Alt, Qt::AltModifier);
    QVERIFY(!enter(other, rowsOf(desk.first.session)));
    QVERIFY(!enter(*button, rowsOf(desk.first.session)));
    QTest::keyRelease(desk.window.windowHandle(), Qt::Key_Alt, Qt::NoModifier);
    QVERIFY(enter(other, rowsOf(desk.first.session)));
    leave(other);
    // Entered, then Alt pressed: Qt routes moves to that widget.
    QMimeData *data = rowsOf(desk.first.session);
    for (QWidget *target : {static_cast<QWidget *>(&other), static_cast<QWidget *>(button)}) {
        QVERIFY(enter(*target, data) && move(*target, data));
        QTest::keyPress(desk.window.windowHandle(), Qt::Key_Alt, Qt::AltModifier);
        QVERIFY(!move(*target, data));
        QTest::keyRelease(desk.window.windowHandle(), Qt::Key_Alt, Qt::NoModifier);
        leave(*target);
    }
    // The canvas entered, then its session busy: the same.
    desk.workspace.select(desk.second.id);
    ContentView &target = *desk.window.content();
    QVERIFY(enter(target, data) && move(target, data));
    desk.second.session.setIsProjectBusy(true);
    QVERIFY(!move(target, data));
    desk.second.session.setIsProjectBusy(false);
    leave(target);
    delete data;
    desk.workspace.select(desk.first.id);
    // A busy source, and a workspace that cannot switch.
    desk.first.session.setIsProjectBusy(true);
    QVERIFY(!enter(other, rowsOf(desk.first.session)));
    QVERIFY(!enter(*button, rowsOf(desk.first.session)));
    desk.first.session.setIsProjectBusy(false);
    QVERIFY(enter(other, rowsOf(desk.first.session)));
    leave(other);
    QCOMPARE(layerCount(desk.second), 0);
}

QTEST_MAIN(ProjectDropTests)
#include "ProjectDropTests.moc"
