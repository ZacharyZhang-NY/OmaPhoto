#include "MenuFixtures.h"

// The View menu: zoom, the pixel grid and Snap.
class ViewMenuTests : public QObject {
    Q_OBJECT
private slots:
    void viewEntriesZoomTheFrontSession();
    void thePixelGridEntryTogglesTheSession();
    void guideEntriesFollowTheSession();
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
    QCOMPARE(bar.session().viewport.zoom(), 0.8);
    bar.action("fit").trigger();
    QVERIFY(bar.session().viewport.followsFit());
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

QTEST_MAIN(ViewMenuTests)
#include "ViewMenuTests.moc"
