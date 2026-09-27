#include "DNGFixture.h"
#include "IO/ImageImporter.h"
#include "IO/RawImporter.h"
#include <QColorSpace>
#include <QTemporaryDir>
#include <QtTest>

// Camera RAW through LibRaw: matching, white balance, the develop.
namespace {
QColor middle(const QImage &image)
{
    return image.pixelColor(image.width() / 2, image.height() / 2);
}

std::array<int, 3> rgb(const QImage &image)
{
    const QColor colour = middle(image);
    return {colour.red(), colour.green(), colour.blue()};
}

// The camera's neutral under a white, through the matrix.
std::array<double, 3> neutralFor(QPointF xy)
{
    const double xyz[3] = {xy.x() / xy.y(), 1, (1 - xy.x() - xy.y()) / xy.y()};
    std::array<double, 3> camera{};
    for (size_t row = 0; row < 3; ++row)
        for (size_t column = 0; column < 3; ++column)
            camera[row] += DNGFixture::colorMatrix[row * 3 + column] * xyz[column];
    return {camera[0] / camera[1], 1, camera[2] / camera[1]};
}

RawDevelopSettings flat(const QString &path)
{
    RawDevelopSettings settings = RawImporter::asShot(path).value();
    settings.boost = 0;
    return settings;
}

std::optional<ImageImportError::Kind> refusal(const std::function<void()> &run)
{
    try {
        run();
    } catch (const ImageImportError &error) {
        return error.kind;
    }
    return std::nullopt;
}
}

class RawImporterTests : public QObject {
    Q_OBJECT
private slots:
    void cameraRawIsKnownByItsExtension();
    void temperatureAndTintFollowThePlanckianLocus();
    void theCamerasOwnBalanceIsWhereTheSlidersStart();
    void boostMixesTheFlatAndTheLibRawCurves();
    void exposureScalesTheLightBeforeTheCurve();
    void temperatureAndTintColourTheDevelop();
    void coloursComeThroughTheCameraMatrix();
    void orientationAndThePreviewsLimit();
    void theQueueKeepsThePreviewsDecodeUntilReleased();
    void aMixingCameraDevelopsAlike();
    void aKeptDecodeDevelopsAsAFreshOne();
    void fourColourSensorsAreUnsupported();
    void whatIsNoRawIsRefused();
    void settingsResetToTheShot();
};

void RawImporterTests::cameraRawIsKnownByItsExtension()
{
    for (const char *name : {"a.dng", "b.CR2", "c.nef", "d.ARW", "e.raf", "f.orf", "g.rw2", "h.cr3"})
        QVERIFY2(RawImporter::matches(name), name);
    for (const char *name : {"a.jpg", "b.psd", "c.tif", "d.png", "e", "f.dng.txt"})
        QVERIFY2(!RawImporter::matches(name), name);
    QVERIFY(RawImporter::isRawType("image/x-adobe-dng") && RawImporter::isRawType("image/x-canon-cr2") && RawImporter::isRawType("image/x-dcraw"));
    QVERIFY(!RawImporter::isRawType("image/tiff") && !RawImporter::isRawType("image/png") && !RawImporter::isRawType("nothing"));
    const QStringList globs = RawImporter::globs();
    for (const char *glob : {"*.dng", "*.cr2", "*.nef", "*.arw", "*.raf"})
        QVERIFY2(globs.contains(glob), glob);
    QVERIFY(!globs.contains("*.jpg") && !globs.contains("*.tif") && globs.count("*.dng") == 1);
    QVERIFY(std::is_sorted(globs.begin(), globs.end()));
}

