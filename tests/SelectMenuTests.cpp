#include "MenuFixtures.h"
#include <QApplication>
#include <QLineEdit>
#include <QRegularExpression>

// The Select and Image menus, the Edit menu's clipboard.
class SelectMenuTests : public QObject {
    Q_OBJECT
private slots:
    void selectEntriesFollowTheSelectionAndActOnIt();
    void selectAllInAFieldIsTheFields();
    void invertFollowsTheActiveLayerAndItsMask();
    void levelsOpensOnPixelsAndRestsNewAndImport();
    void hueSaturationOpensOnPixelsAndRestsLevels();
    void filtersOpenFromTheImageAndFilterMenus();
    void copyPasteAndLayerViaCopyFollowTheSelection();
    void copyAndPasteInAFieldAreTheFields();
    void contentAwareFillFollowsTheSelection();
    void cutTheFillsAndClearFollowTheSelectionAndTheField();
    void transformTheSelectionOrTheLayer();
};

void SelectMenuTests::selectEntriesFollowTheSelectionAndActOnIt()
{
    Bar bar;
    for (const char *name : {"selectAll", "deselect", "inverse", "layerPixels", "maskBlackAreas", "expandSelection", "contractSelection"})
        QVERIFY2(!bar.action(name).isEnabled(), name);
    bar.session().createDocument(50, 50, true);
    QVERIFY(bar.action("selectAll").isEnabled());
    // A blank layer has no pixels and no mask.
    QVERIFY(!bar.action("layerPixels").isEnabled() && !bar.action("maskBlackAreas").isEnabled());
    QVERIFY(!bar.action("deselect").isEnabled() && !bar.action("inverse").isEnabled());
    bar.action("selectAll").trigger();
    QCOMPARE(bar.session().history.undoName(), QString("Select All"));
    QVERIFY(bar.action("deselect").isEnabled() && bar.action("inverse").isEnabled());
    QVERIFY(bar.action("expandSelection").isEnabled() && bar.action("contractSelection").isEnabled());
    QCOMPARE(bar.action("expandSelection").text(), QString("Expand by 1 px"));
    QCOMPARE(bar.action("contractSelection").text(), QString("Contract by 1 px"));
    bar.session().setSelectionExpandAmount(3);
    bar.session().setSelectionContractAmount(7);
    QCOMPARE(bar.action("expandSelection").text(), QString("Expand by 3 px"));
    QCOMPARE(bar.action("contractSelection").text(), QString("Contract by 7 px"));
    bar.action("contractSelection").trigger();
    QCOMPARE(bar.session().history.undoName(), QString("Contract Selection"));
    QCOMPARE(bar.session().selection().value().path.boundingRect(), QRectF(7, 7, 36, 36));
    bar.action("expandSelection").trigger();
    QCOMPARE(bar.session().history.undoName(), QString("Expand Selection"));
    QCOMPARE(bar.session().selection().value().path.boundingRect(), QRectF(4, 4, 42, 42));
    bar.action("inverse").trigger();
    QCOMPARE(bar.session().history.undoName(), QString("Inverse"));
    bar.action("deselect").trigger();
    QVERIFY(!bar.session().selection().has_value());
    // Pixels and a mask: their entries load them.
    bar.session().insert(white());
    QVERIFY(bar.action("layerPixels").isEnabled() && !bar.action("maskBlackAreas").isEnabled());
    bar.action("layerPixels").trigger();
    QCOMPARE(bar.session().history.undoName(), QString("Load Layer Selection"));
    bar.session().addLayerMask(false);
    QVERIFY(bar.action("maskBlackAreas").isEnabled());
    // The same outline again records nothing; deselected, it loads.
    bar.action("maskBlackAreas").trigger();
    QCOMPARE(bar.session().history.undoName(), QString("Add Hide-All Mask"));
    bar.session().deselect();
    bar.action("maskBlackAreas").trigger();
    QCOMPARE(bar.session().history.undoName(), QString("Load Mask Selection"));
    // Busy, every entry waits; a draft holds Expand and Contract.
    bar.session().setIsProjectBusy(true);
    for (const char *name : {"deselect", "inverse", "layerPixels", "maskBlackAreas", "expandSelection", "contractSelection"})
        QVERIFY2(!bar.action(name).isEnabled(), name);
    QVERIFY(bar.action("selectAll").isEnabled());
    bar.session().setIsProjectBusy(false);
    bar.session().selectTool(NavigationTool::lasso);
    bar.session().beginLasso(QPointF(1, 1), SelectionMode::replace);
    QVERIFY(!bar.action("expandSelection").isEnabled() && bar.action("deselect").isEnabled());
}

