#include "IO/PSD/PSDChannelCoder.h"
#include "IO/PSD/PSDDocumentBuilder.h"
#include "IO/PSD/PSDReader.h"
#include "PSDReaderFixtures.h"
#include <QColorSpace>
#include <QtTest>

// The reader's edges Swift's tests leave: bytes, names, flags, kinds.
using PSDReaderFixtures::Record;

namespace {
template <typename Error> std::optional<typename Error::Kind> refusal(const std::function<void()> &run)
{
    try {
        run();
    } catch (const Error &error) {
        return error.kind;
    }
    return std::nullopt;
}

Record pixels(QByteArray name = "Layer", QRect bounds = QRect(0, 0, 2, 2))
{
    Record record;
    record.bounds = bounds;
    record.name = name;
    PSDReaderFixtures::solidChannels(bounds, record.channels);
    return record;
}

Record folder(quint32 type, QByteArray name = "Folder", const char *blend = "norm")
{
    Record record;
    record.name = name;
    record.blendKey = blend;
    record.extras.push_back({"8BIM", "lsct", PSDReaderFixtures::section(type)});
    return record;
}

QByteArray resource(quint16 id, const QByteArray &name, const QByteArray &body)
{
    PSDFixture::Buffer data;
    data.string("8BIM");
    data.u16(id);
    data.u8(uchar(name.size()));
    data.bytes(name);
    if ((name.size() + 1) % 2 == 1)
        data.u8(0);
    data.u32(quint32(body.size()));
    data.bytes(body);
    if (body.size() % 2 == 1)
        data.u8(0);
    return data.data;
}

QByteArray resolution(double dpi)
{
    PSDFixture::Buffer fixed;
    fixed.u32(quint32(dpi * 65536));
    fixed.u32(0);
    return resource(1005, QByteArray(), fixed.data);
}

std::vector<QString> messages(const PSDImport &imported)
{
    std::vector<QString> result;
    for (const PSDConversion &conversion : imported.conversions)
        result.push_back(conversion.layerName + ": " + conversion.message);
    return result;
}
}

class PSDReaderTests : public QObject {
    Q_OBJECT
private slots:
    void packBitsDecodesRunsLiteralsAndNoOps();
    void planesPremultiplyAndMasksCopy();
    void namesReadMacRomanAndPreferUnicode();
    void fillMultipliesOpacityUnlessEffectsKeepIt();
    void maskFlagsAndMasksFromRenderedPixels();
    void longBlocksReadAndRefuseWildLengths();
    void resolutionComesFromItsResource();
    void layerCountsAndSections();
    void foldersPairWithTheirDividers();
    void kindsReportWhatTheImportConverts();
    void clippingNeedsAPixelBaseBelow();
    void levelsCurvesAndHueParse();
};

void PSDReaderTests::packBitsDecodesRunsLiteralsAndNoOps()
{
    // Row 0: literal pair, run; row 1: no-op, run.
    const QByteArray rows("\0\x05\0\x03\x01\x0a\x14\xff\x1e\x80\xfd\x07", 12);
    QCOMPARE(PSDChannelCoder::decode(1, 4, 2, rows), (std::vector<uchar>{10, 20, 30, 30, 7, 7, 7, 7}));
    const auto truncated = [](const QByteArray &data) {
        return refusal<PSDError>([&] { PSDChannelCoder::decode(1, 4, 1, data); });
    };
    const std::optional kind(PSDError::Kind::truncated);
    QCOMPARE(truncated(QByteArray("\0", 1)), kind);
    QCOMPARE(truncated(QByteArray("\0\x09\x01\x0a", 4)), kind);
    QCOMPARE(truncated(QByteArray("\0\x06\x04\1\2\3\4\5", 8)), kind);
    QCOMPARE(truncated(QByteArray("\0\x01\xfd", 3)), kind);
    QCOMPARE(truncated(QByteArray("\0\x02\xff\x07", 4)), kind);
    QCOMPARE(PSDChannelCoder::decode(0, 2, 2, QByteArray("abcde")), (std::vector<uchar>{'a', 'b', 'c', 'd'}));
    QCOMPARE(refusal<PSDError>([] { PSDChannelCoder::decode(0, 2, 2, QByteArray("abc")); }), kind);
    QCOMPARE(refusal<PSDError>([] { PSDChannelCoder::decode(2, 2, 2, QByteArray("abcd")); }), std::optional(PSDError::Kind::unsupportedCompression));
    QVERIFY(PSDChannelCoder::decode(2, 0, 2, QByteArray()).empty());
}

