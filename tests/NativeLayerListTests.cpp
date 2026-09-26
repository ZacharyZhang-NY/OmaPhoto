#include "UI/NativeLayerList.h"
#include "SelectionFixtures.h"
#include <QAction>
#include <QtTest>

// The layer rows: what they show, select, rename and key.
namespace {
ImportedImage filled(int width, int height, QRgb premultiplied, const QString &name)
{
    QImage image(width, height, QImage::Format_RGBA8888_Premultiplied);
    image.fill(QColor::fromRgba(premultiplied));
    return ImportedImage(image, image, name);
}

std::unique_ptr<EditorSession> sessionWithThreeLayers()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(800, 600);
    for (int each = 0; each < 3; ++each)
        session->addBlankLayer();
    return session;
}

// A shown list in an active window.
struct Shown {
    EditorSession &session;
    NativeLayerList list;
    explicit Shown(EditorSession &session) : session(session), list(session)
    {
        list.resize(252, 400);
        list.show();
        if (!QTest::qWaitForWindowActive(&list))
            throw std::runtime_error("the list never became active");
    }
    LayerCell &row(int index) { return *list.cells().at(size_t(index)); }
    QString name(int index) { return row(index).findChild<QLabel *>("layerName")->text(); }
    QString dimensions(int index) { return row(index).findChild<QLabel *>("layerDimensions")->text(); }
    // A click on the row's name, away from every control.
    void click(int index, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QTest::mouseClick(&row(index), Qt::LeftButton, modifiers, QPoint(row(index).width() - 20, 20));
    }
    QSet<QUuid> selected() { return session.selectedLayerIDs(); }
    QUuid id(int index) { return row(index).layerID(); }
};
}

class NativeLayerListTests : public QObject {
    Q_OBJECT
private slots:
    void rowsFollowTheHierarchyTopDown();
    void nativeSelectionDoesNotReloadRowsAndReorderKeepsIdentity();
    void clicksSelectExtendAndRangeAndTargetPixelsOverAMask();
    void thumbnailsTargetTheLayerOrItsMaskAndShiftTogglesIt();
    void theEyeTogglesAndSwipesInOneStep();
    void disclosureLinkAndTheMenuReachTheSession();
    void renamingTypesInTheRowAndReturnsFocus();
    void layerListToolKeysAndTransformNudge();
    void altClickOnTheStripClipsToTheLayerBelow();
    void thumbnailsRedrawOnlyWhenTheirPictureChanges();
    void ctrlClicksOnThumbnailsLoadSelections();
    void deleteClearsASelectionOrDeletesTheLayer();
};

