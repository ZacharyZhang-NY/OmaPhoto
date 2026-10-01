#include "IO/ImageExporter.h"
#include "AddressSpaceLimit.h"
#include "RenderFixtures.h"
#include <QBuffer>
#include <QImageReader>
#include <QImageWriter>
#include <QColorSpace>
#include <QTemporaryDir>
#include <QtTest>

class JPEGExportTests : public QObject {
    Q_OBJECT
private slots:
    void transparencyUsesChosenMatteAndProducesOpaqueSRGB_data();
    void transparencyUsesChosenMatteAndProducesOpaqueSRGB();
    void qualityChangesBytesAndDecodedPixels();
    void thePreviewIsDecodedFromTheFileUpToItsLimit();
    void pixelsLieOverTheMatteOnce();
    void whatCannotBeEncodedDecodedOrAllocatedThrows();
    void cancellationStopsAtSwiftsThreePoints();
    void anEncodingWithoutRoomIsARenderError();
};

void JPEGExportTests::transparencyUsesChosenMatteAndProducesOpaqueSRGB_data()
{
    QTest::addColumn<JPEGOptions>("options");
    QTest::newRow("the default white") << JPEGOptions{};
    QTest::newRow("blue at full quality") << JPEGOptions{.quality = 1, .red = 0, .green = 0, .blue = 1};
    QTest::newRow("a mixed matte") << JPEGOptions{.quality = 1, .red = 0.8, .green = 0.4, .blue = 0.2};
}

void JPEGExportTests::transparencyUsesChosenMatteAndProducesOpaqueSRGB()
{
    QFETCH(JPEGOptions, options);
    const ProjectSnapshot blank{.manifest = {.documentID = QUuid::createUuid(), .width = 20, .height = 12, .activeLayerID = std::nullopt, .layers = {}}, .images = {}};
    const JPEGResult result = ImageExporter::jpeg(ImageExporter::render(blank), options);
    QVERIFY(result.data.startsWith("\xff\xd8\xff"));
    const QImage image = QImage::fromData(result.data, "jpeg");
    QCOMPARE(image.size(), QSize(20, 12));
    QCOMPARE(image.colorSpace(), QColorSpace(QColorSpace::SRgb));
    QVERIFY(!image.hasAlphaChannel());
    const QColor pixel = image.pixelColor(0, 0);
    QVERIFY(std::abs(pixel.redF() - options.red) < 0.03);
    QVERIFY(std::abs(pixel.greenF() - options.green) < 0.03);
    QVERIFY(std::abs(pixel.blueF() - options.blue) < 0.03);
}

void JPEGExportTests::qualityChangesBytesAndDecodedPixels()
{
    QImage busy = BrushRaster::context(128, 128, false);
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x)
            busy.setPixel(x, y, qRgba((x * 37 + y * 17) % 256, (x * 11 + y * 53) % 256, (x * 79 + y * 7) % 256, 255));
    }
    const ExportRaster raster{busy};
    const JPEGResult low = ImageExporter::jpeg(raster, {.quality = 0.1}), high = ImageExporter::jpeg(raster, {.quality = 1});
    QVERIFY(low.data.size() < high.data.size());
    // Quality is clamped to what the format knows.
    QCOMPARE(ImageExporter::jpeg(raster, {.quality = 7}).data, high.data);
    QCOMPARE(ImageExporter::jpeg(raster, {.quality = -3}).data, ImageExporter::jpeg(raster, {.quality = 0}).data);
    const QImage lowPixels = QImage::fromData(low.data), highPixels = QImage::fromData(high.data);
    double difference = 0;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x)
            difference += std::abs(lowPixels.pixelColor(x, y).redF() - highPixels.pixelColor(x, y).redF());
    }
    QVERIFY(difference > 1);
    // Quality 1 is the writer's 100, 0.1 its 10.
    const auto byTheWriter = [&](int quality) {
        QImage opaque = busy.convertToFormat(QImage::Format_RGB888);
        opaque.setDotsPerMeterX(2835);
        opaque.setDotsPerMeterY(2835);
        opaque.setColorSpace(QColorSpace::SRgb);
        QByteArray data;
        QBuffer buffer(&data);
        QImageWriter writer(&buffer, "jpeg");
        writer.setQuality(quality);
        writer.write(opaque);
        return data;
    };
    QCOMPARE(high.data, byTheWriter(100));
    QCOMPARE(low.data, byTheWriter(10));
    QTemporaryDir directory;
    const QString path = directory.filePath("export.jpg");
    ImageExporter::write(high.data, path);
    QFile written(path);
    QVERIFY(written.open(QIODevice::ReadOnly));
    QCOMPARE(written.readAll(), high.data);
}

