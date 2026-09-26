#include "MenuFixtures.h"

// The View menu: zoom, the pixel grid and Snap.
class ViewMenuTests : public QObject {
    Q_OBJECT
private slots:
    void viewEntriesZoomTheFrontSession();
    void thePixelGridEntryTogglesTheSession();
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

QTEST_MAIN(ViewMenuTests)
#include "ViewMenuTests.moc"
