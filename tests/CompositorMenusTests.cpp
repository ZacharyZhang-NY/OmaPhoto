#include "DialogDesk.h"
#include "IO/ProjectStore.h"
#include "MenuFixtures.h"
#include <QDialog>
#include <QLineEdit>
#include <QMenu>
#include <QTemporaryDir>

// The menu bar: each entry's shortcut, enabling and effect.

class CompositorMenusTests : public QObject {
    Q_OBJECT
private slots:
    void everyEntryHasSwiftsShortcutWithCtrlForCommand();
    void fileEntriesFollowTheControllersGate();
    void undoAndRedoCarryTheirNamesAndKeepAFieldsOwn();
    void theMenuBarBorrowsFocusAndTheFieldKeepsUndo();
    void layerEntriesFollowTheActiveLayer();
    void duplicateMergeAndFlipEntriesActOnTheLayers();
    void transformEntriesFollowTheMoveTool();
    void deleteMeansTheMaskTheLayerOrTheLayers();
    void theMenusFollowTheFrontTab();
    void imageEntriesSizeAndFlipTheCanvas();
};

void CompositorMenusTests::everyEntryHasSwiftsShortcutWithCtrlForCommand()
{
    Bar bar;
    const std::pair<const char *, const char *> keys[] = {
        {"newCanvas", "Ctrl+N"}, {"openProject", "Ctrl+O"}, {"importImages", ""}, {"save", "Ctrl+S"}, {"saveAs", "Ctrl+Shift+S"},
        {"exportPNG", "Ctrl+Shift+E"}, {"exportJPEG", "Ctrl+Alt+Shift+S"},
        {"closeProject", "Ctrl+W"}, {"undo", "Ctrl+Z"}, {"redo", "Ctrl+Shift+Z"}, {"fit", "Ctrl+0"}, {"actualPixels", "Ctrl+1"},
        {"zoomIn", "Ctrl+="}, {"zoomOut", "Ctrl+-"}, {"pixelGrid", ""}, {"snap", ""}, {"showGrid", "Ctrl+'"}, {"showGuides", "Ctrl+;"}, {"showRulers", "Ctrl+R"}, {"snapEnabled", "Ctrl+Shift+;"}, {"snapToGuides", ""}, {"snapToGrid", ""}, {"snapToLayers", ""}, {"snapToDocumentBounds", ""}, {"lockGuides", "Ctrl+Alt+;"}, {"clearGuides", ""}, {"transformControls", "Ctrl+H"}, {"transformLayer", "Ctrl+T"},
        {"layerViaCopy", "Ctrl+J"}, {"cut", "Ctrl+X"}, {"copy", "Ctrl+C"}, {"copyMerged", "Ctrl+Shift+C"}, {"paste", "Ctrl+V"}, {"keyboardShortcuts", ""}, {"fillForeground", "Alt+Backspace"}, {"fillBackground", "Ctrl+Backspace"}, {"clearSelectionPixels", ""}, {"contentAwareFill", "Shift+Backspace"}, {"clippingMask", "Ctrl+Alt+G"}, {"groupLayers", "Ctrl+G"},
        {"moveOutOfFolder", ""}, {"newBlankLayer", "Ctrl+Shift+N"}, {"renameLayer", ""}, {"layerVisibility", ""}, {"moveLayerUp", "Ctrl+]"}, {"moveLayerDown", "Ctrl+["},
        {"mergeLayers", "Ctrl+E"}, {"flipHorizontal", ""}, {"flipVertical", ""}, {"deleteLayer", ""},
        {"selectAll", "Ctrl+A"}, {"deselect", "Ctrl+D"}, {"inverse", "Ctrl+Shift+I"}, {"layerPixels", ""}, {"subject", "Ctrl+Alt+A"}, {"maskBlackAreas", ""},
        {"expandSelection", ""}, {"contractSelection", ""}, {"featherSelection", ""}, {"curves", "Ctrl+M"}, {"levels", "Ctrl+L"}, {"hueSaturation", "Ctrl+U"}, {"blackWhite", ""}, {"colorBalance", ""}, {"exposure", ""}, {"gradientMap", ""}, {"grain", ""}, {"invert", "Ctrl+I"},
        {"canvasSize", "Ctrl+Alt+C"}, {"imageSize", "Ctrl+Alt+I"}, {"trim", ""}, {"flipCanvasHorizontal", ""}, {"flipCanvasVertical", ""},
        {"gaussianBlur", ""}, {"motionBlur", ""}, {"addNoise", ""}, {"lensCorrection", ""}, {"removeBackground", ""}, {"newAdjustmentLayer", ""}, {"editAdjustment", ""},
        {"newHueSaturationAdjustment", ""}, {"newLevelsAdjustment", ""}, {"newCurvesAdjustment", ""}, {"newExposureAdjustment", ""},
        {"newGradientMapAdjustment", ""}, {"newGrainAdjustment", ""}, {"newInvertAdjustment", ""}, {"newBlackWhiteAdjustment", ""},
        {"newColorBalanceAdjustment", ""}, {"newAddNoiseAdjustment", ""}, {"newGaussianBlurAdjustment", ""}, {"newMotionBlurAdjustment", ""},
    };
    for (const auto &[name, key] : keys)
        QCOMPARE(bar.action(name).shortcut().toString(), QString::fromLatin1(key));
    // Seven menus in Swift's order, each with its entries.
    const QList<QMenu *> menus = bar.window.menuBar()->findChildren<QMenu *>(QString(), Qt::FindDirectChildrenOnly);
    QStringList titles;
    for (const QMenu *menu : menus)
        titles << menu->title();
    QCOMPARE(titles, (QStringList{"&File", "&Edit", "&View", "&Select", "&Image", "Fil&ter", "&Layer"}));
    QCOMPARE(menus[0]->actions().size(), 11);
    QCOMPARE(menus[1]->actions().size(), 13);
    QCOMPARE(menus[2]->actions().size(), 16);
    QCOMPARE(menus[3]->actions().size(), 10);
    QCOMPARE(menus[4]->actions().size(), 16);
    // Swift's Trim… follows Image Size….
    const QList<QAction *> image = menus[4]->actions();
    QCOMPARE(image.indexOf(&bar.action("trim")), image.indexOf(&bar.action("imageSize")) + 1);
    QCOMPARE(bar.action("trim").text(), QString("Trim…"));
    // Swift's Image menu: Black & White and Color Balance first.
    QStringList adjustments;
    for (int index = 3; index < 8; ++index)
        adjustments << menus[4]->actions()[index]->objectName();
    QCOMPARE(adjustments, (QStringList{"blackWhite", "colorBalance", "exposure", "gradientMap", "grain"}));
    // Swift's Filter menu: Camera Raw after Lens Correction.
    QStringList filters;
    for (QAction *entry : menus[5]->actions())
        filters << entry->text();
    QCOMPARE(filters, (QStringList{"Gaussian Blur…", "Motion Blur…", "Add Noise…", "Lens Correction…", "Camera Raw Filter…", "Remove Background…"}));
    QCOMPARE(menus[6]->actions().size(), 22);
}