void RawImporterTests::temperatureAndTintFollowThePlanckianLocus()
{
    const QPointF d65 = RawImporter::planckian(6500);
    QVERIFY(std::abs(d65.x() - 0.313494) < 1e-6 && std::abs(d65.y() - 0.323663) < 1e-6);
    // Warmer is redder: x grows as the temperature falls.
    QVERIFY(RawImporter::planckian(2000).x() > RawImporter::planckian(3000).x());
    QVERIFY(RawImporter::planckian(3000).x() > RawImporter::planckian(10000).x());
    // Positive tint is a white above the locus: greener.
    const QPointF green = RawImporter::whitePoint(5000, 30), magenta = RawImporter::whitePoint(5000, -30), on = RawImporter::whitePoint(5000, 0);
    QVERIFY(std::abs(on.x() - RawImporter::planckian(5000).x()) < 1e-12);
    QVERIFY(magenta.y() < on.y() && green.y() > on.y());
    for (const double kelvin : {2100.0, 3200.0, 5000.0, 6500.0, 9000.0, 11900.0})
        for (const double tint : {-120.0, -10.0, 0.0, 45.0, 140.0}) {
            const auto [backKelvin, backTint] = RawImporter::temperatureOf(RawImporter::whitePoint(kelvin, tint));
            QVERIFY2(std::abs(backKelvin - kelvin) < 0.5 && std::abs(backTint - tint) < 0.05,
                     qPrintable(QStringLiteral("%1 %2 → %3 %4").arg(kelvin).arg(tint).arg(backKelvin).arg(backTint)));
        }
}

void RawImporterTests::theCamerasOwnBalanceIsWhereTheSlidersStart()
{
    const QTemporaryDir folder;
    // D65 sits a little above the Planckian locus.
    const RawDevelopSettings daylight = RawImporter::asShot(DNGFixture::write(folder.filePath("d65.dng"))).value();
    QVERIFY2(std::abs(daylight.temperature - 6500.7) < 1 && std::abs(daylight.tint - 9.52) < 0.05, qPrintable(QString::number(daylight.tint)));
    QCOMPARE(daylight.asShotTemperature, daylight.temperature);
    QCOMPARE(daylight.asShotTint, daylight.tint);
    QVERIFY(daylight.isAsShot() && daylight.exposure == 0 && daylight.boost == 1);
    DNGFixture::Options tungsten;
    tungsten.asShotNeutral = neutralFor(RawImporter::whitePoint(3200, 20));
    const RawDevelopSettings warm = RawImporter::asShot(DNGFixture::write(folder.filePath("warm.dng"), tungsten)).value();
    QVERIFY2(std::abs(warm.temperature - 3200) < 5 && std::abs(warm.tint - 20) < 0.5,
             qPrintable(QStringLiteral("%1 %2").arg(warm.temperature).arg(warm.tint)));
}

void RawImporterTests::boostMixesTheFlatAndTheLibRawCurves()
{
    const QTemporaryDir folder;
    const QString path = DNGFixture::write(folder.filePath("gray.dng"));
    RawDevelopSettings settings = flat(path);
    // 20000 of 65535: sRGB's curve gives 150, BT.709's 139.
    QCOMPARE(rgb(RawImporter::develop(path, settings)), (std::array<int, 3>{150, 150, 150}));
    settings.boost = 1;
    QCOMPARE(rgb(RawImporter::develop(path, settings)), (std::array<int, 3>{139, 139, 139}));
    settings.boost = 0.5;
    QCOMPARE(rgb(RawImporter::develop(path, settings)), (std::array<int, 3>{145, 145, 145}));
    // In the shadows BT.709 turns straight: 4.5 times the light.
    DNGFixture::Options dark;
    dark.value = [](int, int, int) { return quint16(500); };
    const QString shadow = DNGFixture::write(folder.filePath("dark.dng"), dark);
    QCOMPARE(rgb(RawImporter::develop(shadow, RawImporter::asShot(shadow).value())), (std::array<int, 3>{9, 9, 9}));
    const QImage developed = RawImporter::develop(path, settings);
    QCOMPARE(developed.format(), QImage::Format_RGBA8888_Premultiplied);
    QVERIFY(developed.colorSpace() == QColorSpace(QColorSpace::SRgb));
    QCOMPARE(middle(developed).alpha(), 255);
}

