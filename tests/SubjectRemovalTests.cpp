#include "Document/BrushStroke.h"
#include "Document/SubjectRemoval.h"
#include <QPainter>
#include <QtTest>

// Remove Background's mask: U²-Net's subject, refined as asked.
namespace {
// A red disc on white: a subject the model finds.
QImage disc(int width, int height, QPointF centre, double radius)
{
    QImage image = BrushRaster::context(width, height, false);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(217, 26, 26));
    painter.drawEllipse(centre, radius, radius);
    return image;
}

FilterSettings advanced(double refine, double contrast, double shift)
{
    FilterSettings settings;
    settings.backgroundQuality = BackgroundQuality::advanced;
    settings.refineEdges = refine;
    settings.matteContrast = contrast;
    settings.shiftEdge = shift;
    return settings;
}

// How many pixels of a gray mask pass half.
int kept(const QImage &mask)
{
    int count = 0;
    for (int y = 0; y < mask.height(); ++y) {
        for (int x = 0; x < mask.width(); ++x)
            count += mask.constScanLine(y)[x] >= 128;
    }
    return count;
}

int at(const QImage &mask, int x, int y)
{
    return mask.constScanLine(y)[x];
}
}

class SubjectRemovalTests : public QObject {
    Q_OBJECT
private slots:
    void theModelsInputIsThePapersAndRembgs();
    void theModelsAnswerIsStretchedAndJudged();
    void visionRequestRunsOnAnImage();
    void theMaskKeepsTheSubjectAndHidesTheBackground();
    void aFlatImageHasNoSubject();
    void advancedRefinesShiftsAndClearsHaze();
    void anExistingMaskMultipliesIn();
    void thePreviewClearsTheBackgroundOnASmallerRefine();
    void eachImageHasItsOwnMask();
};

void SubjectRemovalTests::theModelsInputIsThePapersAndRembgs()
{
    constexpr size_t plane = size_t(SubjectRemoval::side) * SubjectRemoval::side;
    // Opaque on the left, clear on the right: black, premultiplied.
    QImage image(64, 32, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::transparent);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x)
            image.setPixelColor(x, y, QColor(100, 50, 200));
    }
    const std::vector<float> input = SubjectRemoval::modelInput(image);
    QCOMPARE(input.size(), 3 * plane);
    // By the brightest byte, 200, then ImageNet's mean and spread.
    const float mean[3] = {0.485f, 0.456f, 0.406f}, spread[3] = {0.229f, 0.224f, 0.225f}, colour[3] = {100, 50, 200};
    for (size_t channel = 0; channel < 3; ++channel) {
        QCOMPARE(input[channel * plane + 10 * 320 + 20], (colour[channel] / 200 - mean[channel]) / spread[channel]);
        QCOMPARE(input[channel * plane + 10 * 320 + 300], (0 - mean[channel]) / spread[channel]);
    }
    // Detail shrinks through Qt's area filter, byte for byte.
    QImage detail(500, 410, QImage::Format_RGBA8888_Premultiplied);
    for (int y = 0; y < 410; ++y) {
        for (int x = 0; x < 500; ++x)
            detail.setPixelColor(x, y, QColor((x * 7 + y * 3) % 256, (x * x + y) % 200, (y * 5) % 256));
    }
    const QImage filtered = detail.scaled(320, 320, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    float brightest = 0;
    for (int y = 0; y < 320; ++y) {
        for (int x = 0; x < 320 * 4; ++x)
            brightest = std::max(brightest, x % 4 == 3 ? 0.0f : float(filtered.constScanLine(y)[x]));
    }
    const std::vector<float> fine = SubjectRemoval::modelInput(detail);
    for (int y = 0; y < 320; y += 7) {
        for (int x = 0; x < 320; ++x)
            QCOMPARE(fine[size_t(y) * 320 + size_t(x)], (float(filtered.constScanLine(y)[x * 4]) / brightest - mean[0]) / spread[0]);
    }
    // All black divides by no nothing.
    QImage black(8, 8, QImage::Format_RGBA8888_Premultiplied);
    black.fill(Qt::black);
    for (const float value : SubjectRemoval::modelInput(black))
        QVERIFY(std::isfinite(value));
}