void CompositorMenusTests::fileEntriesFollowTheControllersGate()
{
    QTemporaryDir folder;
    Bar bar;
    QVERIFY(bar.action("newCanvas").isEnabled() && bar.action("openProject").isEnabled() && bar.action("closeProject").isEnabled());
    QVERIFY(bar.action("importImages").isEnabled());
    // Nothing to save without a canvas.
    QVERIFY(!bar.action("save").isEnabled() && !bar.action("saveAs").isEnabled());
    bar.session().createDocument(8, 8);
    QVERIFY(bar.action("save").isEnabled() && bar.action("saveAs").isEnabled());
    // Busy holds the file entries; a sheet holds Import.
    bar.session().setIsProjectBusy(true);
    QVERIFY(!bar.action("newCanvas").isEnabled() && !bar.action("openProject").isEnabled() && !bar.action("save").isEnabled()
            && !bar.action("saveAs").isEnabled() && !bar.action("closeProject").isEnabled());
    QVERIFY(bar.action("importImages").isEnabled());
    QTRY_VERIFY(!bar.action("importImages").isEnabled());
    bar.session().setIsProjectBusy(false);
    bar.session().setShowsNewDocument(true);
    QVERIFY(!bar.action("importImages").isEnabled() && !bar.action("newCanvas").isEnabled());
    bar.session().setShowsNewDocument(false);
    // Importing alone holds Import as well.
    bar.session().setIsImporting(true);
    QVERIFY(!bar.action("importImages").isEnabled());
    bar.session().setIsImporting(false);
    QVERIFY(bar.action("importImages").isEnabled());
    // The entries do what the toolbar and the controller do.
    bar.action("newCanvas").trigger();
    QCOMPARE(int(bar.workspace.tabs().size()), 2);
    bar.workspace.select(bar.workspace.tabs()[0]->id);
    bar.action("importImages").trigger();
    QVERIFY(bar.session().showsImporter());
    bar.session().setShowsImporter(false);
    DialogDesk desk;
    desk.replies = {folder.filePath("Saved.comp")};
    bar.action("save").trigger();
    QTRY_VERIFY(!bar.session().isModified());
    QCOMPARE(bar.session().projectPath(), std::optional(folder.filePath("Saved.comp")));
    // Saved once, Save asks no more; Save As always does.
    bar.session().addBlankLayer();
    bar.action("save").trigger();
    QTRY_VERIFY(!bar.session().isModified());
    QCOMPARE(desk.seen.size(), 1);
    desk.replies = {folder.filePath("Copy.comp")};
    bar.action("saveAs").trigger();
    QTRY_COMPARE(bar.session().projectPath(), std::optional(folder.filePath("Copy.comp")));
    QCOMPARE(desk.seen.size(), 2);
    desk.replies = {"<cancel>"};
    bar.action("openProject").trigger();
    QTRY_COMPARE(desk.seen.size(), 3);
    // Close Project closes this tab, not the window.
    bar.window.show();
    bar.action("closeProject").trigger();
    QTRY_COMPARE(int(bar.workspace.tabs().size()), 1);
    QVERIFY(bar.window.isVisible());
}

