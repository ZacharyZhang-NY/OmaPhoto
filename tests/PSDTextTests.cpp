#include "IO/PSD/PSDDocumentBuilder.h"
#include "IO/PSD/PSDReader.h"
#include "IO/PSD/PSDVector.h"
#include "PSDFixture+Text.h"
#include "PSDVectorBuilders.h"
#include "PSDVectorFixtures.h"
#include <QtTest>

// Swift's PSDRoundTripTests, the text half, and the twin's own.
namespace {
using PSDFixture::TypeBlock;

std::optional<PSDText::Source> parsed(const TypeBlock &block)
{
    return PSDText::parse(PSDFixture::typeExtra(block));
}

TypeBlock hello()
{
    TypeBlock block;
    block.text = QStringLiteral("Hello");
    return block;
}

bool notes(const std::vector<QString> &list, const QString &note)
{
    return std::find(list.begin(), list.end(), note) != list.end();
}

bool reported(const PSDImport &imported, const QString &message)
{
    return std::any_of(imported.conversions.begin(), imported.conversions.end(), [&](const PSDConversion &item) { return item.message == message; });
}

// One layer carrying the block, read back through a file.
PSDImport importedFile(const QString &name, const QImage &pixels, int side, const TypeBlock &block)
{
    const PSDRecord record = PSDFixture::record(name, pixels, QRectF(QPointF(), pixels.size()));
    const QByteArray file = PSDFixture::data(PSDDocument{side, side, 72, {record}}, PSDFixture::colorImage(side, side, 1, 1, 1), false,
                                             {PSDFixture::AdditionalLayerInfo{"TySh", PSDFixture::tySh(block)}});
    return PSDDocumentBuilder::makeImport(PSDReader::read(file));
}

}

class PSDTextTests : public QObject {
    Q_OBJECT
private slots:
    void photoshopPointTextImportsAsEditableText();
    void photoshopTextSizeUsesMatrixScaleNotDocumentResolution();
    void photoshopTextKeepsTheFirstStyleAndReportsTheRest();
    void photoshopTextReportsLeadingOnlyStyleDifferences();
    void photoshopParagraphTextKeepsItsBox();
    void oversizedPhotoshopParagraphFrameStaysPixels();
    void warpedPhotoshopTextStaysEditableAndSaysSo();
    void verticalOrBrokenPhotoshopTextStaysPixels();
    void missingPhotoshopFontIsReportedButStaysEditable();
    void rotatedPhotoshopTextKeepsItsAngle();
    void wildNumbersNeverTrap();
    void theWordsAreCleanedAndMayComeFromTheEngine();
    void framesReadStandardisedAndPlaced();
    void pointTextSitsOnItsBaseline();
    void textThatCannotBeDrawnStaysPixels();
    void theStyleReadsAsSwiftReadsIt();
    void typeWithAShapeStaysType();
};

void PSDTextTests::photoshopPointTextImportsAsEditableText()
{
    TypeBlock block = hello();
    const PSDText::Source source = parsed(block).value();
    QCOMPARE(source.style.content, QString("Hello"));
    QCOMPARE(source.style.fontName, QString("Helvetica"));
    QCOMPARE(source.style.fontSize, 24.0);
    QVERIFY(source.style.red == 0 && source.style.green == 0 && source.style.blue == 0);
    QVERIFY(source.style.alignment == TextAlignment::left);
    QVERIFY(source.notes.empty());

    const PSDRecord record = PSDFixture::record(QStringLiteral("Greeting"), PSDFixture::colorImage(8, 8, 0, 0, 0), QRectF(1, 2, 8, 8));
    const PSDDocument document = PSDReader::read(PSDFixture::data(PSDDocument{32, 32, 72, {record}}, PSDFixture::colorImage(32, 32, 1, 1, 1), false,
                                                                  {PSDFixture::AdditionalLayerInfo{"TySh", PSDFixture::tySh(block)}}));
    QVERIFY(document.layers.at(0).kind == PSDLayerKind::text);
    QCOMPARE(document.layers.at(0).text.value().style.content, QString("Hello"));
    const PSDImport imported = PSDDocumentBuilder::makeImport(document);
    const ImageLayer &layer = imported.layers.at(0);
    const LayerText live = layer.liveText().value();
    QCOMPARE(live.style.content, QString("Hello"));
    QCOMPARE(live.style.fontName, QString("Helvetica"));
    QCOMPARE(live.style.fontSize, 24.0);
    QVERIFY(layer.asset.value().identity() == live.image);
    QVERIFY(!reported(imported, PSDText::rasterizedNote));
    QVERIFY(std::abs(layer.transform.origin.x() - 40) < 80);
    QVERIFY(std::abs(layer.transform.origin.y() - 50) < 80);
    // The rendered words replace Photoshop's cached pixels.
    const PSDText::Rendered rendered = PSDText::render(document.layers.at(0).text.value());
    QCOMPARE(layer.transform, rendered.transform);
    QCOMPARE(layer.asset.value().image(), rendered.image);
    QVERIFY(rendered.image.size() != QSize(8, 8));
}

