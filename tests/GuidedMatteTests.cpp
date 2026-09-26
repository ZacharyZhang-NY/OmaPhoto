#include "Document/GuidedMatte.h"
#include <QtTest>
#include <algorithm>

// The guided filter under Remove Background's Refine.
namespace {
// Values that vary in both directions, 0 to 1.
std::vector<float> pattern(int width, int height, int seed)
{
    std::vector<float> values(size_t(width) * size_t(height));
    for (size_t index = 0; index < values.size(); ++index)
        values[index] = float((index * 37 + size_t(seed) * 11) % 17) / 16;
    return values;
}

// The mean of a clamped square, summed plainly in doubles.
double mean(const std::vector<double> &source, int width, int height, int radius, int x, int y)
{
    double sum = 0;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx)
            sum += source[size_t(std::clamp(y + dy, 0, height - 1)) * size_t(width) + size_t(std::clamp(x + dx, 0, width - 1))];
    }
    return sum / ((2 * radius + 1) * (2 * radius + 1));
}

// He, Sun and Tang's steps, plainly, in doubles, clamped.
std::vector<double> reference(const std::vector<float> &mask, const std::vector<float> &guide, int width, int height, int radius, double epsilon)
{
    const size_t count = size_t(width) * size_t(height);
    std::vector<double> p(mask.begin(), mask.end()), g(guide.begin(), guide.end()), squares(count), products(count), a(count), b(count), result(count);
    for (size_t i = 0; i < count; ++i) {
        squares[i] = g[i] * g[i];
        products[i] = g[i] * p[i];
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t i = size_t(y * width + x);
            const double meanGuide = mean(g, width, height, radius, x, y), meanMask = mean(p, width, height, radius, x, y);
            const double variance = mean(squares, width, height, radius, x, y) - meanGuide * meanGuide;
            a[i] = (mean(products, width, height, radius, x, y) - meanGuide * meanMask) / (variance + epsilon);
            b[i] = meanMask - a[i] * meanGuide;
        }
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x)
            result[size_t(y * width + x)] = std::clamp(mean(a, width, height, radius, x, y) * g[size_t(y * width + x)] + mean(b, width, height, radius, x, y), 0.0, 1.0);
    }
    return result;
}

QImage gray(int width, int height, const std::function<int(int, int)> &value)
{
    QImage image(width, height, QImage::Format_Grayscale8);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x)
            image.scanLine(y)[x] = uchar(value(x, y));
    }
    return image;
}
}

class GuidedMatteTests : public QObject {
    Q_OBJECT
private slots:
    void theBoxAveragesItsSquareWithTheEdgeRepeated();
    void theFilterIsHesArithmetic();
    void theFilterPullsASoftMaskOntoTheGuidesEdge();
    void levelsReadGrayOverBlackAndImagesRound();
    void refineWorksOnASmallerCopyAndDrawsBackUp();
};

void GuidedMatteTests::theBoxAveragesItsSquareWithTheEdgeRepeated()
{
    const int width = 7, height = 5;
    const std::vector<float> source = pattern(width, height, 1);
    const std::vector<double> wide(source.begin(), source.end());
    for (const int radius : {1, 2, 6, 9}) {
        const std::vector<float> box = GuidedMatte::box(source, width, height, radius);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x)
                QVERIFY(std::abs(box[size_t(y * width + x)] - mean(wide, width, height, radius, x, y)) < 1e-5);
        }
    }
}

void GuidedMatteTests::theFilterIsHesArithmetic()
{
    const int width = 9, height = 6;
    const std::vector<float> mask = pattern(width, height, 2), guide = pattern(width, height, 5);
    const std::vector<float> filtered = GuidedMatte::filter(mask, guide, width, height, 2, 1e-4f);
    const std::vector<double> expected = reference(mask, guide, width, height, 2, 1e-4);
    for (size_t index = 0; index < filtered.size(); ++index)
        QVERIFY2(std::abs(filtered[index] - expected[index]) < 2e-3, qPrintable(QString::number(expected[index])));
    // An overshooting average is held between 0 and 1.
    const std::vector<float> row{0, 0, 0, 0.5f, 1, 0, 0.5f};
    for (const std::vector<float> &cut : {std::vector<float>{0, 0, 1, 0, 0, 1, 0}, std::vector<float>{0, 0, 0, 1, 1, 0, 1}}) {
        const std::vector<float> held = GuidedMatte::filter(cut, row, 7, 1, 1, 1e-4f);
        const std::vector<double> clamped = reference(cut, row, 7, 1, 1, 1e-4);
        QVERIFY(std::count(clamped.begin(), clamped.end(), 0.0) + std::count(clamped.begin(), clamped.end(), 1.0) > 0);
        for (size_t index = 0; index < held.size(); ++index)
            QVERIFY2(std::abs(held[index] - clamped[index]) < 2e-3, qPrintable(QString::number(held[index])));
    }
}

