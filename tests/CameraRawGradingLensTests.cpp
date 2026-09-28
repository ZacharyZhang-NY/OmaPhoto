#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include "UI/CameraRawColorControls.h"
#include "UI/CameraRawControls.h"
#include "UI/CameraRawDetailOpticsControls.h"
#include "UI/CameraRawGeometryCalibrationControls.h"
#include "UI/FloatingPanel.h"
#include "UI/LayerIcons.h"
#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QStyleOptionSlider>
#include <QtTest>
#include <cmath>
#include <numbers>

// Swift's Color Grading, Detail, Optics, Geometry and Calibration groups.
namespace {
// The knob's middle, where a double click resets.
QPoint knob(const CameraRawSlider &slider)
{
    QStyleOptionSlider option;
    option.initFrom(&slider);
    option.orientation = Qt::Horizontal;
    option.minimum = slider.minimum();
    option.maximum = slider.maximum();
    option.sliderPosition = slider.sliderPosition();
    option.sliderValue = slider.value();
    return slider.style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, &slider).center();
}

template <typename Controls> struct Group {
    EditorSession session;
    std::unique_ptr<Controls> controls;
    Group()
    {
        session.createDocument(8, 8);
        QImage image = BrushRaster::context(8, 8, false);
        image.fill(QColor(200, 120, 60));
        session.insert(ImportedImage(image, image, QStringLiteral("Warm")));
        session.beginFilter(FilterKind::cameraRaw);
        controls = std::make_unique<Controls>(session);
        controls->resize(374, 700);
        controls->show();
    }
    template <typename Widget> Widget &child(const QString &name) const
    {
        auto *found = controls->template findChild<Widget *>(name);
        if (!found)
            throw std::runtime_error(name.toStdString());
        return *found;
    }
    const CameraRawSettings &raw() const { return session.filterEdit().value().settings.cameraRaw; }
    const CameraRawPanel &panel() const { return session.filterEdit().value().rawPanel; }
    void set(const std::function<void(CameraRawSettings &)> &change)
    {
        FilterSettings settings = session.filterEdit().value().settings;
        change(settings.cameraRaw);
        session.updateFilter(settings, true);
    }
};
}

class CameraRawGradingLensTests : public QObject {
    Q_OBJECT
private slots:
    void theWheelsSetHueAndSaturationAndResetOnADoubleClick();
    void theWheelsHueRunsCounterclockwise();
    void onePageShowsOneWheelAndTheGradingFitsTheDock();
    void detailRoundsItsRowsAndDimsIdleNoiseDetail();
    void opticsTogglesProfileDefringeAndHueRanges();
    void geometryUprightGuidesAndRoundedRows();
    void calibrationChoosesItsProcessAndPrimaries();
};

