#include "IO/PSD/PSDDocumentBuilder.h"
#include "IO/PSD/PSDReader.h"
#include "PSDFixture.h"
#include <QTemporaryDir>
#include <QtTest>

// Swift's PSDRoundTripTests: files written by the fixture, read back.
namespace {
QByteArray header(quint16 version = 1, quint32 width = 8, quint32 height = 8, quint16 depth = 8, quint16 mode = 3)
{
    PSDFixture::Buffer data;
    data.string("8BPS");
    data.u16(version);
    data.bytes(QByteArray(6, '\0'));
    data.u16(3);
    data.u32(height);
    data.u32(width);
    data.u16(depth);
    data.u16(mode);
    return data.data;
}

QByteArray rawChannel(const QByteArray &plane)
{
    return QByteArray("\0\0", 2) + plane;
}

// One layer "Big" at the origin with the given channels.
QByteArray layerFile(qint32 layerWidth, qint32 layerHeight, const std::vector<PSDFixture::Channel> &channels, quint32 canvasWidth = 8,
                     quint32 canvasHeight = 8)
{
    PSDFixture::Buffer data;
    data.bytes(header(1, canvasWidth, canvasHeight));
    data.u32(0);
    data.u32(0);
    PSDFixture::Buffer records, payloads;
    records.i16(1);
    records.i32(0);
    records.i32(0);
    records.i32(layerHeight);
    records.i32(layerWidth);
    records.u16(quint16(channels.size()));
    for (const PSDFixture::Channel &channel : channels) {
        records.i16(channel.id);
        records.u32(quint32(channel.payload.size()));
        payloads.bytes(channel.payload);
    }
    records.string("8BIMnorm");
    records.bytes(QByteArray("\xff\0\0\0", 4));
    records.u32(12);
    records.u32(0);
    records.u32(0);
    records.u8(3);
    records.string("Big");
    PSDFixture::Buffer info;
    info.u32(quint32(records.data.size() + payloads.data.size()));
    info.bytes(records.data);
    info.bytes(payloads.data);
    info.u32(0);
    data.u32(quint32(info.data.size()));
    data.bytes(info.data);
    return data.data;
}

std::vector<quint32> lsctTypes(const QByteArray &data)
{
    std::vector<quint32> types;
    qsizetype search = 0;
    for (qsizetype found = data.indexOf("lsct", search); found >= 0 && found + 12 <= data.size(); found = data.indexOf("lsct", search)) {
        const qsizetype type = found + 8;
        types.push_back(quint32(uchar(data[type])) << 24 | quint32(uchar(data[type + 1])) << 16 | quint32(uchar(data[type + 2])) << 8 | uchar(data[type + 3]));
        search = found + 4;
    }
    return types;
}

ImageLayer named(const PSDImport &imported, const QString &name)
{
    const auto found = std::find_if(imported.layers.begin(), imported.layers.end(), [&](const ImageLayer &layer) { return layer.name == name; });
    if (found == imported.layers.end())
        throw std::runtime_error("no layer named " + name.toStdString());
    return *found;
}

template <typename Error> std::optional<typename Error::Kind> refusal(const std::function<void()> &run)
{
    try {
        run();
    } catch (const Error &error) {
        return error.kind;
    }
    return std::nullopt;
}
}

class PSDRoundTripTests : public QObject {
    Q_OBJECT
private slots:
    void roundTripLayersOrderVisibilityOpacityAndBlend();
    void roundTripGroupsMasksAndClipping();
    void importedGroupsFollowPhotoshopLsctOrder();
    void oversizedLayerBoundsAreRejected();
    void unusedSpotChannelsAreSkippedBeforeDecode();
    void unsupportedCompressionOnColorChannelsIsStillRejected();
    void matchesRequiresPhotoshopMagic();
    void unknownBlendProducesConversionReport();
    void softLightImportsWithoutConversion();
    void folderOpacityImportsOntoTheFolder();
    void unsupportedHeadersAreRejected();
};

