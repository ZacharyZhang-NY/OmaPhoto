#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include "UI/CameraRawColorControls.h"
#include "UI/LayerIcons.h"
#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QStyleOptionSlider>
#include <QtTest>

// Swift's Curve and Color Mixer groups.
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
        controls->resize(374, 600);
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

void press(QWidget &widget, QPoint at)
{
    QTest::mousePress(&widget, Qt::LeftButton, {}, at);
}

void drag(QWidget &widget, QPoint to)
{
    QMouseEvent move(QEvent::MouseMove, to, widget.mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &move);
}
}

class CameraRawCurveMixerTests : public QObject {
    Q_OBJECT
private slots:
    void theParametricPageMovesRegionsAndDividers();
    void thePointPageAddsMovesAndRemovesPoints();
    void presetsAndRefineSaturation();
    void targetedAdjustmentArmsOneAtATime();
    void hslRowsWriteTheirTabsFamilies();
    void theColorPageEditsTheChosenFamily();
    void pointColorsArePickedChosenAndShifted();
};

void CameraRawCurveMixerTests::theParametricPageMovesRegionsAndDividers()
{
    Group<CameraRawCurveControls> group;
    QVERIFY(group.child<QToolButton>(QStringLiteral("curvePage0")).isChecked() && !group.child<QWidget>(QStringLiteral("curveSelectedPoint")).isVisible());
    for (const auto &[name, member] : {std::pair("curveHighlights", &CameraRawCurveSettings::highlights), std::pair("curveLights", &CameraRawCurveSettings::lights),
                                       std::pair("curveDarks", &CameraRawCurveSettings::darks), std::pair("curveShadows", &CameraRawCurveSettings::shadows)}) {
        auto &slider = group.child<CameraRawSlider>(QString::fromLatin1(name) + QStringLiteral("Slider"));
        slider.setValue(800);
        QCOMPARE(group.raw().curve.*member, 60.0);
        // A double click on the knob resets it.
        QTest::mouseDClick(&slider, Qt::LeftButton, {}, knob(slider));
        QCOMPARE(group.raw().curve.*member, 0.0);
    }
    // Along the bottom: the nearest divider, kept ordered.
    auto &graph = group.child<QWidget>(QStringLiteral("cameraRawCurveGraph"));
    const int h = graph.height();
    press(graph, QPoint(int(graph.width() * 0.3), h - 5));
    drag(graph, QPoint(int(graph.width() * 0.35), h - 5));
    QVERIFY(std::abs(group.raw().curve.shadowSplit - 35) < 1 && group.raw().curve.darkSplit == 50);
    drag(graph, QPoint(graph.width() + 40, 75));
    QCOMPARE(group.raw().curve.shadowSplit, 48.0);
    QTest::mouseRelease(&graph, Qt::LeftButton, {}, QPoint(graph.width() + 40, 75));
    // Above it a drag lifts the region under the press.
    press(graph, QPoint(int(graph.width() * 0.3), 75));
    drag(graph, QPoint(int(graph.width() * 0.9), 75 - h / 4));
    QCOMPARE(group.raw().curve.shadows, std::round(double(h / 4) / h * 200));
    QTest::mouseRelease(&graph, Qt::LeftButton, {}, QPoint(int(graph.width() * 0.9), 75 - h / 4));
    // A double click here leaves the point curve's points.
    group.set([](CameraRawSettings &settings) { settings.curve.rgb = {{0, 0}, {0.5, 0.5}, {1, 1}}; });
    QTest::mouseDClick(&graph, Qt::LeftButton, {}, QPoint(graph.width() / 2, graph.height() / 2));
    QCOMPARE(group.raw().curve.rgb.size(), size_t(3));
}