void CameraRawGradingLensTests::theWheelsSetHueAndSaturationAndResetOnADoubleClick()
{
    Group<CameraRawGradingControls> group;
    auto *box = group.controls->findChild<QWidget *>(QStringLiteral("gradeWheelMidtones"));
    QWidget *wheel = nullptr;
    for (QWidget *child : box->findChildren<QWidget *>())
        if (child->size() == QSize(86, 86))
            wheel = child;
    QVERIFY(wheel);
    // Straight up is hue 90; at the rim, 100.
    QTest::mousePress(wheel, Qt::LeftButton, {}, QPoint(43, 6));
    QTest::mouseRelease(wheel, Qt::LeftButton, {}, QPoint(43, 6));
    QVERIFY(std::abs(group.raw().grading.midtones.hue - 90) < 0.5 && group.raw().grading.midtones.saturation == 100);
    QVERIFY(group.raw().grading.shadows.hue == 0);
    QCOMPARE(box->findChild<QLabel *>(QStringLiteral("gradeReadout"))->text(), QString("90°  100"));
    // The left rim, 37 points out: hue 180, saturation 100.
    QTest::mousePress(wheel, Qt::LeftButton, {}, QPoint(43 - 37, 43));
    QVERIFY(std::abs(group.raw().grading.midtones.hue - 180) < 0.5 && group.raw().grading.midtones.saturation == 100);
    QTest::mouseRelease(wheel, Qt::LeftButton, {}, QPoint(6, 43));
    // A press outside the circle does nothing.
    QTest::mousePress(wheel, Qt::LeftButton, {}, QPoint(1, 1));
    QVERIFY(std::abs(group.raw().grading.midtones.hue - 180) < 0.5);
    QTest::mouseRelease(wheel, Qt::LeftButton, {}, QPoint(1, 1));
    QTest::mouseDClick(wheel, Qt::LeftButton, {}, QPoint(43, 43));
    QVERIFY(group.raw().grading.midtones.hue == 0 && group.raw().grading.midtones.saturation == 0);
    auto *luminance = box->findChild<CameraRawSlider *>(QStringLiteral("gradeLuminance"));
    luminance->setValue(750);
    QCOMPARE(group.raw().grading.midtones.luminance, 50.0);
    QCOMPARE(luminance->width(), 96);
    group.child<CameraRawSlider>(QStringLiteral("gradeBlendingSlider")).setValue(200);
    QCOMPARE(group.raw().grading.blending, 20.0);
    auto &balance = group.child<CameraRawSlider>(QStringLiteral("gradeBalanceSlider"));
    balance.setValue(250);
    QCOMPARE(group.raw().grading.balance, -50.0);
    QTest::mouseDClick(&balance, Qt::LeftButton, {}, knob(balance));
    QCOMPARE(group.raw().grading.balance, 0.0);
}

// 1.2.9's 96edd5f: the rainbow runs the way the hue does.
void CameraRawGradingLensTests::theWheelsHueRunsCounterclockwise()
{
    Group<CameraRawGradingControls> group;
    auto *box = group.controls->findChild<QWidget *>(QStringLiteral("gradeWheelShadows"));
    QWidget *wheel = nullptr;
    for (QWidget *child : box->findChildren<QWidget *>())
        if (child->size() == QSize(86, 86))
            wheel = child;
    const QImage drawn = wheel->grab().toImage();
    const auto hueAt = [&drawn](double degrees) {
        const double radians = degrees * std::numbers::pi / 180;
        return drawn.pixelColor(int(43 + std::cos(radians) * 30), int(43 - std::sin(radians) * 30)).hsvHueF() * 360;
    };
    // Dragged to 120, the wheel shows green; 240, blue.
    QVERIFY2(std::abs(hueAt(120) - 120) < 20, qPrintable(QString::number(hueAt(120))));
    QVERIFY2(std::abs(hueAt(240) - 240) < 20, qPrintable(QString::number(hueAt(240))));
    QVERIFY2(std::abs(hueAt(60) - 60) < 20, qPrintable(QString::number(hueAt(60))));
}

void CameraRawGradingLensTests::onePageShowsOneWheelAndTheGradingFitsTheDock()
{
    Group<CameraRawGradingControls> group;
    auto &page = group.child<QComboBox>(QStringLiteral("gradePage"));
    QCOMPARE(page.count(), 5);
    const auto shown = [&group] {
        QStringList names;
        for (QWidget *box : group.controls->findChildren<QWidget *>())
            if (box->objectName().startsWith(QLatin1String("gradeWheel")) && box->isVisible())
                names << box->objectName();
        names.sort();
        return names;
    };
    QCOMPARE(shown(), (QStringList{"gradeWheelHighlights", "gradeWheelMidtones", "gradeWheelShadows"}));
    emit page.activated(4);
    QVERIFY(group.panel().gradePage == CameraRawGradePage::global);
    QCOMPARE(shown(), QStringList{"gradeWheelGlobal"});
    emit page.activated(2);
    QCOMPARE(shown(), QStringList{"gradeWheelMidtones"});
    // Swift's theDockedPanelFitsTheGradingWheels: padding 24 a side, inset 18.
    CameraRawGradingControls grading(group.session);
    QVERIFY2(grading.sizeHint().width() <= FloatingPanel::dockedWidth - 48 - 18, qPrintable(QString::number(grading.sizeHint().width())));
}

