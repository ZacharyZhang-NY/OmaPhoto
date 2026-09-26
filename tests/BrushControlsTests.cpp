#include "Document/EditorSession.h"
#include "UI/BrushControls.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QSignalSpy>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>
#include <QToolButton>
#include <QtTest>

// Swift's BrushControls: mode, size, hardness, opacity, mask paint.
namespace {
template <typename Widget> Widget &find(QWidget &parent, const char *name)
{
    auto *found = parent.findChild<Widget *>(QString::fromLatin1(name));
    if (!found)
        throw std::runtime_error(name);
    return *found;
}

void type(QLineEdit &field, const QString &text)
{
    field.setFocus();
    field.selectAll();
    QTest::keyClicks(&field, text);
}
}

class BrushControlsTests : public QObject {
    Q_OBJECT
private slots:
    void theTitleAndModesFollowTheSession();
    void theFieldsAndSlidersSetTheBrush();
    void aMaskPaintsBlackOrWhite();
    void busyDimsTheBar();
    void eachBrushToolTitlesTheBarAndShowsItsPicker();
    void cloneStampShowsAlignedSampleAndTheSourceNote();
    void theSmearShowsItsThreeModes();
};

void BrushControlsTests::theTitleAndModesFollowTheSession()
{
    EditorSession session;
    session.createDocument(8, 8, true);
    BrushControls bar(session);
    bar.show();
    QCOMPARE(bar.title->text(), QString("Brush"));
    auto &paint = find<QToolButton>(bar, "brushPaint"), &erase = find<QToolButton>(bar, "brushErase");
    QVERIFY(paint.isChecked() && !erase.isChecked());
    QCOMPARE(erase.text(), QString("Erase"));
    QCOMPARE(paint.toolTip(), QString("Paint with the foreground color (B), or erase pixels away (E)"));
    erase.click();
    QCOMPARE(session.brushMode(), BrushToolMode::erase);
    QCOMPARE(bar.title->text(), QString("Eraser"));
    QVERIFY(erase.isChecked() && !paint.isChecked());
    session.setBrushMode(BrushToolMode::paint);
    QVERIFY(paint.isChecked() && !erase.isChecked());
    QCOMPARE(bar.title->text(), QString("Brush"));
    erase.click();
    paint.click();
    QCOMPARE(session.brushMode(), BrushToolMode::paint);
}