void SelectMenuTests::selectAllInAFieldIsTheFields()
{
    Bar bar;
    bar.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&bar.window));
    // Without a canvas the entry rests; the welcome keeps Ctrl+A.
    QLineEdit &width = *bar.window.findChild<QLineEdit *>("widthInput");
    QTRY_VERIFY(width.hasFocus());
    QVERIFY(!bar.action("selectAll").isEnabled());
    bar.session().createDocument(50, 50, true);
    QTRY_VERIFY(bar.action("selectAll").isEnabled());
    // A rename in progress: the editor's text, never the canvas.
    bar.session().setRenamingLayerID(bar.session().activeLayerID());
    QLineEdit *editor = nullptr;
    QTRY_VERIFY((editor = bar.window.findChild<QLineEdit *>("layerNameEditor")) && editor->hasFocus());
    editor->deselect();
    QVERIFY(editor->selectedText().isEmpty());
    bar.action("selectAll").trigger();
    QCOMPARE(editor->selectedText(), QString("Layer 1"));
    QVERIFY(!bar.session().selection().has_value());
    QTest::keyClick(editor, Qt::Key_Escape);
    QTRY_VERIFY(!bar.session().renamingLayerID().has_value());
    bar.action("selectAll").trigger();
    QVERIFY(bar.session().selection().has_value());
}

void SelectMenuTests::invertFollowsTheActiveLayerAndItsMask()
{
    Bar bar;
    QAction &invert = bar.action("invert");
    QVERIFY(!invert.isEnabled());
    QCOMPARE(invert.text(), QString("Invert"));
    bar.session().createDocument(8, 8, true);
    QVERIFY(!invert.isEnabled());
    bar.session().insert(white());
    QVERIFY(invert.isEnabled());
    bar.session().addLayerMask();
    QCOMPARE(invert.text(), QString("Invert Mask"));
    invert.trigger();
    QTRY_VERIFY(!bar.session().isProjectBusy());
    QCOMPARE(bar.session().history.undoName(), QString("Invert Mask"));
    QCOMPARE(int(bar.session().activeLayer().value().mask.value().asset.image().constScanLine(0)[0]), 0);
    bar.session().selectLayerTarget(bar.session().activeLayerID().value(), false);
    QCOMPARE(invert.text(), QString("Invert"));
    invert.trigger();
    QTRY_VERIFY(!bar.session().isProjectBusy());
    QCOMPARE(bar.session().history.undoName(), QString("Invert"));
    QCOMPARE(bar.session().activeLayer().value().asset.value().image().pixelColor(0, 0), QColor(Qt::black));
    bar.session().setIsImporting(true);
    QVERIFY(!invert.isEnabled());
}

void SelectMenuTests::levelsOpensOnPixelsAndRestsNewAndImport()
{
    Bar bar;
    QAction &levels = bar.action("levels"), &import = bar.action("importImages"), &undo = bar.action("undo");
    QAction &fresh = *bar.window.findChild<QAction *>("newCanvasToolbar");
    QCOMPARE(levels.text(), QString("Levels…"));
    QVERIFY(!levels.isEnabled());
    // A blank layer holds no pixels to adjust.
    bar.session().createDocument(8, 8, true);
    QVERIFY(!levels.isEnabled());
    bar.session().insert(white());
    QVERIFY(levels.isEnabled() && import.isEnabled() && fresh.isEnabled());
    levels.trigger();
    QVERIFY(bar.session().levels());
    // Open, it rests itself, New and Import; Undo is bare.
    QVERIFY(!levels.isEnabled() && !import.isEnabled() && !fresh.isEnabled());
    QCOMPARE(undo.text(), QString("Undo"));
    QVERIFY(!undo.isEnabled());
    bar.session().cancelLevels();
    QVERIFY(levels.isEnabled() && import.isEnabled() && fresh.isEnabled());
    QCOMPARE(undo.text(), QString("Undo Import Image"));
    bar.session().addLayerMask();
    QVERIFY(!levels.isEnabled());
}

void SelectMenuTests::hueSaturationOpensOnPixelsAndRestsLevels()
{
    Bar bar;
    QAction &hue = bar.action("hueSaturation"), &levels = bar.action("levels"), &import = bar.action("importImages");
    QCOMPARE(hue.text(), QString("Hue/Saturation…"));
    QVERIFY(!hue.isEnabled());
    bar.session().createDocument(8, 8, true);
    QVERIFY(!hue.isEnabled());
    bar.session().insert(white());
    QVERIFY(hue.isEnabled() && levels.isEnabled());
    hue.trigger();
    const QUuid opened = bar.session().hueSaturation().value().id;
    // Open, it rests Levels; its own entry stays, as Swift's.
    QVERIFY(!levels.isEnabled() && hue.isEnabled() && import.isEnabled());
    hue.trigger();
    QCOMPARE(bar.session().hueSaturation().value().id, opened);
    bar.session().cancelHueSaturation();
    QVERIFY(levels.isEnabled());
    // Open Levels rests it in turn.
    levels.trigger();
    QVERIFY(!hue.isEnabled());
    bar.session().cancelLevels();
    QVERIFY(hue.isEnabled());
}