void SubjectRemovalTests::theModelsAnswerIsStretchedAndJudged()
{
    constexpr int side = SubjectRemoval::side;
    std::vector<float> answer(size_t(side) * side, 0.25f);
    answer[0] = 0.75f;
    answer[1] = 0.5f;
    // Stretched from its lowest to its highest, truncated.
    const QImage mask = SubjectRemoval::modelMask(answer.data(), QSize(side, side));
    QVERIFY(mask.format() == QImage::Format_Grayscale8 && mask.size() == QSize(side, side));
    QVERIFY(at(mask, 0, 0) == 255 && at(mask, 1, 0) == 127 && at(mask, 5, 5) == 0);
    // Scaled, filtered, to the size asked.
    const QImage wide = SubjectRemoval::modelMask(answer.data(), QSize(640, 160));
    QVERIFY(wide.format() == QImage::Format_Grayscale8 && wide.size() == QSize(640, 160) && at(wide, 639, 159) == 0);
    // Shrunk, alternate columns average rather than alias.
    std::vector<float> stripes(size_t(side) * side);
    for (size_t index = 0; index < stripes.size(); ++index)
        stripes[index] = index % 2 ? 0.75f : 0.25f;
    const QImage shrunk = SubjectRemoval::modelMask(stripes.data(), QSize(100, 100));
    for (int x = 0; x < 100; ++x)
        QVERIFY2(at(shrunk, x, 50) > 80 && at(shrunk, x, 50) < 175, qPrintable(QString::number(at(shrunk, x, 50))));
    // Enlarged through premultiplied RGBA, byte for byte.
    std::vector<float> noise(size_t(side) * side);
    QImage stretched(side, side, QImage::Format_Grayscale8);
    for (size_t index = 0; index < noise.size(); ++index) {
        noise[index] = float((index * 37) % 101) / 100;
        stretched.scanLine(int(index) / side)[index % size_t(side)] = uchar(noise[index] * 255);
    }
    QCOMPARE(SubjectRemoval::modelMask(noise.data(), QSize(640, 480)),
             stretched.convertToFormat(QImage::Format_RGBA8888_Premultiplied)
                 .scaled(640, 480, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                 .convertToFormat(QImage::Format_Grayscale8));
    // A checkerboard halves through premultiplied RGBA, averaging.
    std::vector<float> checks(size_t(side) * side);
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x)
            checks[size_t(y) * side + size_t(x)] = (x + y) % 2 ? 0.75f : 0.25f;
    }
    const QImage halved = SubjectRemoval::modelMask(checks.data(), QSize(side / 2, side / 2));
    for (int y = 0; y < side / 2; y += 9) {
        for (int x = 0; x < side / 2; ++x)
            QVERIFY2(std::abs(at(halved, x, y) - 127) <= 1, qPrintable(QString::number(at(halved, x, y))));
    }
    // At half the model takes a subject; below, none.
    answer[0] = 0.5f;
    QCOMPARE(at(SubjectRemoval::modelMask(answer.data(), QSize(side, side)), 0, 0), 255);
    answer[0] = 0.4999f;
    answer[1] = 0.3f;
    QVERIFY_THROWS_EXCEPTION(SubjectRemovalError, SubjectRemoval::modelMask(answer.data(), QSize(side, side)));
}

// Swift's test: a result the image's size, or none.
void SubjectRemovalTests::visionRequestRunsOnAnImage()
{
    const QImage image = disc(400, 300, QPointF(200, 150), 80);
    const QImage result = SubjectRemoval::run(image, FilterSettings());
    QVERIFY(result.size() == image.size() && result.format() == QImage::Format_RGBA8888_Premultiplied);
}

void SubjectRemovalTests::theMaskKeepsTheSubjectAndHidesTheBackground()
{
    const QImage image = disc(400, 300, QPointF(200, 150), 80);
    const QImage mask = SubjectRemoval::subjectMask(image, std::nullopt, FilterSettings());
    QVERIFY(mask.format() == QImage::Format_Grayscale8 && mask.size() == image.size());
    // White over the disc, black round it.
    QCOMPARE(at(mask, 200, 150), 255);
    for (const QPoint corner : {QPoint(0, 0), QPoint(399, 0), QPoint(0, 299), QPoint(399, 299), QPoint(200, 20), QPoint(40, 150)})
        QVERIFY2(at(mask, corner.x(), corner.y()) == 0, qPrintable(QStringLiteral("%1,%2").arg(corner.x()).arg(corner.y())));
    // About the disc's area passes half.
    const double area = 3.14159265 * 80 * 80;
    QVERIFY2(std::abs(kept(mask) - area) < area * 0.1, qPrintable(QString::number(kept(mask))));
}

