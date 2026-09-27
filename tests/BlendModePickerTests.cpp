#include "UI/BlendModePicker.h"
#include "UI/LayerMaskMenu.h"
#include <QAbstractItemView>
#include <QtTest>

// The blend menu and the add-mask button follow the session.
namespace {
ImportedImage white()
{
    QImage image(2, 2, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::white);
    return ImportedImage(image, image, "White");
}
}

class BlendModePickerTests : public QObject {
    Q_OBJECT
private slots:
    void theMenuListsEveryModeAndFollowsTheActiveLayer();
    void hoveringTriesAModeOnAndChoosingKeepsIt();
    void aMenuClosedWithoutAChoiceLeavesTheLayerAlone();
    void theMaskButtonAddsAWhiteMaskOnce();
};

void BlendModePickerTests::theMenuListsEveryModeAndFollowsTheActiveLayer()
{
    EditorSession session;
    BlendModePicker picker(session);
    // Photoshop's six groups, a separator between each.
    QCOMPARE(picker.count(), int(allLayerBlendModes.size()) + 5);
    QStringList titles;
    for (int index = 0; index < picker.count(); ++index)
        titles << picker.itemText(index);
    QCOMPARE(titles, (QStringList{"Normal", "", "Darken", "Multiply", "Color Burn", "Linear Burn", "", "Lighten", "Screen", "Color Dodge",
                                  "Linear Dodge (Add)", "", "Overlay", "Soft Light", "Hard Light", "Vivid Light", "Linear Light", "Pin Light",
                                  "Hard Mix", "", "Difference", "Exclusion", "Subtract", "Divide", "", "Hue", "Saturation", "Color", "Luminosity"}));
    for (const int separator : {1, 6, 11, 19, 24})
        QCOMPARE(picker.itemData(separator, Qt::AccessibleDescriptionRole).toString(), QString("separator"));
    QVERIFY(!picker.isEnabled());
    session.createDocument(4, 4);
    session.insert(white());
    QVERIFY(picker.isEnabled());
    QCOMPARE(picker.currentText(), QString("Normal"));
    session.setLayerBlendMode(LayerBlendMode::screen);
    QCOMPARE(picker.currentText(), QString("Screen"));
    // A second layer's own mode; a folder disables the menu.
    session.insert(white());
    session.setLayerBlendMode(LayerBlendMode::hue);
    QCOMPARE(picker.currentText(), QString("Hue"));
    session.selectLayer(session.document().value().layers.front().id);
    QCOMPARE(picker.currentText(), QString("Screen"));
    session.groupSelectedLayers();
    QVERIFY(!picker.isEnabled());
    QCOMPARE(picker.currentText(), QString("Normal"));
}

void BlendModePickerTests::hoveringTriesAModeOnAndChoosingKeepsIt()
{
    EditorSession session;
    session.createDocument(4, 4);
    session.insert(white());
    const QUuid layer = session.activeLayerID().value();
    BlendModePicker picker(session);
    picker.show();
    QVERIFY(QTest::qWaitForWindowExposed(&picker));
    picker.showPopup();
    QTRY_VERIFY(picker.view()->isVisible());
    // Hovering previews on the layer the menu opened for.
    emit picker.highlighted(3);
    QCOMPARE(session.blendPreview().value().mode, LayerBlendMode::multiply);
    QCOMPARE(session.blendPreview().value().layerID, layer);
    QCOMPARE(session.activeLayer().value().blendMode, LayerBlendMode::normal);
    emit picker.highlighted(8);
    QCOMPARE(session.blendPreview().value().mode, LayerBlendMode::screen);
    // The session's news leaves an open menu alone.
    picker.view()->setCurrentIndex(picker.model()->index(8, 0));
    session.setLayerOpacity(0.5);
    QVERIFY(session.blendPreview().has_value());
    QCOMPARE(picker.view()->currentIndex().row(), 8);
    // A click closes the menu, then chooses; preview clears after.
    picker.hidePopup();
    emit picker.activated(8);
    QCOMPARE(session.activeLayer().value().blendMode, LayerBlendMode::screen);
    QCOMPARE(picker.currentText(), QString("Screen"));
    QCOMPARE(session.history.undoName(), QString("Layer Blend Mode"));
    QTRY_VERIFY(!session.blendPreview().has_value());
    QCOMPARE(picker.currentText(), QString("Screen"));
    // Keys on the closed menu choose for the active layer.
    picker.setFocus();
    QTest::keyClick(&picker, Qt::Key_Down);
    QCOMPARE(session.activeLayer().value().blendMode, LayerBlendMode::colorDodge);
    QCOMPARE(picker.currentText(), QString("Color Dodge"));
    // Keys step over the lines between groups.
    QTest::keyClick(&picker, Qt::Key_Down);
    QTest::keyClick(&picker, Qt::Key_Down);
    QCOMPARE(picker.currentText(), QString("Overlay"));
    session.undo();
    session.undo();
    // Closed, the menu follows the session again.
    session.undo();
    QCOMPARE(picker.currentText(), QString("Screen"));
}

