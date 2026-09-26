#include "LevelsFixtures.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectStore.h"
#include <QTemporaryDir>
#include <cmath>
#include <numeric>

// Swift's LevelsTests.
class LevelsTests : public QObject {
    Q_OBJECT
private slots:
    void identityAndChannelSelectionAreExactNoOps();
    void inputClippingGammaOutputInversionAndAlpha();
    void channelsCoexistAndUseDocumentedOrder();
    void histogramExcludesTransparencyAndWeightsSelection();
    void selectionPreviewCancelCommitUndoAndPersistence();
    void stalePreviewCannotReturnAfterOffOrReopen();
    void histogramDisplayKeepsDistributionVisibleBesideClippingSpikes();
    void autoAlgorithmsAndEyedropperCalibration();
    void eyedropperSamplesOriginalAndRejectsTransparentPixels();
};

void LevelsTests::identityAndChannelSelectionAreExactNoOps()
{
    const std::unique_ptr<EditorSession> session = rampSession();
    const std::optional<CanvasDocument> before = session->document();
    const QImage source = session->activeLayer().value().asset.value().image();
    QCOMPARE(LevelsFilter::run(job(source)).cacheKey(), source.cacheKey());
    const int count = session->history.undoCount();
    session->beginLevels();
    LevelsSettings settings;
    settings.channel = LevelsChannel::blue;
    session->updateLevels(settings, true);
    QVERIFY(!session->canEditLayers() && !session->canUseHistory() && !session->canStartProjectOperation());
    commit(*session);
    QVERIFY(session->document() == before && session->history.undoCount() == count && !session->levels());
}

void LevelsTests::inputClippingGammaOutputInversionAndAlpha()
{
    const QImage source = image(ramp);
    LevelsSettings settings;
    settings.setCurrent(LevelRange{64, 1, 128, 0, 255});
    const std::vector<uchar> clipped = bytes(LevelsFilter::run(job(source, settings)));
    QCOMPARE(std::vector<uchar>(clipped.begin(), clipped.begin() + 12), (std::vector<uchar>{0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255, 255}));
    settings.setCurrent(LevelRange{0, 2, 255, 0, 255});
    const std::vector<uchar> brightened = bytes(LevelsFilter::run(job(source, settings)));
    QVERIFY(std::abs(int(brightened[4]) - 128) <= 1);
    QVERIFY(std::abs(int(brightened[8]) - 181) <= 1);
    QVERIFY(brightened[19] == 128 && brightened[23] == 0);
    QVERIFY(brightened[16] <= 128 && brightened[17] <= 128);
    settings.setCurrent(LevelRange{0, 1, 255, 255, 0});
    const std::vector<uchar> inverted = bytes(LevelsFilter::run(job(source, settings)));
    QVERIFY(inverted[0] == 255 && inverted[12] == 0);
    // A soft edge inverts as its colour, alpha kept.
    QVERIFY(inverted[16] == 64 && inverted[17] == 96 && inverted[18] == 128 && inverted[19] == 128);
}

void LevelsTests::channelsCoexistAndUseDocumentedOrder()
{
    LevelsSettings settings;
    settings.channel = LevelsChannel::red;
    settings.setCurrent(LevelRange{0, 2, 255, 0, 255});
    settings.channel = LevelsChannel::rgb;
    settings.setCurrent(LevelRange{40, 1, 210, 0, 255});
    const std::vector<uchar> result = bytes(LevelsFilter::run(job(image({{64, 64, 64, 255}}), settings)));
    const double expectedRed = LevelRange{40, 1, 210, 0, 255}.apply(LevelRange{0, 2, 255, 0, 255}.apply(64.0 / 255));
    QVERIFY(std::abs(result[0] - expectedRed * 255) <= 1);
    QVERIFY(result[1] == result[2] && result[0] > result[1]);
    const LevelRange invalid = LevelRange{300, NAN, -1, -100, 400}.normalized();
    QVERIFY(invalid.black < invalid.white && invalid.gamma == 1 && invalid.outputBlack == 0 && invalid.outputWhite == 255);
}