void CameraRawGradingLensTests::detailRoundsItsRowsAndDimsIdleNoiseDetail()
{
    Group<CameraRawDetailControls> group;
    auto &amount = group.child<CameraRawSlider>(QStringLiteral("sharpenAmountSlider"));
    amount.setValue(333);
    QCOMPARE(group.raw().detail.sharpenAmount, 50.0);
    auto &radius = group.child<CameraRawSlider>(QStringLiteral("sharpenRadiusSlider"));
    QTest::mouseDClick(&radius, Qt::LeftButton, {}, knob(radius));
    QCOMPARE(group.raw().detail.sharpenRadius, 10.0);
    auto &detail = group.child<CameraRawSlider>(QStringLiteral("noiseLuminanceDetailSlider"));
    QVERIFY(!detail.isEnabled());
    group.child<CameraRawSlider>(QStringLiteral("noiseLuminanceSlider")).setValue(400);
    QVERIFY(detail.isEnabled() && !group.child<CameraRawSlider>(QStringLiteral("noiseColorDetailSlider")).isEnabled());
    // Alt on Masking shows the mask; other rows clear it.
    QWindow *window = group.controls->windowHandle();
    QTest::keyPress(window, Qt::Key_Alt, Qt::AltModifier);
    group.child<CameraRawSlider>(QStringLiteral("sharpenMaskingSlider")).setValue(500);
    QVERIFY(group.panel().sharpenMask && group.raw().detail.sharpenMasking == 50);
    amount.setValue(400);
    QVERIFY(!group.panel().sharpenMask);
    QTest::keyRelease(window, Qt::Key_Alt);
    group.child<CameraRawSlider>(QStringLiteral("sharpenMaskingSlider")).setValue(600);
    QVERIFY(!group.panel().sharpenMask);
}

void CameraRawGradingLensTests::opticsTogglesProfileDefringeAndHueRanges()
{
    Group<CameraRawOpticsControls> group;
    auto &profile = group.child<QWidget>(QStringLiteral("profileDistortionSlider"));
    QVERIFY(!profile.isVisible());
    group.child<QCheckBox>(QStringLiteral("enableLensProfile")).click();
    QVERIFY(group.raw().optics.enableLensProfile && profile.isVisible());
    group.child<QCheckBox>(QStringLiteral("removeChromaticAberration")).click();
    QVERIFY(group.raw().optics.removeChromaticAberration);
    // A range from zero rounds; a signed range does not.
    group.child<CameraRawSlider>(QStringLiteral("purpleAmountSlider")).setValue(333);
    QCOMPARE(group.raw().optics.purpleAmount, 33.0);
    group.child<CameraRawSlider>(QStringLiteral("distortionSlider")).setValue(333);
    QVERIFY(std::abs(group.raw().optics.distortion - -33.4) < 1e-9);
    auto &midpoint = group.child<CameraRawSlider>(QStringLiteral("lensMidpointSlider"));
    midpoint.setValue(0);
    QTest::mouseDClick(&midpoint, Qt::LeftButton, {}, knob(midpoint));
    QCOMPARE(group.raw().optics.vignetteMidpoint, 50.0);
    // Hue sliders round, and reset to their own ends.
    auto &low = group.child<CameraRawSlider>(QStringLiteral("greenHueLow"));
    low.setValue(111);
    QCOMPARE(group.raw().optics.greenHueLow, 40.0);
    QTest::mouseDClick(&low, Qt::LeftButton, {}, knob(low));
    QCOMPARE(group.raw().optics.greenHueLow, 60.0);
    auto &high = group.child<CameraRawSlider>(QStringLiteral("purpleHueHigh"));
    QTest::mouseDClick(&high, Qt::LeftButton, {}, knob(high));
    QCOMPARE(group.raw().optics.purpleHueHigh, 310.0);
    auto &hint = group.child<QLabel>(QStringLiteral("defringeHint"));
    QVERIFY(!hint.isVisible());
    group.child<QToolButton>(QStringLiteral("defringeSampler")).click();
    QVERIFY(group.panel().samplesDefringe && hint.isVisible());
}