void CameraRawCurveMixerTests::thePointPageAddsMovesAndRemovesPoints()
{
    Group<CameraRawCurveControls> group;
    group.child<QToolButton>(QStringLiteral("curvePage1")).click();
    QVERIFY(group.panel().curvePage == CameraRawCurvePage::point);
    group.child<QToolButton>(QStringLiteral("pointChannel1")).click();
    QVERIFY(group.panel().pointChannel == CameraRawPointChannel::red);
    auto &graph = group.child<QWidget>(QStringLiteral("cameraRawCurveGraph"));
    const int w = graph.width(), h = graph.height();
    // A still click adds a point where it lands.
    QTest::mouseClick(&graph, Qt::LeftButton, {}, QPoint(w / 2, h / 4));
    QCOMPARE(group.raw().curve.red.size(), size_t(3));
    QVERIFY(std::abs(group.raw().curve.red[1].x - 0.5) < 0.01 && std::abs(group.raw().curve.red[1].y - 0.75) < 0.01);
    QVERIFY(group.raw().curve.rgb == CameraRawCurveSettings::linear());
    QCOMPARE(group.child<QLabel>(QStringLiteral("curveSelectedPoint")).text(), QString("In %1   Out %2").arg(std::lround(group.raw().curve.red[1].x * 255))
                                                                                   .arg(std::lround(group.raw().curve.red[1].y * 255)));
    // A drag moves the nearest inner point.
    press(graph, QPoint(w / 2, h / 4));
    drag(graph, QPoint(int(w * 0.6), h / 2));
    QTest::mouseRelease(&graph, Qt::LeftButton, {}, QPoint(int(w * 0.6), h / 2));
    QCOMPARE(group.raw().curve.red.size(), size_t(3));
    QVERIFY(std::abs(group.raw().curve.red[1].x - 0.6) < 0.01 && std::abs(group.raw().curve.red[1].y - 0.5) < 0.01);
    // A double click far off removes nothing; near, it does.
    QTest::mouseDClick(&graph, Qt::LeftButton, {}, QPoint(int(w * 0.1), h / 2));
    QCOMPARE(group.raw().curve.red.size(), size_t(3));
    const size_t before = group.raw().curve.red.size();
    QTest::mouseDClick(&graph, Qt::LeftButton, {}, QPoint(int(w * 0.6), h / 2));
    QCOMPARE(group.raw().curve.red.size(), before - 1);
}

void CameraRawCurveMixerTests::presetsAndRefineSaturation()
{
    Group<CameraRawCurveControls> group;
    group.child<QToolButton>(QStringLiteral("curvePage1")).click();
    auto &preset = group.child<QComboBox>(QStringLiteral("curvePreset"));
    QCOMPARE(preset.currentText(), QString("Linear"));
    emit preset.activated(3);
    QVERIFY(group.raw().curve.rgb == CameraRawCurveSettings::strongContrast());
    QCOMPARE(preset.currentText(), QString("Strong Contrast"));
    emit preset.activated(2);
    QVERIFY(group.raw().curve.rgb == CameraRawCurveSettings::mediumContrast());
    emit preset.activated(1);
    QVERIFY(group.raw().curve.rgb == CameraRawCurveSettings::linear());
    group.set([](CameraRawSettings &settings) { settings.curve.rgb = {{0, 0}, {0.4, 0.3}, {1, 1}}; });
    QCOMPARE(preset.currentText(), QString("Custom"));
    // Custom changes nothing; Refine Saturation shows for RGB alone.
    emit preset.activated(0);
    QCOMPARE(group.raw().curve.rgb.size(), size_t(3));
    auto &refine = group.child<CameraRawSlider>(QStringLiteral("refineSaturationSlider"));
    QVERIFY(refine.isVisible());
    refine.setValue(750);
    QCOMPARE(group.raw().curve.refineSaturation, 50.0);
    group.child<QToolButton>(QStringLiteral("pointChannel2")).click();
    QVERIFY(!refine.isVisible());
}

void CameraRawCurveMixerTests::targetedAdjustmentArmsOneAtATime()
{
    Group<CameraRawCurveControls> curve;
    CameraRawMixerControls mixer(curve.session);
    auto &curveTarget = curve.child<QPushButton>(QStringLiteral("curveTargeted"));
    auto *mixerTarget = mixer.findChild<QPushButton *>(QStringLiteral("mixerTargeted"));
    curveTarget.click();
    QVERIFY(curve.panel().targetsCurve && curveTarget.isChecked());
    mixerTarget->click();
    QVERIFY(curve.panel().targetsMixer && !curve.panel().targetsCurve && !curveTarget.isChecked() && mixerTarget->isChecked());
    mixerTarget->click();
    QVERIFY(!curve.panel().targetsMixer);
    mixerTarget->click();
    curveTarget.click();
    QVERIFY(curve.panel().targetsCurve && !curve.panel().targetsMixer);
    // Both carry Swift's scope glyph.
    const qreal ratio = curveTarget.devicePixelRatioF();
    const QImage scope = LayerIcons::pixmap(LayerIcon::scope, 14, curveTarget.palette().color(QPalette::ButtonText), ratio).toImage();
    QVERIFY(curveTarget.icon().pixmap(QSize(14, 14), ratio).toImage() == scope && mixerTarget->icon().pixmap(QSize(14, 14), ratio).toImage() == scope);
}