void JPEGExportTests::thePreviewIsDecodedFromTheFileUpToItsLimit()
{
    const JPEGResult small = ImageExporter::jpeg({noise(64, 48, 1)}, {.quality = 0.1});
    QCOMPARE(small.preview.size(), QSize(64, 48));
    // Compression shows in the preview: it is not the source.
    QCOMPARE(small.preview.convertToFormat(QImage::Format_RGB888), QImage::fromData(small.data).convertToFormat(QImage::Format_RGB888));
    QVERIFY(small.preview.convertToFormat(QImage::Format_RGB888) != noise(64, 48, 1).convertToFormat(QImage::Format_RGB888));
    // Full size for 100%, past Qt's 128 MB decode limit.
    QCOMPARE(ImageExporter::jpeg({solid(6000, 6000, qRgba(10, 200, 30, 255))}, {}).preview.size(), QSize(6000, 6000));
    QCOMPARE(ImageExporter::jpeg({solid(2400, 600, qRgba(10, 200, 30, 255))}, {}).preview.size(), QSize(2400, 600));
    QCOMPARE(JPEGResult::previewLimit, 8192);
    QCOMPARE(ImageExporter::jpeg({solid(8192, 4, qRgba(10, 200, 30, 255))}, {}).preview.size(), QSize(8192, 4));
    const JPEGResult wide = ImageExporter::jpeg({solid(9000, 900, qRgba(10, 200, 30, 255))}, {});
    QCOMPARE(wide.preview.size(), QSize(8192, 819));
    QCOMPARE(QImage::fromData(wide.data).size(), QSize(9000, 900));
    QCOMPARE(ImageExporter::jpeg({solid(900, 9000, qRgba(10, 200, 30, 255))}, {}).preview.size(), QSize(819, 8192));
    // Thin images keep a pixel across.
    QCOMPARE(ImageExporter::jpeg({solid(9000, 1, qRgba(10, 200, 30, 255))}, {}).preview.size(), QSize(8192, 1));
    QCOMPARE(ImageExporter::jpeg({solid(1, 9000, qRgba(10, 200, 30, 255))}, {}).preview.size(), QSize(1, 8192));
}

void JPEGExportTests::pixelsLieOverTheMatteOnce()
{
    // Half-transparent red over a blue matte.
    const JPEGResult result = ImageExporter::jpeg({solid(16, 16, qRgba(128, 0, 0, 128))}, {.quality = 1, .red = 0, .green = 0, .blue = 1});
    const QColor pixel = QImage::fromData(result.data).pixelColor(8, 8);
    QVERIFY2(std::abs(pixel.red() - 128) <= 3 && pixel.green() <= 3 && std::abs(pixel.blue() - 127) <= 3, qPrintable(pixel.name()));
}

void JPEGExportTests::whatCannotBeEncodedDecodedOrAllocatedThrows()
{
    const auto kind = [](const ExportRaster &raster) -> std::optional<ExportError::Kind> {
        try {
            ImageExporter::jpeg(raster, {});
        } catch (const ExportError &error) {
            return error.kind;
        }
        return std::nullopt;
    };
    // JPEG ends at 65,535 pixels a side.
    QImage endless(70'000, 1, QImage::Format_RGBA8888_Premultiplied);
    endless.fill(0);
    QCOMPARE(kind({endless}), std::optional(ExportError::Kind::encode));
    // A preview the reader may not allocate.
    const int allowed = QImageReader::allocationLimit();
    QImageReader::setAllocationLimit(1);
    const std::optional<ExportError::Kind> unread = kind({solid(1000, 1000, qRgba(1, 2, 3, 255))});
    QImageReader::setAllocationLimit(allowed);
    QCOMPARE(unread, std::optional(ExportError::Kind::encode));
    QCOMPARE(kind({solid(1000, 1000, qRgba(1, 2, 3, 255))}), std::nullopt);
    // No room for the flattened copy: 192 MB.
    const QImage large = solid(8192, 8192, qRgba(1, 2, 3, 255));
    std::optional<AddressSpaceLimit> limit(std::in_place, 100 * 1024 * 1024);
    const std::optional<ExportError::Kind> unflattened = kind({large});
    limit.reset();
    QCOMPARE(unflattened, std::optional(ExportError::Kind::render));
}

// The flatten fits; the growing file does not.
void JPEGExportTests::anEncodingWithoutRoomIsARenderError()
{
    const ExportRaster raster{noise(6000, 4000, 7)};
    std::optional<ExportError::Kind> failed;
    {
        const AddressSpaceLimit limit(170 * 1024 * 1024);
        try {
            ImageExporter::jpeg(raster, {.quality = 1});
        } catch (const ExportError &error) {
            failed = error.kind;
        }
    }
    QCOMPARE(failed, std::optional(ExportError::Kind::render));
}

// Before the flatten, before the encoding, before the preview.
void JPEGExportTests::cancellationStopsAtSwiftsThreePoints()
{
    const ExportRaster raster{noise(64, 48, 1)};
    int asked = 0;
    QCOMPARE(ImageExporter::jpeg(raster, {}, [&asked] { return ++asked < 0; }).data, ImageExporter::jpeg(raster, {}).data);
    QCOMPARE(asked, 3);
    for (int point = 1; point <= 3; ++point) {
        asked = 0;
        QVERIFY_THROWS_EXCEPTION(CancellationError, ImageExporter::jpeg(raster, {}, [&asked, point] { return ++asked == point; }));
        QCOMPARE(asked, point);
    }
}

QTEST_MAIN(JPEGExportTests)
#include "JPEGExportTests.moc"