void BlendModePickerTests::aMenuClosedWithoutAChoiceLeavesTheLayerAlone()
{
    EditorSession session;
    session.createDocument(4, 4);
    session.insert(white());
    session.setLayerBlendMode(LayerBlendMode::multiply);
    session.insert(white());
    const QUuid top = session.activeLayerID().value();
    BlendModePicker picker(session);
    picker.show();
    QVERIFY(QTest::qWaitForWindowExposed(&picker));
    picker.showPopup();
    emit picker.highlighted(3);
    picker.hidePopup();
    QCOMPARE(session.activeLayer().value().blendMode, LayerBlendMode::normal);
    QTRY_VERIFY(!session.blendPreview().has_value());
    QCOMPARE(picker.currentText(), QString("Normal"));
    // Another layer active at the choice: nothing changes.
    picker.showPopup();
    emit picker.highlighted(3);
    session.selectLayer(session.document().value().layers.front().id);
    // Open, the menu keeps its text; closed, the active layer.
    QCOMPARE(picker.currentText(), QString("Normal"));
    picker.hidePopup();
    emit picker.activated(3);
    QCOMPARE(session.document().value().layers.front().blendMode, LayerBlendMode::multiply);
    QCOMPARE(session.document().value().layers.back().blendMode, LayerBlendMode::normal);
    QCOMPARE(session.history.undoName(), QString("Import Image"));
    QCOMPARE(picker.currentText(), QString("Multiply"));
    QTRY_VERIFY(!session.blendPreview().has_value());
    session.selectLayer(top);
    QCOMPARE(picker.currentText(), QString("Normal"));
    // A cancelled menu keeps no target: keys choose.
    picker.showPopup();
    picker.hidePopup();
    QCoreApplication::processEvents();
    session.selectLayer(session.document().value().layers.front().id);
    picker.setFocus();
    QTest::keyClick(&picker, Qt::Key_Down);
    QCOMPARE(session.activeLayer().value().blendMode, LayerBlendMode::colorBurn);
}

void BlendModePickerTests::theMaskButtonAddsAWhiteMaskOnce()
{
    EditorSession session;
    LayerMaskMenu button(session);
    QCOMPARE(button.accessibleName(), QString("Add layer mask"));
    QVERIFY(!button.isEnabled());
    session.createDocument(4, 4);
    session.insert(white());
    QVERIFY(button.isEnabled());
    button.click();
    const ImageLayer layer = session.activeLayer().value();
    QVERIFY(layer.mask.has_value());
    QCOMPARE(layer.mask.value().asset.image().pixelColor(0, 0), QColor(255, 255, 255));
    QVERIFY(!button.isEnabled());
    session.undo();
    QVERIFY(!session.activeLayer().value().mask.has_value() && button.isEnabled());
    session.setIsProjectBusy(true);
    QVERIFY(!button.isEnabled());
    session.setIsProjectBusy(false);
    QVERIFY(button.isEnabled());
}

QTEST_MAIN(BlendModePickerTests)
#include "BlendModePickerTests.moc"