void CameraRawGradingLensTests::geometryUprightGuidesAndRoundedRows()
{
    Group<CameraRawGeometryControls> group;
    auto &draw = group.child<QPushButton>(QStringLiteral("drawGuides"));
    QVERIFY(!draw.isVisible());
    group.child<QToolButton>(QStringLiteral("upright1")).click();
    QVERIFY(group.raw().geometry.upright == CameraRawUprightMode::guided && draw.isVisible());
    QVERIFY(!group.child<QPushButton>(QStringLiteral("clearGuides")).isVisible());
    const QImage line = LayerIcons::pixmap(LayerIcon::lineDiagonal, 14, draw.palette().color(QPalette::ButtonText), draw.devicePixelRatioF()).toImage();
    QVERIFY(draw.icon().pixmap(QSize(14, 14), draw.devicePixelRatioF()).toImage() == line);
    draw.click();
    QVERIFY(group.panel().drawingGeometryGuide && group.child<QLabel>(QStringLiteral("drawGuidesHint")).isVisible());
    group.set([](CameraRawSettings &settings) { settings.geometry.guides = {{0.1, 0.5, 0.9, 0.5}}; });
    group.child<QPushButton>(QStringLiteral("clearGuides")).click();
    QVERIFY(group.raw().geometry.guides.empty());
    // Off puts the guide tool away.
    group.child<QToolButton>(QStringLiteral("upright0")).click();
    QVERIFY(group.raw().geometry.upright == CameraRawUprightMode::off && !group.panel().drawingGeometryGuide);
    emit group.child<QComboBox>(QStringLiteral("projection")).activated(1);
    QVERIFY(group.raw().geometry.projection == CameraRawProjection::rectilinear);
    group.child<CameraRawSlider>(QStringLiteral("rotateSlider")).setValue(333);
    QCOMPARE(group.raw().geometry.rotate, -15.0);
    group.child<CameraRawSlider>(QStringLiteral("offsetYSlider")).setValue(333);
    QCOMPARE(group.raw().geometry.offsetY, -33.0);
    group.child<QCheckBox>(QStringLiteral("constrainCrop")).click();
    QVERIFY(group.raw().geometry.constrainCrop);
}

void CameraRawGradingLensTests::calibrationChoosesItsProcessAndPrimaries()
{
    Group<CameraRawCalibrationControls> group;
    auto &process = group.child<QComboBox>(QStringLiteral("processVersion"));
    QCOMPARE(process.currentText(), QString("Version 6"));
    QCOMPARE(group.child<QLabel>(QStringLiteral("processSummary")).text(), summary(CameraRawProcessVersion::version6));
    emit process.activated(1);
    QVERIFY(group.raw().calibration.process == CameraRawProcessVersion::version2);
    QCOMPARE(group.child<QLabel>(QStringLiteral("processSummary")).text(), summary(CameraRawProcessVersion::version2));
    for (const auto &[name, member] : {std::pair("shadowTint", &CameraRawCalibrationSettings::shadowTint), std::pair("redHue", &CameraRawCalibrationSettings::redHue),
                                       std::pair("redSaturation", &CameraRawCalibrationSettings::redSaturation),
                                       std::pair("greenHue", &CameraRawCalibrationSettings::greenHue),
                                       std::pair("greenSaturation", &CameraRawCalibrationSettings::greenSaturation),
                                       std::pair("blueHue", &CameraRawCalibrationSettings::blueHue),
                                       std::pair("blueSaturation", &CameraRawCalibrationSettings::blueSaturation)}) {
        auto &slider = group.child<CameraRawSlider>(QString::fromLatin1(name) + QStringLiteral("Slider"));
        slider.setValue(666);
        QVERIFY2(group.raw().calibration.*member == 33, name);
        QTest::mouseDClick(&slider, Qt::LeftButton, {}, knob(slider));
        QVERIFY2(group.raw().calibration.*member == 0, name);
    }
}

QTEST_MAIN(CameraRawGradingLensTests)
#include "CameraRawGradingLensTests.moc"