void PSDTextTests::photoshopTextSizeUsesMatrixScaleNotDocumentResolution()
{
    // Only the matrix scales the engine's size.
    QCOMPARE(parsed(hello()).value().style.fontSize, 24.0);
    TypeBlock doubled = hello();
    doubled.xx = doubled.yy = 2;
    QCOMPARE(parsed(doubled).value().style.fontSize, 48.0);
    doubled.fontSize = 25;
    QCOMPARE(parsed(doubled).value().style.fontSize, 50.0);
}

void PSDTextTests::photoshopTextKeepsTheFirstStyleAndReportsTheRest()
{
    TypeBlock block = hello();
    block.red = 1;
    block.justification = "2";
    block.tracking = "1000";
    block.leading = 30;
    block.secondSize = 48;
    const PSDText::Source source = parsed(block).value();
    QCOMPARE(source.style.content, QString("Hello"));
    QVERIFY(source.style.alignment == TextAlignment::center);
    QCOMPARE(source.style.tracking, 24.0);
    QCOMPARE(source.style.leading, 30.0);
    QCOMPARE(source.style.red, 1.0);
    QCOMPARE(source.style.fontSize, 24.0);
    QVERIFY(notes(source.notes, PSDText::firstStyleNote));
}

void PSDTextTests::photoshopTextReportsLeadingOnlyStyleDifferences()
{
    TypeBlock byLeading = hello();
    byLeading.leading = 30;
    byLeading.secondLeading = 48;
    const PSDText::Source leading = parsed(byLeading).value();
    QCOMPARE(leading.style.leading, 30.0);
    QVERIFY(notes(leading.notes, PSDText::firstStyleNote));
    TypeBlock byScale = hello();
    byScale.secondHorizontalScale = 1.2;
    QVERIFY(notes(parsed(byScale).value().notes, PSDText::firstStyleNote));
    // The same second run says nothing.
    TypeBlock same = hello();
    same.secondSize = 24;
    QVERIFY(parsed(same).value().notes.empty());
    // Each other field of the signature counts alone.
    for (const char *field : {"/Font 1\n", "/Tracking 5\n", "/AutoLeading false\n", "/VerticalScale 1.2\n", "/FauxBold true\n",
                              "/FauxItalic true\n", "/FillColor << /Values [ 1.0 0 0 1 ] >>\n"}) {
        TypeBlock second = hello();
        second.secondOverride = field;
        QVERIFY2(parsed(second).value().notes == std::vector{PSDText::firstStyleNote}, field);
    }
}

void PSDTextTests::photoshopParagraphTextKeepsItsBox()
{
    TypeBlock block = hello();
    block.tx = 10;
    block.ty = 30;
    block.bounds = std::array{0.0, 0.0, 200.0, 80.0};
    block.glyphBounds = std::array{0.0, -10.0, 40.0, 10.0};
    const PSDText::Source source = parsed(block).value();
    QVERIFY(source.anchorIsFrame);
    QCOMPARE(source.style.boxSize.value(), QSizeF(224, 104));
    QCOMPARE(source.documentAnchor, QPointF(10, 30));
    // A frame barely wider than its letters stays point text.
    block.bounds = std::array{0.0, 0.0, 44.0, 80.0};
    QVERIFY(!parsed(block).value().anchorIsFrame);
    QVERIFY(!parsed(block).value().style.boxSize);
}