void CompositorMenusTests::undoAndRedoCarryTheirNamesAndKeepAFieldsOwn()
{
    Bar bar;
    bar.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&bar.window));
    QAction &undo = bar.action("undo"), &redo = bar.action("redo");
    // The welcome's field holds focus: the entries are its own.
    QLineEdit &first = *bar.window.findChild<QLineEdit *>("widthInput");
    QTRY_VERIFY(first.hasFocus());
    QCOMPARE(undo.text(), QString("Undo"));
    QVERIFY(undo.isEnabled() && redo.isEnabled());
    first.clearFocus();
    QTRY_VERIFY(!undo.isEnabled() && !redo.isEnabled());
    bar.session().createDocument(8, 8);
    bar.session().addBlankLayer();
    QCOMPARE(undo.text(), QString("Undo New Blank Layer"));
    QVERIFY(undo.isEnabled() && !redo.isEnabled());
    undo.trigger();
    QCOMPARE(redo.text(), QString("Redo New Blank Layer"));
    QCOMPARE(undo.text(), QString("Undo New Canvas"));
    redo.trigger();
    QCOMPARE(int(bar.session().document().value().layers.size()), 1);
    // Busy, or a rename: the names go, the entries close.
    bar.session().setIsProjectBusy(true);
    QCOMPARE(undo.text(), QString("Undo"));
    QVERIFY(!undo.isEnabled());
    bar.session().setIsProjectBusy(false);
    // A rename types in the row: the entries are its.
    bar.session().setRenamingLayerID(bar.session().activeLayerID());
    QLineEdit &editor = *bar.window.findChild<QLineEdit *>("layerNameEditor");
    QTRY_VERIFY(editor.hasFocus());
    QVERIFY(undo.isEnabled() && undo.text() == "Undo");
    QTest::keyClick(&editor, Qt::Key_Escape);
    QVERIFY(!bar.session().renamingLayerID().has_value());
    QTRY_COMPARE(undo.text(), QString("Undo New Blank Layer"));
    // In a field the entries are the field's, by focus.
    bar.session().addBlankLayer();
    bar.session().undo();
    bar.session().selectTool(NavigationTool::zoom);
    QLineEdit &zoom = *bar.window.findChild<QLineEdit *>("zoomPercentage");
    QCOMPARE(undo.text(), QString("Undo New Blank Layer"));
    zoom.setFocus();
    QTRY_VERIFY(zoom.hasFocus());
    QVERIFY(undo.isEnabled() && redo.isEnabled() && bar.session().canUndo() && bar.session().canRedo());
    QCOMPARE(undo.text(), QString("Undo"));
    QCOMPARE(redo.text(), QString("Redo"));
    zoom.clearFocus();
    QTRY_COMPARE(undo.text(), QString("Undo New Blank Layer"));
    QCOMPARE(redo.text(), QString("Redo New Blank Layer"));
    bar.workspace.current().session.clearProject();
    // A fresh welcome takes focus: the entries are its field's.
    QLineEdit &width = *bar.window.findChild<QLineEdit *>("widthInput");
    QTRY_VERIFY(width.hasFocus());
    QVERIFY(undo.isEnabled() && !bar.session().canUndo());
    width.selectAll();
    QTest::keyClicks(&width, "640");
    undo.trigger();
    QCOMPARE(width.text(), QString("1920"));
    redo.trigger();
    QCOMPARE(width.text(), QString("640"));
    QVERIFY(!bar.session().document().has_value());
}