void RawImporterTests::exposureScalesTheLightBeforeTheCurve()
{
    const QTemporaryDir folder;
    const QString path = DNGFixture::write(folder.filePath("gray.dng"));
    RawDevelopSettings settings = flat(path);
    settings.exposure = 1;
    QCOMPARE(rgb(RawImporter::develop(path, settings))[1], 205);
    settings.exposure = -3;
    QCOMPARE(rgb(RawImporter::develop(path, settings))[1], 55);
    settings.exposure = 3;
    QCOMPARE(rgb(RawImporter::develop(path, settings))[1], 255);
    // Near white LibRaw keeps the level: no stretch.
    DNGFixture::Options bright;
    bright.value = [](int, int, int) { return quint16(52000); };
    const QString near = DNGFixture::write(folder.filePath("bright.dng"), bright);
    QCOMPARE(rgb(RawImporter::develop(near, flat(near))), (std::array<int, 3>{230, 230, 230}));
}

void RawImporterTests::temperatureAndTintColourTheDevelop()
{
    const QTemporaryDir folder;
    const QString path = DNGFixture::write(folder.filePath("gray.dng"));
    RawDevelopSettings settings = flat(path);
    // Balanced for tungsten, a daylight gray turns blue; green holds.
    settings.temperature = 3200;
    const std::array<int, 3> cool = rgb(RawImporter::develop(path, settings));
    QVERIFY2(cool[2] > 150 && cool[1] == 150 && cool[0] < 150, qPrintable(QStringLiteral("%1 %2 %3").arg(cool[0]).arg(cool[1]).arg(cool[2])));
    settings.temperature = 12000;
    const std::array<int, 3> warm = rgb(RawImporter::develop(path, settings));
    QVERIFY2(warm[0] > 150 && warm[1] == 150 && warm[2] < 150, qPrintable(QStringLiteral("%1 %2 %3").arg(warm[0]).arg(warm[1]).arg(warm[2])));
    // Balanced for a greener white, as Adobe's positive tint, magenta.
    settings.temperature = settings.asShotTemperature;
    settings.tint = settings.asShotTint + 60;
    const std::array<int, 3> magenta = rgb(RawImporter::develop(path, settings));
    QVERIFY2(magenta[0] > 150 && magenta[1] == 150 && magenta[2] > 150, qPrintable(QStringLiteral("%1 %2 %3").arg(magenta[0]).arg(magenta[1]).arg(magenta[2])));
    // A white the camera cannot see is refused, not ignored.
    settings.temperature = 2000;
    settings.tint = settings.asShotTint;
    QCOMPARE(refusal([&] { RawImporter::develop(path, settings); }), std::optional(ImageImportError::Kind::whiteBalance));
    QCOMPARE(QString(ImageImportError(ImageImportError::Kind::whiteBalance).what()),
             QString("This camera can’t record that white balance. Move Temperature or Tint back toward the shot."));
}

void RawImporterTests::coloursComeThroughTheCameraMatrix()
{
    const QTemporaryDir folder;
    DNGFixture::Options red;
    red.value = [](int channel, int, int) { return quint16(channel == 0 ? 30000 : 0); };
    const QString path = DNGFixture::write(folder.filePath("red.dng"), red);
    const std::array<int, 3> developed = rgb(RawImporter::develop(path, flat(path)));
    QVERIFY2(developed[0] > 170 && developed[1] < 5 && developed[2] < 5, qPrintable(QStringLiteral("%1 %2 %3").arg(developed[0]).arg(developed[1]).arg(developed[2])));
}

void RawImporterTests::orientationAndThePreviewsLimit()
{
    const QTemporaryDir folder;
    DNGFixture::Options turned;
    turned.orientation = 6;
    const QString path = DNGFixture::write(folder.filePath("turned.dng"), turned);
    QCOMPARE(RawImporter::pixelSize(path).value(), QSize(48, 64));
    QCOMPARE(RawImporter::develop(path, flat(path)).size(), QSize(48, 64));
    const QString upright = DNGFixture::write(folder.filePath("upright.dng"));
    QCOMPARE(RawImporter::pixelSize(upright).value(), QSize(64, 48));
    // Past the limit, a half-size decode that then fits.
    QCOMPARE(RawImporter::develop(upright, flat(upright), 32).size(), QSize(32, 24));
    QCOMPARE(RawImporter::develop(upright, flat(upright), 20).size(), QSize(20, 15));
    QCOMPARE(RawImporter::develop(upright, flat(upright), 64).size(), QSize(64, 48));
}