void SubjectRemovalTests::aFlatImageHasNoSubject()
{
    QImage flat = BrushRaster::context(64, 48, false);
    flat.fill(QColor(90, 140, 200));
    for (const bool preview : {false, true}) {
        try {
            if (preview)
                SubjectRemoval::run(flat, FilterSettings());
            else
                SubjectRemoval::subjectMask(flat, std::nullopt, FilterSettings());
            QFAIL("a flat image has a subject");
        } catch (const SubjectRemovalError &error) {
            QVERIFY(error.kind == SubjectRemovalError::Kind::noSubject);
            QCOMPARE(QString::fromUtf8(error.what()),
                     QString("No foreground subject was detected in this layer. Try an image with a more distinct subject."));
        }
    }
}

void SubjectRemovalTests::advancedRefinesShiftsAndClearsHaze()
{
    const QImage image = disc(400, 300, QPointF(200, 150), 80);
    const QImage basic = SubjectRemoval::subjectMask(image, std::nullopt, FilterSettings());
    // Basic is the model's mask, whatever the other settings say.
    FilterSettings plain = advanced(40, 100, -10);
    plain.backgroundQuality = BackgroundQuality::basic;
    QCOMPARE(SubjectRemoval::subjectMask(image, std::nullopt, plain), basic);
    // Nothing asked, Advanced leaves the mask as it is.
    QCOMPARE(SubjectRemoval::subjectMask(image, std::nullopt, advanced(0, 0, 0)), basic);
    // Refine pulls the mask onto the layer's own edges.
    QVERIFY(SubjectRemoval::subjectMask(image, std::nullopt, advanced(12, 0, 0)) != basic);
    // Shift Edge shrinks the mask inward, or grows it.
    const int kept0 = kept(basic);
    // Measured: the disc's area, near enough to the pixel.
    QVERIFY2(std::abs(kept0 - 20138) <= 40, qPrintable(QString::number(kept0)));
    const QImage shrunk = SubjectRemoval::subjectMask(image, std::nullopt, advanced(0, 0, -10));
    const QImage grown = SubjectRemoval::subjectMask(image, std::nullopt, advanced(0, 0, 10));
    QVERIFY2(std::abs(kept(shrunk) - 18328) <= 40 && std::abs(kept(grown) - 21772) <= 40, qPrintable(QStringLiteral("%1 %2").arg(kept(shrunk)).arg(kept(grown))));
    // A hard cut leaves one step of gray at most.
    for (const QImage &cut : {shrunk, grown}) {
        for (int y = 0; y < cut.height(); ++y) {
            for (int x = 0; x < cut.width(); ++x)
                QVERIFY(at(cut, x, y) == 0 || at(cut, x, y) >= 250);
        }
    }
    // Full contrast leaves black and white alone.
    const QImage hard = SubjectRemoval::subjectMask(image, std::nullopt, advanced(0, 100, 0));
    int grays = 0;
    for (int y = 0; y < hard.height(); ++y) {
        for (int x = 0; x < hard.width(); ++x)
            grays += at(hard, x, y) > 5 && at(hard, x, y) < 250;
    }
    QVERIFY2(grays < 60, qPrintable(QString::number(grays)));
    // A cut at the middle keeps exactly what passed half.
    QCOMPARE(kept(hard), kept0);
    // Contrast is a line through the middle, rounded once.
    const QImage hazy = SubjectRemoval::subjectMask(image, std::nullopt, advanced(0, 25, 0));
    const float slope = 1 / std::max(0.02f, 1 - float(25.0 / 100) * 0.98f);
    for (int y = 0; y < 300; y += 2) {
        for (int x = 0; x < 400; ++x) {
            const float value = slope * (float(at(basic, x, y)) / 255) + (1 - slope) / 2;
            QCOMPARE(at(hazy, x, y), int(std::lround(std::clamp(value, 0.0f, 1.0f) * 255)));
        }
    }
    // A shrink keeps a subject's side on the image's edge.
    QImage edge = BrushRaster::context(400, 300, false);
    edge.fill(Qt::white);
    QPainter painter(&edge);
    painter.fillRect(QRect(0, 60, 160, 180), QColor(217, 26, 26));
    painter.end();
    QCOMPARE(at(SubjectRemoval::subjectMask(edge, std::nullopt, FilterSettings()), 0, 150), 254);
    const QImage cut = SubjectRemoval::subjectMask(edge, std::nullopt, advanced(0, 0, -10));
    QVERIFY(at(cut, 0, 150) == 255 && at(cut, 0, 62) == 0 && at(cut, 170, 150) == 0);
}

