#include "Document/BrushStroke.h"
#include "Document/HueSaturation.h"
#include "Document/Levels.h"
#include <QRandomGenerator>
#include <QtTest>
#include <mutex>
extern "C" {
#include "LevelsPixels.h"
}

// Compositor 1.4: Levels and Hue/Saturation in bands across the cores.
namespace {
// Premultiplied noise, some of it partly clear.
QImage noise(int width, int height)
{
    QImage image = BrushRaster::context(width, height, false);
    QRandomGenerator random(7);
    for (int y = 0; y < height; ++y) {
        uchar *row = image.scanLine(y);
        for (int x = 0; x < width; ++x) {
            const uchar alpha = uchar(random.bounded(256));
            for (int channel = 0; channel < 3; ++channel)
                row[x * 4 + channel] = uchar(random.bounded(alpha + 1));
            row[x * 4 + 3] = alpha;
        }
    }
    return image;
}

std::vector<uchar> bytes(const QImage &image)
{
    std::vector<uchar> result;
    for (int y = 0; y < image.height(); ++y)
        result.insert(result.end(), image.constScanLine(y), image.constScanLine(y) + image.width() * 4);
    return result;
}
}

class AdjustmentBandsTests : public QObject {
    Q_OBJECT
private slots:
    void bandsCoverEveryPixelOnce();
    void aCopyKeepsTheBytes();
    void levelsInBandsMatchesOnePass();
    void hueSaturationInBandsMatchesOnePass();
    void cachedTablesFollowTheirSettings();
};

void AdjustmentBandsTests::bandsCoverEveryPixelOnce()
{
    for (const qsizetype count : {qsizetype(10), qsizetype(249'999), qsizetype(250'007), qsizetype(1'000'003)}) {
        std::vector<int> marks(size_t(count), 0);
        std::mutex lock;
        int calls = 0;
        BrushRaster::inBands(count, [&](qsizetype start, qsizetype length) {
            QVERIFY(length > 0);
            for (qsizetype index = start; index < start + length; ++index)
                ++marks[size_t(index)];
            const std::lock_guard<std::mutex> held(lock);
            ++calls;
        });
        QVERIFY(std::all_of(marks.begin(), marks.end(), [](int mark) { return mark == 1; }));
        // Small counts run as one band.
        QCOMPARE(calls > 1, count >= 250'000);
    }
}

void AdjustmentBandsTests::aCopyKeepsTheBytes()
{
    const QImage source = noise(37, 11);
    const QImage copied = BrushRaster::copy(source);
    QCOMPARE(copied.format(), QImage::Format_RGBA8888_Premultiplied);
    QCOMPARE(bytes(copied), bytes(source));
    QVERIFY(copied.constBits() != source.constBits());
    // Another layout is drawn into one.
    QImage argb(3, 1, QImage::Format_ARGB32);
    argb.fill(qRgba(255, 0, 0, 255));
    QCOMPARE(bytes(BrushRaster::copy(argb)), (std::vector<uchar>{255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255}));
}

void AdjustmentBandsTests::levelsInBandsMatchesOnePass()
{
    const QImage source = noise(701, 401);
    LevelsSettings settings;
    settings.ranges[0] = LevelRange{.black = 20, .gamma = 1.7, .white = 230, .outputBlack = 10, .outputWhite = 240};
    std::array<float, 3 * 256> tables;
    for (size_t channel = 0; channel < 3; ++channel) {
        for (size_t value = 0; value <= 255; ++value)
            tables[channel * 256 + value] = float(settings.apply(double(value) / 255, allLevelsChannels[channel + 1]));
    }
    QImage reference = source.copy();
    levels_apply(reference.bits(), size_t(reference.width()) * size_t(reference.height()), tables.data());
    const QImage banded = LevelsFilter::run(LevelsJob{source, settings, std::nullopt, QTransform()});
    QCOMPARE(bytes(banded), bytes(reference));
    QVERIFY(bytes(banded) != bytes(source));
}

void AdjustmentBandsTests::hueSaturationInBandsMatchesOnePass()
{
    const QImage source = noise(701, 401);
    const HueSaturationSettings settings(120, 40, -10);
    const std::vector<float> cube = HueSaturationFilter::cube(settings);
    QImage reference = source.copy();
    cube_apply(reference.bits(), size_t(reference.width()) * size_t(reference.height()), cube.data(), HueSaturationFilter::dimension);
    const QImage banded = HueSaturationFilter::run(HueSaturationJob{source, settings, std::nullopt, QTransform(), false}).image;
    QCOMPARE(bytes(banded), bytes(reference));
    QVERIFY(bytes(banded) != bytes(source));
}

void AdjustmentBandsTests::cachedTablesFollowTheirSettings()
{
    // More settings than the cache holds, asked twice over.
    std::vector<std::vector<float>> first;
    for (int hue = 0; hue < 10; ++hue)
        first.push_back(HueSaturationFilter::cube(HueSaturationSettings(hue * 30)));
    for (int round = 0; round < 2; ++round) {
        for (int hue = 9; hue >= 0; --hue)
            QVERIFY(HueSaturationFilter::cube(HueSaturationSettings(hue * 30)) == first[size_t(hue)]);
    }
    QVERIFY(first[0] != first[1]);
}

QTEST_GUILESS_MAIN(AdjustmentBandsTests)
#include "AdjustmentBandsTests.moc"