void RawImporterTests::theQueueKeepsThePreviewsDecodeUntilReleased()
{
    const QTemporaryDir folder;
    const QString path = DNGFixture::write(folder.filePath("gray.dng"));
    RawImporter::Queue &queue = RawImporter::Queue::shared();
    const RawDevelopSettings settings = flat(path);
    QCOMPARE(rgb(queue.develop(path, settings, 40))[1], 150);
    // The file changes; the kept decode does not, until released.
    DNGFixture::Options darker;
    darker.value = [](int, int, int) { return quint16(10000); };
    DNGFixture::write(path, darker);
    QCOMPARE(rgb(queue.develop(path, settings, 40))[1], 150);
    QCOMPARE(rgb(queue.develop(path, settings, std::nullopt))[1], 109);
    RawDevelopSettings brighter = settings;
    brighter.exposure = 1;
    QCOMPARE(rgb(queue.develop(path, brighter, 40))[1], 205);
    queue.release();
    QCOMPARE(rgb(queue.develop(path, brighter, 40))[1], 150);
    // Another limit halves the kept frame; another file decodes afresh.
    QCOMPARE(queue.develop(path, settings, 20).size(), QSize(20, 15));
    QCOMPARE(queue.develop(path, settings, 40).size(), QSize(32, 24));
    QCOMPARE(queue.develop(path, settings, 40).size(), QSize(32, 24));
    const QString other = DNGFixture::write(folder.filePath("other.dng"));
    QCOMPARE(rgb(queue.develop(other, settings, 20))[1], 150);
    QCOMPARE(rgb(queue.develop(path, settings, 20))[1], 109);
    queue.release();
}

void RawImporterTests::aMixingCameraDevelopsAlike()
{
    // Channels leak into each other and differ in gain.
    const std::array<double, 9> leaky{0.35, 0.1, 0.05, 0.1, 0.8, 0.1, 0.04, 0.2, 0.56};
    const auto seen = [&](std::array<double, 3> scene) {
        return [leaky, scene](int channel, int, int) {
            const size_t row = size_t(channel) * 3;
            return quint16(std::lround(65535 * (leaky[row] * scene[0] + leaky[row + 1] * scene[1] + leaky[row + 2] * scene[2])));
        };
    };
    const QTemporaryDir folder;
    DNGFixture::Options gray;
    gray.mix = leaky;
    gray.asShotNeutral = {(0.35 + 0.1 + 0.05) / 1.0, 1, (0.04 + 0.2 + 0.56) / 1.0};
    gray.value = seen({20000 / 65535.0, 20000 / 65535.0, 20000 / 65535.0});
    const QString path = DNGFixture::write(folder.filePath("gray.dng"), gray);
    const RawDevelopSettings shot = RawImporter::asShot(path).value();
    QVERIFY2(std::abs(shot.temperature - 6500.7) < 2 && std::abs(shot.tint - 9.52) < 0.1, qPrintable(QStringLiteral("%1 %2").arg(shot.temperature).arg(shot.tint)));
    QCOMPARE(rgb(RawImporter::develop(path, flat(path))), (std::array<int, 3>{150, 150, 150}));
    DNGFixture::Options red = gray;
    red.value = seen({30000 / 65535.0, 0, 0});
    const QString redPath = DNGFixture::write(folder.filePath("red.dng"), red);
    const std::array<int, 3> developed = rgb(RawImporter::develop(redPath, flat(path)));
    QVERIFY2(developed[0] > 170 && developed[1] < 5 && developed[2] < 5, qPrintable(QStringLiteral("%1 %2 %3").arg(developed[0]).arg(developed[1]).arg(developed[2])));
    // Clipped photosites stay white, whichever channels clip.
    for (const double level : {1.3, 3.0}) {
        DNGFixture::Options clipped = gray;
        clipped.value = [leaky, level](int channel, int, int) {
            const size_t row = size_t(channel) * 3;
            return quint16(std::min(65535.0, 65535 * level * (leaky[row] + leaky[row + 1] + leaky[row + 2])));
        };
        const QString clippedPath = DNGFixture::write(folder.filePath("clipped.dng"), clipped);
        for (const float boost : {0.0f, 1.0f}) {
            RawDevelopSettings settings = shot;
            settings.boost = boost;
            QCOMPARE(rgb(RawImporter::develop(clippedPath, settings)), (std::array<int, 3>{255, 255, 255}));
        }
    }
}

