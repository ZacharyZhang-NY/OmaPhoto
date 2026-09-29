#include "IO/PSD/PSDChannelCoder.h"
#include "IO/PSD/PSDDocumentBuilder.h"
#include "IO/PSD/PSDReader.h"
#include "AddressSpaceLimit.h"
#include "PSDReaderFixtures.h"
#include <QtTest>

// Swift 1.2.10 (1fcf854): oversized layers cut to the canvas.
namespace {
QImage rgbaImage(int width, int height)
{
    std::vector<uchar> red, green, blue, alpha;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            red.push_back(uchar(x * 30 + y));
            green.push_back(uchar(y * 50));
            blue.push_back(uchar(255 - x * 20));
            alpha.push_back(255);
        }
    }
    return PSDChannelCoder::rgbaImage(width, height, red, green, blue, alpha);
}

QImage grayImage(int width, int height)
{
    std::vector<uchar> gray;
    for (int index = 0; index < width * height; ++index)
        gray.push_back(uchar(index * 10));
    return PSDChannelCoder::maskImage(width, height, gray);
}

// A masked layer hanging off a 4 by 4 canvas.
PSDDocument overhang()
{
    PSDRecord layer = PSDFixture::record(QStringLiteral("Overhang"), rgbaImage(6, 3), QRectF(-2, 1, 6, 3));
    layer.mask = grayImage(6, 3);
    return PSDDocument{4, 4, 72, {layer}};
}

QByteArray file(const PSDDocument &document)
{
    return PSDFixture::data(document, rgbaImage(4, 4));
}

bool cropNoted(const PSDDocument &parsed, const QString &name)
{
    const PSDImport imported = PSDDocumentBuilder::makeImport(parsed, PSDDocumentBuilder::assets(parsed));
    return std::any_of(imported.conversions.begin(), imported.conversions.end(), [&](const PSDConversion &note) {
        return note.layerName == name && note.message.contains(QStringLiteral("Cropped to the canvas"));
    });
}

std::vector<uchar> sliced(const std::vector<uchar> &plane, int width, const PSDCrop &crop)
{
    std::vector<uchar> result;
    for (int row = 0; row < crop.height; ++row)
        result.insert(result.end(), plane.begin() + (crop.y + row) * width + crop.x, plane.begin() + (crop.y + row) * width + crop.x + crop.width);
    return result;
}
}

class CropToCanvasImportTests : public QObject {
    Q_OBJECT
private slots:
    void fittingDocumentKeepsOversizedLayerPixels();
    void overBudgetDocumentCropsImageAndMaskToCanvas();
    void croppedDocumentStillOverBudgetIsRejected();
    void offCanvasLayerIsKeptWithoutPixelsWhenCropping();
    void channelCropsMatchFullDecodes();
    void theLayersAreSummedBeforeAnyCut();
    void aSourcePastIntStaysWide();
    void theCutIsJudgedBeforeAnyPixel();
    void anInvertedRecordIsCutToNothing();
};

void CropToCanvasImportTests::fittingDocumentKeepsOversizedLayerPixels()
{
    const PSDDocument source = overhang();
    const PSDDocument parsed = PSDReader::read(file(source), 100);
    const PSDRecord &layer = parsed.layers.at(0);
    QCOMPARE(layer.bounds, QRectF(-2, 1, 6, 3));
    QVERIFY(!layer.croppedToCanvas);
    QCOMPARE(layer.image.value(), source.layers[0].image.value());
    QVERIFY(!cropNoted(parsed, QStringLiteral("Overhang")));
}

void CropToCanvasImportTests::overBudgetDocumentCropsImageAndMaskToCanvas()
{
    // Eighteen pixels are past twelve; the canvas's twelve fit.
    const PSDDocument source = overhang();
    const PSDDocument parsed = PSDReader::read(file(source), 12);
    const PSDRecord &layer = parsed.layers.at(0);
    QCOMPARE(layer.bounds, QRectF(0, 1, 4, 3));
    QVERIFY(layer.croppedToCanvas);
    QCOMPARE(layer.image.value(), source.layers[0].image.value().copy(2, 0, 4, 3));
    QCOMPARE(layer.mask.value(), source.layers[0].mask.value().copy(2, 0, 4, 3));
    QVERIFY(cropNoted(parsed, QStringLiteral("Overhang")));
}

void CropToCanvasImportTests::croppedDocumentStillOverBudgetIsRejected()
{
    try {
        PSDReader::read(file(overhang()), 11);
        QFAIL("a file past the budget once cut was read");
    } catch (const ImageImportError &error) {
        QCOMPARE(error.kind, ImageImportError::Kind::tooLarge);
    }
}

