#include "LevelsFixtures.h"
#include <QtTest>
#include <cmath>

// Levels' rules to the value: ranges, the scale, auto, sampling.
class LevelsModelTests : public QObject {
    Q_OBJECT
private slots:
    void rangesClampIntoRangeAndResetWhatIsNoNumber();
    void applyClampsTheInputBetweenBlackAndWhite();
    void settingsStoreAndCompareNormalized();
    void theScaleReadsTheSortedFiniteInterior();
    void autoCutsAThousandthAtEitherEnd();
    void aSingleToneChannelKeepsItsRange();
    void contrastSpansTheWidestChannel();
    void neutralCentresTheMeanWithinItsFloor();
    void samplingCalibratesEachChannelAndResetsTheRest();
    void histogramRowsKeepTheirOwnCoverage();
};

void LevelsModelTests::rangesClampIntoRangeAndResetWhatIsNoNumber()
{
    const LevelRange high = LevelRange{300, 20, 400, 300, 300}.normalized();
    QVERIFY(high.black == 254 && high.white == 255 && high.gamma == 9.99 && high.outputBlack == 255 && high.outputWhite == 255);
    const LevelRange low = LevelRange{-5, 0.05, 100, -1, -1}.normalized();
    QVERIFY(low.black == 0 && low.gamma == 0.1 && low.outputBlack == 0 && low.outputWhite == 0);
    // White stays a step above black.
    QCOMPARE((LevelRange{100, 1, 50, 0, 255}.normalized().white), 101.0);
    // What is no number takes its default, infinity included.
    QVERIFY((LevelRange{INFINITY, NAN, NAN, NAN, -INFINITY}.normalized()) == LevelRange());
}

void LevelsModelTests::applyClampsTheInputBetweenBlackAndWhite()
{
    // Past white is output white; under black, output black.
    QCOMPARE((LevelRange{0, 1, 128, 0, 200}.apply(1)), 200.0 / 255);
    QCOMPARE((LevelRange{128, 1, 255, 50, 255}.apply(0)), 50.0 / 255);
    QCOMPARE((LevelRange{128, 1, 255, 50, 255}.apply(1)), 1.0);
    // Gamma 2 lifts a quarter to a half.
    QVERIFY(std::abs(LevelRange{0, 2, 255, 0, 255}.apply(0.25) - 0.5) < 1e-12);
    // It applies itself normalized: black 300 is 254.
    QCOMPARE((LevelRange{300, 1, 255, 0, 255}.apply(254.0 / 255)), 0.0);
}

void LevelsModelTests::settingsStoreAndCompareNormalized()
{
    LevelsSettings settings;
    settings.channel = LevelsChannel::green;
    settings.setCurrent(LevelRange{300, 1, 255, 0, 255});
    QCOMPARE(settings.ranges[2].black, 254.0);
    QCOMPARE(settings.current().black, 254.0);
    // A raw range that normalizes to the identity is one.
    LevelsSettings raw;
    raw.ranges[1] = LevelRange{-5, NAN, 400, -1, 300};
    QVERIFY(raw.isIdentity());
}

void LevelsModelTests::theScaleReadsTheSortedFiniteInterior()
{
    // An infinite bin counts for nothing.
    std::array<double, 256> spiked;
    spiked.fill(100);
    spiked[128] = INFINITY;
    QCOMPARE(LevelsHistogramDisplay::scale(spiked), 100.0);
    // The ends stay out of the typical peak.
    std::array<double, 256> first{}, last{};
    first[0] = last[255] = 1;
    first[128] = last[128] = 100;
    QCOMPARE(LevelsHistogramDisplay::scale(first), 100.0);
    QCOMPARE(LevelsHistogramDisplay::scale(last), 100.0);
    // Falling bins: the sorted 95th percentile, 241, times four.
    std::array<double, 256> falling{};
    for (size_t index = 1; index < 255; ++index)
        falling[index] = double(255 - index);
    falling[0] = 1e6;
    QCOMPARE(LevelsHistogramDisplay::scale(falling), 964.0);
}

void LevelsModelTests::autoCutsAThousandthAtEitherEnd()
{
    LevelsHistogram tails{};
    for (size_t channel = 1; channel < 4; ++channel) {
        tails[channel][5] = 0.5;
        tails[channel][100] = 99;
        tails[channel][250] = 0.5;
    }
    const LevelsSettings contrast = settings(LevelsAuto::contrast, tails);
    QVERIFY(contrast.ranges[0].black == 5 && contrast.ranges[0].white == 250);
    const LevelsSettings colour = settings(LevelsAuto::color, tails);
    QVERIFY(colour.ranges[3].black == 5 && colour.ranges[3].white == 250);
}