void PSDReaderTests::planesPremultiplyAndMasksCopy()
{
    const QImage image = PSDChannelCoder::rgbaImage(1, 1, {200}, {255}, {0}, {128});
    QCOMPARE(image.format(), QImage::Format_RGBA8888_Premultiplied);
    QVERIFY(image.colorSpace() == QColorSpace(QColorSpace::SRgb));
    const uchar *pixel = image.constScanLine(0);
    QCOMPARE((std::vector<uchar>(pixel, pixel + 4)), (std::vector<uchar>{100, 128, 0, 128}));
    const QImage mask = PSDChannelCoder::maskImage(3, 2, {1, 2, 3, 4, 5, 6});
    QCOMPARE(mask.format(), QImage::Format_Grayscale8);
    QCOMPARE(mask.constScanLine(1)[2], uchar(6));
    // A missing plane reads black, or opaque for alpha.
    Record colourless;
    colourless.bounds = QRect(0, 0, 1, 1);
    colourless.channels = {{0, QByteArray("\0\0\xff", 3)}};
    const QImage red = PSDReader::read(PSDReaderFixtures::file({colourless})).layers.at(0).image.value();
    QCOMPARE(red.pixel(0, 0), qRgba(255, 0, 0, 255));
}

void PSDReaderTests::namesReadMacRomanAndPreferUnicode()
{
    Record roman = pixels(QByteArray("Caf\x8e \xa5", 6));
    Record unicode = pixels("ignored");
    unicode.extras.push_back({"8BIM", "luni", PSDReaderFixtures::unicode(QString::fromUtf8("\0Übung\0\0", 9))});
    Record empty = pixels("");
    Record noUnits = pixels("Kept");
    noUnits.extras.push_back({"8BIM", "luni", QByteArray(4, '\0')});
    const PSDDocument document = PSDReader::read(PSDReaderFixtures::file({roman, unicode, empty, noUnits}));
    QCOMPARE(document.layers[0].name, QString::fromUtf8("Café •"));
    QCOMPARE(document.layers[1].name, QString::fromUtf8("Übung"));
    QCOMPARE(document.layers[2].name, QString("Layer"));
    QCOMPARE(document.layers[3].name, QString("Kept"));
}

void PSDReaderTests::fillMultipliesOpacityUnlessEffectsKeepIt()
{
    Record filled = pixels();
    filled.opacity = 128;
    filled.extras.push_back({"8BIM", "iOpa", QByteArray("\x80", 1)});
    Record effects = pixels();
    effects.extras = {{"8BIM", "iOpa", QByteArray("\x80", 1)}, {"8BIM", "lfx2", QByteArray("fx")}};
    Record full = pixels();
    full.extras = {{"8BIM", "iOpa", QByteArray("\xff", 1)}, {"8BIM", "lrFX", QByteArray("fx")}};
    full.opacity = 51;
    const PSDDocument document = PSDReader::read(PSDReaderFixtures::file({filled, effects, full}));
    QCOMPARE(document.layers[0].opacity, (128 / 255.0) * (128 / 255.0));
    QCOMPARE(document.layers[1].opacity, 1.0);
    QVERIFY(document.layers[1].kind == PSDLayerKind::effects);
    QCOMPARE(document.layers[2].opacity, 0.2);
    QCOMPARE(messages(PSDDocumentBuilder::makeImport(document)),
             (std::vector<QString>{"Layer: Layer effects were discarded, so the appearance may differ.",
                                   "Layer: Layer effects were discarded, so the appearance may differ."}));
}