void CropToCanvasImportTests::offCanvasLayerIsKeptWithoutPixelsWhenCropping()
{
    // Past the right edge, and wholly before the left.
    const PSDRecord right = PSDFixture::record(QStringLiteral("Outside"), rgbaImage(2, 2), QRectF(5, 0, 2, 2));
    const PSDRecord left = PSDFixture::record(QStringLiteral("Before"), rgbaImage(2, 2), QRectF(-5, 0, 2, 2));
    const PSDDocument parsed = PSDReader::read(file(PSDDocument{4, 4, 72, {right, left}}), 1);
    for (const PSDRecord &layer : parsed.layers)
        QVERIFY(!layer.image && layer.croppedToCanvas);
    QVERIFY(cropNoted(parsed, QStringLiteral("Outside")) && cropNoted(parsed, QStringLiteral("Before")));
}

void CropToCanvasImportTests::channelCropsMatchFullDecodes()
{
    const int width = 5, height = 4;
    QByteArray source;
    for (int index = 0; index < width * height; ++index)
        source.append(char(index));
    const PSDCrop crop{1, 1, 3, 2};
    const std::vector<uchar> raw = PSDChannelCoder::decode(0, width, height, source);
    QCOMPARE(PSDChannelCoder::decode(0, width, height, source, false, crop), sliced(raw, width, crop));
    for (const bool large : {false, true}) {
        std::vector<uchar> plane(source.begin(), source.end());
        const QByteArray packed = PSDFixture::encode(plane, width, height, large);
        const std::vector<uchar> full = PSDChannelCoder::decode(1, width, height, packed, large);
        QCOMPARE(full, raw);
        QCOMPARE(PSDChannelCoder::decode(1, width, height, packed, large, crop), sliced(full, width, crop));
    }
}

void CropToCanvasImportTests::theLayersAreSummedBeforeAnyCut()
{
    // Each fits thirty alone, not both: cut, they do.
    PSDDocument document = overhang();
    document.layers[0].mask.reset();
    document.layers.insert(document.layers.begin(), PSDFixture::record(QStringLiteral("Whole"), rgbaImage(4, 4), QRectF(0, 0, 4, 4)));
    const PSDDocument parsed = PSDReader::read(file(document), 30);
    QVERIFY(!parsed.layers.at(0).croppedToCanvas && parsed.layers.at(1).croppedToCanvas);
    QCOMPARE(parsed.layers.at(1).bounds, QRectF(0, 1, 4, 3));
}

void CropToCanvasImportTests::aSourcePastIntStaysWide()
{
    // A source 2^31 wide, cut to a one-pixel canvas.
    for (const char *payload : {"\0\0", "\0\1"}) {
        PSDReaderFixtures::Record record;
        record.bounds = QRect(0, 0, 1, 1);
        record.channels = {{0, QByteArray(payload, 2)}};
        QByteArray data = PSDReaderFixtures::file({record}, {}, 1, 1);
        data.replace(48, 4, QByteArray::fromHex("ffffffff"));
        data.replace(56, 4, QByteArray::fromHex("7fffffff"));
        // As tall too: its row counts judged before held.
        data.replace(44, 4, QByteArray::fromHex("ffffffff"));
        data.replace(52, 4, QByteArray::fromHex("7fffffff"));
        std::optional<PSDError::Kind> kind;
        {
            // Nothing the size of the source may be held.
            const AddressSpaceLimit limit(64ll * 1024 * 1024);
            try {
                PSDReader::read(data, 1);
            } catch (const PSDError &error) {
                kind = error.kind;
            }
        }
        QCOMPARE(kind, std::optional(PSDError::Kind::truncated));
    }
}

void CropToCanvasImportTests::theCutIsJudgedBeforeAnyPixel()
{
    // Two pixels in one: refused before the truncated first.
    PSDReaderFixtures::Record first, second;
    first.bounds = second.bounds = QRect(0, 0, 1, 1);
    first.channels = {{0, QByteArray("\0\0", 2)}};
    second.channels = {{0, QByteArray("\0\0\x80", 3)}};
    try {
        PSDReader::read(PSDReaderFixtures::file({first, second}, {}, 1, 1), 1);
        QFAIL("two pixels were read in a budget of one");
    } catch (const ImageImportError &error) {
        QCOMPARE(error.kind, ImageImportError::Kind::tooLarge);
    }
}

void CropToCanvasImportTests::anInvertedRecordIsCutToNothing()
{
    // Right before left: cut to nothing, and reported.
    PSDReaderFixtures::Record inverted, wide;
    inverted.bounds = QRect(2, 0, -1, 1);
    wide.bounds = QRect(-1, 0, 5, 1);
    PSDReaderFixtures::solidChannels(wide.bounds, wide.channels);
    const PSDDocument parsed = PSDReader::read(PSDReaderFixtures::file({inverted, wide}, {}, 4, 1), 4);
    QVERIFY(parsed.layers.at(0).croppedToCanvas && parsed.layers.at(1).croppedToCanvas);
    QCOMPARE(parsed.layers.at(0).bounds, QRectF(2, 0, 0, 1));
    QCOMPARE(parsed.layers.at(1).bounds, QRectF(0, 0, 4, 1));
}

QTEST_GUILESS_MAIN(CropToCanvasImportTests)
#include "CropToCanvasImportTests.moc"