void BrushControlsTests::theFieldsAndSlidersSetTheBrush()
{
    EditorSession session;
    session.createDocument(8, 8, true);
    BrushControls bar(session);
    bar.show();
    QVERIFY(QTest::qWaitForWindowActive(&bar));
    auto &size = find<QLineEdit>(bar, "brushSize"), &hardness = find<QLineEdit>(bar, "brushHardness"), &opacity = find<QLineEdit>(bar, "brushOpacity");
    QCOMPARE(size.text(), QString("40"));
    QCOMPARE(hardness.text(), QString("100"));
    QCOMPARE(opacity.text(), QString("100"));
    QCOMPARE(opacity.toolTip(), QString("Press 1–9 for 10–90%, 0 for 100%"));
    // Typing applies at once, within Swift's bounds.
    type(size, QStringLiteral("120"));
    QCOMPARE(session.brushSettings().diameter, 120.0);
    type(size, QStringLiteral("5000"));
    QCOMPARE(session.brushSettings().diameter, 2000.0);
    type(size, QStringLiteral("0"));
    QCOMPARE(session.brushSettings().diameter, 1.0);
    QTest::keyClick(&size, Qt::Key_Up, Qt::ShiftModifier);
    QCOMPARE(session.brushSettings().diameter, 11.0);
    type(hardness, QStringLiteral("0"));
    QCOMPARE(session.brushSettings().hardness, 0.0);
    type(hardness, QStringLiteral("25"));
    QCOMPARE(session.brushSettings().hardness, 0.25);
    type(opacity, QStringLiteral("0"));
    QCOMPARE(session.brushSettings().opacity, 0.01);
    // The sliders set a thousandth a step; the fields follow.
    size.clearFocus();
    hardness.clearFocus();
    opacity.clearFocus();
    find<QSlider>(bar, "brushHardnessSlider").setValue(750);
    QCOMPARE(session.brushSettings().hardness, 0.75);
    QCOMPARE(hardness.text(), QString("75"));
    auto &opacitySlider = find<QSlider>(bar, "brushOpacitySlider");
    QCOMPARE(opacitySlider.minimum(), 10);
    opacitySlider.setValue(500);
    QCOMPARE(session.brushSettings().opacity, 0.5);
    QCOMPARE(opacity.text(), QString("50"));
    // The session's own changes reach the sliders, silently.
    session.changeBrushHardness(false);
    QCOMPARE(find<QSlider>(bar, "brushHardnessSlider").value(), 500);
    QCOMPARE(session.brushSettings().hardness, 0.5);
    BrushSettings third = session.brushSettings();
    third.hardness = 1.0 / 3;
    third.opacity = 2.0 / 3;
    session.setBrushSettings(third);
    QCOMPARE(find<QSlider>(bar, "brushHardnessSlider").value(), 333);
    QCOMPARE(opacitySlider.value(), 667);
    QVERIFY(session.brushSettings().hardness == 1.0 / 3 && session.brushSettings().opacity == 2.0 / 3);
    // Arrows step from the exact value, as Swift's arrowSteps.
    hardness.setFocus();
    QTest::keyClick(&hardness, Qt::Key_Up);
    QCOMPARE(session.brushSettings().hardness, 1.0 / 3 + 0.01);
    QCOMPARE(hardness.text(), QString("34"));
    opacity.setFocus();
    QTest::keyClick(&opacity, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(session.brushSettings().opacity, 2.0 / 3 - 0.1);
    QCOMPARE(opacity.text(), QString("57"));
}

void BrushControlsTests::aMaskPaintsBlackOrWhite()
{
    EditorSession session;
    session.createDocument(8, 8, true);
    BrushControls bar(session);
    bar.show();
    auto &picker = find<QComboBox>(bar, "maskPaint");
    auto &note = find<QLabel>(bar, "brushMaskNote");
    QLabel *label = nullptr;
    for (QLabel *each : bar.findChildren<QLabel *>()) {
        if (each->text() == QStringLiteral("Paint"))
            label = each;
    }
    QVERIFY(label && !label->isVisible() && !picker.isVisible() && !note.isVisible());
    session.addLayerMask(true);
    QVERIFY(session.isMaskSelected());
    QVERIFY(label->isVisible() && picker.isVisible() && note.isVisible());
    QCOMPARE(note.text(), QString("Mask"));
    QCOMPARE(picker.itemText(0), QString("Black · Hide"));
    QCOMPARE(picker.itemText(1), QString("White · Reveal"));
    QCOMPARE(picker.currentIndex(), session.maskPaintWhite() ? 1 : 0);
    picker.setCurrentIndex(1);
    emit picker.activated(1);
    QVERIFY(session.maskPaintWhite());
    session.setMaskPaintWhite(false);
    QCOMPARE(picker.currentIndex(), 0);
}

void BrushControlsTests::busyDimsTheBar()
{
    EditorSession session;
    session.createDocument(8, 8, true);
    BrushControls bar(session);
    QVERIFY(bar.isEnabled());
    session.setIsProjectBusy(true);
    QTRY_VERIFY(session.showsBusy());
    QVERIFY(!bar.isEnabled());
    session.setIsProjectBusy(false);
    QVERIFY(bar.isEnabled());
}

void BrushControlsTests::eachBrushToolTitlesTheBarAndShowsItsPicker()
{
    EditorSession session;
    session.createDocument(8, 8, true);
    session.selectTool(NavigationTool::brush);
    BrushControls bar(session);
    bar.show();
    auto &paint = find<QToolButton>(bar, "brushPaint");
    const QList<QAbstractButton *> types = find<QButtonGroup>(bar, "spotHealingType").buttons();
    QCOMPARE(types.size(), 3);
    QVERIFY(paint.isVisible() && !types[0]->isVisible());
    // Spot Healing: its title and Swift's three types, no mode.
    session.selectTool(NavigationTool::spotHealing);
    QCOMPARE(bar.title->text(), QString("Spot Healing"));
    QVERIFY(!paint.isVisible() && !find<QToolButton>(bar, "brushErase").isVisible());
    QCOMPARE(types[0]->text(), QString("Content-Aware"));
    QCOMPARE(types[1]->text(), QString("Create Texture"));
    QCOMPARE(types[2]->text(), QString("Proximity Match"));
    QVERIFY(types[0]->isVisible() && types[0]->isChecked());
    types[2]->click();
    QCOMPARE(session.spotHealingMode(), SpotHealingMode::proximityMatch);
    session.setSpotHealingMode(SpotHealingMode::createTexture);
    QVERIFY(types[1]->isChecked() && !types[2]->isChecked());
    // The other brush tools title theirs; Smear says strength.
    QLabel *opacity = nullptr;
    for (QLabel *each : bar.findChildren<QLabel *>()) {
        if (each->text() == QStringLiteral("Opacity"))
            opacity = each;
    }
    QVERIFY(opacity);
    session.selectTool(NavigationTool::cloneStamp);
    QCOMPARE(bar.title->text(), QString("Clone Stamp"));
    QVERIFY(!types[0]->isVisible() && !paint.isVisible());
    session.selectTool(NavigationTool::blur);
    QCOMPARE(bar.title->text(), QString("Smear"));
    QCOMPARE(opacity->text(), QString("Strength"));
    session.selectTool(NavigationTool::brush);
    QCOMPARE(opacity->text(), QString("Opacity"));
    QVERIFY(paint.isVisible());
}

void BrushControlsTests::cloneStampShowsAlignedSampleAndTheSourceNote()
{
    EditorSession session;
    session.createDocument(8, 8, true);
    session.selectTool(NavigationTool::brush);
    BrushControls bar(session);
    bar.show();
    auto &aligned = find<QCheckBox>(bar, "cloneAligned");
    auto &thisLayer = find<QToolButton>(bar, "cloneThisLayer");
    auto &allLayers = find<QToolButton>(bar, "cloneAllLayers");
    auto &note = find<QLabel>(bar, "cloneSourceNote");
    QVERIFY(!aligned.isVisible() && !thisLayer.isVisible() && !allLayers.isVisible() && !note.isVisible());
    session.selectTool(NavigationTool::cloneStamp);
    QVERIFY(aligned.isVisible() && aligned.isChecked() && thisLayer.isChecked() && !allLayers.isChecked());
    QCOMPARE(aligned.text(), QString("Aligned"));
    QCOMPARE(aligned.toolTip(), QString("Keep the source moving with the brush between strokes; off starts every stroke at the source point"));
    QCOMPARE(thisLayer.text(), QString("This Layer"));
    QCOMPARE(allLayers.text(), QString("All Layers"));
    QCOMPARE(allLayers.toolTip(), QString("Copy from the active layer only, or from every visible layer as shown"));
    QCOMPARE(thisLayer.toolTip(), allLayers.toolTip());
    // Without a source, the bar says how to set one.
    QVERIFY(note.isVisible());
    QCOMPARE(note.text(), QString("Alt-click to set the source"));
    QCOMPARE(note.foregroundRole(), QPalette::PlaceholderText);
    session.setCloneSource(QPointF(2, 2));
    QVERIFY(!note.isVisible());
    // Clicks write the settings; session changes write nothing back.
    allLayers.click();
    QVERIFY(session.cloneSettings().sampleAllLayers && session.cloneSettings().aligned);
    aligned.click();
    QVERIFY(!session.cloneSettings().aligned && session.cloneSettings().sampleAllLayers);
    thisLayer.click();
    QVERIFY(!session.cloneSettings().sampleAllLayers && !session.cloneSettings().aligned);
    QSignalSpy changed(&session, &EditorSession::changed);
    session.setCloneSettings(CloneSettings{true, true});
    QCOMPARE(changed.size(), 1);
    QVERIFY(aligned.isChecked() && allLayers.isChecked() && !thisLayer.isChecked());
    session.setCloneSettings(CloneSettings{false, true});
    QVERIFY(!aligned.isChecked());
    session.selectTool(NavigationTool::spotHealing);
    QVERIFY(!aligned.isVisible() && !allLayers.isVisible() && !note.isVisible());
}

void BrushControlsTests::theSmearShowsItsThreeModes()
{
    EditorSession session;
    session.createDocument(8, 8, true);
    session.selectTool(NavigationTool::brush);
    BrushControls bar(session);
    bar.show();
    const QList<QAbstractButton *> modes = find<QButtonGroup>(bar, "blurMode").buttons();
    QCOMPARE(modes.size(), 3);
    QVERIFY(!modes[0]->isVisible());
    session.selectTool(NavigationTool::blur);
    QCOMPARE(modes[0]->text(), QString("Liquify"));
    QCOMPARE(modes[1]->text(), QString("Blur"));
    QCOMPARE(modes[2]->text(), QString("Smudge"));
    for (QAbstractButton *mode : modes)
        QCOMPARE(mode->toolTip(), QString("Liquify pushes pixels · Blur softens · Smudge drags color along"));
    QVERIFY(modes[0]->isVisible() && modes[0]->isChecked() && !find<QToolButton>(bar, "brushPaint").isVisible());
    modes[2]->click();
    QCOMPARE(session.blurMode(), BlurToolMode::smudge);
    session.setBlurMode(BlurToolMode::blur);
    QVERIFY(modes[1]->isChecked() && !modes[2]->isChecked());
    session.selectTool(NavigationTool::spotHealing);
    QVERIFY(!modes[1]->isVisible());
}

QTEST_MAIN(BrushControlsTests)
#include "BrushControlsTests.moc"
