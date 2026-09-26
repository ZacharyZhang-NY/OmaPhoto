#include "UI/LayerAppearanceControls.h"
#include "UI/BlendModePicker.h"
#include <QtTest>

// The opacity slider and field, one undo step a drag.
namespace {
ImportedImage white()
{
    QImage image(2, 2, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::white);
    return ImportedImage(image, image, "White");
}

struct Controls {
    EditorSession session;
    LayerAppearanceControls widget{session};
    QSlider &slider = *widget.findChild<QSlider *>("opacitySlider");
    QLineEdit &field = *widget.findChild<QLineEdit *>("opacityPercent");
    double opacity() { return session.activeLayer().value().opacity; }
};
}

class LayerAppearanceControlsTests : public QObject {
    Q_OBJECT
private slots:
    void theControlsFollowTheActiveLayerAndItsGate();
    void aSliderDragIsOneUndoStep();
    void theFieldAppliesOnReturnEscapeAndLeavingAndStepsWithArrows();
    void aFieldLeftBehindByAnotherLayerAppliesNothing();
};

void LayerAppearanceControlsTests::theControlsFollowTheActiveLayerAndItsGate()
{
    Controls controls;
    QVERIFY(!controls.widget.isEnabled());
    QCOMPARE(controls.field.text(), QString("100"));
    controls.session.createDocument(4, 4);
    controls.session.insert(white());
    QVERIFY(controls.widget.isEnabled());
    controls.session.setLayerOpacity(0.25);
    QCOMPARE(controls.slider.value(), 250);
    QCOMPARE(controls.field.text(), QString("25"));
    controls.session.insert(white());
    QCOMPARE(controls.slider.value(), 1000);
    QCOMPARE(controls.field.text(), QString("100"));
    controls.session.selectLayer(controls.session.document().value().layers.front().id);
    QCOMPARE(controls.field.text(), QString("25"));
    // Showing a finer opacity must not write the slider's back.
    controls.session.setLayerOpacity(0.12345);
    QCOMPARE(controls.slider.value(), 123);
    QCOMPARE(controls.session.activeLayer().value().opacity, 0.12345);
    QVERIFY(controls.widget.findChild<BlendModePicker *>() != nullptr);
    controls.session.selectLayers({controls.session.document().value().layers.front().id, controls.session.document().value().layers.back().id}, std::nullopt);
    QVERIFY(!controls.widget.isEnabled());
}

void LayerAppearanceControlsTests::aSliderDragIsOneUndoStep()
{
    Controls controls;
    controls.session.createDocument(4, 4);
    controls.session.insert(white());
    const int steps = controls.session.history.undoCount();
    controls.slider.setSliderDown(true);
    emit controls.slider.sliderPressed();
    controls.slider.setValue(600);
    controls.slider.setValue(400);
    QCOMPARE(controls.opacity(), 0.4);
    QVERIFY(!controls.session.canUndo());
    controls.slider.setSliderDown(false);
    emit controls.slider.sliderReleased();
    QCOMPARE(controls.session.history.undoCount(), steps + 1);
    QCOMPARE(controls.session.history.undoName(), QString("Layer Opacity"));
    controls.session.undo();
    QCOMPARE(controls.opacity(), 1.0);
    QCOMPARE(controls.slider.value(), 1000);
    // The keyboard moves the slider too, a step each.
    controls.slider.setValue(900);
    QCOMPARE(controls.opacity(), 0.9);
    QCOMPARE(controls.session.history.undoCount(), steps + 1);
}