void NativeLayerListTests::rowsFollowTheHierarchyTopDown()
{
    EditorSession session;
    Shown shown(session);
    QCOMPARE(shown.list.cells().size(), size_t(0));
    session.createDocument(400, 200);
    session.insert(filled(100, 50, qRgba(255, 0, 0, 255), "Red"));
    session.addBlankLayer();
    QCOMPARE(shown.list.cells().size(), size_t(2));
    // The top of the document is the top row.
    QCOMPARE(shown.name(0), QString("Layer 1"));
    QCOMPARE(shown.name(1), QString("Red"));
    QCOMPARE(shown.dimensions(0), QString("400 × 200 px"));
    // A name shows as typed, never as markup.
    QCOMPARE(shown.row(0).findChild<QLabel *>("layerName")->textFormat(), Qt::PlainText);
    QCOMPARE(shown.row(0).findChild<QLabel *>("layerDimensions")->textFormat(), Qt::PlainText);
    QCOMPARE(shown.dimensions(1), QString("100 × 50 px"));
    // The canvas box, 36 wide, at ratio 2.
    QCOMPARE(shown.row(1).thumbnail().icon().availableSizes(), QList<QSize>{QSize(72, 36)});
    QVERIFY(!shown.row(1).maskThumbnail().isVisible());
    QVERIFY(!shown.row(1).findChild<QToolButton *>("layerDisclosure")->isVisible());
    // A folder: its child steps in; collapsed, the child goes.
    session.selectLayer(shown.id(1));
    session.groupSelectedLayers();
    QCOMPARE(shown.list.cells().size(), size_t(3));
    QCOMPARE(shown.dimensions(1), QString("Folder"));
    QVERIFY(shown.row(1).findChild<QToolButton *>("layerDisclosure")->isVisible());
    QCOMPARE(shown.name(2), QString("Red"));
    const int stepped = shown.row(2).findChild<QToolButton *>("layerDisclosure")->x();
    QCOMPARE(stepped - shown.row(1).findChild<QToolButton *>("layerDisclosure")->x(), 24);
    session.toggleGroupExpansion(shown.id(1));
    QCOMPARE(shown.list.cells().size(), size_t(2));
    session.toggleGroupExpansion(shown.id(1));
    // A clipped layer shows its mark and its source.
    session.selectLayer(shown.id(0));
    session.addBlankLayer();
    QVERIFY(session.linkMask(shown.id(1), shown.id(0)));
    QCOMPARE(shown.name(0), QString("↳ Layer 2"));
    QCOMPARE(shown.dimensions(0), QString("Clipped to Layer 1"));
    QCOMPARE(shown.row(0).thumbnail().x() - shown.row(1).thumbnail().x(), 24);
    // A hidden layer fades; a mask shows thumbnail and link.
    session.toggleLayerVisibility(shown.id(1));
    QCOMPARE(shown.row(1).graphicsEffect()->property("opacity").toDouble(), 0.35);
    QCOMPARE(shown.row(0).graphicsEffect()->property("opacity").toDouble(), 1.0);
    session.selectLayer(shown.id(1));
    session.addLayerMask();
    QVERIFY(shown.row(1).maskThumbnail().isVisible());
    QVERIFY(shown.row(1).findChild<QToolButton *>("maskLink")->isVisible());
    QCOMPARE(shown.row(1).maskThumbnail().icon().availableSizes(), QList<QSize>{QSize(60, 30)});
    session.toggleLayerMask();
    QVERIFY(shown.row(1).findChild<QLabel *>("maskDisabledMark")->isVisible());
}

void NativeLayerListTests::nativeSelectionDoesNotReloadRowsAndReorderKeepsIdentity()
{
    const auto session = sessionWithThreeLayers();
    Shown shown(*session);
    const std::vector<LayerCell *> cells = shown.list.cells();
    const QUuid bottom = session->document().value().layers.front().id;
    shown.click(2);
    QCOMPARE(session->activeLayerID(), std::optional(bottom));
    QCOMPARE(shown.list.cells(), cells);
    QVERIFY(session->placeLayer(bottom, std::nullopt));
    QCOMPARE(shown.id(0), bottom);
    QVERIFY(session->selectedLayerIDs().contains(bottom));
    QVERIFY(session->placeLayer(bottom, std::nullopt, std::nullopt, true));
    QCOMPARE(shown.id(2), bottom);
    QVERIFY(!session->placeLayer(QUuid::createUuid(), std::nullopt));
    QVERIFY(!session->placeLayer(bottom, std::nullopt, QUuid::createUuid()));
    session->setIsImporting(true);
    QVERIFY(!session->placeLayer(bottom, std::nullopt));
}

void NativeLayerListTests::clicksSelectExtendAndRangeAndTargetPixelsOverAMask()
{
    const auto session = sessionWithThreeLayers();
    Shown shown(*session);
    shown.click(0);
    QCOMPARE(shown.selected(), QSet<QUuid>{shown.id(0)});
    QVERIFY(shown.list.hasFocus());
    shown.click(2, Qt::ControlModifier);
    QCOMPARE(shown.selected(), (QSet<QUuid>{shown.id(0), shown.id(2)}));
    shown.click(2, Qt::ControlModifier);
    QCOMPARE(shown.selected(), QSet<QUuid>{shown.id(0)});
    shown.click(2, Qt::ShiftModifier);
    QCOMPARE(shown.selected(), (QSet<QUuid>{shown.id(0), shown.id(1), shown.id(2)}));
    QCOMPARE(session->activeLayerID(), std::optional(shown.id(2)));
    // A plain click inside a multi-selection keeps it.
    shown.click(1);
    QCOMPARE(shown.selected().size(), 3);
    shown.click(1, Qt::ControlModifier);
    QCOMPARE(shown.selected().size(), 2);
    shown.click(1);
    QCOMPARE(shown.selected(), QSet<QUuid>{shown.id(1)});
    // With the mask targeted, the name takes the layer back.
    session->addLayerMask();
    QVERIFY(session->isMaskSelected());
    shown.click(1);
    QVERIFY(!session->isMaskSelected());
    QCOMPARE(shown.selected(), QSet<QUuid>{shown.id(1)});
    // Inside a multi-selection the mask target stays put.
    QTest::mouseClick(&shown.row(1).maskThumbnail(), Qt::LeftButton);
    session->selectLayers({shown.id(1), shown.id(2)}, shown.id(1));
    QVERIFY(session->isMaskSelected());
    shown.click(1);
    QVERIFY(session->isMaskSelected());
    QCOMPARE(shown.selected(), (QSet<QUuid>{shown.id(1), shown.id(2)}));
}