void SelectMenuTests::filtersOpenFromTheImageAndFilterMenus()
{
    Bar bar;
    const std::pair<const char *, FilterKind> entries[] = {
        {"curves", FilterKind::curves},           {"exposure", FilterKind::exposure},     {"gradientMap", FilterKind::gradientMap},
        {"grain", FilterKind::grain},             {"gaussianBlur", FilterKind::gaussianBlur}, {"motionBlur", FilterKind::motionBlur},
        {"addNoise", FilterKind::addNoise},       {"lensCorrection", FilterKind::lensCorrection}, {"removeBackground", FilterKind::removeBackground}};
    for (const auto &[name, kind] : entries) {
        QCOMPARE(bar.action(name).text(), rawValue(kind) + QStringLiteral("…"));
        QVERIFY2(!bar.action(name).isEnabled(), name);
    }
    bar.session().createDocument(8, 8, true);
    bar.session().insert(white());
    // Each opens its filter; open, a filter rests every entry.
    for (const auto &[name, kind] : entries) {
        QVERIFY2(bar.action(name).isEnabled(), name);
        bar.action(name).trigger();
        QCOMPARE(bar.session().filterEdit().value().kind, kind);
        for (const auto &[other, unused] : entries)
            QVERIFY2(!bar.action(other).isEnabled(), other);
        bar.session().cancelFilter();
    }
    // Hue/Saturation open rests them too, as Levels.
    bar.session().beginHueSaturation();
    for (const auto &[name, kind] : entries)
        QVERIFY2(!bar.action(name).isEnabled(), name);
    bar.session().cancelHueSaturation();
    QVERIFY(bar.action("curves").isEnabled());
}

void SelectMenuTests::copyPasteAndLayerViaCopyFollowTheSelection()
{
    Bar bar;
    QAction &copy = bar.action("copy"), &merged = bar.action("copyMerged"), &paste = bar.action("paste"), &via = bar.action("layerViaCopy");
    // Copy and Paste are always live and check when chosen.
    QVERIFY(copy.isEnabled() && paste.isEnabled() && !merged.isEnabled() && !via.isEnabled());
    copy.trigger();
    paste.trigger();
    QVERIFY(!bar.session().document().has_value());
    bar.session().createDocument(8, 8, true);
    QCOMPARE(via.text(), QString("Duplicate Layer"));
    QVERIFY(via.isEnabled() && !merged.isEnabled());
    // A blank layer's selection has no pixels for a layer.
    bar.session().selectAll();
    QVERIFY(!via.isEnabled());
    bar.session().deselect();
    QVERIFY(via.isEnabled());
    bar.session().insert(white());
    QVERIFY(merged.isEnabled());
    bar.session().selectAll();
    QCOMPARE(via.text(), QString("Layer via Copy"));
    QVERIFY(via.isEnabled());
    copy.trigger();
    QVERIFY(bar.session().pixelClipboard().has_value());
    paste.trigger();
    QCOMPARE(bar.session().history.undoName(), QString("Paste"));
    QCOMPARE(int(bar.session().document().value().layers.size()), 3);
    bar.session().selectAll();
    via.trigger();
    QCOMPARE(bar.session().history.undoName(), QString("Layer via Copy"));
    merged.trigger();
    QVERIFY(bar.session().pixelClipboard().has_value());
    bar.session().setIsProjectBusy(true);
    QVERIFY(!merged.isEnabled() && !via.isEnabled() && copy.isEnabled() && paste.isEnabled());
}

void SelectMenuTests::copyAndPasteInAFieldAreTheFields()
{
    Bar bar;
    bar.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&bar.window));
    QLineEdit &width = *bar.window.findChild<QLineEdit *>("widthInput");
    QTRY_VERIFY(width.hasFocus());
    width.setText("77");
    width.selectAll();
    bar.action("copy").trigger();
    width.setText("");
    bar.action("paste").trigger();
    QCOMPARE(width.text(), QString("77"));
    QVERIFY(!bar.session().pixelClipboard().has_value());
}

void SelectMenuTests::contentAwareFillFollowsTheSelection()
{
    Bar bar;
    QAction &fill = bar.action("contentAwareFill");
    QCOMPARE(fill.text(), QString("Content-Aware Fill…"));
    QVERIFY(!fill.isEnabled());
    bar.session().createDocument(8, 8, true);
    bar.session().insert(white());
    QVERIFY(!fill.isEnabled());
    bar.session().selectAll();
    QVERIFY(fill.isEnabled());
    fill.trigger();
    QVERIFY(bar.session().filterEdit().has_value() && !fill.isEnabled());
    bar.session().cancelFilter();
    QVERIFY(fill.isEnabled());
    bar.session().setIsProjectBusy(true);
    QVERIFY(!fill.isEnabled());
}