void SubjectRemovalTests::anExistingMaskMultipliesIn()
{
    const QImage image = disc(400, 300, QPointF(200, 150), 80);
    QImage existing(image.size(), QImage::Format_Grayscale8);
    existing.fill(128);
    const QImage combined = SubjectRemoval::subjectMask(image, existing, FilterSettings());
    const QImage subject = SubjectRemoval::subjectMask(image, std::nullopt, FilterSettings());
    // What either one hides stays hidden: the product, rounded.
    QVERIFY(combined.format() == QImage::Format_Grayscale8 && at(combined, 200, 150) == 128 && at(combined, 0, 0) == 0);
    for (int y = 0; y < 300; ++y) {
        for (int x = 0; x < 400; ++x)
            QCOMPARE(at(combined, x, y), (at(subject, x, y) * 128 + 127) / 255);
    }
    // A mask of another grid is refused, never read past.
    QVERIFY_THROWS_EXCEPTION(std::logic_error, SubjectRemoval::subjectMask(image, QImage(2, 2, QImage::Format_Grayscale8), FilterSettings()));
    // Any image reads as gray, as Swift draws it.
    QImage colour(image.size(), QImage::Format_RGBA8888_Premultiplied);
    colour.fill(QColor(128, 128, 128));
    QCOMPARE(SubjectRemoval::subjectMask(image, colour, FilterSettings()), combined);
}

void SubjectRemovalTests::thePreviewClearsTheBackgroundOnASmallerRefine()
{
    const QImage image = disc(400, 300, QPointF(200, 150), 80);
    const QImage preview = SubjectRemoval::run(image, FilterSettings());
    // The disc stays; the white round it goes clear.
    QCOMPARE(preview.pixelColor(200, 150), image.pixelColor(200, 150));
    QCOMPARE(preview.pixelColor(0, 0), QColor(0, 0, 0, 0));
    // Each byte times the mask, rounded, over every channel.
    const QImage mask = SubjectRemoval::subjectMask(image, std::nullopt, FilterSettings());
    for (int x = 100; x < 300; x += 7) {
        const int coverage = at(mask, x, 150);
        const QRgb pixel = image.pixel(x, 150);
        QCOMPARE(qAlpha(preview.pixel(x, 150)), (255 * coverage + 127) / 255);
        QCOMPARE(qRed(preview.pixel(x, 150)), (qRed(pixel) * coverage + 127) / 255);
    }
    // Any format comes back premultiplied, the same pixels.
    const QImage straight = SubjectRemoval::run(image.convertToFormat(QImage::Format_ARGB32), FilterSettings());
    QVERIFY(straight.format() == QImage::Format_RGBA8888_Premultiplied && straight.pixelColor(0, 0) == QColor(0, 0, 0, 0));
    QCOMPARE(straight.pixelColor(200, 150), preview.pixelColor(200, 150));
    // Past 1400 a side, the preview refines a smaller copy.
    const QImage large = disc(2800, 2100, QPointF(1400, 1050), 560);
    const QImage refined = SubjectRemoval::run(large, advanced(12, 0, 0));
    const QImage full = SubjectRemoval::subjectMask(large, std::nullopt, advanced(12, 0, 0));
    int differ = 0;
    for (int y = 0; y < 2100; y += 3) {
        for (int x = 0; x < 2800; x += 3)
            differ += qAlpha(refined.pixel(x, y)) != at(full, x, y);
    }
    QVERIFY2(differ > 100, qPrintable(QString::number(differ)));
}

void SubjectRemovalTests::eachImageHasItsOwnMask()
{
    const QImage left = SubjectRemoval::subjectMask(disc(400, 300, QPointF(120, 150), 70), std::nullopt, FilterSettings());
    const QImage right = SubjectRemoval::subjectMask(disc(400, 300, QPointF(280, 150), 70), std::nullopt, FilterSettings());
    QVERIFY(at(left, 120, 150) == 255 && at(left, 280, 150) == 0);
    QVERIFY(at(right, 280, 150) == 255 && at(right, 120, 150) == 0);
}

QTEST_GUILESS_MAIN(SubjectRemovalTests)
#include "SubjectRemovalTests.moc"