void RawImporterTests::aKeptDecodeDevelopsAsAFreshOne()
{
    const QTemporaryDir folder;
    DNGFixture::Options leaky;
    leaky.mix = {0.35, 0.1, 0.05, 0.1, 0.8, 0.1, 0.04, 0.2, 0.56};
    leaky.value = [](int channel, int, int) { return quint16(std::array{10000, 20000, 16000}[size_t(channel)]); };
    const QString path = DNGFixture::write(folder.filePath("leaky.dng"), leaky);
    RawImporter::Queue &queue = RawImporter::Queue::shared();
    RawDevelopSettings settings = flat(path);
    for (const auto &[kelvin, tint] : std::vector<std::pair<float, float>>{{3200, 0}, {3200, 0}, {9000, 40}, {settings.asShotTemperature, settings.asShotTint}}) {
        settings.temperature = kelvin;
        settings.tint = tint;
        QCOMPARE(rgb(queue.develop(path, settings, 40)), rgb(RawImporter::develop(path, settings, 40)));
    }
    queue.release();
}

void RawImporterTests::fourColourSensorsAreUnsupported()
{
    const QTemporaryDir folder;
    DNGFixture::Options four;
    four.fourColours = true;
    const QString path = DNGFixture::write(folder.filePath("four.dng"), four);
    QCOMPARE(RawImporter::pixelSize(path).value(), QSize(64, 48));
    QVERIFY(!RawImporter::asShot(path));
    QCOMPARE(refusal([&] { RawImporter::develop(path, RawDevelopSettings()); }), std::optional(ImageImportError::Kind::unsupported));
}

void RawImporterTests::whatIsNoRawIsRefused()
{
    const QTemporaryDir folder;
    QFile fake(folder.filePath("fake.dng"));
    QVERIFY(fake.open(QIODevice::WriteOnly) && fake.write("not a raw") == 9);
    fake.close();
    QCOMPARE(refusal([&] { RawImporter::develop(fake.fileName(), RawDevelopSettings()); }), std::optional(ImageImportError::Kind::unreadable));
    QVERIFY(!RawImporter::asShot(fake.fileName()) && !RawImporter::pixelSize(fake.fileName()));
    const QString missing = folder.filePath("missing.dng");
    QCOMPARE(refusal([&] { RawImporter::develop(missing, RawDevelopSettings()); }), std::optional(ImageImportError::Kind::unreadable));
    QVERIFY(!RawImporter::asShot(missing) && !RawImporter::pixelSize(missing));
}

void RawImporterTests::settingsResetToTheShot()
{
    RawDevelopSettings settings;
    settings.asShotTemperature = 4100;
    settings.asShotTint = 7;
    QVERIFY(!settings.isAsShot());
    settings.reset();
    QVERIFY(settings.isAsShot());
    QVERIFY(settings.temperature == 4100 && settings.tint == 7 && settings.exposure == 0 && settings.boost == 1);
    for (const auto &change : std::vector<std::function<void(RawDevelopSettings &)>>{
             [](RawDevelopSettings &s) { s.exposure = 0.5; }, [](RawDevelopSettings &s) { s.boost = 0.9f; },
             [](RawDevelopSettings &s) { s.temperature = 4000; }, [](RawDevelopSettings &s) { s.tint = 0; }}) {
        RawDevelopSettings changed = settings;
        change(changed);
        QVERIFY(!changed.isAsShot());
    }
}

QTEST_GUILESS_MAIN(RawImporterTests)
#include "RawImporterTests.moc"
