#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include <QPainter>
#include <QtTest>
#include <set>

// Swift's FilterTests: the blurs, noise and lens correction.
namespace {
// Premultiplied RGBA bytes, row after row.
std::vector<uchar> bytes(const QImage &image)
{
    const QImage drawn = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    std::vector<uchar> result;
    for (int y = 0; y < drawn.height(); ++y)
        result.insert(result.end(), drawn.constScanLine(y), drawn.constScanLine(y) + drawn.width() * 4);
    return result;
}

QImage run(FilterKind kind, const QImage &image, const FilterSettings &settings, quint32 seed = 0)
{
    return PixelFilter::run(FilterJob{kind, image, settings, 1, std::nullopt, QTransform(), seed});
}

// An opaque white dot amid a clear 41 by 41.
QImage dot()
{
    QImage image = BrushRaster::context(41, 41, false);
    QPainter(&image).fillRect(20, 20, 1, 1, Qt::white);
    return image;
}
}

class FilterTests : public QObject {
    Q_OBJECT
private slots:
    void gaussianBlurSoftensTheEdgesIntoRoomMadeForItAsOneUndoStep();
    void motionBlurStreaksAlongItsAngleCounterclockwiseFromHorizontal();
    void addNoiseChangesColorButNeverAlphaAndMonochromaticKeepsGrays();
    void removeDistortionBendsAboutTheCenterAndOnlyPincushionCorrectionOpensTheCorners();
};

void FilterTests::gaussianBlurSoftensTheEdgesIntoRoomMadeForItAsOneUndoStep()
{
    EditorSession session;
    session.createDocument(40, 20);
    // Left half opaque white, right half clear.
    QImage image = BrushRaster::context(40, 20, false);
    QPainter(&image).fillRect(0, 0, 20, 20, Qt::white);
    session.insert(ImportedImage(image, image, QStringLiteral("Half")));
    session.beginFilter(FilterKind::gaussianBlur);
    QVERIFY(session.filterEdit() && !session.canEditLayers());
    session.updateFilter(FilterSettings{.radius = 3}, true);
    const int count = session.history.undoCount();
    bool done = false;
    session.commitFilter([&done] { done = true; });
    QTRY_VERIFY(done);
    QVERIFY(!session.filterEdit() && session.history.undoCount() == count + 1);
    QCOMPARE(session.filterSettings().radius, 3.0);
    // Unclamped: the layer grew by the blur's reach, then trimmed.
    const ImageLayer layer = session.activeLayer().value();
    const QImage result = layer.asset.value().image();
    QCOMPARE(layer.transform.origin, QPointF(-8, -8));
    QCOMPARE(result.size(), QSize(36, 36));
    const auto alpha = [&result](int x) { return int(bytes(result)[size_t((18 * result.width() + x + 8) * 4 + 3)]); };
    // Its own border softens, as the hard edge does.
    QCOMPARE(alpha(-8), 1);
    QCOMPARE(alpha(0), 144);
    QCOMPARE(alpha(19), 144);
    QCOMPARE(alpha(20), 111);
    QCOMPARE(alpha(27), 1);
}

void FilterTests::motionBlurStreaksAlongItsAngleCounterclockwiseFromHorizontal()
{
    const auto streak = [](double angle) {
        const std::vector<uchar> made = bytes(run(FilterKind::motionBlur, dot(), FilterSettings{.angle = angle, .distance = 16}));
        return [made](int x, int y) { return int(made[size_t((y * 41 + x) * 4 + 3)]); };
    };
    const auto horizontal = streak(0);
    QVERIFY(horizontal(24, 20) > 0 && horizontal(16, 20) > 0 && horizontal(20, 24) == 0);
    const auto vertical = streak(90);
    QVERIFY(vertical(20, 24) > 0 && vertical(20, 16) > 0 && vertical(24, 20) == 0);
    // 45° runs up-right and down-left on screen, never up-left.
    const auto diagonal = streak(45);
    QVERIFY(diagonal(23, 17) > 0 && diagonal(17, 23) > 0 && diagonal(17, 17) == 0);
}