void GuidedMatteTests::theFilterPullsASoftMaskOntoTheGuidesEdge()
{
    // The guide steps at 20; the mask ramps across twenty.
    const int width = 40, height = 4;
    std::vector<float> guide(size_t(width * height)), mask(size_t(width * height));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            guide[size_t(y * width + x)] = x < 20 ? 0.2f : 0.8f;
            mask[size_t(y * width + x)] = std::clamp(float(x - 10) / 20, 0.0f, 1.0f);
        }
    }
    const std::vector<float> filtered = GuidedMatte::filter(mask, guide, width, height, 4, 1e-4f);
    // At the guide's edge the mask steps five times faster.
    QVERIFY(filtered[20] - filtered[19] > 0.24f && mask[20] - mask[19] < 0.051f);
    // Far from it, the flat ends stay black and white.
    QVERIFY(filtered[0] == 0 && filtered[39] == 1);
}

void GuidedMatteTests::levelsReadGrayOverBlackAndImagesRound()
{
    // Gray reads exactly; colour through Qt's weights; clear as black.
    const QImage steps = gray(3, 1, [](int x, int) { return x * 100; });
    QCOMPARE(GuidedMatte::levels(steps, 3, 1), (std::vector<float>{0, 100.0f / 255, 200.0f / 255}));
    QImage colour(2, 1, QImage::Format_RGBA8888_Premultiplied);
    colour.setPixelColor(0, 0, QColor(255, 0, 0));
    colour.setPixelColor(1, 0, QColor(0, 0, 0, 0));
    QCOMPARE(GuidedMatte::levels(colour, 2, 1), (std::vector<float>{float(qGray(255, 0, 0)) / 255, 0}));
    // Shrunk, a checkerboard averages rather than aliasing.
    const QImage checks = gray(4, 4, [](int x, int y) { return (x + y) % 2 ? 255 : 0; });
    for (const float level : GuidedMatte::levels(checks, 2, 2))
        QVERIFY(std::abs(level - 0.5f) <= 1.0f / 255);
    // Back to bytes: rounded, and held inside 0 to 255.
    const QImage bytes = GuidedMatte::image({-0.5f, 0.5f, 1.5f, 0.2f}, 4, 1);
    QVERIFY(bytes.format() == QImage::Format_Grayscale8 && bytes.size() == QSize(4, 1));
    QCOMPARE(QByteArray(reinterpret_cast<const char *>(bytes.constScanLine(0)), 4), QByteArray("\x00\x80\xff\x33", 4));
}

void GuidedMatteTests::refineWorksOnASmallerCopyAndDrawsBackUp()
{
    const QImage mask = gray(300, 20, [](int x, int y) { return std::clamp((x - 100) * 2 + y * 3, 0, 255); });
    const QImage guide = gray(300, 20, [](int x, int y) { return x < 150 ? 40 + y : 220 - y; });
    // Within the limit it is the filter at full size.
    const QImage whole = GuidedMatte::refine(mask, guide, 3, 300);
    QCOMPARE(whole, GuidedMatte::image(GuidedMatte::filter(GuidedMatte::levels(mask, 300, 20), GuidedMatte::levels(guide, 300, 20), 300, 20, 3, 1e-4f), 300, 20));
    // Past it: a third the size and radius, drawn back.
    const QImage small = GuidedMatte::image(GuidedMatte::filter(GuidedMatte::levels(mask, 100, 7), GuidedMatte::levels(guide, 100, 7), 100, 7, 4, 1e-4f), 100, 7);
    const QImage reduced = GuidedMatte::refine(mask, guide, 12, 100);
    QVERIFY(reduced.format() == QImage::Format_Grayscale8 && reduced.size() == QSize(300, 20));
    QCOMPARE(reduced, small.scaled(300, 20, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_Grayscale8));
    // A faint guide, where epsilon decides how much counts.
    const QImage faint = gray(300, 20, [](int x, int y) { return 100 + (x + y) % 3; });
    QCOMPARE(GuidedMatte::refine(mask, faint, 3, 300),
             GuidedMatte::image(GuidedMatte::filter(GuidedMatte::levels(mask, 300, 20), GuidedMatte::levels(faint, 300, 20), 300, 20, 3, 1e-4f), 300, 20));
    // A radius that rounds to nothing still reaches a pixel.
    QCOMPARE(GuidedMatte::refine(mask, guide, 0.2, 300),
             GuidedMatte::image(GuidedMatte::filter(GuidedMatte::levels(mask, 300, 20), GuidedMatte::levels(guide, 300, 20), 300, 20, 1, 1e-4f), 300, 20));
}

QTEST_GUILESS_MAIN(GuidedMatteTests)
#include "GuidedMatteTests.moc"