void PSDRoundTripTests::roundTripLayersOrderVisibilityOpacityAndBlend()
{
    PSDRecord bottom = PSDFixture::record("Red", PSDFixture::colorImage(2, 2, 1, 0, 0), QRectF(0, 0, 2, 2));
    bottom.opacity = 0.5;
    bottom.blendKey = "mul ";
    PSDRecord top = PSDFixture::record("Blue", PSDFixture::colorImage(2, 2, 0, 0, 1), QRectF(2, 0, 2, 2));
    top.isVisible = false;
    const QByteArray data = PSDFixture::data(PSDDocument{4, 4, 144, {bottom, top}}, PSDFixture::colorImage(4, 4, 0, 0, 0, 0));
    QVERIFY(data.startsWith("8BPS"));
    const PSDDocument document = PSDReader::read(data);
    QCOMPARE(document.width, 4);
    QCOMPARE(document.height, 4);
    QCOMPARE(document.resolution, 144.0);
    QCOMPARE(document.layers.size(), size_t(2));
    QCOMPARE(document.layers[0].name, QString("Red"));
    QCOMPARE(document.layers[1].name, QString("Blue"));
    QVERIFY(document.layers[0].isVisible && !document.layers[1].isVisible);
    QVERIFY(std::abs(document.layers[0].opacity - 0.5) < 0.01);
    QCOMPARE(document.layers[0].blendKey, QString("mul "));
    QCOMPARE(document.layers[0].image.value().width(), 2);
    // The pixels come back as written, premultiplied.
    QCOMPARE(document.layers[0].image.value().pixel(1, 1), qRgba(255, 0, 0, 255));
    QCOMPARE(document.layers[0].bounds, QRectF(0, 0, 2, 2));
    QCOMPARE(document.layers[1].bounds, QRectF(2, 0, 2, 2));
    const PSDImport imported = PSDDocumentBuilder::makeImport(document);
    QVERIFY(imported.conversions.empty());
    QCOMPARE(imported.layers.size(), size_t(2));
    QCOMPARE(imported.layers[0].name, QString("Red"));
    QCOMPARE(imported.layers[1].name, QString("Blue"));
    QVERIFY(imported.layers[0].blendMode == LayerBlendMode::multiply);
    QVERIFY(!imported.layers[1].isVisible);
    QCOMPARE(imported.layers[1].origin(), QPointF(2, 0));
    QCOMPARE(imported.layers[1].size(), QSizeF(2, 2));
    QCOMPARE(imported.layers[0].id, document.layers[0].id);
    QCOMPARE(imported.layers[0].asset.value().name, QString("Red"));
    QVERIFY(!imported.layers[0].asset.value().thumbnail.isNull());
}

void PSDRoundTripTests::roundTripGroupsMasksAndClipping()
{
    const QUuid groupID = QUuid::createUuid();
    PSDRecord group = PSDFixture::record("Stack");
    group.id = groupID;
    group.isGroup = true;
    group.blendKey = "pass";
    PSDRecord base = PSDFixture::record("Base", PSDFixture::colorImage(2, 2, 0, 1, 0), QRectF(0, 0, 2, 2));
    base.parentID = groupID;
    base.mask = PSDFixture::grayImage(2, 2, 255);
    PSDRecord child = PSDFixture::record("Clipped", PSDFixture::colorImage(2, 2, 1, 1, 0), QRectF(0, 0, 2, 2));
    child.parentID = groupID;
    child.clipping = true;
    const QByteArray data = PSDFixture::data(PSDDocument{4, 4, 72, {group, base, child}}, PSDFixture::colorImage(4, 4, 0, 0, 0, 0));
    const PSDImport imported = PSDDocumentBuilder::makeImport(PSDReader::read(data));
    QVERIFY(imported.conversions.empty());
    const ImageLayer folder = named(imported, "Stack");
    QVERIFY(folder.isGroup);
    QCOMPARE(folder.size(), QSizeF(4, 4));
    const ImageLayer importedBase = named(imported, "Base"), importedChild = named(imported, "Clipped");
    QCOMPARE(importedBase.parentID, std::optional(folder.id));
    QCOMPARE(importedChild.parentID, std::optional(folder.id));
    QVERIFY(importedBase.mask.has_value());
    QCOMPARE(importedBase.mask.value().asset.image().pixel(0, 0), qRgb(255, 255, 255));
    QCOMPARE(importedChild.maskSourceID, std::optional(importedBase.id));
}

