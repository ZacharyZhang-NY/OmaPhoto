#include "AddressSpaceLimit.h"
#include "Document/EditorSession.h"
#include "IO/ImageImporter.h"
#include "IO/PSD/PSDChannelCoder.h"
#include "IO/PSD/PSDReader.h"
#include "PSDFixture.h"
#include <QColorSpace>
#include <QTemporaryDir>
#include <QtTest>

// Swift 1.2.10: Large Documents, and files of a background alone.
namespace {
QString written(const QTemporaryDir &folder, const QString &name, const QByteArray &bytes)
{
    QFile file(folder.filePath(name));
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        throw std::runtime_error("could not write a fixture");
    return file.fileName();
}

// Photoshop's ICC resource, 1039, added after the others.
QByteArray withProfile(QByteArray file, const QByteArray &profile)
{
    PSDFixture::Buffer resource;
    resource.string("8BIM");
    resource.u16(1039);
    resource.u16(0);
    resource.u32(quint32(profile.size()));
    resource.bytes(profile);
    if (profile.size() % 2 == 1)
        resource.u8(0);
    // The resources' length sits after the header and colour data.
    const quint32 length = quint32(uchar(file[30])) << 24 | quint32(uchar(file[31])) << 16 | quint32(uchar(file[32])) << 8 | uchar(file[33]);
    PSDFixture::Buffer grown;
    grown.u32(length + quint32(resource.data.size()));
    file.replace(30, 4, grown.data);
    file.insert(34 + qsizetype(length), resource.data);
    return file;
}
}

class PSBImportTests : public QObject {
    Q_OBJECT
private slots:
    void PSBRoundTripMatchesPSDLayerContent();
    void PSBExceedingCanvasLimitIsRejected();
    void PSBLargeAdditionalInfoBlockDoesNotHideUnicodeName();
    void aLayerlessFileReadsAsItsMergedImage();
    void theMergedImageTakesItsProfileAndTheBudget();
    void aLayerlessFileImportsAsOneLayer();
    void aMergedImageIsJudgedBeforeTheFileIsRead();
    void theReadingEndsBeforeTheMergedRead();
    void aLayeredReadSkipsTheProfile();
};

void PSBImportTests::PSBRoundTripMatchesPSDLayerContent()
{
    PSDRecord bottom = PSDFixture::record(QStringLiteral("Red"), PSDFixture::colorImage(3, 2, 1, 0, 0), QRectF(0, 0, 3, 2));
    PSDRecord middle = PSDFixture::record(QStringLiteral("Green"), PSDFixture::colorImage(2, 3, 0, 1, 0), QRectF(1, 1, 2, 3));
    PSDRecord top = PSDFixture::record(QStringLiteral("Blue mask"), PSDFixture::colorImage(2, 2, 0, 0, 1), QRectF(2, 2, 2, 2));
    top.mask = PSDFixture::grayImage(2, 2, 128);
    const PSDDocument source{5, 5, 144, {bottom, middle, top}};
    const QImage composite = PSDFixture::colorImage(5, 5, 0, 0, 0);
    const PSDDocument psd = PSDReader::read(PSDFixture::data(source, composite));
    const PSDDocument psb = PSDReader::read(PSDFixture::data(source, composite, true));
    QCOMPARE(psb.layers.size(), psd.layers.size());
    for (size_t index = 0; index < psd.layers.size(); ++index) {
        QCOMPARE(psb.layers[index].name, psd.layers[index].name);
        QCOMPARE(psb.layers[index].bounds, psd.layers[index].bounds);
        QCOMPARE(psb.layers[index].image.value(), psd.layers[index].image.value());
    }
    QCOMPARE(psb.layers[2].mask.value(), psd.layers[2].mask.value());
    QCOMPARE(psb.resolution, 144.0);
}

void PSBImportTests::PSBExceedingCanvasLimitIsRejected()
{
    const QImage image = PSDFixture::colorImage(2, 2, 1, 0, 0);
    QByteArray data = PSDFixture::data(PSDDocument{2, 2, 72, {PSDFixture::record(QStringLiteral("Large"), image, QRectF(0, 0, 2, 2))}}, image, true);
    data.replace(18, 4, QByteArray("\0\0\x75\x31", 4));
    try {
        PSDReader::read(data);
        QFAIL("a Large Document past the side limit was read");
    } catch (const ImageImportError &error) {
        QCOMPARE(error.kind, ImageImportError::Kind::tooLarge);
    }
}

