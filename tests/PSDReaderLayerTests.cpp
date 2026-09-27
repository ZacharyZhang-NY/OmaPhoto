#include "IO/PSD/PSDChannelCoder.h"
#include "IO/PSD/PSDDocumentBuilder.h"
#include "IO/PSD/PSDReader.h"
#include "PSDReaderFixtures.h"
#include "PSDVectorBuilders.h"
#include "PSDVectorFixtures.h"
#include <QTemporaryDir>
#include <sys/stat.h>
#include <thread>
#include <QtTest>

// The reader's bounds and budgets, and vector layers read whole.
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

Record pixels(QByteArray name, QRect bounds = QRect(0, 0, 2, 2))
{
    Record record;
    record.bounds = bounds;
    record.name = name;
    PSDReaderFixtures::solidChannels(bounds, record.channels);
    return record;
}

quint32 u32(const QByteArray &data, qsizetype at)
{
    return quint32(uchar(data[at])) << 24 | quint32(uchar(data[at + 1])) << 16 | quint32(uchar(data[at + 2])) << 8 | uchar(data[at + 3]);
}
}

class PSDReaderLayerTests : public QObject {
    Q_OBJECT
private slots:
    void everyCutBeforeTheLastLayerByteIsTruncated();
    void aRecordWithoutItsSignatureIsTruncated();
    void masksAndLayersSpendOneBudget();
    void resourcesEndWhereTheyDeclare();
    void packBitsRowsResumeAtTheirDeclaredEnd();
    void vectorLayersAreReadWholeAndSpendTheBudget();
    void textWithLevelsReadsAsAnAdjustment();
    void aMissingFileIsUnreadableAndAnEmptyOneTruncated();
    void blendKeysTrimAndAssetsKeepTheirIdentity();
};

void PSDReaderLayerTests::everyCutBeforeTheLastLayerByteIsTruncated()
{
    PSDRecord group = PSDFixture::record("Folder");
    group.isGroup = true;
    PSDRecord inside = PSDFixture::record("Inside", PSDFixture::colorImage(3, 2, 1, 0, 0), QRectF(1, 1, 3, 2));
    inside.parentID = group.id;
    inside.mask = PSDFixture::grayImage(3, 2, 200);
    const QByteArray data = PSDFixture::data(PSDDocument{8, 8, 72, {group, inside}}, PSDFixture::colorImage(8, 8, 0, 0, 0));
    QCOMPARE(PSDReader::read(data).layers.size(), size_t(2));
    // The header, the resources, the section's length, its info's length.
    const qsizetype resources = u32(data, 30);
    const qsizetype info = 26 + 4 + 4 + resources + 4;
    const qsizetype last = info + 4 + qsizetype(u32(data, info)) - 1;
    for (qsizetype cut = 0; cut < last; ++cut)
        QCOMPARE(refusal<PSDError>([&] { PSDReader::read(data.left(cut)); }), std::optional(PSDError::Kind::truncated));
}

void PSDReaderLayerTests::aRecordWithoutItsSignatureIsTruncated()
{
    Record record = pixels("Unsigned");
    record.signature = "8BIX";
    QCOMPARE(refusal<PSDError>([&] { PSDReader::read(PSDReaderFixtures::file({record})); }), std::optional(PSDError::Kind::truncated));
}