void CompositorMenusTests::theMenuBarBorrowsFocusAndTheFieldKeepsUndo()
{
    Bar bar;
    bar.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&bar.window));
    QAction &undo = bar.action("undo");
    QWidget *const menuBar = bar.window.menuBar();
    QLineEdit &width = *bar.window.findChild<QLineEdit *>("widthInput");
    QTRY_VERIFY(width.hasFocus());
    QTest::keyClicks(&width, "9");
    QCOMPARE(width.text(), QString("19209"));
    // Alt+E opens Edit from the keyboard: the bar takes focus.
    QTest::keyClick(&bar.window, Qt::Key_E, Qt::AltModifier);
    QTRY_COMPARE(QApplication::focusWidget(), menuBar);
    QVERIFY(QApplication::activePopupWidget());
    QVERIFY(undo.isEnabled());
    QCOMPARE(undo.text(), QString("Undo"));
    undo.trigger();
    QCOMPARE(width.text(), QString("1920"));
    QAction &redo = bar.action("redo");
    QVERIFY(redo.isEnabled());
    redo.trigger();
    QCOMPARE(width.text(), QString("19209"));
    // Escape closes the menu, then the bar gives focus back.
    QTest::keyClick(QApplication::activePopupWidget(), Qt::Key_Escape);
    QTest::keyClick(menuBar, Qt::Key_Escape);
    QTRY_VERIFY(width.hasFocus());
    QVERIFY(undo.isEnabled() && undo.text() == "Undo");
    // Over a canvas too, the bar keeps the field's Undo.
    bar.session().createDocument(8, 8);
    bar.session().addBlankLayer();
    bar.session().selectTool(NavigationTool::zoom);
    QLineEdit &zoom = *bar.window.findChild<QLineEdit *>("zoomPercentage");
    zoom.setFocus();
    QTRY_VERIFY(zoom.hasFocus());
    QTest::keyClick(&bar.window, Qt::Key_E, Qt::AltModifier);
    QTRY_COMPARE(QApplication::focusWidget(), menuBar);
    QVERIFY(undo.isEnabled());
    QCOMPARE(undo.text(), QString("Undo"));
    QTest::keyClick(QApplication::activePopupWidget(), Qt::Key_Escape);
    QTest::keyClick(menuBar, Qt::Key_Escape);
    QTRY_VERIFY(zoom.hasFocus());
    QCOMPARE(undo.text(), QString("Undo"));
    zoom.clearFocus();
    QTRY_COMPARE(undo.text(), QString("Undo New Blank Layer"));
}