void PSBImportTests::PSBLargeAdditionalInfoBlockDoesNotHideUnicodeName()
{
    const QImage image = PSDFixture::colorImage(2, 2, 1, 0, 0);
    const PSDRecord layer = PSDFixture::record(QStringLiteral("Café layer"), image, QRectF(0, 0, 2, 2));
    const QByteArray data =
        PSDFixture::data(PSDDocument{2, 2, 72, {layer}}, image, true, {PSDFixture::AdditionalLayerInfo{"LMsk", QByteArray("\1\2\3", 3)}});
    QCOMPARE(PSDReader::read(data).layers.at(0).name, QStringLiteral("Café layer"));
}

void PSBImportTests::aLayerlessFileReadsAsItsMergedImage()
{
    // No layer records; each channel's rows pack apart.
    const QTemporaryDir folder;
    std::vector<uchar> red, green, blue;
    for (int index = 0; index < 15; ++index) {
        red.push_back(uchar(index * 17));
        green.push_back(uchar(index < 8 ? 40 : 200));
        blue.push_back(uchar(90));
    }
    const QImage composite = PSDChannelCoder::rgbaImage(5, 3, red, green, blue, std::vector<uchar>(15, 255));
    for (const bool large : {false, true}) {
        const QString path = written(folder, large ? "Flat.psb" : "Flat.psd", PSDFixture::data(PSDDocument{5, 3, 72, {}}, composite, large));
        QVERIFY(PSDReader::read(path).layers.empty());
        const QImage merged = PSDReader::merged(path);
        QCOMPARE(merged, composite);
        QCOMPARE(merged.colorSpace(), QColorSpace(QColorSpace::SRgb));
    }
}