void PSDReaderTests::maskFlagsAndMasksFromRenderedPixels()
{
    std::vector<Record> records;
    for (const uchar flags : {0, 1, 2, 8}) {
        Record record = pixels();
        record.mask = QRect(1, 1, 3, 1);
        record.maskFlags = flags;
        record.channels.push_back({-2, QByteArray("\0\0\x10\x20\x30", 5)});
        records.push_back(record);
    }
    const PSDDocument document = PSDReader::read(PSDReaderFixtures::file(records));
    const QImage mask = document.layers[0].mask.value();
    QCOMPARE(mask.size(), QSize(3, 1));
    QCOMPARE((std::vector<uchar>(mask.constScanLine(0), mask.constScanLine(0) + 3)), (std::vector<uchar>{0x10, 0x20, 0x30}));
    QVERIFY(document.layers[0].maskEnabled && document.layers[0].maskLinked);
    QVERIFY(document.layers[1].maskEnabled && !document.layers[1].maskLinked);
    QVERIFY(!document.layers[2].maskEnabled && document.layers[2].maskLinked);
    QVERIFY(!document.layers[3].mask);
    const PSDImport imported = PSDDocumentBuilder::makeImport(document);
    QVERIFY(imported.layers[1].mask.value().isEnabled && !imported.layers[1].mask.value().isLinked);
    QVERIFY(!imported.layers[2].mask.value().isEnabled);
    // A mask that is not gray is reported, left off.
    PSDDocument coloured = document;
    coloured.layers[0].mask = PSDFixture::colorImage(3, 1, 1, 1, 1);
    const PSDImport skipped = PSDDocumentBuilder::makeImport(coloured);
    QVERIFY(!skipped.layers[0].mask);
    QCOMPARE(messages(skipped), std::vector<QString>{"Layer: The layer mask couldn’t be converted to 8-bit grayscale and was skipped."});
}

void PSDReaderTests::longBlocksReadAndRefuseWildLengths()
{
    Record wide = pixels("pascal");
    wide.extras.push_back({"8B64", "luni", PSDReaderFixtures::unicode("Long")});
    QCOMPARE(PSDReader::read(PSDReaderFixtures::file({wide})).layers[0].name, QString("Long"));
    wide.extras[0].high = 0x80000000;
    QCOMPARE(refusal<ImageImportError>([&] { PSDReader::read(PSDReaderFixtures::file({wide})); }), std::optional(ImageImportError::Kind::tooLarge));
    wide.extras[0].high = 1;
    QCOMPARE(refusal<PSDError>([&] { PSDReader::read(PSDReaderFixtures::file({wide})); }), std::optional(PSDError::Kind::truncated));
    // A block of another signature ends the extras.
    Record odd = pixels("pascal");
    odd.extras = {{"XXXX", "luni", PSDReaderFixtures::unicode("Never")}};
    QCOMPARE(PSDReader::read(PSDReaderFixtures::file({odd})).layers[0].name, QString("pascal"));
}