void CompositorMenusTests::layerEntriesFollowTheActiveLayer()
{
    Bar bar;
    for (const char *name : {"clippingMask", "groupLayers", "moveOutOfFolder", "newBlankLayer", "renameLayer", "layerVisibility", "moveLayerUp", "moveLayerDown", "deleteLayer"})
        QVERIFY2(!bar.action(name).isEnabled(), name);
    bar.session().createDocument(8, 8);
    QVERIFY(bar.action("groupLayers").isEnabled() && bar.action("newBlankLayer").isEnabled());
    // No active layer: nothing to rename, hide, move or delete.
    QVERIFY(!bar.action("renameLayer").isEnabled() && !bar.action("layerVisibility").isEnabled() && !bar.action("deleteLayer").isEnabled());
    bar.action("newBlankLayer").trigger();
    bar.action("newBlankLayer").trigger();
    const QUuid lower = bar.session().document().value().layers[0].id, upper = bar.session().activeLayerID().value();
    QVERIFY(bar.action("renameLayer").isEnabled() && bar.action("layerVisibility").isEnabled() && bar.action("deleteLayer").isEnabled());
    QVERIFY(!bar.action("moveLayerUp").isEnabled() && bar.action("moveLayerDown").isEnabled());
    QCOMPARE(bar.action("layerVisibility").text(), QString("Hide Layer"));
    bar.action("layerVisibility").trigger();
    QCOMPARE(bar.action("layerVisibility").text(), QString("Show Layer"));
    QVERIFY(!bar.session().activeLayer().value().isVisible);
    bar.action("layerVisibility").trigger();
    bar.action("renameLayer").trigger();
    QCOMPARE(bar.session().renamingLayerID(), std::optional(upper));
    bar.session().setRenamingLayerID(std::nullopt);
    // Clipping: the entry names what it will do.
    QCOMPARE(bar.action("clippingMask").text(), QString("Create Clipping Mask"));
    QVERIFY(bar.action("clippingMask").isEnabled());
    bar.action("clippingMask").trigger();
    QCOMPARE(bar.session().activeLayer().value().maskSourceID, std::optional(lower));
    QCOMPARE(bar.action("clippingMask").text(), QString("Release Clipping Mask"));
    bar.action("clippingMask").trigger();
    QCOMPARE(bar.session().activeLayer().value().maskSourceID, std::nullopt);
    bar.session().selectLayer(lower);
    QVERIFY(!bar.action("clippingMask").isEnabled());
    bar.action("moveLayerUp").trigger();
    QCOMPARE(bar.session().document().value().layers[1].id, lower);
    bar.action("moveLayerDown").trigger();
    QCOMPARE(bar.session().document().value().layers[0].id, lower);
    // Grouping, then out of the folder.
    bar.session().selectLayers({lower, upper}, upper);
    bar.action("groupLayers").trigger();
    QCOMPARE(int(bar.session().document().value().layers.size()), 3);
    bar.session().selectLayer(upper);
    QVERIFY(bar.action("moveOutOfFolder").isEnabled());
    bar.action("moveOutOfFolder").trigger();
    QCOMPARE(bar.session().activeLayer().value().parentID, std::nullopt);
    QVERIFY(!bar.action("moveOutOfFolder").isEnabled());
    // A busy project closes every layer entry.
    bar.session().setIsProjectBusy(true);
    for (const char *name : {"clippingMask", "groupLayers", "moveOutOfFolder", "newBlankLayer", "renameLayer", "layerVisibility", "moveLayerUp", "moveLayerDown", "deleteLayer"})
        QVERIFY2(!bar.action(name).isEnabled(), name);
}

void CompositorMenusTests::duplicateMergeAndFlipEntriesActOnTheLayers()
{
    Bar bar;
    for (const char *name : {"layerViaCopy", "mergeLayers", "flipHorizontal", "flipVertical"})
        QVERIFY2(!bar.action(name).isEnabled(), name);
    QCOMPARE(bar.action("mergeLayers").text(), QString("Merge Down"));
    bar.session().createDocument(8, 8);
    bar.action("newBlankLayer").trigger();
    // A blank layer copies; no pixels flip or merge.
    QVERIFY(bar.action("layerViaCopy").isEnabled());
    QVERIFY(!bar.action("mergeLayers").isEnabled() && !bar.action("flipHorizontal").isEnabled() && !bar.action("flipVertical").isEnabled());
    bar.session().insert(white());
    const QUuid lower = bar.session().activeLayerID().value();
    QVERIFY(bar.action("flipHorizontal").isEnabled() && bar.action("flipVertical").isEnabled());
    bar.action("flipHorizontal").trigger();
    QVERIFY(bar.session().activeLayer().value().transform.flipX);
    bar.action("flipVertical").trigger();
    QVERIFY(bar.session().activeLayer().value().transform.flipY);
    QCOMPARE(bar.session().history.undoName(), QString("Flip Vertical"));
    bar.action("layerViaCopy").trigger();
    QCOMPARE(int(bar.session().document().value().layers.size()), 3);
    QCOMPARE(bar.session().activeLayer().value().name, QString("White copy"));
    // The merge entry names its plan and bakes it.
    QCOMPARE(bar.action("mergeLayers").text(), QString("Merge Down"));
    QVERIFY(bar.action("mergeLayers").isEnabled());
    bar.session().selectLayers({lower, bar.session().activeLayerID().value()}, lower);
    QCOMPARE(bar.action("mergeLayers").text(), QString("Merge Layers"));
    bar.action("mergeLayers").trigger();
    QCOMPARE(int(bar.session().document().value().layers.size()), 2);
    QCOMPARE(bar.session().history.undoName(), QString("Merge Layers"));
    const QUuid merged = bar.session().activeLayerID().value();
    bar.session().addGroup();
    QVERIFY(!bar.action("layerViaCopy").isEnabled());
    bar.session().selectLayer(merged);
    QVERIFY(bar.action("layerViaCopy").isEnabled());
    bar.session().setIsProjectBusy(true);
    for (const char *name : {"layerViaCopy", "mergeLayers", "flipHorizontal", "flipVertical"})
        QVERIFY2(!bar.action(name).isEnabled(), name);
}