void PSDRoundTripTests::importedGroupsFollowPhotoshopLsctOrder()
{
    PSDRecord group = PSDFixture::record("Stack");
    group.isGroup = true;
    PSDRecord child = PSDFixture::record("Base", PSDFixture::colorImage(2, 2, 0, 1, 0), QRectF(0, 0, 2, 2));
    child.parentID = group.id;
    const QByteArray data = PSDFixture::data(PSDDocument{4, 4, 72, {group, child}}, PSDFixture::colorImage(4, 4, 0, 0, 0, 0));
    QCOMPARE(lsctTypes(data), (std::vector<quint32>{3, 1}));
    const PSDImport imported = PSDDocumentBuilder::makeImport(PSDReader::read(data));
    const ImageLayer folder = named(imported, "Stack");
    QVERIFY(folder.isGroup);
    QCOMPARE(named(imported, "Base").parentID, std::optional(folder.id));
}

void PSDRoundTripTests::oversizedLayerBoundsAreRejected()
{
    const QByteArray raw("\0\0", 2);
    const auto oversized = layerFile(30'000, 30'000, {{-1, raw}, {0, raw}, {1, raw}, {2, raw}});
    QCOMPARE(refusal<ImageImportError>([&] { PSDReader::read(oversized); }), std::optional(ImageImportError::Kind::tooLarge));
    const QImage fill = PSDFixture::colorImage(20, 20, 1, 0, 0);
    const QByteArray data = PSDFixture::data(PSDDocument{20, 20, 72, {PSDFixture::record("Huge", fill, QRectF(0, 0, 20, 20))}}, fill);
    QCOMPARE(refusal<ImageImportError>([&] { PSDReader::read(data, 50); }), std::optional(ImageImportError::Kind::tooLarge));
    // The budget holds at its edge: 400 read, 399 not.
    QCOMPARE(PSDReader::read(data, 400).layers.size(), size_t(1));
    QCOMPARE(refusal<ImageImportError>([&] { PSDReader::read(data, 399); }), std::optional(ImageImportError::Kind::tooLarge));
}

void PSDRoundTripTests::unusedSpotChannelsAreSkippedBeforeDecode()
{
    std::vector<PSDFixture::Channel> channels;
    for (const qint16 id : {-1, 0, 1, 2})
        channels.push_back({id, rawChannel(QByteArray(4, '\xff'))});
    // Compression 99 throws if unpacked; 56 channels at most.
    for (qint16 id = 3; id <= 54; ++id)
        channels.push_back({id, QByteArray("\0\x63\0\0", 4)});
    const PSDDocument document = PSDReader::read(layerFile(2, 2, channels));
    QCOMPARE(document.layers.size(), size_t(1));
    QCOMPARE(document.layers[0].image.value().size(), QSize(2, 2));
    QCOMPARE(document.layers[0].name, QString("Big"));
    channels.push_back({55, QByteArray("\0\0", 2)});
    QCOMPARE(refusal<ImageImportError>([&] { PSDReader::read(layerFile(2, 2, channels)); }), std::optional(ImageImportError::Kind::tooLarge));
}

void PSDRoundTripTests::unsupportedCompressionOnColorChannelsIsStillRejected()
{
    const QByteArray pixels(4, '\xff');
    const std::vector<PSDFixture::Channel> channels{{-1, rawChannel(pixels)}, {0, QByteArray("\0\x63\0\0", 4)}, {1, rawChannel(pixels)}, {2, rawChannel(pixels)}};
    QCOMPARE(refusal<PSDError>([&] { PSDReader::read(layerFile(2, 2, channels)); }), std::optional(PSDError::Kind::unsupportedCompression));
}

void PSDRoundTripTests::matchesRequiresPhotoshopMagic()
{
    const QTemporaryDir folder;
    QFile jpeg(folder.filePath("picture.psd"));
    QVERIFY(jpeg.open(QIODevice::WriteOnly) && jpeg.write("\xff\xd8\xff\xe0", 4) == 4);
    jpeg.close();
    QVERIFY(!PSDReader::matches(jpeg.fileName()));
    QFile psd(folder.filePath("picture.bin"));
    QVERIFY(psd.open(QIODevice::WriteOnly) && psd.write("8BPS") == 4);
    psd.close();
    QVERIFY(PSDReader::matches(psd.fileName()));
    QVERIFY(!PSDReader::matches(folder.filePath("missing.psd")));
    QVERIFY(PSDReader::matches(QByteArray("8BPS")) && !PSDReader::matches(QByteArray("8BP")));
}