void PSDReaderTests::resolutionComesFromItsResource()
{
    const auto dpi = [](const QByteArray &resources) { return PSDReader::read(PSDReaderFixtures::file({pixels()}, resources)).resolution; };
    QCOMPARE(dpi(QByteArray()), 72.0);
    QCOMPARE(dpi(resolution(300)), 300.0);
    QCOMPARE(dpi(resolution(0.5)), 72.0);
    QCOMPARE(dpi(resolution(20'000)), 9600.0);
    // Odd names and bodies are padded before the next resource.
    QCOMPARE(dpi(resource(1000, "ab", "xyz") + resolution(150)), 150.0);
    QCOMPARE(dpi(resource(1005, QByteArray(), "abc") + resolution(150)), 150.0);
}

void PSDReaderTests::layerCountsAndSections()
{
    const QByteArray noLayers = PSDReaderFixtures::header(8, 8) + QByteArray(8, '\0');
    const PSDDocument empty = PSDReader::read(noLayers);
    QVERIFY(empty.layers.empty());
    QCOMPARE(empty.width, 8);
    // A negative count reads as its size.
    QCOMPARE(PSDReader::read(PSDReaderFixtures::file({pixels()}, QByteArray(), 8, 8, -1)).layers.size(), size_t(1));
    QCOMPARE(refusal<ImageImportError>([] { PSDReader::read(PSDReaderFixtures::file({}, QByteArray(), 8, 8, 10'001)); }),
             std::optional(ImageImportError::Kind::tooLarge));
    QCOMPARE(refusal<PSDError>([] { PSDReader::read(PSDReaderFixtures::file({}, QByteArray(), 8, 8, 10'000)); }), std::optional(PSDError::Kind::truncated));
    QCOMPARE(refusal<PSDError>([] { PSDReader::read(PSDReaderFixtures::header(8, 8)); }), std::optional(PSDError::Kind::truncated));
}

void PSDReaderTests::foldersPairWithTheirDividers()
{
    Record closed = folder(2, "Closed");
    closed.extras[0].key = "lsdk";
    const PSDDocument document = PSDReader::read(PSDReaderFixtures::file({folder(3), pixels("Inside"), closed, folder(1, "Lone", "mul ")}));
    QCOMPARE(document.layers.size(), size_t(3));
    QVERIFY(document.layers[1].isGroup && document.layers[1].blendKey == "pass");
    QCOMPARE(document.layers[0].parentID, std::optional(document.layers[1].id));
    QCOMPARE(document.layers[1].bounds, QRectF(0, 0, 8, 8));
    QVERIFY(!document.layers[1].image);
    QVERIFY(document.layers[2].isGroup && !document.layers[2].parentID);
    QCOMPARE(document.layers[2].blendKey, QString("mul "));
    QCOMPARE(messages(PSDDocumentBuilder::makeImport(document)),
             std::vector<QString>{"Lone: Folder blend mode “mul ” isn’t supported. The folder will be pass-through."});
    QCOMPARE(refusal<PSDError>([] { PSDReader::read(PSDReaderFixtures::file({folder(3), pixels()})); }), std::optional(PSDError::Kind::truncated));
}

void PSDReaderTests::kindsReportWhatTheImportConverts()
{
    Record text = pixels("Words");
    text.extras.push_back({"8BIM", "TySh", QByteArray("t")});
    Record smart = pixels("Smart");
    smart.extras.push_back({"8BIM", "SoLd", QByteArray("s")});
    Record outline = pixels("Outline");
    outline.extras.push_back({"8BIM", "vsms", QByteArray("v")});
    Record exposure;
    exposure.name = "Exposure";
    exposure.extras.push_back({"8BIM", "expA", QByteArray("e")});
    Record weird = pixels("Weird");
    weird.blendKey = "\xff\xff\xff\xff";
    const PSDDocument document = PSDReader::read(PSDReaderFixtures::file({text, smart, outline, exposure, weird}));
    QVERIFY(document.layers[0].kind == PSDLayerKind::text);
    QVERIFY(document.layers[1].kind == PSDLayerKind::smartObject);
    QVERIFY(document.layers[2].kind == PSDLayerKind::vector);
    QVERIFY(document.layers[3].kind == PSDLayerKind::adjustment && !document.layers[3].adjustment);
    // A key that is no ASCII reads as nothing.
    QCOMPARE(document.layers[4].blendKey, QString());
    const PSDImport imported = PSDDocumentBuilder::makeImport(document);
    QCOMPARE(messages(imported), (std::vector<QString>{
                                     "Words: Editable Photoshop text becomes pixels and can’t be retyped.",
                                     "Smart: The smart object was rasterized. Linked contents can’t be edited.",
                                     "Outline: Vector shape was rasterized to pixels.",
                                     "Exposure: This adjustment type isn’t supported and was skipped.",
                                     "Weird: Blend mode “” isn’t supported and will be applied as Normal.",
                                 }));
    QCOMPARE(imported.layers.size(), size_t(4));
    QCOMPARE(imported.layers[3].name, QString("Weird"));
}

void PSDReaderTests::clippingNeedsAPixelBaseBelow()
{
    Record bottom = pixels("Bottom");
    bottom.clipping = true;
    Record under = pixels("Under");
    Record levels;
    levels.name = "Levels";
    levels.extras.push_back({"8BIM", "levl", QByteArray(292, '\0')});
    Record onLevels = pixels("On Levels");
    onLevels.clipping = true;
    Record base = pixels("Base");
    Record first = pixels("First");
    first.clipping = true;
    Record second = pixels("Second");
    second.clipping = true;
    // An adjustment between parts clipping from the base below.
    const PSDDocument document = PSDReader::read(PSDReaderFixtures::file({bottom, under, levels, onLevels, base, first, second}));
    const PSDImport imported = PSDDocumentBuilder::makeImport(document);
    QVERIFY(imported.layers[2].adjustment.has_value());
    QVERIFY(!imported.layers[0].maskSourceID && !imported.layers[3].maskSourceID);
    QCOMPARE(imported.layers[5].maskSourceID, std::optional(imported.layers[4].id));
    QCOMPARE(imported.layers[6].maskSourceID, std::optional(imported.layers[4].id));
    const QString skipped = QStringLiteral(": This clipping mask’s base isn’t supported, so clipping was skipped.");
    QCOMPARE(messages(imported), (std::vector<QString>{"Levels: Adjustment parameters may not match Photoshop exactly.", "Bottom" + skipped,
                                                       "On Levels" + skipped}));
}

void PSDReaderTests::levelsCurvesAndHueParse()
{
    PSDFixture::Buffer levels;
    levels.u16(2);
    for (int channel = 0; channel < 4; ++channel) {
        for (const quint16 value : {10, 240, 5, 250, 512})
            levels.u16(quint16(value + channel));
    }
    levels.bytes(QByteArray(292 - levels.data.size(), '\0'));
    const LayerAdjustment level = PSDAdjustments::parse({{"levl", levels.data}}).value();
    QVERIFY(level.kind == AdjustmentKind::levels);
    QCOMPARE(level.levels.ranges[0], (LevelRange{10, 2, 240, 5, 250}));
    QCOMPARE(level.levels.ranges[3], (LevelRange{13, 515 / 256.0, 243, 8, 253}));
    QVERIFY(!PSDAdjustments::parse({{"levl", levels.data.left(291)}}));
    PSDFixture::Buffer curves;
    curves.u8(0);
    curves.u16(1);
    curves.u16(1);
    curves.u16(2);
    for (const quint16 value : {255, 200, 0, 50})
        curves.u16(value);
    const LayerAdjustment curve = PSDAdjustments::parse({{"curv", curves.data}}).value();
    QVERIFY(curve.kind == AdjustmentKind::curves);
    QCOMPARE(curve.curves.channels[0], (std::vector<CurvePoint>{{0, 0}, {50, 0}, {200, 255}, {255, 255}}));
    QCOMPARE(curve.curves.channels[1], (std::vector<CurvePoint>{{0, 0}, {255, 255}}));
    QByteArray version = curves.data;
    version[2] = 2;
    QVERIFY(!PSDAdjustments::parse({{"curv", version}}));
    QVERIFY(!PSDAdjustments::parse({{"curv", curves.data.left(curves.data.size() - 1)}}));
    QVERIFY(!PSDAdjustments::parse({{"curv", QByteArray("\0\0\x01\0", 4)}}));
    PSDFixture::Buffer twice;
    twice.u8(0);
    twice.u16(4);
    twice.u16(1);
    twice.u16(2);
    for (const quint16 value : {10, 50, 20, 50})
        twice.u16(value);
    QVERIFY(!PSDAdjustments::parse({{"curv", twice.data}}));
    PSDFixture::Buffer hue;
    hue.u16(0);
    hue.u8(1);
    hue.u8(0);
    for (const qint16 value : {30, -20, 10, -5, 40, 0})
        hue.i16(value);
    const LayerAdjustment shift = PSDAdjustments::parse({{"hue ", hue.data}}).value();
    QVERIFY(shift.kind == AdjustmentKind::hsv);
    const HueSaturationSettings settings = shift.hsvSettings.value();
    QVERIFY(settings.colorize);
    QCOMPARE(settings.adjustments.at(ColorRange::master), (RangeAdjustment{30, -20, 10}));
    QCOMPARE(settings.adjustments.at(ColorRange::reds), (RangeAdjustment{-5, 40, 0}));
    QVERIFY(!settings.adjustments.contains(ColorRange::yellows));
    // `hue2` comes before `hue `.
    PSDFixture::Buffer plain;
    plain.u32(0);
    QVERIFY(!PSDAdjustments::parse({{"hue2", plain.data}, {"hue ", hue.data}}).value().hsvSettings.value().colorize);
    QVERIFY(!PSDAdjustments::parse({{"hue ", QByteArray("\0\0\1", 3)}}));
    QVERIFY(!PSDAdjustments::parse({{"expA", hue.data}}));
}

QTEST_GUILESS_MAIN(PSDReaderTests)
#include "PSDReaderTests.moc"