void LevelsTests::histogramExcludesTransparencyAndWeightsSelection()
{
    const QImage source = image({{255, 0, 0, 255}, {0, 128, 0, 128}, {0, 0, 0, 0}});
    const LevelsHistogram bins = LevelsFilter::histogram(job(source));
    QVERIFY(bins[1][255] == 1 && std::abs(bins[2][255] - 128.0 / 255) < 0.00001);
    const double total = std::accumulate(bins[0].begin(), bins[0].end(), 0.0);
    QVERIFY(std::abs(total - (1.0 + 128.0 / 255.0)) < 0.00001);
    QPainterPath first;
    first.addRect(0, 0, 1, 1);
    const SelectionClip selection = DocumentSelection{first, false}.clip(QSizeF(3, 1));
    const LevelsHistogram selected = LevelsFilter::histogram(job(source, {}, selection));
    QVERIFY(selected[1][255] == 1 && selected[2][255] == 0);
    const LevelsHistogram empty = LevelsFilter::histogram(job(source, {}, SelectionClip{QRectF(), std::nullopt}));
    for (const std::array<double, 256> &channel : empty)
        QVERIFY(std::all_of(channel.begin(), channel.end(), [](double bin) { return bin == 0; }));
}

void LevelsTests::selectionPreviewCancelCommitUndoAndPersistence()
{
    const std::unique_ptr<EditorSession> session = rampSession();
    QPainterPath two;
    two.addRect(0, 0, 2, 1);
    session->applySelection(two, SelectionMode::replace, QStringLiteral("Select"));
    const std::optional<CanvasDocument> before = session->document();
    const int count = session->history.undoCount();
    session->beginLevels();
    QTRY_VERIFY(session->levels().value().histogramReady);
    LevelsSettings settings;
    settings.setCurrent(LevelRange{0, 1, 255, 255, 0});
    session->updateLevels(settings, true);
    QTRY_VERIFY(session->levels().value().preparedPreview);
    const std::vector<uchar> preview = bytes(session->levels().value().preparedPreview.value());
    QVERIFY(preview[0] == 255 && preview[8] == 128);
    QVERIFY(session->document() == before);
    session->updateLevels(settings, false);
    QVERIFY(!session->levels().value().previewImage(session->levels().value().layerID));
    session->cancelLevels();
    QVERIFY(session->document() == before && session->history.undoCount() == count);
    session->beginLevels();
    session->updateLevels(settings, false);
    commit(*session);
    QVERIFY(session->history.undoCount() == count + 1 && session->history.undoName() == QString("Levels"));
    const QImage committed = session->activeLayer().value().asset.value().image();
    QCOMPARE(bytes(committed), preview);
    session->undo();
    QVERIFY(session->document() == before);
    session->redo();
    QCOMPARE(session->activeLayer().value().asset.value().image().cacheKey(), committed.cacheKey());
    QTemporaryDir folder;
    const QString path = folder.filePath(QStringLiteral("Levels.comp"));
    ProjectStore::save(session->projectSnapshot().value(), path);
    QCOMPARE(bytes(ImageExporter::render(ProjectStore::load(path)).image), preview);
}

void LevelsTests::stalePreviewCannotReturnAfterOffOrReopen()
{
    const std::unique_ptr<EditorSession> session = rampSession();
    session->beginLevels();
    LevelsSettings settings;
    settings.setCurrent(LevelRange{0, 2, 255, 0, 255});
    session->updateLevels(settings, true);
    const QUuid old = session->levels().value().id;
    session->updateLevels(settings, false);
    // The run turned off finishes unseen.
    QTest::qWait(100);
    QVERIFY(!session->levels().value().preparedPreview);
    session->updateLevels(settings, true);
    session->cancelLevels();
    session->beginLevels();
    QTest::qWait(100);
    QVERIFY(session->levels().value().id != old && !session->levels().value().preparedPreview);
    session->cancelLevels();
}

