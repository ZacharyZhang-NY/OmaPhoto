#include "UI/SliderSnap.h"
#include <QProxyStyle>
#include <QSlider>
#include <QStyle>
#include <QStyleFactory>
#include <QtTest>

// Clicking the track must snap the knob to the click.
class SliderSnapTests : public QObject {
    Q_OBJECT
private slots:
    void clickingTheTrackSnapsTheKnobAndStillEditsTheValue();
};

namespace {
// Qt's own absolute set: the knob centred under the press.
class Absolute : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;
    int styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget, QStyleHintReturn *returned) const override
    {
        return hint == SH_Slider_AbsoluteSetButtons ? int(Qt::LeftButton) : QProxyStyle::styleHint(hint, option, widget, returned);
    }
};

struct Probe {
    QSlider slider;
    QStringList log;
    explicit Probe(Qt::Orientation orientation = Qt::Horizontal)
        : slider(orientation)
    {
        slider.setRange(0, 100);
        slider.setValue(10);
        slider.resize(orientation == Qt::Horizontal ? QSize(200, 24) : QSize(24, 200));
        slider.show();
        QObject::connect(&slider, &QSlider::sliderPressed, [this] { log << "began"; });
        QObject::connect(&slider, &QSlider::valueChanged, [this] { log << "set"; });
        QObject::connect(&slider, &QSlider::sliderReleased, [this] { log << "ended"; });
    }
    void click(Qt::MouseButton button, QPoint at)
    {
        QTest::mousePress(&slider, button, Qt::NoModifier, at);
        QTest::mouseRelease(&slider, button, Qt::NoModifier, at);
    }
};
}

void SliderSnapTests::clickingTheTrackSnapsTheKnobAndStillEditsTheValue()
{
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    {
        // Qt alone steps a page towards the press.
        Probe plain;
        plain.click(Qt::LeftButton, QPoint(180, 12));
        QCOMPARE(plain.slider.value(), 20);
    }
    // Qt's own centring is the oracle for the landing.
    std::vector<int> centred;
    for (const int x : {30, 180, 30}) {
        Probe oracle;
        oracle.slider.setInvertedAppearance(centred.size() == 2);
        oracle.slider.setStyle(new Absolute(QStyleFactory::create(QStringLiteral("Fusion"))));
        oracle.click(Qt::LeftButton, QPoint(x, 12));
        centred.push_back(oracle.slider.value());
    }
    SliderSnap::install();
    Probe probe;
    QTest::mousePress(&probe.slider, Qt::LeftButton, Qt::NoModifier, QPoint(180, 12));
    // Editing begins, then the value lands under the press.
    QCOMPARE(probe.slider.value(), centred.at(1));
    QVERIFY(centred.at(1) > 85 && centred.at(1) < 100);
    QVERIFY(probe.slider.isSliderDown());
    QCOMPARE(probe.log, (QStringList{"began", "set"}));
    QTest::mouseMove(&probe.slider, QPoint(100, 12));
    QVERIFY(probe.slider.value() > 40 && probe.slider.value() < 60);
    QTest::mouseRelease(&probe.slider, Qt::LeftButton, Qt::NoModifier, QPoint(100, 12));
    QCOMPARE(probe.log.last(), QString("ended"));
    // The knob drags from where it is; others snap nothing.
    const int kept = probe.slider.value();
    probe.click(Qt::LeftButton, QPoint(103, 12));
    QCOMPARE(probe.slider.value(), kept);
    probe.click(Qt::RightButton, QPoint(10, 12));
    QCOMPARE(probe.slider.value(), kept);
    Probe low;
    low.slider.setValue(90);
    low.click(Qt::LeftButton, QPoint(30, 12));
    QCOMPARE(low.slider.value(), centred.at(0));
    Probe inverted;
    inverted.slider.setInvertedAppearance(true);
    inverted.click(Qt::LeftButton, QPoint(30, 12));
    QCOMPARE(inverted.slider.value(), centred.at(2));
    QVERIFY(centred.at(2) > 85);
    // An empty range takes no snap; nothing stays held.
    Probe empty;
    empty.slider.setRange(5, 5);
    empty.log.clear();
    empty.click(Qt::LeftButton, QPoint(180, 12));
    QVERIFY(!empty.slider.isSliderDown() && !empty.log.contains("began"));
    // Narrower than its knob, a slider has nowhere to snap.
    Probe narrow;
    narrow.slider.resize(10, 40);
    narrow.slider.setValue(50);
    narrow.log.clear();
    narrow.click(Qt::LeftButton, QPoint(5, 1));
    QVERIFY(!narrow.slider.isSliderDown() && !narrow.log.contains("began"));
    // An owner that rounds may move the knob; drags hold.
    Probe rounded;
    QObject::connect(&rounded.slider, &QSlider::valueChanged, [&rounded](int value) {
        const QSignalBlocker quiet(&rounded.slider);
        rounded.slider.setValue(value < 25 ? 0 : value < 75 ? 50 : 100);
    });
    QTest::mousePress(&rounded.slider, Qt::LeftButton, Qt::NoModifier, QPoint(130, 12));
    QVERIFY(rounded.slider.value() == 50 && rounded.slider.isSliderDown());
    QTest::mouseMove(&rounded.slider, QPoint(195, 12));
    QCOMPARE(rounded.slider.value(), 100);
    QTest::mouseRelease(&rounded.slider, Qt::LeftButton, Qt::NoModifier, QPoint(195, 12));
    QVERIFY(!rounded.slider.isSliderDown() && rounded.log.last() == QString("ended"));
    // A press with another button held takes no snap.
    Probe held;
    QTest::mousePress(&held.slider, Qt::RightButton, Qt::NoModifier, QPoint(10, 12));
    QTest::mousePress(&held.slider, Qt::LeftButton, Qt::NoModifier, QPoint(180, 12));
    QCOMPARE(held.slider.value(), 10);
    QTest::mouseRelease(&held.slider, Qt::LeftButton, Qt::NoModifier, QPoint(180, 12));
    QTest::mouseRelease(&held.slider, Qt::RightButton, Qt::NoModifier, QPoint(10, 12));
    QVERIFY(!held.slider.isSliderDown());
    probe.slider.setEnabled(false);
    probe.click(Qt::LeftButton, QPoint(10, 12));
    QCOMPARE(probe.slider.value(), kept);
    Probe upright(Qt::Vertical);
    upright.click(Qt::LeftButton, QPoint(12, 20));
    QCOMPARE(upright.slider.value(), 20);
}

QTEST_MAIN(SliderSnapTests)
#include "SliderSnapTests.moc"
