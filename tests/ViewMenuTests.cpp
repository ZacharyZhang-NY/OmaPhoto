#include "MenuFixtures.h"
#include "Rendering/EditorCanvas.h"
#include "UI/GridSettingsSheet.h"
#include <QLineEdit>

// The View menu: zoom, the pixel grid and Snap.
class ViewMenuTests : public QObject {
    Q_OBJECT
private slots:
    void viewEntriesZoomTheFrontSession();
    void thePixelGridEntryTogglesTheSession();
    void guideEntriesFollowTheSession();
    void gridSettingsOpensItsSheet();
};

void ViewMenuTests::viewEntriesZoomTheFrontSession()
{
    Bar bar;
    for (const char *name : {"fit", "actualPixels", "zoomIn", "zoomOut"})
        QVERIFY(!bar.action(name).isEnabled());
    bar.session().createDocument(400, 300);
    for (const char *name : {"fit", "actualPixels", "zoomIn", "zoomOut"})
        QVERIFY(bar.action(name).isEnabled());
    bar.session().zoom(0.5);
    bar.action("actualPixels").trigger();
    QCOMPARE(bar.session().viewport.zoom(), 1.0);
    bar.action("zoomIn").trigger();
    QCOMPARE(bar.session().viewport.zoom(), 1.25);
    bar.action("zoomOut").trigger();
    bar.action("zoomOut").trigger();
    QCOMPARE(bar.session().viewport.zoom(), 2.0 / 3.0);
    bar.action("fit").trigger();
    QVERIFY(bar.session().viewport.followsFit());
    // A text field or open text keeps the keys.
    bar.window.show();
    QVERIFY(QTest::qWaitForWindowActive(&bar.window));
    bar.session().zoom(1);
    auto *field = new QLineEdit(&bar.window);
    field->show();
    field->setFocus();
    QTRY_VERIFY(field->hasFocus());
    bar.action("zoomIn").trigger();
    QCOMPARE(bar.session().viewport.zoom(), 1.0);
    auto &canvas = *bar.window.findChild<CanvasView *>();
    canvas.setFocus();
    QTRY_VERIFY(canvas.hasFocus());
    bar.session().selectTool(NavigationTool::type);
    bar.session().beginText(QPointF(10, 10), true);
    QVERIFY(bar.session().textDraft());
    canvas.setFocus();
    QTRY_VERIFY(canvas.hasFocus());
    bar.action("zoomIn").trigger();
    QCOMPARE(bar.session().viewport.zoom(), 1.0);
    bar.session().cancelText();
    bar.action("zoomIn").trigger();
    QCOMPARE(bar.session().viewport.zoom(), 1.25);
}

void ViewMenuTests::thePixelGridEntryTogglesTheSession()
{
    Bar bar;
    QAction &grid = bar.action("pixelGrid");
    QVERIFY(grid.isCheckable() && grid.isChecked() && grid.isEnabled());
    QCOMPARE(grid.text(), QString("Pixel Grid (800% and above)"));
    grid.trigger();
    QVERIFY(!bar.session().showsPixelGrid() && !grid.isChecked());
    grid.trigger();
    QVERIFY(bar.session().showsPixelGrid() && grid.isChecked());
    // The session's own change reaches the mark.
    bar.session().setShowsPixelGrid(false);
    QVERIFY(!grid.isChecked());
    bar.workspace.newCanvas();
    QVERIFY(grid.isChecked() && grid.isEnabled());
    // Snap, beneath it, follows the session the same way.
    QAction &snap = bar.action("snap");
    QVERIFY(snap.isCheckable() && snap.isChecked() && snap.isEnabled() && snap.text() == "Snap");
    snap.trigger();
    QVERIFY(!bar.session().snappingEnabled() && !snap.isChecked());
    snap.trigger();
    QVERIFY(bar.session().snappingEnabled() && snap.isChecked());
    bar.session().setSnappingEnabled(false);
    QVERIFY(!snap.isChecked());
}

void ViewMenuTests::guideEntriesFollowTheSession()
{
    Bar bar;
    const std::vector<std::tuple<const char *, QString, bool (EditorSession::*)() const, bool>> toggles{
        {"showGrid", "Grid", &EditorSession::showsGrid, false},
        {"showGuides", "Guides", &EditorSession::showsGuides, true},
        {"showRulers", "Rulers", &EditorSession::showsRulers, false},
        {"snapEnabled", "Snap", &EditorSession::snapEnabled, true},
        {"snapToGuides", "Guides", &EditorSession::snapToGuides, true},
        {"snapToGrid", "Grid", &EditorSession::snapToGrid, false},
        {"snapToLayers", "Layers", &EditorSession::snapToLayers, true},
        {"snapToDocumentBounds", "Document Bounds", &EditorSession::snapToDocumentBounds, true},
        {"lockGuides", "Lock Guides", &EditorSession::locksGuides, false}};
    // Without a document every entry rests.
    for (const auto &[name, text, read, start] : toggles) {
        QAction &entry = bar.action(name);
        QVERIFY2(entry.isCheckable() && !entry.isEnabled() && entry.isChecked() == start && entry.text() == text, name);
    }
    QVERIFY(!bar.action("clearGuides").isEnabled());
    bar.session().createDocument(20, 20);
    for (const auto &[name, text, read, start] : toggles) {
        QAction &entry = bar.action(name);
        QVERIFY2(entry.isEnabled(), name);
        entry.trigger();
        QVERIFY2((bar.session().*read)() != start && entry.isChecked() != start, name);
        entry.trigger();
        QVERIFY2((bar.session().*read)() == start && entry.isChecked() == start, name);
    }
    // The submenus Swift names, and Clear Guides with guides.
    QCOMPARE(qobject_cast<QMenu *>(bar.action("showGrid").parent())->title(), QString("Show"));
    QCOMPARE(qobject_cast<QMenu *>(bar.action("snapToGrid").parent())->title(), QString("Snap To"));
    bar.session().addGuide({QUuid::createUuid(), CanvasGuide::Axis::vertical, 5});
    QVERIFY(bar.action("clearGuides").isEnabled());
    bar.action("clearGuides").trigger();
    QVERIFY(bar.session().document().value().guides.empty());
    QVERIFY(!bar.action("clearGuides").isEnabled());
}

void ViewMenuTests::gridSettingsOpensItsSheet()
{
    Bar bar;
    QAction &entry = bar.action("gridSettings");
    QVERIFY(entry.text() == "Grid Settings…" && !entry.isCheckable() && !entry.isEnabled());
    // Swift's place: after the Show submenu, before Rulers.
    const QList<QAction *> view = qobject_cast<QMenu *>(entry.parent())->actions();
    const qsizetype at = view.indexOf(&entry);
    QVERIFY(view[at - 1]->menu() && view[at - 1]->menu()->title() == "Show" && view[at + 1] == &bar.action("showRulers"));
    bar.session().createDocument(20, 20);
    QVERIFY(entry.isEnabled());
    bar.window.show();
    entry.trigger();
    QTRY_VERIFY(bar.window.findChild<GridSettingsSheet *>());
    QVERIFY(bar.session().showsGrid());
    QTest::keyClick(bar.window.findChild<GridSettingsSheet *>(), Qt::Key_Escape);
    QTRY_VERIFY(!bar.window.findChild<GridSettingsSheet *>());
    QVERIFY(!bar.session().showsGrid());
}

QTEST_MAIN(ViewMenuTests)
#include "ViewMenuTests.moc"