void LevelsTests::histogramDisplayKeepsDistributionVisibleBesideClippingSpikes()
{
    std::array<double, 256> bins;
    bins.fill(100);
    bins[255] = 100'000;
    QCOMPARE(LevelsHistogramDisplay::scale(bins), 400.0);
    bins[0] = 200'000;
    QCOMPARE(LevelsHistogramDisplay::scale(bins), 400.0);
    // A lone spike away from the ends keeps it readable.
    bins[128] = 500'000;
    QCOMPARE(LevelsHistogramDisplay::scale(bins), 400.0);
    QCOMPARE(bins[128], 500'000.0);
    std::array<double, 256> flat;
    flat.fill(100);
    QCOMPARE(LevelsHistogramDisplay::scale(flat), 100.0);
    std::array<double, 256> sparse{};
    QCOMPARE(LevelsHistogramDisplay::scale(sparse), 0.0);
    sparse[255] = 50;
    QCOMPARE(LevelsHistogramDisplay::scale(sparse), 50.0);
    sparse[0] = 100;
    QCOMPARE(LevelsHistogramDisplay::scale(sparse), 100.0);
    sparse[128] = 200;
    QCOMPARE(LevelsHistogramDisplay::scale(sparse), 200.0);
}

void LevelsTests::autoAlgorithmsAndEyedropperCalibration()
{
    LevelsHistogram bins{};
    for (size_t channel = 1; channel <= 3; ++channel) {
        bins[channel][20 * channel] = 100;
        bins[channel][200 + channel * 10] = 100;
    }
    const LevelsSettings linked = settings(LevelsAuto::contrast, bins);
    QVERIFY(linked.ranges[0].black == 20 && linked.ranges[0].white == 230);
    const LevelsSettings colour = settings(LevelsAuto::color, bins);
    QVERIFY(colour.ranges[1].black == 20 && colour.ranges[3].black == 60);
    QVERIFY(colour.ranges[0] == LevelRange());
    QCOMPARE(settings(LevelsAuto::neutral, bins).ranges[1].gamma, 1.0);
    for (const LevelsAuto mode : allLevelsAutos)
        QVERIFY(settings(mode, LevelsHistogram{}).isIdentity());
    const std::array<double, 3> rgb{0.25, 0.4, 0.6};
    const std::array<LevelsChannel, 3> channels{LevelsChannel::red, LevelsChannel::green, LevelsChannel::blue};
    for (const LevelsSample mode : allLevelsSamples) {
        const LevelsSettings sampled = LevelsSettings().sampling(rgb, mode);
        const double target = mode == LevelsSample::black ? 0 : mode == LevelsSample::white ? 1 : 0.5;
        for (size_t index = 0; index < 3; ++index)
            QVERIFY(std::abs(sampled.apply(rgb[index], channels[index]) - target) < 0.0001);
    }
}

void LevelsTests::eyedropperSamplesOriginalAndRejectsTransparentPixels()
{
    const std::unique_ptr<EditorSession> session = rampSession();
    session->beginLevels();
    session->setLevelsSampleMode(LevelsSample::gray);
    session->sampleLevels(QPointF(2.5, 0.5));
    const LevelsSettings first = session->levels().value().settings;
    QVERIFY(!first.isIdentity());
    session->sampleLevels(QPointF(2.5, 0.5));
    QVERIFY(session->levels().value().settings == first);
    session->sampleLevels(QPointF(5.5, 0.5));
    QVERIFY(session->levels().value().settings == first);
    session->sampleLevels(QPointF(-1, 0.5));
    QVERIFY(session->levels().value().settings == first);
    session->cancelLevels();
}

QTEST_GUILESS_MAIN(LevelsTests)
#include "LevelsTests.moc"
