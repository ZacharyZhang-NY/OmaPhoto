#include "UI/CameraRawSlider.h"
#include "UI/SliderSnap.h"
#include <QStyleOptionSlider>
#include <QtTest>

// Swift's CameraRawSliderTests.
namespace {
// A two-colour track's ends.
std::pair<QColor, QColor> ends(const CameraRawSliderTrack &track)
{
    const std::vector<QColor> colors = track.colors().value();
    if (colors.size() != 2)
        throw std::runtime_error("not a two-colour track");
    return {colors[0], colors[1]};
}

struct Probe {
    double changed = std::nan("");
    int resets = 0;
    CameraRawSlider slider;
    explicit Probe(CameraRawSliderTrack track = {})
        : slider(-100, 100, track, QStringLiteral("Help"), [this](double value) { changed = value; }, [this] { ++resets; })
    {
        slider.resize(200, 24);
        slider.show();
    }
    QRect knob() const
    {
        QStyleOptionSlider option;
        option.initFrom(&slider);
        option.orientation = Qt::Horizontal;
        option.minimum = slider.minimum();
        option.maximum = slider.maximum();
        option.sliderPosition = slider.sliderPosition();
        option.sliderValue = slider.value();
        return slider.style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, &slider);
    }
};
}

class CameraRawSliderTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { SliderSnap::install(); }
    void colorTracksRunFromTheCoolOrMutedEndToTheWarmOrStrongEnd();
    void temperatureBarIsBlueOnTheLeftAndADoubleClickHitsOnlyTheKnob();
    void trackClickValueMatchesTheClickedPosition();
    void aFamilysTracksCentreOnItsHue();
    void aDragKeepsItsKnob();
    void aReshapeLeavesADragAlone();
};

void CameraRawSliderTests::colorTracksRunFromTheCoolOrMutedEndToTheWarmOrStrongEnd()
{
    using Kind = CameraRawSliderTrack::Kind;
    const auto [cool, warm] = ends(CameraRawSliderTrack{Kind::temperature});
    QVERIFY(cool.blueF() > warm.blueF());
    const auto [green, mauve] = ends(CameraRawSliderTrack{Kind::tint});
    QVERIFY(green.greenF() > mauve.greenF());
    const auto [gray, red] = ends(CameraRawSliderTrack{Kind::chroma});
    QVERIFY(std::abs(gray.redF() - gray.greenF()) < 0.05);
    QVERIFY(red.redF() > red.greenF() + 0.4);
    QVERIFY(!CameraRawSliderTrack{Kind::plain}.colors());
}

void CameraRawSliderTests::temperatureBarIsBlueOnTheLeftAndADoubleClickHitsOnlyTheKnob()
{
    Probe probe({CameraRawSliderTrack::Kind::temperature});
    probe.slider.display(0);
    const QImage drawn = probe.slider.grab().toImage();
    const QColor left = drawn.pixelColor(8, drawn.height() / 2), right = drawn.pixelColor(drawn.width() - 8, drawn.height() / 2);
    QVERIFY2(left.blue() > left.red(), qPrintable(left.name()));
    QVERIFY2(right.red() + right.green() > right.blue() + 100, qPrintable(right.name()));
    probe.slider.display(40);
    const QRect knob = probe.knob();
    QVERIFY(probe.slider.isOnKnob(knob.center()));
    QVERIFY(!probe.slider.isOnKnob(QPoint(2, knob.center().y())));
    // Swift's two points round the knob count as the knob.
    QVERIFY(probe.slider.isOnKnob(QPoint(knob.right() + 2, knob.center().y())));
    QVERIFY(!probe.slider.isOnKnob(QPoint(knob.right() + 4, knob.center().y())));
    // A double click resets on the knob, not the track.
    QTest::mouseDClick(&probe.slider, Qt::LeftButton, {}, knob.center());
    QCOMPARE(probe.resets, 1);
    QTest::mouseDClick(&probe.slider, Qt::LeftButton, {}, QPoint(2, knob.center().y()));
    QCOMPARE(probe.resets, 1);
}

void CameraRawSliderTests::trackClickValueMatchesTheClickedPosition()
{
    for (const auto &[x, expected] : {std::pair(0, -100.0), std::pair(100, 0.0), std::pair(199, 100.0)}) {
        Probe probe;
        probe.slider.display(x < 100 ? 50 : -50);
        QTest::mousePress(&probe.slider, Qt::LeftButton, {}, QPoint(x, 12));
        QVERIFY2(std::abs(probe.changed - expected) <= 2, qPrintable(QString::number(probe.changed)));
        QTest::mouseRelease(&probe.slider, Qt::LeftButton, {}, QPoint(x, 12));
    }
}

void CameraRawSliderTests::aFamilysTracksCentreOnItsHue()
{
    using Kind = CameraRawSliderTrack::Kind;
    // Hue runs 50 degrees either side; red wraps below zero.
    const auto [below, above] = ends(CameraRawSliderTrack{Kind::hue, 0});
    QVERIFY(std::abs(below.hueF() * 360 - 310) < 1 && std::abs(above.hueF() * 360 - 50) < 1);
    const auto [muted, strong] = ends(CameraRawSliderTrack{Kind::saturation, 240});
    QVERIFY(muted.hsvSaturationF() < 0.05 && std::abs(strong.hueF() * 360 - 240) < 1);
    const auto [dark, light] = ends(CameraRawSliderTrack{Kind::luminance, 120});
    QVERIFY(dark.valueF() < 0.2 && light.valueF() > 0.9 && std::abs(dark.hueF() * 360 - 120) < 1);
}

void CameraRawSliderTests::aDragKeepsItsKnob()
{
    Probe probe;
    probe.slider.display(20);
    QCOMPARE(probe.slider.shown(), 20.0);
    probe.slider.setSliderDown(true);
    probe.slider.display(-60);
    QCOMPARE(probe.slider.shown(), 20.0);
    probe.slider.setSliderDown(false);
    probe.slider.display(-60);
    QCOMPARE(probe.slider.shown(), -60.0);
    // Shown values write nothing back.
    QVERIFY(std::isnan(probe.changed));
}

void CameraRawSliderTests::aReshapeLeavesADragAlone()
{
    using Kind = CameraRawSliderTrack::Kind;
    Probe probe;
    probe.slider.display(20);
    probe.slider.setSliderDown(true);
    // Mid-drag: the new track shows, the knob and value stay.
    probe.slider.reshape(-50, 50, CameraRawSliderTrack{Kind::spectrum, 90}, 30);
    QVERIFY(probe.slider.isSliderDown());
    QCOMPARE(probe.slider.value(), 600);
    QCOMPARE(probe.slider.track().kind, Kind::spectrum);
    QVERIFY(std::isnan(probe.changed));
    probe.slider.setSliderDown(false);
    // Let go, new bounds place the value anew.
    probe.slider.reshape(0, 100, CameraRawSliderTrack{Kind::chroma}, 25);
    QCOMPARE(probe.slider.value(), 250);
    QCOMPARE(probe.slider.shown(), 25.0);
}

QTEST_MAIN(CameraRawSliderTests)
#include "CameraRawSliderTests.moc"