void PSDRoundTripTests::unknownBlendProducesConversionReport()
{
    const QImage fill = PSDFixture::colorImage(2, 2, 1, 0, 0);
    PSDRecord layer = PSDFixture::record("Vivid", fill, QRectF(0, 0, 2, 2));
    layer.blendKey = "vLit";
    const PSDImport imported = PSDDocumentBuilder::makeImport(PSDReader::read(PSDFixture::data(PSDDocument{2, 2, 72, {layer}}, fill)));
    QCOMPARE(imported.conversions.size(), size_t(1));
    QCOMPARE(imported.conversions[0].layerName, QString("Vivid"));
    QCOMPARE(imported.conversions[0].message, QString("Blend mode “vLit” isn’t supported and will be applied as Normal."));
    QVERIFY(imported.layers[0].blendMode == LayerBlendMode::normal);
}

void PSDRoundTripTests::softLightImportsWithoutConversion()
{
    const QImage fill = PSDFixture::colorImage(2, 2, 1, 0, 0);
    PSDRecord layer = PSDFixture::record("Soft", fill, QRectF(0, 0, 2, 2));
    layer.blendKey = "sLit";
    const PSDImport imported = PSDDocumentBuilder::makeImport(PSDReader::read(PSDFixture::data(PSDDocument{2, 2, 72, {layer}}, fill)));
    QVERIFY(imported.conversions.empty());
    QVERIFY(imported.layers[0].blendMode == LayerBlendMode::softLight);
}

void PSDRoundTripTests::folderOpacityImportsOntoTheFolder()
{
    const QImage fill = PSDFixture::colorImage(2, 2, 0, 1, 0);
    PSDRecord group = PSDFixture::record("Stack");
    group.isGroup = true;
    group.opacity = 0.5;
    PSDRecord child = PSDFixture::record("Base", fill, QRectF(0, 0, 2, 2));
    child.parentID = group.id;
    const PSDImport imported = PSDDocumentBuilder::makeImport(PSDReader::read(PSDFixture::data(PSDDocument{4, 4, 72, {group, child}}, fill)));
    // One byte of opacity: half comes back as 128/255.
    QCOMPARE(named(imported, "Stack").opacity, 128 / 255.0);
    QVERIFY(imported.conversions.empty());
}

void PSDRoundTripTests::unsupportedHeadersAreRejected()
{
    QCOMPARE(refusal<PSDError>([] { PSDReader::read(header(2)); }), std::optional(PSDError::Kind::unsupportedVersion));
    QCOMPARE(refusal<PSDError>([] { PSDReader::read(header(1, 8, 8, 8, 4)); }), std::optional(PSDError::Kind::unsupportedColorMode));
    QCOMPARE(refusal<PSDError>([] { PSDReader::read(header(1, 8, 8, 16)); }), std::optional(PSDError::Kind::unsupportedDepth));
    QCOMPARE(refusal<ImageImportError>([] { PSDReader::read(header(1, 30'001, 10)); }), std::optional(ImageImportError::Kind::tooLarge));
    QCOMPARE(refusal<ImageImportError>([] { PSDReader::read(header(1, 10'001, 10'000)); }), std::optional(ImageImportError::Kind::tooLarge));
    QCOMPARE(refusal<ImageImportError>([] { PSDReader::read(header(1, 0, 10)); }), std::optional(ImageImportError::Kind::tooLarge));
    QCOMPARE(refusal<ImageImportError>([] { PSDReader::read(QByteArray("8BPX")); }), std::optional(ImageImportError::Kind::unreadable));
    QCOMPARE(refusal<PSDError>([] { PSDReader::read(header()); }), std::optional(PSDError::Kind::truncated));
    QCOMPARE(QString::fromUtf8(PSDError(PSDError::Kind::unsupportedDepth).what()), QString("Only 8-bit RGB Photoshop files can be imported."));
    QCOMPARE(QString::fromUtf8(PSDError(PSDError::Kind::unsupportedColorMode).what()), QString("Only 8-bit RGB Photoshop files can be imported."));
    QCOMPARE(QString::fromUtf8(PSDError(PSDError::Kind::unsupportedVersion).what()), QString("Large Document (.psb) Photoshop files aren’t supported."));
    QCOMPARE(QString::fromUtf8(PSDError(PSDError::Kind::truncated).what()), QString("The Photoshop file could not be read. It may be damaged or incomplete."));
    QCOMPARE(QString::fromUtf8(PSDError(PSDError::Kind::unsupportedCompression).what()),
             QString("This Photoshop file uses a layer compression method that isn’t supported."));
}

QTEST_GUILESS_MAIN(PSDRoundTripTests)
#include "PSDRoundTripTests.moc"
