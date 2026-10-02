#include "DialogDesk.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectController.h"
#include "IO/ProjectDigest.h"
#include "ProjectFixtures.h"
#include <QBuffer>
#include <QColorSpace>
#include <QImageReader>
#include <QImageWriter>
#include <QTemporaryDir>
#include <QtTest>

// The project's QuickLook/Preview.jpg: written on save, ignored on load.
namespace {
const QDir::Filters everything = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden;

// One layer stretched over a canvas of this size.
ProjectSnapshot stretched(int width, int height, const QImage &pixels)
{
    const QUuid id = QUuid::createUuid();
    const ProjectLayerRecord layer{.id = id, .name = "Stretched", .isVisible = true,
                                   .transform = {.origin = {0, 0}, .size = {double(width), double(height)}}, .imageFile = uuidString(id) + ".png"};
    return {.manifest = {.documentID = QUuid::createUuid(), .width = width, .height = height, .activeLayerID = id, .layers = {layer}},
            .images = {{id, asset(pixels, "source")}}};
}

QImage decoded(const QByteArray &data)
{
    QImage image = QImage::fromData(data, "jpeg");
    if (image.isNull())
        throw std::runtime_error("the preview is no JPEG");
    return image;
}

QSize previewSize(int width, int height)
{
    QImage empty(1, 1, QImage::Format_RGBA8888_Premultiplied);
    empty.fill(0);
    return decoded(ImageExporter::quickLookImages(stretched(width, height, empty)).value().preview).size();
}
}

class QuickLookPreviewTests : public QObject {
    Q_OBJECT
private slots:
    void theStoreWritesThePreviewAndLoadingIgnoresIt();
    void thePreviewIsTheImageOnWhiteAtQualityEighty();
    void theLongSideIsAt1024AndNothingGrows();
    void pastFiftyMegapixelsThereIsNone();
    void aCanvasThatCannotRenderSavesWithoutOne();
    void everySaveWritesIt();
};

void QuickLookPreviewTests::theStoreWritesThePreviewAndLoadingIgnoresIt()
{
    QTemporaryDir root;
    const QString path = root.filePath("Previewed.comp");
    const ProjectSnapshot snapshot = twoLayers();
    ProjectStore::save(snapshot, path, QuickLookImages{"not even a JPEG"});
    QCOMPARE(QDir(path).entryList(everything), (QStringList{"images", "manifest.json", "QuickLook"}));
    QCOMPARE(QDir(path + "/QuickLook").entryList(everything), QStringList{"Preview.jpg"});
    QCOMPARE(contents(path + "/QuickLook/Preview.jpg"), QByteArray("not even a JPEG"));
    // Its bytes are never read.
    QCOMPARE(ProjectStore::load(path).manifest.encoded(), snapshot.manifest.encoded());
    // Rewritten or deleted, it is no external change.
    const QByteArray digest = ProjectDigest::compute(path);
    overwrite(path + "/QuickLook/Preview.jpg", QByteArray(5000, 'x'));
    QCOMPARE(ProjectDigest::compute(path), digest);
    QVERIFY(QDir(path + "/QuickLook").removeRecursively());
    QCOMPARE(ProjectDigest::compute(path), digest);
    // Saved without one, the project has none.
    ProjectStore::save(snapshot, path);
    QCOMPARE(QDir(path).entryList(everything), (QStringList{"images", "manifest.json"}));
    QCOMPARE(ProjectStore::load(path).manifest.encoded(), snapshot.manifest.encoded());
}

void QuickLookPreviewTests::thePreviewIsTheImageOnWhiteAtQualityEighty()
{
    // Half opaque red, half clear, over 2048 by 1024.
    const ProjectSnapshot snapshot = stretched(2048, 1024, halfRed());
    const QByteArray preview = ImageExporter::quickLookImages(snapshot).value().preview;
    const QImage image = decoded(preview);
    QCOMPARE(image.size(), QSize(1024, 512));
    QVERIFY(qAbs(image.pixelColor(200, 256).red() - 255) <= 3 && image.pixelColor(200, 256).green() <= 3);
    QCOMPARE(image.pixelColor(800, 256), QColor(Qt::white));
    // Byte for byte: smooth scaling, white, quality 80, 72 dpi.
    const QImage scaled = ImageExporter::render(snapshot).image.scaled(1024, 512, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QImage flattened(1024, 512, QImage::Format_RGB888);
    flattened.fill(Qt::white);
    QPainter(&flattened).drawImage(QRectF(flattened.rect()), scaled);
    flattened.setDotsPerMeterX(2835);
    flattened.setDotsPerMeterY(2835);
    flattened.setColorSpace(QColorSpace::SRgb);
    QByteArray expected;
    QBuffer buffer(&expected);
    QImageWriter writer(&buffer, "jpeg");
    writer.setQuality(80);
    QVERIFY(writer.write(flattened));
    QCOMPARE(preview, expected);
}

void QuickLookPreviewTests::theLongSideIsAt1024AndNothingGrows()
{
    QCOMPARE(previewSize(300, 200), QSize(300, 200));
    QCOMPARE(previewSize(1024, 7), QSize(1024, 7));
    QCOMPARE(previewSize(1025, 1025), QSize(1024, 1024));
    // 1001 × 1024 / 3000 is 341.67: rounded, not cut.
    QCOMPARE(previewSize(3000, 1001), QSize(1024, 342));
    QCOMPARE(previewSize(10, 5000), QSize(2, 1024));
    // A sliver keeps a pixel.
    QCOMPARE(previewSize(1, 3000), QSize(1, 1024));
    QCOMPARE(previewSize(3000, 1), QSize(1024, 1));
}

void QuickLookPreviewTests::pastFiftyMegapixelsThereIsNone()
{
    QImage empty(1, 1, QImage::Format_RGBA8888_Premultiplied);
    empty.fill(0);
    QVERIFY(!ImageExporter::quickLookImages(stretched(7072, 7071, empty)));
    QCOMPARE(decoded(ImageExporter::quickLookImages(stretched(10'000, 5'000, empty)).value().preview).size(), QSize(1024, 512));
}

void QuickLookPreviewTests::aCanvasThatCannotRenderSavesWithoutOne()
{
    ProjectSnapshot snapshot = twoLayers();
    snapshot.images.clear();
    QTest::ignoreMessage(QtWarningMsg, "no Quick Look preview: An image inside the project is missing or damaged. The current document has not been replaced.");
    QVERIFY(!ImageExporter::quickLookImages(snapshot));
}

void QuickLookPreviewTests::everySaveWritesIt()
{
    QTemporaryDir folder;
    EditorSession session;
    session.createDocument(8, 6);
    QImage image(8, 6, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::blue);
    session.insert(ImportedImage(image, QImage(), "Blue"));
    ProjectController controller(session);
    DialogDesk desk;
    desk.replies = {folder.filePath("Saved")};
    bool saved = false;
    controller.save(false, [&](bool done) { saved = done; });
    QTRY_VERIFY(saved);
    const QImage preview = decoded(contents(folder.filePath("Saved.comp/QuickLook/Preview.jpg")));
    QCOMPARE(preview.size(), QSize(8, 6));
    QVERIFY(preview.pixelColor(4, 3).blue() >= 250 && preview.pixelColor(4, 3).red() <= 5);
}

QTEST_MAIN(QuickLookPreviewTests)
#include "QuickLookPreviewTests.moc"