void FilterTests::addNoiseChangesColorButNeverAlphaAndMonochromaticKeepsGrays()
{
    // Left half opaque mid gray, right half clear.
    QImage gray = BrushRaster::context(32, 8, false);
    QPainter(&gray).fillRect(0, 0, 16, 8, QColor::fromRgbF(0.5, 0.5, 0.5));
    const auto pixels = [&gray](const FilterSettings &settings) { return bytes(run(FilterKind::addNoise, gray, settings, 7)); };
    std::vector<size_t> opaque, clear;
    for (size_t offset = 0; offset < 32 * 8 * 4; offset += 4)
        (offset / 4 % 32 < 16 ? opaque : clear).push_back(offset);
    const std::vector<uchar> color = pixels(FilterSettings{.amount = 10});
    // The same seed gives the same grain.
    QVERIFY(pixels(FilterSettings{.amount = 10}) == color);
    std::set<int> values;
    bool channelsDiffer = false;
    for (const size_t offset : opaque) {
        QCOMPARE(int(color[offset + 3]), 255);
        QVERIFY(color[offset] >= 112 && color[offset] <= 144);
        values.insert(color[offset]);
        channelsDiffer |= color[offset] != color[offset + 1];
    }
    QVERIFY(values.size() > 5 && channelsDiffer);
    for (const size_t offset : clear)
        QVERIFY(color[offset] == 0 && color[offset + 3] == 0);
    const std::vector<uchar> mono = pixels(FilterSettings{.amount = 10, .gaussian = true, .monochromatic = true});
    for (const size_t offset : opaque)
        QVERIFY(mono[offset] == mono[offset + 1] && mono[offset + 1] == mono[offset + 2] && mono[offset + 3] == 255);
}

void FilterTests::removeDistortionBendsAboutTheCenterAndOnlyPincushionCorrectionOpensTheCorners()
{
    // An opaque image with a distinct colour in each quadrant.
    QImage source = BrushRaster::context(40, 30, false);
    {
        QPainter painter(&source);
        const QRect quarters[] = {QRect(0, 0, 20, 15), QRect(20, 0, 20, 15), QRect(0, 15, 20, 15), QRect(20, 15, 20, 15)};
        for (int index = 0; index < 4; ++index)
            painter.fillRect(quarters[index], QColor::fromRgbF(index / 3.0, 0.5, 1 - index / 3.0));
    }
    const auto pixels = [&source](double distortion) { return bytes(run(FilterKind::lensCorrection, source, FilterSettings{.distortion = distortion})); };
    const auto alpha = [](const std::vector<uchar> &made, int x, int y) { return int(made[size_t((y * 40 + x) * 4 + 3)]); };
    const std::vector<uchar> original = pixels(0);
    for (size_t index = 0; index < 40 * 30; ++index)
        QCOMPARE(int(original[index * 4 + 3]), 255);
    // Straightening barrel stretches the edges outward: nothing opens.
    const std::vector<uchar> barrel = pixels(100);
    QVERIFY(alpha(barrel, 0, 0) == 255 && alpha(barrel, 39, 29) == 255);
    // Straightening pincushion pulls the edges in: the corners clear.
    const std::vector<uchar> pincushion = pixels(-100);
    QVERIFY(alpha(pincushion, 0, 0) == 0 && alpha(pincushion, 39, 29) == 0);
    const size_t middle = (15 * 40 + 20) * 4;
    QVERIFY(std::equal(pincushion.begin() + qsizetype(middle), pincushion.begin() + qsizetype(middle + 4), original.begin() + qsizetype(middle)));
}

QTEST_GUILESS_MAIN(FilterTests)
#include "FilterTests.moc"