void CompositorMenusTests::transformEntriesFollowTheMoveTool()
{
    Bar bar;
    QAction &transform = bar.action("transformLayer");
    QAction &controls = bar.action("transformControls");
    QVERIFY(!transform.isEnabled() && !controls.isEnabled() && controls.isCheckable() && controls.isChecked());
    bar.session().createDocument(8, 8);
    QVERIFY(!transform.isEnabled() && controls.isEnabled());
    bar.session().insert(white());
    QVERIFY(transform.isEnabled());
    // Ctrl+T begins the edit that waits for Apply.
    transform.trigger();
    QVERIFY(bar.session().transformEdit().value().persistent);
    QVERIFY(!transform.isEnabled());
    bar.session().cancelTransform();
    // Ctrl+H toggles the box, in the Move tool alone.
    controls.trigger();
    QVERIFY(!bar.session().showsTransformControls() && !controls.isChecked());
    bar.session().setShowsTransformControls(true);
    QVERIFY(controls.isChecked());
    bar.session().selectTool(NavigationTool::hand);
    QVERIFY(!controls.isEnabled());
    bar.session().selectTool(NavigationTool::move);
    bar.session().setIsProjectBusy(true);
    QVERIFY(!transform.isEnabled());
}

void CompositorMenusTests::deleteMeansTheMaskTheLayerOrTheLayers()
{
    Bar bar;
    bar.session().createDocument(8, 8);
    bar.session().insert(white());
    const QUuid first = bar.session().activeLayerID().value();
    bar.session().insert(white());
    const QUuid second = bar.session().activeLayerID().value();
    QCOMPARE(bar.action("deleteLayer").text(), QString("Delete Layer"));
    bar.session().addLayerMask();
    bar.session().selectLayerTarget(second, true);
    QCOMPARE(bar.action("deleteLayer").text(), QString("Delete Layer Mask"));
    bar.action("deleteLayer").trigger();
    QVERIFY(!bar.session().activeLayer().value().mask.has_value());
    QCOMPARE(int(bar.session().document().value().layers.size()), 2);
    // Swift's title says mask; its action deletes the layers.
    bar.session().addLayerMask();
    bar.session().selectLayers({first, second}, second);
    bar.session().selectLayerTarget(second, true);
    bar.session().selectLayers({first, second}, second);
    QVERIFY(bar.session().isMaskSelected() && bar.session().selectedLayerIDs().size() == 2);
    QCOMPARE(bar.action("deleteLayer").text(), QString("Delete Layer Mask"));
    bar.action("deleteLayer").trigger();
    QVERIFY(bar.session().document().value().layers.empty());
    QCOMPARE(bar.session().history.undoName(), QString("Delete Layers"));
    // Several selected, no mask chosen: the plural title.
    bar.session().insert(white());
    const QUuid third = bar.session().activeLayerID().value();
    bar.session().insert(white());
    bar.session().selectLayers({third, bar.session().activeLayerID().value()}, third);
    QCOMPARE(bar.action("deleteLayer").text(), QString("Delete Layers"));
    bar.action("deleteLayer").trigger();
    QVERIFY(bar.session().document().value().layers.empty());
    // One layer, its mask not chosen: the layer goes.
    bar.session().insert(white());
    bar.session().addLayerMask();
    bar.session().selectLayerTarget(bar.session().activeLayerID().value(), false);
    QCOMPARE(bar.action("deleteLayer").text(), QString("Delete Layer"));
    bar.action("deleteLayer").trigger();
    QVERIFY(bar.session().document().value().layers.empty());
}