void LevelsModelTests::aSingleToneChannelKeepsItsRange()
{
    LevelsHistogram single{};
    for (size_t channel = 1; channel < 4; ++channel)
        single[channel][128] = 100;
    for (const LevelsAuto mode : allLevelsAutos)
        QVERIFY(settings(mode, single).isIdentity());
}

void LevelsModelTests::contrastSpansTheWidestChannel()
{
    LevelsHistogram spread{};
    spread[1][30] = spread[1][240] = 100;
    spread[2][40] = spread[2][200] = 100;
    spread[3][50] = spread[3][210] = 100;
    const LevelsSettings contrast = settings(LevelsAuto::contrast, spread);
    QVERIFY(contrast.ranges[0].black == 30 && contrast.ranges[0].white == 240);
    QVERIFY(contrast.ranges[1] == LevelRange() && contrast.ranges[3] == LevelRange());
}

void LevelsModelTests::neutralCentresTheMeanWithinItsFloor()
{
    // A quarter of the weight at white: gamma 2 halves.
    LevelsHistogram quarter{};
    for (size_t channel = 1; channel < 4; ++channel) {
        quarter[channel][0] = 75;
        quarter[channel][255] = 25;
    }
    QVERIFY(std::abs(settings(LevelsAuto::neutral, quarter).ranges[1].gamma - 2) < 1e-12);
    QCOMPARE(settings(LevelsAuto::color, quarter).ranges[1].gamma, 1.0);
    // A mean near white stops at gamma 0.1.
    LevelsHistogram bright{};
    for (size_t channel = 1; channel < 4; ++channel) {
        bright[channel][10] = 0.2;
        bright[channel][250] = 99.8;
    }
    QCOMPARE(settings(LevelsAuto::neutral, bright).ranges[2].gamma, 0.1);
}

void LevelsModelTests::samplingCalibratesEachChannelAndResetsTheRest()
{
    LevelsSettings settings;
    settings.ranges[0] = LevelRange{20, 1.5, 230, 10, 240};
    settings.ranges[1] = LevelRange{0, 1, 100, 10, 240};
    settings.ranges[2] = LevelRange{150, 1, 255, 0, 255};
    settings.ranges[3] = LevelRange{0, 1, 255, 10, 255};
    // Black past a channel's white stops a step below it.
    const LevelsSettings black = settings.sampling({200.0 / 255, 0.2, 0.2}, LevelsSample::black);
    QCOMPARE(black.ranges[0], LevelRange());
    QVERIFY(black.ranges[1].black == 99 && black.ranges[1].white == 100);
    QVERIFY(black.ranges[1].outputBlack == 0 && black.ranges[1].outputWhite == 255);
    // White under a channel's black stops a step above it.
    QCOMPARE(settings.sampling({0.2, 100.0 / 255, 0.2}, LevelsSample::white).ranges[2].white, 151.0);
    // Gray at or past white leaves that channel whole.
    const LevelsSettings gray = settings.sampling({1, 0.5, 0.5}, LevelsSample::gray);
    QCOMPARE(gray.ranges[1], settings.ranges[1]);
    QCOMPARE(gray.ranges[0], LevelRange());
    // Gray just under white is stored normalized: gamma 0.1.
    QCOMPARE(settings.sampling({0.5, 0.5, 254.0 / 255}, LevelsSample::gray).ranges[3].gamma, 0.1);
}

void LevelsModelTests::histogramRowsKeepTheirOwnCoverage()
{
    // Three wide: mask rows are padded, pixel rows not.
    QImage source(3, 2, QImage::Format_RGBA8888_Premultiplied);
    for (int x = 0; x < 3; ++x) {
        source.setPixelColor(x, 0, Qt::red);
        source.setPixelColor(x, 1, Qt::green);
    }
    QPainterPath lower;
    lower.addRect(0, 1, 3, 1);
    const LevelsHistogram bins = LevelsFilter::histogram(job(source, {}, DocumentSelection{lower, false}.clip(QSizeF(3, 2))));
    QVERIFY(bins[1][255] == 0 && bins[1][0] == 3 && bins[2][255] == 3 && bins[3][0] == 3);
    QVERIFY(std::abs(bins[0][0] - 2) < 1e-12 && std::abs(bins[0][255] - 1) < 1e-12);
}

QTEST_GUILESS_MAIN(LevelsModelTests)
#include "LevelsModelTests.moc"
