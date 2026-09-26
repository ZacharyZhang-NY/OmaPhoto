#include "Document/LiveLayerMask.h"
#include "IO/ImageExporter.h"
#include "SessionFixtures.h"
#include <QTemporaryDir>
#include <QtTest>

// The session cases of Swift's LiveMaskTests.
namespace {
// Two by two, red by `color`, with these alphas, premultiplied.
ImportedImage asset(const QList<int> &alpha, int color = 255)
{
    QImage image(2, 2, QImage::Format_RGBA8888_Premultiplied);
    for (int index = 0; index < 4; ++index) {
        uchar *pixel = image.scanLine(index / 2) + (index % 2) * 4;
        pixel[0] = uchar(color * alpha[index] / 255);
        pixel[1] = 0;
        pixel[2] = 0;
        pixel[3] = uchar(alpha[index]);
    }
    return ImportedImage(image, image, "Fixture");
}

QList<int> alphas(const QImage &image)
{
    QList<int> result;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x)
            result << image.pixelColor(x, y).alpha();
    }
    return result;
}

QList<int> rendered(const EditorSession &session)
{
    return alphas(ImageExporter::render(session.projectSnapshot().value()).image);
}

std::vector<QUuid> ids(const EditorSession &session)
{
    std::vector<QUuid> result;
    for (const ImageLayer &layer : session.document().value().layers)
        result.push_back(layer.id);
    return result;
}

std::optional<QUuid> base(const EditorSession &session, size_t index)
{
    return session.document().value().layers.at(index).maskSourceID;
}

void sampleNearest(EditorSession &session)
{
    rewrite(session, [](ProjectSnapshot &snapshot) {
        for (ProjectLayerRecord &layer : snapshot.manifest.layers)
            layer.transform.sampling = LayerSampling::nearest;
    });
}

// An opaque layer clipped to a hidden, mixed-alpha one.
std::unique_ptr<EditorSession> fixture()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(2, 2);
    session->insert(asset({255, 255, 255, 255}));
    session->insert(asset({255, 0, 128, 255}, 0));
    const std::vector<QUuid> layers = ids(*session);
    rewrite(*session, [&](ProjectSnapshot &snapshot) {
        for (ProjectLayerRecord &layer : snapshot.manifest.layers)
            layer.transform.sampling = LayerSampling::nearest;
        record(snapshot, layers[1]).isVisible = false;
    });
    if (!session->linkMask(layers[1], layers[0]))
        throw std::runtime_error("the fixture cannot link its mask");
    return session;
}
}

class LiveMaskSessionTests : public QObject {
    Q_OBJECT
private slots:
    void clippingColorPreservesSoftBaseAlphaWithoutBlackFringe();
    void optionClickCreatesSharedStackAndDragOutReleases();
    void hiddenBlackSourceSuppliesAlphaAndRasterMasksMultiply();
    void cyclesUndoPersistenceBakeAndDelete();
    void movingSourceChangesCoverageAndChainsMultiply();
};

void LiveMaskSessionTests::clippingColorPreservesSoftBaseAlphaWithoutBlackFringe()
{
    EditorSession session;
    session.createDocument(2, 2);
    session.insert(asset({255, 128, 32, 0}, 0));
    session.insert(asset({255, 255, 255, 255}));
    const QUuid id = session.activeLayerID().value();
    session.toggleClippingMask(id);
    sampleNearest(session);
    const QImage result = ImageExporter::render(session.projectSnapshot().value()).image;
    QCOMPARE(alphas(result), (QList<int>{255, 128, 32, 0}));
    // Red times alpha, no black fringe: red equals alpha.
    const QImage flat = result.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    for (int index = 0; index < 4; ++index) {
        const uchar *pixel = flat.constScanLine(index / 2) + (index % 2) * 4;
        QCOMPARE(int(pixel[0]), int(pixel[3]));
        QVERIFY(pixel[1] == 0 && pixel[2] == 0);
    }
    rewrite(session, [&](ProjectSnapshot &snapshot) { record(snapshot, id).opacity = 0.5; });
    QCOMPARE(rendered(session), (QList<int>{255, 128, 32, 0}));
    rewrite(session, [&](ProjectSnapshot &snapshot) { record(snapshot, id).opacity = 1; });
    QImage white(2, 2, QImage::Format_RGBA8888_Premultiplied);
    white.fill(Qt::white);
    session.insert(ImportedImage(white, white, "White"));
    const QUuid background = session.activeLayerID().value();
    QVERIFY(session.placeLayer(background, std::nullopt, std::nullopt, true));
    const QImage flattened = ImageExporter::render(session.projectSnapshot().value()).image;
    QCOMPARE(alphas(flattened), (QList<int>{255, 255, 255, 255}));
    for (int index = 0; index < 4; ++index)
        QCOMPARE(flattened.pixelColor(index % 2, index / 2).red(), 255);
}