void CompositorMenusTests::theMenusFollowTheFrontTab()
{
    Bar bar;
    bar.session().createDocument(8, 8);
    bar.session().addBlankLayer();
    QCOMPARE(bar.action("undo").text(), QString("Undo New Blank Layer"));
    bar.workspace.newCanvas();
    QCOMPARE(bar.action("undo").text(), QString("Undo"));
    QVERIFY(!bar.action("fit").isEnabled() && !bar.action("save").isEnabled());
    // The old tab's news no longer reaches the menus.
    bar.workspace.tabs()[0]->session.addBlankLayer();
    QCOMPARE(bar.action("undo").text(), QString("Undo"));
    bar.workspace.select(bar.workspace.tabs()[0]->id);
    QCOMPARE(bar.action("undo").text(), QString("Undo New Blank Layer"));
    QVERIFY(bar.action("fit").isEnabled() && bar.action("save").isEnabled());
    // Back and forth: only the front tab's news counts.
    bar.workspace.select(bar.workspace.tabs()[1]->id);
    bar.workspace.tabs()[0]->session.addBlankLayer();
    QCOMPARE(bar.action("undo").text(), QString("Undo"));
    bar.workspace.tabs()[1]->session.createDocument(4, 4);
    QCOMPARE(bar.action("undo").text(), QString("Undo New Canvas"));
    bar.workspace.select(bar.workspace.tabs()[0]->id);
    bar.workspace.tabs()[1]->session.addBlankLayer();
    QCOMPARE(bar.action("undo").text(), QString("Undo New Blank Layer"));
    bar.workspace.tabs()[0]->session.undo();
    QCOMPARE(bar.action("undo").text(), QString("Undo New Blank Layer"));
    QCOMPARE(bar.action("redo").text(), QString("Redo New Blank Layer"));
    // A tab left behind drives the menus no more.
    bar.action("undo").setText(QString("stale"));
    bar.workspace.tabs()[1]->session.addBlankLayer();
    QCOMPARE(bar.action("undo").text(), QString("stale"));
    bar.workspace.tabs()[0]->session.notify();
    QCOMPARE(bar.action("undo").text(), QString("Undo New Blank Layer"));
}

void CompositorMenusTests::imageEntriesSizeAndFlipTheCanvas()
{
    Bar bar;
    bar.window.show();
    const char *entries[] = {"canvasSize", "imageSize", "trim", "flipCanvasHorizontal", "flipCanvasVertical"};
    for (const char *name : entries)
        QVERIFY2(!bar.action(name).isEnabled(), name);
    bar.session().createDocument(100, 50);
    bar.session().insert(white(), QPointF(20.5, 10.5));
    for (const char *name : entries)
        QVERIFY2(bar.action(name).isEnabled(), name);
    // Busy holds them all; a drawn frame holds the flips.
    bar.session().setIsProjectBusy(true);
    for (const char *name : entries)
        QVERIFY2(!bar.action(name).isEnabled(), name);
    bar.session().setIsProjectBusy(false);
    bar.session().selectTool(NavigationTool::crop);
    QVERIFY(bar.action("canvasSize").isEnabled() && bar.action("trim").isEnabled() && !bar.action("flipCanvasHorizontal").isEnabled());
    bar.session().selectTool(NavigationTool::move);
    bar.action("flipCanvasHorizontal").trigger();
    QCOMPARE(bar.session().activeLayer().value().origin(), QPointF(79, 10));
    QCOMPARE(bar.session().history.undoName(), QString("Flip Canvas Horizontal"));
    bar.action("flipCanvasVertical").trigger();
    QCOMPARE(bar.session().activeLayer().value().origin(), QPointF(79, 39));
    // The size entries open their sheets over the window.
    for (const auto &[name, title] : {std::pair("canvasSize", "Canvas Size"), std::pair("imageSize", "Image Size"), std::pair("trim", "Trim")}) {
        bar.action(name).trigger();
        auto *sheet = bar.window.findChild<QDialog *>();
        QVERIFY(sheet && sheet->isVisible());
        QCOMPARE(sheet->windowTitle(), QString::fromLatin1(title));
        QVERIFY(!bar.action(name).isEnabled());
        QTest::keyClick(sheet, Qt::Key_Escape);
        QTRY_VERIFY(!bar.session().isProjectBusy());
        QTRY_VERIFY(!bar.window.findChild<QDialog *>());
    }
}

QTEST_MAIN(CompositorMenusTests)
#include "CompositorMenusTests.moc"