void PSBImportTests::theMergedImageTakesItsProfileAndTheBudget()
{
    const QTemporaryDir folder;
    const QImage composite = PSDFixture::colorImage(5, 3, 0.5, 0.25, 0.125);
    const QByteArray flat = PSDFixture::data(PSDDocument{5, 3, 72, {}}, composite);
    // Fifteen pixels against fourteen: refused before any pixel.
    try {
        PSDReader::merged(written(folder, "Flat.psd", flat), 14);
        QFAIL("a merged image past the budget was read");
    } catch (const ImageImportError &error) {
        QCOMPARE(error.kind, ImageImportError::Kind::tooLarge);
    }
    // Past one surface yet in budget: read on, truncated.
    if (DocumentLimits::documentPixelBudget() > qint64(20'001) * 10'000) {
        PSDFixture::Buffer header;
        header.string("8BPS");
        header.u16(1);
        header.bytes(QByteArray(6, '\0'));
        header.u16(3);
        header.u32(10'000);
        header.u32(20'001);
        header.u16(8);
        header.u16(3);
        header.bytes(QByteArray(12, '\0'));
        header.u16(1);
        try {
            PSDReader::merged(written(folder, "Wide.psd", header.data));
            QFAIL("a header alone was read");
        } catch (const PSDError &error) {
            QCOMPARE(error.kind, PSDError::Kind::truncated);
        }
    }
    // Display P3 numbers arrive as the sRGB they name.
    const QString tagged = written(folder, "P3.psd", withProfile(flat, QColorSpace(QColorSpace::DisplayP3).iccProfile()));
    QCOMPARE(PSDReader::merged(tagged).colorSpace(), QColorSpace(QColorSpace::DisplayP3));
    const ImportedImage asset = ImageImporter::decode(tagged, DocumentLimits::documentPixelBudget(), true);
    const QColor expected = QColorSpace(QColorSpace::DisplayP3).transformationToColorSpace(QColorSpace::SRgb).map(composite.pixelColor(0, 0));
    const QColor got = asset.image().pixelColor(0, 0);
    QVERIFY2(std::abs(got.red() - expected.red()) <= 1 && std::abs(got.green() - expected.green()) <= 1 && std::abs(got.blue() - expected.blue()) <= 1,
             qPrintable(got.name() + " " + expected.name()));
    QVERIFY(got != composite.pixelColor(0, 0));
}

void PSBImportTests::aLayerlessFileImportsAsOneLayer()
{
    const QTemporaryDir folder;
    const QImage composite = PSDFixture::colorImage(5, 3, 0, 0.5, 1);
    const QString path = written(folder, "Background.psb", PSDFixture::data(PSDDocument{5, 3, 72, {}}, composite, true));
    EditorSession session;
    session.createDocument(8, 8);
    bool done = false;
    session.importImages({QUrl::fromLocalFile(path)}, QPointF(6, 5), [&done] { done = true; });
    QVERIFY(QTest::qWaitFor([&done] { return done; }, 20'000));
    // No conversion sheet: the merged image comes straight in.
    QVERIFY(!session.showsConversionSheet() && !session.importError());
    QCOMPARE(session.document().value().layers.size(), size_t(1));
    const ImageLayer layer = session.activeLayer().value();
    QCOMPARE(layer.name, QString("Background"));
    QCOMPARE(layer.asset.value().image().pixelColor(2, 1), composite.pixelColor(2, 1));
    // Dropped at a point, it lands there.
    const QPointF centre = layer.transform.center();
    QVERIFY2(std::abs(centre.x() - 6) <= 0.5 && std::abs(centre.y() - 5) <= 0.5, qPrintable(QString("%1, %2").arg(centre.x()).arg(centre.y())));
}

void PSBImportTests::aMergedImageIsJudgedBeforeTheFileIsRead()
{
    // Sparse 75 MB planes: past a budget of one.
    const QTemporaryDir folder;
    for (const bool large : {false, true}) {
        PSDFixture::Buffer header;
        header.string("8BPS");
        header.u16(large ? 2 : 1);
        header.bytes(QByteArray(6, '\0'));
        header.u16(3);
        header.u32(5'000);
        header.u32(5'000);
        header.u16(8);
        header.u16(3);
        header.bytes(QByteArray(large ? 16 : 12, '\0'));
        header.u16(0);
        const QString path = written(folder, large ? "Big.psb" : "Big.psd", header.data);
        QVERIFY(QFile(path).resize(header.data.size() + 75'000'000));
        QVERIFY(PSDReader::read(path, 1).layers.empty());
        std::optional<ImageImportError::Kind> kind;
        {
            const AddressSpaceLimit limit(32ll * 1024 * 1024);
            try {
                ImageImporter::decode(path, 1, true);
            } catch (const ImageImportError &error) {
                kind = error.kind;
            }
        }
        QCOMPARE(kind, std::optional(ImageImportError::Kind::tooLarge));
    }
}

void PSBImportTests::theReadingEndsBeforeTheMergedRead()
{
    // Gone once the reading ends: Swift reads the merged after.
    const QTemporaryDir folder;
    const QString path = written(folder, "Gone.psd", PSDFixture::data(PSDDocument{5, 3, 72, {}}, PSDFixture::colorImage(5, 3, 1, 0, 0)));
    EditorSession session;
    session.createDocument(8, 8);
    bool reading = false;
    connect(&session, &EditorSession::changed, this, [&] {
        reading = reading || session.showsConversionSheet();
        if (reading && !session.showsConversionSheet() && QFile::exists(path))
            QFile::remove(path);
    });
    bool done = false;
    session.importImages({QUrl::fromLocalFile(path)}, std::nullopt, [&done] { done = true; });
    QVERIFY(QTest::qWaitFor([&done] { return done; }, 20'000));
    QVERIFY(reading && !QFile::exists(path));
    QVERIFY(session.document().value().layers.empty());
    QCOMPARE(session.importError(), std::optional("Gone.psd: " + QString(ImageImportError(ImageImportError::Kind::unreadable).what())));
}

void PSBImportTests::aLayeredReadSkipsTheProfile()
{
    // A 64 MB profile, never copied while layers are read.
    const QImage image = PSDFixture::colorImage(2, 2, 1, 0, 0);
    for (const bool large : {false, true}) {
        const QByteArray file = withProfile(PSDFixture::data(PSDDocument{2, 2, 72, {PSDFixture::record(QStringLiteral("Red"), image, QRectF(0, 0, 2, 2))}}, image, large),
                                            QByteArray(64 * 1024 * 1024, 'x'));
        std::optional<PSDDocument> read;
        {
            const AddressSpaceLimit limit(32ll * 1024 * 1024);
            read = PSDReader::read(file);
        }
        QCOMPARE(read.value().layers.size(), size_t(1));
        QCOMPARE(read.value().layers[0].image.value(), PSDReader::read(file).layers[0].image.value());
        QCOMPARE(read.value().layers[0].image.value().pixel(1, 1), qRgb(255, 0, 0));
    }
}

QTEST_MAIN(PSBImportTests)
#include "PSBImportTests.moc"