void LayerAppearanceControlsTests::theFieldAppliesOnReturnEscapeAndLeavingAndStepsWithArrows()
{
    Controls controls;
    controls.session.createDocument(4, 4);
    controls.session.insert(white());
    controls.widget.show();
    QVERIFY(QTest::qWaitForWindowActive(&controls.widget));
    controls.field.setFocus();
    controls.field.selectAll();
    QTest::keyClicks(&controls.field, "40");
    QTest::keyClick(&controls.field, Qt::Key_Return);
    QCOMPARE(controls.opacity(), 0.4);
    QVERIFY(!controls.field.hasFocus());
    QCOMPARE(controls.session.canvasFocusRequest(), 1);
    controls.field.setFocus();
    controls.field.selectAll();
    QTest::keyClicks(&controls.field, "abc");
    QTest::keyClick(&controls.field, Qt::Key_Escape);
    QCOMPARE(controls.opacity(), 0.4);
    QCOMPARE(controls.field.text(), QString("40"));
    QCOMPARE(controls.session.canvasFocusRequest(), 2);
    // Leaving applies without asking for the canvas; past 100 clamps.
    controls.field.setFocus();
    controls.field.selectAll();
    QTest::keyClicks(&controls.field, "250");
    controls.field.clearFocus();
    QCOMPARE(controls.opacity(), 1.0);
    QCOMPARE(controls.field.text(), QString("100"));
    QCOMPARE(controls.session.canvasFocusRequest(), 2);
    // Arrows step one percent, ten with Shift, within the range.
    controls.field.setFocus();
    QTest::keyClick(&controls.field, Qt::Key_Down);
    QCOMPARE(controls.opacity(), 0.99);
    QTest::keyClick(&controls.field, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(controls.opacity(), 0.89);
    QCOMPARE(controls.field.text(), QString("89"));
    QTest::keyClick(&controls.field, Qt::Key_Up, Qt::ShiftModifier);
    QTest::keyClick(&controls.field, Qt::Key_Up, Qt::ShiftModifier);
    QCOMPARE(controls.opacity(), 1.0);
    // A finer value is kept once, shown rounded.
    const int steps = controls.session.history.undoCount();
    controls.field.selectAll();
    QTest::keyClicks(&controls.field, "12.345");
    QTest::keyClick(&controls.field, Qt::Key_Return);
    QCOMPARE(controls.opacity(), 0.12345);
    QCOMPARE(controls.field.text(), QString("12"));
    QCOMPARE(controls.session.history.undoCount(), steps + 1);
    controls.field.setFocus();
    QTest::keyClick(&controls.field, Qt::Key_Up);
    QCOMPARE(controls.opacity(), 0.13);
    controls.field.selectAll();
    QTest::keyClicks(&controls.field, "100");
    QTest::keyClick(&controls.field, Qt::Key_Return);
    controls.field.setFocus();
    // Typing goes on while the session moves; the slider follows.
    controls.field.selectAll();
    QTest::keyClicks(&controls.field, "5");
    controls.session.setLayerOpacity(0.3);
    QCOMPARE(controls.field.text(), QString("5"));
    QCOMPARE(controls.slider.value(), 300);
    QTest::keyClick(&controls.field, Qt::Key_Enter);
    QCOMPARE(controls.opacity(), 0.05);
}

void LayerAppearanceControlsTests::aFieldLeftBehindByAnotherLayerAppliesNothing()
{
    Controls controls;
    controls.session.createDocument(4, 4);
    controls.session.insert(white());
    const QUuid first = controls.session.activeLayerID().value();
    controls.session.insert(white());
    controls.widget.show();
    QVERIFY(QTest::qWaitForWindowActive(&controls.widget));
    controls.field.setFocus();
    controls.field.selectAll();
    QTest::keyClicks(&controls.field, "20");
    // Another layer active: the typed value belongs to no one.
    controls.session.selectLayer(first);
    QVERIFY(!controls.field.hasFocus());
    QCOMPARE(controls.field.text(), QString("100"));
    for (const ImageLayer &layer : controls.session.document().value().layers)
        QCOMPARE(layer.opacity, 1.0);
    // The controls end an open drag when they go.
    controls.slider.setSliderDown(true);
    emit controls.slider.sliderPressed();
    controls.slider.setValue(500);
    QVERIFY(!controls.session.canUndo());
    auto *later = new LayerAppearanceControls(controls.session);
    delete later;
    QVERIFY(controls.session.canUndo());
}

QTEST_MAIN(LayerAppearanceControlsTests)
#include "LayerAppearanceControlsTests.moc"