void NativeLayerListTests::thumbnailsTargetTheLayerOrItsMaskAndShiftTogglesIt()
{
    const auto session = sessionWithThreeLayers();
    Shown shown(*session);
    shown.click(0);
    session->addLayerMask();
    QVERIFY(session->isMaskSelected());
    QTest::mouseClick(&shown.row(0).thumbnail(), Qt::LeftButton);
    QVERIFY(!session->isMaskSelected());
    QCOMPARE(shown.selected(), QSet<QUuid>{shown.id(0)});
    QTest::mouseClick(&shown.row(0).maskThumbnail(), Qt::LeftButton);
    QVERIFY(session->isMaskSelected());
    QVERIFY(session->activeLayer().value().mask.value().isEnabled);
    QTest::mouseClick(&shown.row(0).maskThumbnail(), Qt::LeftButton, Qt::ShiftModifier);
    QVERIFY(!session->activeLayer().value().mask.value().isEnabled && session->isMaskSelected());
    QVERIFY(shown.row(0).maskThumbnail().isTargeted() && !shown.row(0).thumbnail().isTargeted());
    // Shift on the layer thumbnail ranges the rows, targets nothing.
    QTest::mouseClick(&shown.row(2).thumbnail(), Qt::LeftButton, Qt::ShiftModifier);
    QCOMPARE(shown.selected(), (QSet<QUuid>{shown.id(0), shown.id(1), shown.id(2)}));
    QVERIFY(!layerWith(*session, shown.id(0)).mask.value().isEnabled);
    QCOMPARE(session->activeLayerID(), std::optional(shown.id(2)));
    // Alt on a thumbnail is the row's; the target stays.
    session->selectLayers({shown.id(0)}, shown.id(0));
    QTest::mouseClick(&shown.row(0).maskThumbnail(), Qt::LeftButton);
    QTest::mouseClick(&shown.row(0).thumbnail(), Qt::LeftButton, Qt::AltModifier);
    QVERIFY(session->isMaskSelected());
    // Another row's thumbnail selects that row; Ctrl selects none.
    QTest::mouseClick(&shown.row(0).thumbnail(), Qt::LeftButton);
    QTest::mouseClick(&shown.row(2).thumbnail(), Qt::LeftButton);
    QCOMPARE(shown.selected(), QSet<QUuid>{shown.id(2)});
    QVERIFY(!session->isMaskSelected());
    QVERIFY(shown.row(2).thumbnail().isTargeted() && !shown.row(0).thumbnail().isTargeted());
    // A multi-selection shows no ring.
    session->extendSelection(shown.id(0));
    QVERIFY(!shown.row(2).thumbnail().isTargeted() && !shown.row(0).thumbnail().isTargeted());
    session->selectLayers({shown.id(2)}, shown.id(2));
    QTest::mouseClick(&shown.row(0).thumbnail(), Qt::LeftButton, Qt::ControlModifier);
    QCOMPARE(shown.selected(), QSet<QUuid>{shown.id(2)});
    // Importing, thumbnails answer no click.
    session->setIsImporting(true);
    QVERIFY(!shown.row(0).thumbnail().isEnabled() && !shown.row(0).maskThumbnail().isEnabled());
    QTest::mouseClick(&shown.row(0).thumbnail(), Qt::LeftButton);
    QCOMPARE(shown.selected(), QSet<QUuid>{shown.id(2)});
}