void PSDReaderLayerTests::masksAndLayersSpendOneBudget()
{
    const QByteArray pair = PSDReaderFixtures::file({pixels("One"), pixels("Two")});
    QCOMPARE(PSDReader::read(pair, 8).layers.size(), size_t(2));
    QCOMPARE(refusal<ImageImportError>([&] { PSDReader::read(pair, 7); }), std::optional(ImageImportError::Kind::tooLarge));
    Record masked = pixels("Masked", QRect(0, 0, 1, 1));
    masked.mask = QRect(0, 0, 2, 2);
    masked.channels.push_back({-2, QByteArray("\0\0\x10\x20\x30\x40", 6)});
    QCOMPARE(PSDReader::read(PSDReaderFixtures::file({masked}), 4).layers[0].mask.value().size(), QSize(2, 2));
    QCOMPARE(refusal<ImageImportError>([&] { PSDReader::read(PSDReaderFixtures::file({masked}), 3); }), std::optional(ImageImportError::Kind::tooLarge));
    masked.mask = QRect(0, 0, 30'001, 1);
    QCOMPARE(refusal<ImageImportError>([&] { PSDReader::read(PSDReaderFixtures::file({masked})); }), std::optional(ImageImportError::Kind::tooLarge));
}

void PSDReaderLayerTests::resourcesEndWhereTheyDeclare()
{
    PSDFixture::Buffer fixed;
    fixed.string("8BIM");
    fixed.u16(1005);
    fixed.u16(0);
    fixed.u32(8);
    fixed.u32(150 * 65536);
    fixed.u32(0);
    // Eight bytes, too few for a resource, end the loop.
    const PSDDocument document = PSDReader::read(PSDReaderFixtures::file({pixels("After")}, fixed.data + "junk1234"));
    QCOMPARE(document.resolution, 150.0);
    QCOMPARE(document.layers.at(0).name, QString("After"));
}

void PSDReaderLayerTests::packBitsRowsResumeAtTheirDeclaredEnd()
{
    // Row 0 holds two unread bytes; row 1 follows them.
    const QByteArray rows("\0\x04\0\x02\xff\x07\x55\x55\xff\x09", 10);
    QCOMPARE(PSDChannelCoder::decode(1, 2, 2, rows), (std::vector<uchar>{7, 7, 9, 9}));
}

void PSDReaderLayerTests::vectorLayersAreReadWholeAndSpendTheBudget()
{
    using namespace PSDVectorBuilders;
    Record ellipse = pixels("Ellipse");
    ellipse.extras = {{"8BIM", "SoCo", colorDescriptor(0, 110, 255)}, {"8BIM", "vogk", originationData(5, QRectF(10, 20, 30, 40))}};
    Record circle;
    circle.name = "Circle";
    for (const auto &[key, payload] : PSDVectorFixtures::circle())
        circle.extras.push_back({"8BIM", key == "vmsk" ? "vmsk" : key == "SoCo" ? "SoCo" : "vstk", payload});
    const QByteArray data = PSDReaderFixtures::file({ellipse, circle}, QByteArray(), 1920, 1080);
    const PSDDocument document = PSDReader::read(data);
    // A live shape takes its own pixels over the layer's.
    const PSDRecord &shape = document.layers[0];
    QVERIFY(shape.kind == PSDLayerKind::vector && shape.shape.value().kind == ShapeKind::ellipse);
    QCOMPARE(shape.bounds, QRectF(10, 20, 30, 40));
    QCOMPARE(shape.image.value().size(), QSize(30, 40));
    QVERIFY(shape.shapeNotes.empty());
    const PSDRecord &raster = document.layers[1];
    QVERIFY(raster.kind == PSDLayerKind::vector && !raster.shape);
    QVERIFY(std::abs(raster.bounds.width() - 328) < 12);
    QCOMPARE(raster.image.value().size(), raster.bounds.size().toSize());
    // The channels, the shape and the raster share one budget.
    const qint64 spent = 4 + 30 * 40 + qint64(raster.image.value().width()) * raster.image.value().height();
    QCOMPARE(PSDReader::read(data, spent).layers.size(), size_t(2));
    QCOMPARE(refusal<ImageImportError>([&] { PSDReader::read(data, spent - 1); }), std::optional(ImageImportError::Kind::tooLarge));
    QCOMPARE(refusal<ImageImportError>([&] { PSDReader::read(data, 4 + 30 * 40 - 1); }), std::optional(ImageImportError::Kind::tooLarge));
    // Text that draws a shape reads as one, stroke noted.
    Record styled = ellipse;
    styled.extras.push_back({"8BIM", "TySh", QByteArray("t")});
    styled.extras.push_back({"8BIM", "vstk", strokeStyle(true, true, 3, 255, 0, 0)});
    Record outlined = circle;
    outlined.extras.push_back({"8BIM", "TySh", QByteArray("t")});
    const QByteArray texts = PSDReaderFixtures::file({styled, outlined, circle}, QByteArray(), 1920, 1080);
    const PSDDocument typed = PSDReader::read(texts);
    QVERIFY(typed.layers[0].kind == PSDLayerKind::vector && typed.layers[1].kind == PSDLayerKind::vector);
    QCOMPARE(typed.layers[0].shapeNotes, std::vector<QString>{"The Photoshop stroke isn’t supported on shape layers and was omitted."});
    // A second raster spends what the first left.
    const qint64 each = qint64(raster.image.value().width()) * raster.image.value().height();
    QCOMPARE(PSDReader::read(texts, 4 + 30 * 40 + 2 * each).layers.size(), size_t(3));
    QCOMPARE(refusal<ImageImportError>([&] { PSDReader::read(texts, 4 + 30 * 40 + 2 * each - 1); }), std::optional(ImageImportError::Kind::tooLarge));
}

void PSDReaderLayerTests::textWithLevelsReadsAsAnAdjustment()
{
    Record text = pixels("Words");
    text.extras = {{"8BIM", "TySh", QByteArray("t")}, {"8BIM", "levl", QByteArray(292, '\0')}};
    const PSDRecord record = PSDReader::read(PSDReaderFixtures::file({text})).layers.at(0);
    QVERIFY(record.kind == PSDLayerKind::adjustment && record.adjustment.has_value());
}

void PSDReaderLayerTests::aMissingFileIsUnreadableAndAnEmptyOneTruncated()
{
    const QTemporaryDir folder;
    QCOMPARE(refusal<ImageImportError>([&] { PSDReader::read(folder.filePath("missing.psd")); }), std::optional(ImageImportError::Kind::unreadable));
    QFile empty(folder.filePath("empty.psd"));
    QVERIFY(empty.open(QIODevice::WriteOnly));
    empty.close();
    QCOMPARE(refusal<PSDError>([&] { PSDReader::read(empty.fileName()); }), std::optional(PSDError::Kind::truncated));
    QFile written(folder.filePath("one.psd"));
    QVERIFY(written.open(QIODevice::WriteOnly) && written.write(PSDReaderFixtures::file({pixels("From disk")})) > 0);
    written.close();
    QCOMPARE(PSDReader::read(written.fileName()).layers.at(0).name, QString("From disk"));
    // A pipe cannot be mapped: its bytes are read instead.
    const QString pipe = folder.filePath("pipe.psd");
    QCOMPARE(mkfifo(QFile::encodeName(pipe).constData(), 0600), 0);
    const QByteArray bytes = PSDReaderFixtures::file({pixels("Through a pipe")});
    std::thread writer([&] {
        QFile end(pipe);
        if (end.open(QIODevice::WriteOnly))
            end.write(bytes);
    });
    const PSDDocument piped = PSDReader::read(pipe);
    writer.join();
    QCOMPARE(piped.layers.at(0).name, QString("Through a pipe"));
}

void PSDReaderLayerTests::blendKeysTrimAndAssetsKeepTheirIdentity()
{
    Record spaced = pixels("Spaced");
    spaced.blendKey = " xy ";
    Record tabbed = pixels("Tabbed");
    tabbed.blendKey = "\txy\t";
    const PSDDocument document = PSDReader::read(PSDReaderFixtures::file({spaced, tabbed}));
    const PSDImport imported = PSDDocumentBuilder::makeImport(document);
    QCOMPARE(imported.conversions.at(0).message, QString("Blend mode “xy” isn’t supported and will be applied as Normal."));
    QCOMPARE(imported.conversions.at(1).message, QString("Blend mode “xy” isn’t supported and will be applied as Normal."));
    // The worker's assets are the layers' own, named for them.
    const std::map<QUuid, ImportedImage> assets = PSDDocumentBuilder::assets(document);
    QCOMPARE(assets.size(), size_t(2));
    QCOMPARE(assets.at(document.layers[1].id).name, QString("Tabbed"));
    const PSDImport given = PSDDocumentBuilder::makeImport(document, assets);
    QCOMPARE(given.layers[1].asset.value().identity(), assets.at(document.layers[1].id).identity());
    QCOMPARE(given.layers[1].name, QString("Tabbed"));
    // A folder clipped to the layer below is never linked.
    Record clipped;
    clipped.name = "Clipped folder";
    clipped.clipping = true;
    clipped.extras.push_back({"8BIM", "lsct", PSDReaderFixtures::section(1)});
    Record divider;
    divider.extras.push_back({"8BIM", "lsct", PSDReaderFixtures::section(3)});
    const PSDImport folders = PSDDocumentBuilder::makeImport(PSDReader::read(PSDReaderFixtures::file({pixels("Base"), divider, clipped})));
    QVERIFY(folders.layers.at(1).isGroup && !folders.layers.at(1).maskSourceID);
    QCOMPARE(folders.conversions.size(), size_t(1));
    QCOMPARE(folders.conversions[0].layerName + folders.conversions[0].message,
             QString("Clipped folderThis clipping mask’s base isn’t supported, so clipping was skipped."));
}

QTEST_GUILESS_MAIN(PSDReaderLayerTests)
#include "PSDReaderLayerTests.moc"