void CameraRawCurveMixerTests::hslRowsWriteTheirTabsFamilies()
{
    Group<CameraRawMixerControls> group;
    QVERIFY(group.child<QToolButton>(QStringLiteral("mixerPage0")).isChecked());
    for (int tab = 0; tab < 3; ++tab) {
        group.child<QToolButton>(QStringLiteral("mixerTab%1").arg(tab)).click();
        for (int index = 0; index < 8; ++index) {
            const QString name = CameraRawMixerSettings::names.at(size_t(index));
            auto &slider = group.child<CameraRawSlider>(QStringLiteral("family") + name + QStringLiteral("Slider"));
            QCOMPARE(slider.toolTip(), QStringLiteral("%1 of %2.").arg(tab == 0 ? "Hue" : tab == 1 ? "Saturation" : "Luminance", name));
            slider.setValue(600 + index * 10);
            const std::array<double, 8> &family = tab == 0 ? group.raw().mixer.hue : tab == 1 ? group.raw().mixer.saturation : group.raw().mixer.luminance;
            QVERIFY(std::abs(family.at(size_t(index)) - (20 + index * 2)) < 1e-9);
        }
    }
    // Each tab's rows show its own track: saturation starts gray.
    const QImage drawn = group.child<CameraRawSlider>(QStringLiteral("familyGreensSlider")).grab().toImage();
    group.child<QToolButton>(QStringLiteral("mixerTab1")).click();
    const QImage gray = group.child<CameraRawSlider>(QStringLiteral("familyGreensSlider")).grab().toImage();
    QVERIFY(drawn != gray);
    const QColor start = gray.pixelColor(10, gray.height() / 2);
    QVERIFY(std::abs(start.red() - start.green()) < 12);
}

void CameraRawCurveMixerTests::theColorPageEditsTheChosenFamily()
{
    Group<CameraRawMixerControls> group;
    group.child<QToolButton>(QStringLiteral("mixerPage1")).click();
    QVERIFY(group.panel().mixerPage == CameraRawMixerPage::color);
    group.child<QAbstractButton>(QStringLiteral("mixerSwatch5")).click();
    QCOMPARE(group.panel().mixerSwatch, 5);
    group.child<CameraRawSlider>(QStringLiteral("colorLuminanceSlider")).setValue(250);
    QCOMPARE(group.raw().mixer.luminance[5], -50.0);
    QCOMPARE(group.raw().mixer.luminance[4], 0.0);
    group.child<CameraRawSlider>(QStringLiteral("colorHueSlider")).setValue(750);
    QCOMPARE(group.raw().mixer.hue[5], 50.0);
    QVERIFY(!group.controls->findChild<QWidget *>(QStringLiteral("colorHueField")));
}

void CameraRawCurveMixerTests::pointColorsArePickedChosenAndShifted()
{
    Group<CameraRawMixerControls> group;
    group.child<QToolButton>(QStringLiteral("mixerPage2")).click();
    group.child<QToolButton>(QStringLiteral("pointColorSampler")).click();
    QVERIFY(group.panel().samplesPointColor);
    QVERIFY(!group.child<QWidget>(QStringLiteral("visualizeRange")).isVisible());
    group.set([](CameraRawSettings &settings) {
        settings.mixer.points = {CameraRawPointColor{.hue = 30, .saturation = 1, .luminance = 0.5},
                                 CameraRawPointColor{.hue = 200, .saturation = 0.5, .luminance = 0.5}};
    });
    QVERIFY(group.child<QAbstractButton>(QStringLiteral("pickedColor1")).isVisible());
    group.child<QAbstractButton>(QStringLiteral("pickedColor1")).click();
    QCOMPARE(group.panel().pointIndex, 1);
    group.child<CameraRawSlider>(QStringLiteral("hueShiftSlider")).setValue(750);
    QCOMPARE(group.raw().mixer.points[1].hueShift, 50.0);
    QCOMPARE(group.raw().mixer.points[0].hueShift, 0.0);
    auto &hueRange = group.child<CameraRawSlider>(QStringLiteral("hueRangeSlider"));
    hueRange.setValue(1000);
    QCOMPARE(group.raw().mixer.points[1].hueRange, 180.0);
    QTest::mouseDClick(&hueRange, Qt::LeftButton, {}, knob(hueRange));
    QCOMPARE(group.raw().mixer.points[1].hueRange, 30.0);
    auto &range = group.child<CameraRawSlider>(QStringLiteral("saturationRangeSlider"));
    QTest::mouseDClick(&range, Qt::LeftButton, {}, knob(range));
    group.child<CameraRawSlider>(QStringLiteral("saturationRangeSlider")).setValue(0);
    QCOMPARE(group.raw().mixer.points[1].saturationRange, 0.05);
    group.child<QCheckBox>(QStringLiteral("visualizeRange")).click();
    QVERIFY(group.raw().mixer.points[1].visualize && !group.raw().mixer.points[0].visualize);
    group.child<QCheckBox>(QStringLiteral("visualizeRange")).click();
    QVERIFY(!group.raw().mixer.points[1].visualize);
}

QTEST_MAIN(CameraRawCurveMixerTests)
#include "CameraRawCurveMixerTests.moc"