void NativeLayerListTests::theEyeTogglesAndSwipesInOneStep()
{
    const auto session = sessionWithThreeLayers();
    Shown shown(*session);
    auto *eye = shown.row(0).findChild<EyeSwipeButton *>("layerEye");
    QCOMPARE(eye->accessibleName(), QString("Hide Layer 3"));
    QTest::mouseClick(eye, Qt::LeftButton);
    QVERIFY(!session->document().value().layers.back().isVisible);
    QCOMPARE(eye->accessibleName(), QString("Show Layer 3"));
    QCOMPARE(session->history.undoName(), QString("Hide Layer"));
    // Dragged down a row: the eyes passed change, no other.
    session->toggleLayerVisibility(shown.id(1));
    session->toggleLayerVisibility(shown.id(2));
    const int steps = session->history.undoCount();
    const QPoint end(10, 16 + LayerCell::rowHeight);
    QTest::mousePress(eye, Qt::LeftButton, Qt::NoModifier, QPoint(10, 16));
    QMouseEvent move(QEvent::MouseMove, QPointF(end), eye->mapToGlobal(end), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(eye, &move);
    QTest::mouseRelease(eye, Qt::LeftButton, Qt::NoModifier, end);
    QVERIFY(layerWith(*session, shown.id(0)).isVisible && layerWith(*session, shown.id(1)).isVisible);
    QVERIFY(!layerWith(*session, shown.id(2)).isVisible);
    QCOMPARE(session->history.undoCount(), steps + 1);
    session->undo();
    QVERIFY(!layerWith(*session, shown.id(0)).isVisible && !layerWith(*session, shown.id(1)).isVisible);
    // Busy, the eye is disabled.
    session->setIsProjectBusy(true);
    QVERIFY(!eye->isEnabled());
}

void NativeLayerListTests::disclosureLinkAndTheMenuReachTheSession()
{
    EditorSession session;
    session.createDocument(20, 10);
    session.insert(filled(4, 4, qRgba(0, 0, 255, 255), "Blue"));
    session.insert(filled(4, 4, qRgba(0, 255, 0, 255), "Green"));
    session.groupSelectedLayers();
    Shown shown(session);
    const QUuid folder = shown.id(0), green = shown.id(1), blue = shown.id(2);
    QTest::mouseClick(shown.row(0).findChild<QToolButton *>("layerDisclosure"), Qt::LeftButton);
    QVERIFY(session.collapsedGroupIDs().contains(folder));
    QCOMPARE(shown.list.cells().size(), size_t(2));
    QTest::mouseClick(shown.row(0).findChild<QToolButton *>("layerDisclosure"), Qt::LeftButton);
    QCOMPARE(shown.list.cells().size(), size_t(3));
    // The green row's menu acts on green, whatever is active.
    session.selectLayer(blue);
    const auto trigger = [&](int row, const char *name) { shown.row(row).findChild<QAction *>(QString::fromLatin1(name))->trigger(); };
    trigger(1, "addBlackMask");
    QVERIFY(layerWith(session, green).mask.has_value());
    QCOMPARE(layerWith(session, green).mask.value().asset.image().pixelColor(0, 0), QColor(0, 0, 0));
    QCOMPARE(session.activeLayerID(), std::optional(green));
    QTest::mouseClick(shown.row(1).findChild<QToolButton *>("maskLink"), Qt::LeftButton);
    QVERIFY(!layerWith(session, green).mask.value().isLinked);
    trigger(1, "toggleMask");
    QVERIFY(!layerWith(session, green).mask.value().isEnabled);
    trigger(1, "deleteMask");
    QVERIFY(!layerWith(session, green).mask.has_value());
    trigger(1, "addWhiteMask");
    QCOMPARE(layerWith(session, green).mask.value().asset.image().pixelColor(0, 0), QColor(255, 255, 255));
    trigger(1, "toggleVisibility");
    QVERIFY(!layerWith(session, green).isVisible);
    QCOMPARE(layerWith(session, green).parentID, std::optional(folder));
    trigger(1, "moveOut");
    QVERIFY(!layerWith(session, green).parentID.has_value());
    const auto rowOf = [&](QUuid id) {
        for (int row = 0; row < 3; ++row) {
            if (shown.id(row) == id)
                return row;
        }
        throw std::runtime_error("no row");
    };
    trigger(rowOf(folder), "renameLayer");
    QCOMPARE(session.renamingLayerID(), std::optional(folder));
    session.setRenamingLayerID(std::nullopt);
    // Delete takes the row's layer, or its whole selection.
    session.selectLayers({green, blue}, blue);
    trigger(rowOf(green), "deleteLayer");
    QCOMPARE(session.document().value().layers.size(), size_t(1));
    trigger(0, "deleteLayer");
    QVERIFY(session.document().value().layers.empty());
}

void NativeLayerListTests::renamingTypesInTheRowAndReturnsFocus()
{
    const auto session = sessionWithThreeLayers();
    Shown shown(*session);
    shown.click(1);
    QTest::mouseDClick(&shown.row(1), Qt::LeftButton, Qt::NoModifier, QPoint(shown.row(1).width() - 20, 20));
    QCOMPARE(session->renamingLayerID(), std::optional(shown.id(1)));
    auto *editor = shown.row(1).findChild<QLineEdit *>("layerNameEditor");
    QTRY_VERIFY(editor->isVisible() && editor->hasFocus());
    QCOMPARE(editor->selectedText(), QString("Layer 2"));
    QTest::keyClicks(editor, "Sky");
    QTest::keyClick(editor, Qt::Key_Return);
    QCOMPARE(layerWith(*session, shown.id(1)).name, QString("Sky"));
    QVERIFY(!session->renamingLayerID().has_value());
    QVERIFY(!editor->isVisible() && shown.list.hasFocus());
    QCOMPARE(shown.name(1), QString("Sky"));
    // Escape keeps the old name; leaving keeps what was typed.
    session->setRenamingLayerID(shown.id(1));
    QTRY_VERIFY(editor->isVisible());
    QTest::keyClicks(editor, "Nope");
    QTest::keyClick(editor, Qt::Key_Escape);
    QCOMPARE(layerWith(*session, shown.id(1)).name, QString("Sky"));
    QVERIFY(!session->renamingLayerID().has_value());
    session->setRenamingLayerID(shown.id(1));
    QTRY_VERIFY(editor->isVisible());
    editor->selectAll();
    QTest::keyClicks(editor, "Sea");
    shown.list.setFocus();
    QCOMPARE(layerWith(*session, shown.id(1)).name, QString("Sea"));
    // A pending rename closes the edits: nothing moves under it.
    session->setRenamingLayerID(shown.id(1));
    QTRY_VERIFY(editor->isVisible());
    QVERIFY(!session->placeLayer(shown.id(1), std::nullopt));
    QVERIFY(!session->canEditLayers());
    QTest::keyClick(editor, Qt::Key_Escape);
    // A pixel thumbnail's double click renames; busy, nothing.
    QTest::mouseDClick(&shown.row(1).thumbnail(), Qt::LeftButton);
    QCOMPARE(session->renamingLayerID(), std::optional(shown.id(1)));
    QTest::keyClick(editor, Qt::Key_Escape);
    session->setIsProjectBusy(true);
    session->setRenamingLayerID(shown.id(1));
    QVERIFY(!editor->isVisible());
}

void NativeLayerListTests::layerListToolKeysAndTransformNudge()
{
    EditorSession session;
    session.createDocument(100, 100);
    session.insert(filled(10, 10, qRgba(255, 0, 0, 255), "Red"));
    Shown shown(session);
    shown.list.setFocus();
    QTest::keyClick(&shown.list, Qt::Key_H);
    QCOMPARE(session.tool(), NavigationTool::hand);
    // T chooses Type, since Swift 1.1.6.
    QTest::keyClick(&shown.list, Qt::Key_T);
    QCOMPARE(session.tool(), NavigationTool::type);
    QTest::keyClick(&shown.list, Qt::Key_V);
    QCOMPARE(session.tool(), NavigationTool::move);
    session.beginTransform();
    QTest::keyClick(&shown.list, Qt::Key_Right);
    QCOMPARE(session.transformEdit().value().draft.origin.x(), 46.0);
    QCOMPARE(session.activeLayer().value().origin().x(), 45.0);
    QTest::keyClick(&shown.list, Qt::Key_Escape);
    QVERIFY(!session.transformEdit().has_value());
    QCOMPARE(session.activeLayer().value().origin().x(), 45.0);
    QTest::keyClick(&shown.list, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(session.activeLayer().value().origin().y(), 55.0);
    QTest::keyClick(&shown.list, Qt::Key_Return);
    QVERIFY(!session.transformEdit().has_value());
    QCOMPARE(session.activeLayer().value().origin().y(), 55.0);
    // Ctrl-letters are the menu's; other tools' arrows walk rows.
    QTest::keyClick(&shown.list, Qt::Key_H, Qt::ControlModifier);
    QCOMPARE(session.tool(), NavigationTool::move);
    session.addBlankLayer();
    session.selectTool(NavigationTool::brush);
    QCOMPARE(session.activeLayerID(), std::optional(shown.id(0)));
    QTest::keyClick(&shown.list, Qt::Key_Down);
    QCOMPARE(session.activeLayerID(), std::optional(shown.id(1)));
    QTest::keyClick(&shown.list, Qt::Key_Down);
    QCOMPARE(session.activeLayerID(), std::optional(shown.id(1)));
    QTest::keyClick(&shown.list, Qt::Key_Up);
    QCOMPARE(session.activeLayerID(), std::optional(shown.id(0)));
    QTest::keyClick(&shown.list, Qt::Key_E);
    QCOMPARE(session.tool(), NavigationTool::brush);
}

void NativeLayerListTests::altClickOnTheStripClipsToTheLayerBelow()
{
    const auto session = sessionWithThreeLayers();
    Shown shown(*session);
    const QPoint strip(shown.row(0).width() - 20, LayerCell::rowHeight - 4);
    QTest::mouseClick(&shown.row(0), Qt::LeftButton, Qt::AltModifier, strip);
    QCOMPARE(layerWith(*session, shown.id(0)).maskSourceID, std::optional(shown.id(1)));
    QCOMPARE(shown.name(0), QString("↳ Layer 3"));
    QTest::mouseClick(&shown.row(0), Qt::LeftButton, Qt::AltModifier, strip);
    QVERIFY(!layerWith(*session, shown.id(0)).maskSourceID.has_value());
    // Above the strip Alt selects plainly; Ctrl-Alt too.
    QTest::mouseClick(&shown.row(2), Qt::LeftButton, Qt::AltModifier, QPoint(shown.row(2).width() - 20, 20));
    QCOMPARE(shown.selected(), QSet<QUuid>{shown.id(2)});
    QVERIFY(!layerWith(*session, shown.id(2)).maskSourceID.has_value());
    QTest::mouseClick(&shown.row(0), Qt::LeftButton, Qt::AltModifier, QPoint(shown.row(0).width() - 20, LayerCell::rowHeight - 14));
    QCOMPARE(shown.selected(), QSet<QUuid>{shown.id(0)});
    QVERIFY(!layerWith(*session, shown.id(0)).maskSourceID.has_value());
    QTest::mouseClick(&shown.row(0), Qt::LeftButton, Qt::AltModifier | Qt::ControlModifier, strip);
    QVERIFY(!layerWith(*session, shown.id(0)).maskSourceID.has_value());
}

void NativeLayerListTests::thumbnailsRedrawOnlyWhenTheirPictureChanges()
{
    EditorSession session;
    session.createDocument(40, 20);
    session.insert(filled(10, 10, qRgba(255, 0, 0, 255), "Red"));
    Shown shown(session);
    const qint64 before = shown.row(0).thumbnail().icon().cacheKey();
    session.renameLayer(shown.id(0), "Rose");
    session.selectLayer(shown.id(0));
    session.setLayerOpacity(0.5);
    QCOMPARE(shown.row(0).thumbnail().icon().cacheKey(), before);
    session.nudgeLayer(1, 0);
    QVERIFY(shown.row(0).thumbnail().icon().cacheKey() != before);
    const qint64 moved = shown.row(0).thumbnail().icon().cacheKey();
    session.addLayerMask();
    QCOMPARE(shown.row(0).thumbnail().icon().cacheKey(), moved);
    const qint64 mask = shown.row(0).maskThumbnail().icon().cacheKey();
    session.toggleLayerMask();
    QCOMPARE(shown.row(0).maskThumbnail().icon().cacheKey(), mask);
}

void NativeLayerListTests::deleteClearsASelectionOrDeletesTheLayer()
{
    const auto session = sessionWithThreeLayers();
    QImage red(8, 8, QImage::Format_RGBA8888_Premultiplied);
    red.fill(Qt::red);
    session->insert(ImportedImage(red, red, "Red"));
    Shown shown(*session);
    shown.list.setFocus();
    // The red sits mid-canvas; the selection takes its left half.
    const QPointF origin = session->activeLayer().value().transform.origin;
    session->applySelection(rectPath(QRectF(origin, QSizeF(4, 8))), SelectionMode::replace, "Select");
    QTest::keyClick(&shown.list, Qt::Key_Delete);
    QTRY_COMPARE(session->history.undoName(), QString("Clear"));
    session->deselect();
    const int layers = int(session->document().value().layers.size());
    QTest::keyClick(&shown.list, Qt::Key_Backspace);
    QCOMPARE(int(session->document().value().layers.size()), layers - 1);
    // With Ctrl the key is not Delete's.
    QTest::keyClick(&shown.list, Qt::Key_Delete, Qt::ControlModifier);
    QCOMPARE(int(session->document().value().layers.size()), layers - 1);
}

QTEST_MAIN(NativeLayerListTests)
#include "NativeLayerListTests.moc"

void NativeLayerListTests::ctrlClicksOnThumbnailsLoadSelections()
{
    EditorSession session;
    session.createDocument(400, 200);
    session.insert(filled(100, 50, qRgba(255, 0, 0, 255), "Red"), QPointF(100, 100));
    const QUuid red = session.activeLayerID().value();
    QImage block(100, 50, QImage::Format_Grayscale8);
    block.fill(Qt::white);
    for (int y = 10; y < 30; ++y)
        std::fill_n(block.scanLine(y) + 10, 20, uchar(0));
    rewrite(session, [&](ProjectSnapshot &snapshot) { setMask(snapshot, red, LayerMask::assetFrom(block)); });
    session.selectLayerTarget(red, false);
    Shown shown(session);
    LayerCell &row = shown.row(0);
    QCOMPARE(row.thumbnail().toolTip(), QString("Select image pixels"));
    QCOMPARE(row.maskThumbnail().toolTip(), QString("Select layer mask; Shift-click to enable/disable; Ctrl-click to select its black areas (Ctrl-Shift adds, Ctrl-Alt subtracts)"));
    // Ctrl on the picture: its opaque pixels, the row untouched.
    QTest::mouseClick(&row.thumbnail(), Qt::LeftButton, Qt::ControlModifier);
    QCOMPARE(session.history.undoName(), QString("Load Layer Selection"));
    QCOMPARE(session.selection().value().path.boundingRect(), QRectF(50, 75, 100, 50));
    QVERIFY(!session.isMaskSelected() && row.thumbnail().isTargeted());
    // Ctrl-Alt on the mask subtracts; Ctrl-Shift adds back.
    QTest::mouseClick(&row.maskThumbnail(), Qt::LeftButton, Qt::ControlModifier | Qt::AltModifier);
    QCOMPARE(session.history.undoName(), QString("Load Mask Selection"));
    QCOMPARE(int(session.selection().value().coverage(400, 200).constScanLine(90)[70]), 0);
    QCOMPARE(int(session.selection().value().coverage(400, 200).constScanLine(90)[120]), 255);
    QVERIFY(!session.isMaskSelected());
    session.applySelection(rectPath(QRectF(0, 0, 10, 10)), SelectionMode::replace, "Select");
    QTest::mouseClick(&row.maskThumbnail(), Qt::LeftButton, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(session.selection().value().path.boundingRect(), QRectF(0, 0, 80, 105));
    // Plain Ctrl replaces; a Ctrl double click loads twice.
    session.selectAll();
    QTest::mouseClick(&row.maskThumbnail(), Qt::LeftButton, Qt::ControlModifier);
    QCOMPARE(session.selection().value().path.boundingRect(), QRectF(60, 85, 20, 20));
    const int count = session.history.undoCount();
    QTest::mouseDClick(&row.thumbnail(), Qt::LeftButton, Qt::ControlModifier);
    QCOMPARE(session.history.undoCount(), count + 1);
    QVERIFY(!row.isRenaming());
}