void PSDTextTests::oversizedPhotoshopParagraphFrameStaysPixels()
{
    TypeBlock block = hello();
    block.bounds = std::array{0.0, 0.0, 40'000.0, 100.0};
    block.glyphBounds = std::array{0.0, 0.0, 40.0, 10.0};
    QVERIFY(!parsed(block));
    const PSDImport imported = importedFile(QStringLiteral("Billboard"), PSDFixture::colorImage(4, 4, 0, 1, 0), 16, block);
    const ImageLayer &layer = imported.layers.at(0);
    QVERIFY(!layer.liveText());
    QCOMPARE(layer.asset.value().size(), QSize(4, 4));
    QVERIFY(reported(imported, PSDText::rasterizedNote));
}

void PSDTextTests::warpedPhotoshopTextStaysEditableAndSaysSo()
{
    TypeBlock block = hello();
    block.fauxBold = true;
    block.warp = true;
    const PSDText::Source source = parsed(block).value();
    QCOMPARE(source.style.content, QString("Hello"));
    QVERIFY(notes(source.notes, PSDText::warpNote));
    QVERIFY(notes(source.notes, PSDText::fauxNote));
    // The builder reports the parser's notes, in order.
    const PSDImport imported = importedFile(QStringLiteral("Arc"), PSDFixture::colorImage(4, 4, 0, 0, 0), 16, block);
    QCOMPARE(imported.conversions.at(0).message, PSDText::warpNote);
    QCOMPARE(imported.conversions.at(1).message, PSDText::fauxNote);
    TypeBlock italic = hello();
    italic.fauxItalic = true;
    QCOMPARE(parsed(italic).value().notes, std::vector{PSDText::fauxNote});
}

void PSDTextTests::verticalOrBrokenPhotoshopTextStaysPixels()
{
    TypeBlock vertical = hello();
    vertical.vertical = true;
    QVERIFY(!parsed(vertical));
    QVERIFY(!PSDText::parse({{QStringLiteral("TySh"), QByteArray("\0\1", 2)}}));
    TypeBlock uneven = hello();
    uneven.xx = 2;
    QVERIFY(!parsed(uneven));
    TypeBlock sheared = hello();
    sheared.xy = 0.5;
    QVERIFY(!parsed(sheared));

    const PSDImport imported = importedFile(QStringLiteral("Sideways"), PSDFixture::colorImage(4, 4, 0, 0, 1), 16, vertical);
    const ImageLayer &layer = imported.layers.at(0);
    QVERIFY(!layer.liveText());
    QCOMPARE(layer.asset.value().size(), QSize(4, 4));
    QVERIFY(reported(imported, PSDText::rasterizedNote));
}

void PSDTextTests::missingPhotoshopFontIsReportedButStaysEditable()
{
    TypeBlock block = hello();
    block.font = QStringLiteral("DefinitelyMissingFontXYZ");
    PSDRecord record = PSDFixture::record(QStringLiteral("Missing"), PSDFixture::colorImage(2, 2, 0, 0, 0));
    record.kind = PSDLayerKind::text;
    record.text = parsed(block).value();
    const PSDImport imported = PSDDocumentBuilder::makeImport(PSDDocument{64, 64, 72, {record}});
    QCOMPARE(imported.layers.at(0).liveText().value().style.fontName, QString("DefinitelyMissingFontXYZ"));
    const QString note = PSDText::missingFontNote(QStringLiteral("DefinitelyMissingFontXYZ")).value();
    QCOMPARE(note, QString("The font “DefinitelyMissingFontXYZ” isn’t installed, so the text was drawn with a substitute font."));
    QVERIFY(reported(imported, note));
    // An installed face by its PostScript name says nothing.
    QVERIFY(!PSDText::missingFontNote(QStringLiteral("DejaVuSans")));
}

void PSDTextTests::rotatedPhotoshopTextKeepsItsAngle()
{
    TypeBlock block = hello();
    block.xx = 0;
    block.xy = -1;
    block.yx = 1;
    block.yy = 0;
    const PSDText::Source source = parsed(block).value();
    QVERIFY(std::abs(source.rotation - 90) < 0.01);
    QVERIFY(!source.flipY);
    block.xy = 1;
    QVERIFY(parsed(block).value().flipY);
    // Two per cent of shear or unevenness passes, no more.
    const std::vector<std::pair<std::array<double, 4>, bool>> limits{{{1, 0.019, 0, 1}, true},  {{1, 0.021, 0, 1}, false},
                                                                     {{1, 0, 0, 1.019}, true},  {{1, 0, 0, 1.021}, false},
                                                                     {{1.019, 0, 0, 1}, true},  {{1.021, 0, 0, 1}, false}};
    for (const auto &[matrix, passes] : limits) {
        TypeBlock near = hello();
        near.xx = matrix[0];
        near.xy = matrix[1];
        near.yx = matrix[2];
        near.yy = matrix[3];
        QCOMPARE(parsed(near).has_value(), passes);
    }
}

void PSDTextTests::wildNumbersNeverTrap()
{
    TypeBlock block = hello();
    block.fontIndex = "1e300";
    block.justification = "1e300";
    const PSDText::Source source = parsed(block).value();
    QCOMPARE(source.style.fontName, QString("Helvetica"));
    QVERIFY(source.style.alignment == TextAlignment::left);
    QCOMPARE(source.notes, std::vector{PSDText::justifyNote});
    // Full justification, 3, is left with the note.
    block.justification = "3";
    QCOMPARE(parsed(block).value().notes, std::vector{PSDText::justifyNote});
    block.justification = "1";
    QVERIFY(parsed(block).value().style.alignment == TextAlignment::right);
    block.fontIndex = "1";
    QCOMPARE(parsed(block).value().style.fontName, QString("Helvetica"));
}

void PSDTextTests::theWordsAreCleanedAndMayComeFromTheEngine()
{
    TypeBlock block = hello();
    block.text = QStringLiteral("﻿") + QChar(0) + QStringLiteral("A\r\nB\rC") + QChar(0);
    QCOMPARE(parsed(block).value().style.content, QString("A\nB\nC"));
    block.text = QStringLiteral("H") + QChar(0xD800);
    QCOMPARE(parsed(block).value().style.content, QStringLiteral("H") + QChar::ReplacementCharacter);
    // Without a `Txt ` item the engine's words count.
    block.text = QStringLiteral("From the engine");
    block.typed = false;
    QCOMPARE(parsed(block).value().style.content, QString("From the engine"));
    // Empty words stay pixels, never falling back.
    block.typed = true;
    block.text = QString();
    QVERIFY(!parsed(block));
}

void PSDTextTests::framesReadStandardisedAndPlaced()
{
    TypeBlock block = hello();
    block.tx = 10;
    block.ty = 30;
    block.bounds = std::array{200.0, 80.0, 0.0, 0.0};
    block.glyphBounds = std::array{40.0, 10.0, 0.0, -10.0};
    const PSDText::Source flipped = parsed(block).value();
    QCOMPARE(flipped.style.boxSize.value(), QSizeF(224, 104));
    QCOMPARE(flipped.documentAnchor, QPointF(10, 30));
    // Turned a quarter, the frame's corner goes through the matrix.
    block.xx = 0;
    block.xy = -1;
    block.yx = 1;
    block.yy = 0;
    block.bounds = std::array{5.0, 7.0, 205.0, 87.0};
    QCOMPARE(parsed(block).value().documentAnchor, QPointF(3, 35));
    // Scaled twice, the frame doubles.
    block.xx = block.yy = 2;
    block.xy = block.yx = 0;
    QCOMPARE(parsed(block).value().style.boxSize.value(), QSizeF(424, 184));
    QCOMPARE(parsed(block).value().documentAnchor, QPointF(20, 44));
    // Rendered, the frame's padded corner lands on its anchor.
    for (const auto &matrix : std::vector<std::array<double, 4>>{{1, 0, 0, 1}, {0, -1, 1, 0}, {0, 1, 1, 0}}) {
        block.xx = matrix[0];
        block.xy = matrix[1];
        block.yx = matrix[2];
        block.yy = matrix[3];
        const PSDText::Source source = parsed(block).value();
        const PSDText::Rendered placed = PSDText::render(source);
        QCOMPARE(placed.image.size(), source.style.boxSize.value().toSize());
        const double pad = LayerTextStyle::padding;
        const QPointF corner = BrushRaster::pixelToDocument(placed.transform, placed.image.width(), placed.image.height()).map(QPointF(pad, pad));
        QVERIFY2(std::abs(corner.x() - source.documentAnchor.x()) < 1e-6 && std::abs(corner.y() - source.documentAnchor.y()) < 1e-6,
                 qPrintable(QString("%1 %2").arg(corner.x()).arg(corner.y())));
    }
}

void PSDTextTests::pointTextSitsOnItsBaseline()
{
    TypeBlock block = hello();
    block.text = QStringLiteral("HHH");
    block.font = QStringLiteral("DejaVuSans");
    block.fontSize = 40;
    const PSDText::Source source = parsed(block).value();
    const PSDText::Rendered rendered = PSDText::render(source);
    QCOMPARE(rendered.transform.size, QSizeF(rendered.image.size()));
    QCOMPARE(rendered.transform.origin.x(), 40 - LayerTextStyle::padding);
    // The letters' last inked row ends on the document's baseline.
    int bottom = -1;
    for (int y = 0; y < rendered.image.height(); ++y)
        for (int x = 0; x < rendered.image.width(); ++x)
            if (qAlpha(rendered.image.pixel(x, y)) > 128)
                bottom = y;
    const double baseline = 50 - rendered.transform.origin.y();
    QVERIFY(baseline > LayerTextStyle::padding + 20 && baseline < rendered.image.height() - LayerTextStyle::padding);
    QVERIFY2(std::abs(bottom + 1 - baseline) < 1, qPrintable(QString("%1 %2").arg(bottom).arg(baseline)));
    // Turned and flipped, the anchor still lands on its point.
    for (const bool right : {false, true}) {
        TypeBlock turned = block;
        turned.xx = 0;
        turned.xy = 1;
        turned.yx = 1;
        turned.yy = 0;
        turned.justification = right ? "1" : "2";
        const PSDText::Source twisted = parsed(turned).value();
        QVERIFY(twisted.flipY);
        const PSDText::Rendered placed = PSDText::render(twisted);
        const double x = right ? placed.image.width() - LayerTextStyle::padding : placed.image.width() / 2.0;
        const QPointF lands = BrushRaster::pixelToDocument(placed.transform, placed.image.width(), placed.image.height())
                                  .map(QPointF(x, baseline));
        QVERIFY2(std::abs(lands.x() - 40) < 1e-6 && std::abs(lands.y() - 50) < 1e-6, qPrintable(QString("%1 %2").arg(lands.x()).arg(lands.y())));
    }
}

void PSDTextTests::textThatCannotBeDrawnStaysPixels()
{
    TypeBlock block = hello();
    block.tx = 1e300;
    const PSDText::Source far = parsed(block).value();
    QVERIFY_THROWS_EXCEPTION(ProjectError, PSDText::render(far));
    const PSDImport imported = importedFile(QStringLiteral("Far"), PSDFixture::colorImage(4, 4, 1, 0, 0), 16, block);
    const ImageLayer &layer = imported.layers.at(0);
    QVERIFY(!layer.liveText());
    QCOMPARE(layer.asset.value().size(), QSize(4, 4));
    QCOMPARE(layer.transform.origin, QPointF(0, 0));
    QVERIFY(reported(imported, PSDText::rasterizedNote));
}

void PSDTextTests::theStyleReadsAsSwiftReadsIt()
{
    // Runs without a style sheet read nothing, as Swift's fallback.
    TypeBlock bare = hello();
    bare.sheet = false;
    bare.fontSize = 30;
    bare.red = 1;
    QCOMPARE(parsed(bare).value().style.fontSize, 12.0);
    QCOMPARE(parsed(bare).value().style.red, 0.0);
    // A size that is no size leaves every default.
    TypeBlock zero = hello();
    zero.fontSize = 0;
    zero.red = 1;
    QCOMPARE(parsed(zero).value().style.fontSize, 72.0);
    QCOMPARE(parsed(zero).value().style.red, 0.0);
    TypeBlock nameless = hello();
    nameless.font = QString();
    QCOMPARE(parsed(nameless).value().style.fontName, QString("Helvetica"));
    TypeBlock infinite = hello();
    infinite.tracking = "1e999";
    QCOMPARE(parsed(infinite).value().style.tracking, 0.0);
    // Automatic leading ignores the stored number.
    QCOMPARE(parsed(hello()).value().style.leading, 0.0);
    // Channels past one count in bytes.
    TypeBlock bytes = hello();
    bytes.red = 255;
    bytes.green = 51;
    const LayerTextStyle coloured = parsed(bytes).value().style;
    QCOMPARE(coloured.red, 1.0);
    QCOMPARE(coloured.green, 0.2);
    // Without an engine the size is twelve, scaled.
    TypeBlock plain = hello();
    plain.withEngine = false;
    plain.xx = plain.yy = 2;
    QCOMPARE(parsed(plain).value().style.fontSize, 24.0);
    QCOMPARE(parsed(plain).value().style.content, QString("Hello"));
    // A frame too short for its letters stays point text.
    TypeBlock shallow = hello();
    shallow.bounds = std::array{0.0, 0.0, 200.0, 22.0};
    shallow.glyphBounds = std::array{0.0, -10.0, 40.0, 10.0};
    QVERIFY(!parsed(shallow).value().anchorIsFrame);
    // Flipped, the frame's corner flips through the matrix.
    TypeBlock flipped = hello();
    flipped.tx = 10;
    flipped.ty = 30;
    flipped.yy = -1;
    flipped.bounds = std::array{5.0, 7.0, 205.0, 87.0};
    flipped.glyphBounds = std::array{0.0, -10.0, 40.0, 10.0};
    const PSDText::Source mirrored = parsed(flipped).value();
    QVERIFY(mirrored.flipY);
    QCOMPARE(mirrored.documentAnchor, QPointF(15, 23));
    // Eight million bytes at most, padding after the warp included.
    const QByteArray block = PSDFixture::tySh(hello());
    QVERIFY(PSDText::parse({{QStringLiteral("TySh"), block + QByteArray(8'000'000 - block.size(), '\0')}}));
    QVERIFY(!PSDText::parse({{QStringLiteral("TySh"), block + QByteArray(8'000'001 - block.size(), '\0')}}));
    QVERIFY(PSDText::parse({{QStringLiteral("tySh"), block}}));
}

void PSDTextTests::typeWithAShapeStaysType()
{
    // Swift reads the type first: no shape, no mask.
    std::vector<PSDFixture::AdditionalLayerInfo> extras{{"TySh", PSDFixture::tySh(hello())}};
    PSDVector::Extra shape = PSDVectorFixtures::rectangle();
    shape["vogk"] = PSDVectorBuilders::originationData(2, QRectF(945, 153, 646, 182), {0, 0, 0, 0});
    QVERIFY(PSDVector::live(shape, PSDVectorFixtures::canvas));
    for (const char *key : {"vmsk", "SoCo", "vstk", "vogk"})
        extras.push_back({key, shape.at(QString::fromLatin1(key))});
    for (const bool pixels : {true, false}) {
        const PSDRecord record = pixels ? PSDFixture::record(QStringLiteral("Both"), PSDFixture::colorImage(4, 4, 0, 0, 0), QRectF(0, 0, 4, 4))
                                        : PSDFixture::record(QStringLiteral("Both"));
        const PSDDocument document = PSDReader::read(PSDFixture::data(PSDDocument{1920, 1080, 72, {record}}, PSDFixture::colorImage(2, 2, 1, 1, 1), false, extras));
        const PSDRecord &read = document.layers.at(0);
        QVERIFY(read.kind == PSDLayerKind::text);
        QVERIFY(!read.shape);
        QCOMPARE(read.image.has_value(), pixels);
        QVERIFY(PSDDocumentBuilder::makeImport(document).layers.at(0).liveText());
    }
}

QTEST_MAIN(PSDTextTests)
#include "PSDTextTests.moc"