void LiveMaskSessionTests::optionClickCreatesSharedStackAndDragOutReleases()
{
    EditorSession session;
    session.createDocument(2, 2);
    for (int index = 0; index < 3; ++index)
        session.insert(asset({255, 255, 255, 255}));
    const std::vector<QUuid> layers = ids(session);
    QVERIFY(!session.canToggleClippingMask(layers[0]) && session.canToggleClippingMask(layers[1]));
    session.toggleClippingMask(layers[0]);
    QCOMPARE(base(session, 0), std::nullopt);
    session.toggleClippingMask(layers[1]);
    session.toggleClippingMask(layers[2]);
    QCOMPARE(base(session, 1), std::optional(layers[0]));
    QCOMPARE(base(session, 2), std::optional(layers[0]));
    QVERIFY(session.canToggleClippingMask(layers[2]));
    session.toggleClippingMask(layers[2]);
    QCOMPARE(base(session, 2), std::nullopt);
    QCOMPARE(base(session, 1), std::optional(layers[0]));
    session.toggleClippingMask(layers[2]);
    session.toggleClippingMask(layers[1]);
    QVERIFY(!base(session, 1).has_value() && !base(session, 2).has_value());
    session.undo();
    QCOMPARE(base(session, 2), std::optional(layers[0]));
    QVERIFY(session.placeLayer(layers[2], std::nullopt, std::nullopt, true));
    QVERIFY(session.document().value().layers.front().id == layers[2] && !base(session, 0).has_value());
    QCOMPARE(session.document().value().layers.back().maskSourceID, std::optional(layers[0]));
    session.undo();
    QVERIFY(session.document().value().layers.back().id == layers[2]);
    QCOMPARE(session.document().value().layers.back().maskSourceID, std::optional(layers[0]));
}

void LiveMaskSessionTests::hiddenBlackSourceSuppliesAlphaAndRasterMasksMultiply()
{
    const std::unique_ptr<EditorSession> session = fixture();
    const std::vector<QUuid> layers = ids(*session);
    QCOMPARE(rendered(*session), (QList<int>{255, 0, 128, 255}));
    rewrite(*session, [&](ProjectSnapshot &snapshot) { record(snapshot, layers[1]).opacity = 0.5; });
    const QList<int> half = rendered(*session);
    QVERIFY2(std::abs(half[0] - 128) <= 1 && half[1] == 0 && std::abs(half[2] - 64) <= 1, qPrintable(QString::number(half[0])));
    QImage gray(2, 2, QImage::Format_Grayscale8);
    gray.fill(128);
    rewrite(*session, [&](ProjectSnapshot &snapshot) { setMask(snapshot, layers[0], LayerMask::assetFrom(gray)); });
    QVERIFY(rendered(*session)[0] < half[0]);
}

void LiveMaskSessionTests::cyclesUndoPersistenceBakeAndDelete()
{
    const std::unique_ptr<EditorSession> fixed = fixture();
    EditorSession &session = *fixed;
    const QUuid target = ids(session)[0], source = ids(session)[1];
    QCOMPARE(session.history.undoName(), QString("Create Clipping Mask"));
    QVERIFY(!session.linkMask(target, source));
    QVERIFY(!session.linkMask(target, target));
    session.undo();
    QCOMPARE(base(session, 0), std::nullopt);
    session.redo();
    QCOMPARE(base(session, 0), std::optional(source));
    const ProjectSnapshot snapshot = session.projectSnapshot().value();
    QTemporaryDir folder;
    const QString path = folder.filePath("LiveMask.comp");
    ProjectStore::save(snapshot, path);
    const ProjectSnapshot loaded = ProjectStore::load(path);
    QCOMPARE(loaded.manifest.layers[0].maskSourceID, std::optional(source));
    const QList<int> before = alphas(ImageExporter::render(loaded).image);
    const ImportedImage baked = LiveMaskBaker::bake(loaded, target).value();
    session.finishDeletingLayer(source, {{target, baked}});
    QCOMPARE(rendered(session), before);
    QCOMPARE(base(session, 0), std::nullopt);
    session.undo();
    QCOMPARE(int(session.document().value().layers.size()), 2);
    QCOMPARE(base(session, 0), std::optional(source));
    std::vector<ProjectLayerRecord> bad = snapshot.manifest.layers;
    bad[1].maskSourceID = target;
    bool refused = false;
    try {
        LiveMaskGraph::validate(bad);
    } catch (const ProjectError &) {
        refused = true;
    }
    QVERIFY(refused);
}

void LiveMaskSessionTests::movingSourceChangesCoverageAndChainsMultiply()
{
    const std::unique_ptr<EditorSession> session = fixture();
    const std::vector<QUuid> layers = ids(*session);
    rewrite(*session, [&](ProjectSnapshot &snapshot) { record(snapshot, layers[1]).transform.origin.rx() += 1; });
    QCOMPARE(rendered(*session), (QList<int>{0, 255, 0, 128}));
    rewrite(*session, [&](ProjectSnapshot &snapshot) { record(snapshot, layers[1]).transform.origin.rx() -= 1; });
    session->selectLayer(layers[1]);
    session->insert(asset({0, 255, 255, 255}));
    const QUuid third = session->activeLayerID().value();
    rewrite(*session, [&](ProjectSnapshot &snapshot) { record(snapshot, third).isVisible = false; });
    QVERIFY(session->linkMask(third, layers[1]));
    QCOMPARE(rendered(*session), (QList<int>{0, 0, 128, 255}));
}

QTEST_GUILESS_MAIN(LiveMaskSessionTests)
#include "LiveMaskSessionTests.moc"