void SelectMenuTests::cutTheFillsAndClearFollowTheSelectionAndTheField()
{
    Bar bar;
    QAction &cut = bar.action("cut"), &foreground = bar.action("fillForeground"), &background = bar.action("fillBackground"), &clear = bar.action("clearSelectionPixels");
    QCOMPARE(foreground.text(), QString("Fill with Foreground Color"));
    QCOMPARE(background.text(), QString("Fill with Background Color"));
    QCOMPARE(clear.text(), QString("Clear Selection Pixels"));
    // Cut checks when chosen; the others follow the pixels.
    QVERIFY(cut.isEnabled() && !foreground.isEnabled() && !background.isEnabled() && !clear.isEnabled());
    cut.trigger();
    QVERIFY(!bar.session().document().has_value());
    bar.session().createDocument(8, 8, true);
    bar.session().insert(white());
    QVERIFY(foreground.isEnabled() && background.isEnabled() && !clear.isEnabled());
    // A rename closes the fills, as Swift's `canEditPixels` does.
    bar.session().setRenamingLayerID(bar.session().activeLayerID());
    QVERIFY(!foreground.isEnabled() && !background.isEnabled());
    bar.session().setRenamingLayerID(std::nullopt);
    // In a text field the entries are its own deletions.
    bar.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&bar.window));
    QLineEdit &percent = *bar.window.findChild<QLineEdit *>("opacityPercent");
    percent.setFocus();
    QTRY_VERIFY(percent.hasFocus());
    percent.setText(QStringLiteral("45"));
    percent.end(false);
    foreground.trigger();
    QCOMPARE(percent.text(), QString(""));
    percent.setText(QStringLiteral("45"));
    percent.end(false);
    background.trigger();
    QCOMPARE(percent.text(), QString(""));
    // At the start nothing lies behind the caret.
    percent.setText(QStringLiteral("45"));
    percent.home(false);
    foreground.trigger();
    QCOMPARE(percent.text(), QString("45"));
    background.trigger();
    QCOMPARE(percent.text(), QString("45"));
    // Mid-text they delete backwards; a selection goes alone.
    percent.setCursorPosition(1);
    background.trigger();
    QCOMPARE(percent.text(), QString("5"));
    percent.setText(QStringLiteral("1245"));
    percent.setSelection(1, 2);
    foreground.trigger();
    QCOMPARE(percent.text(), QString("15"));
    percent.setText(QStringLiteral("1245"));
    percent.setSelection(3, -2);
    background.trigger();
    QCOMPARE(percent.text(), QString("15"));
    QCOMPARE(bar.session().history.undoName(), QString("Import Image"));
    percent.clearFocus();
    QTRY_VERIFY(!percent.hasFocus());
    // Without a selection Cut has nothing, whole layer or not.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("nothing to cut")));
    cut.trigger();
    bar.session().selectAll();
    QVERIFY(clear.isEnabled());
    cut.trigger();
    QTRY_COMPARE(bar.session().history.undoName(), QString("Clear"));
    QVERIFY(bar.session().pixelClipboard().has_value());
    bar.session().undo();
    clear.trigger();
    QTRY_COMPARE(bar.session().history.undoName(), QString("Clear"));
    background.trigger();
    QTRY_COMPARE(bar.session().history.undoName(), QString("Fill"));
}

void SelectMenuTests::transformTheSelectionOrTheLayer()
{
    Bar bar;
    QAction &transform = bar.action("transformLayer");
    QVERIFY(!transform.isEnabled());
    bar.session().createDocument(8, 8, true);
    bar.session().insert(white());
    QCOMPARE(transform.text(), QString("Transform Layer"));
    QVERIFY(transform.isEnabled());
    bar.session().selectAll();
    QCOMPARE(transform.text(), QString("Transform Selection"));
    transform.trigger();
    QTRY_VERIFY(bar.session().transformEdit().has_value() && bar.session().transformEdit().value().floating);
    // Floating, neither transform may start again.
    QVERIFY(!transform.isEnabled());
    QCOMPARE(transform.text(), QString("Transform Layer"));
    bar.session().cancelTransform();
    bar.session().deselect();
    transform.trigger();
    QVERIFY(bar.session().transformEdit().has_value() && !bar.session().transformEdit().value().floating);
}

QTEST_MAIN(SelectMenuTests)
#include "SelectMenuTests.moc"
