#include "Document/BrushStroke.h"
#include "IO/CanvasResizer.h"
#include "IO/ImageExporter.h"
#include "IO/ImageResizer.h"
#include "SessionFixtures.h"
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>

// The session cases of Swift's LayerMaskTests.
namespace {
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
    const QList<int> exported = alphas(ImageExporter::render(session.projectSnapshot().value()).image);
    // Export and the canvas's own composite must agree.
    const CanvasDocument &document = session.document().value();
    QImage canvas = BrushRaster::context(document.width, document.height, false);
    {
        QPainter painter(&canvas);
        session.drawLiveComposite(document, painter);
    }
    if (alphas(canvas) != exported)
        throw std::runtime_error("the live composite differs from the export");
    return exported;
}

bool within(const QList<int> &values, const QList<int> &expected, int tolerance)
{
    if (values.size() != expected.size())
        return false;
    for (qsizetype index = 0; index < values.size(); ++index) {
        if (std::abs(values[index] - expected[index]) > tolerance)
            return false;
    }
    return true;
}

bool isProjectError(const std::function<void()> &body)
{
    try {
        body();
    } catch (const ProjectError &) {
        return true;
    }
    return false;
}
}

class LayerMaskSessionTests : public QObject {
    Q_OBJECT
private slots:
    void addDisableDeleteUndoAndTargetSelection();
    void coverageOpacityAndDisabledMasksRenderCorrectly();
    void transformedMaskResizeAndCanvasChangesStayAligned();
    void projectAndPNGPreserveCoverageAndDisabledState();
    void masksRejectOlderSchemaAndUnsafePaths();
    void folderMasksClipEveryLayerInsideAndMultiplyWithTheirOwnMasks();
    void folderMasksSaveResizeAndNeedTheNewFormat();
    void folderMaskCanBePaintedInvertedAndLoadedAsASelection();
};

void LayerMaskSessionTests::addDisableDeleteUndoAndTargetSelection()
{
    const std::unique_ptr<EditorSession> session = redSession();
    const ImageIdentity original = session->activeLayer().value().asset.value().identity();
    const int count = session->history.undoCount();
    session->addLayerMask(false);
    QVERIFY(session->isMaskSelected() && session->activeLayer().value().mask.has_value());
    QCOMPARE(session->activeLayer().value().mask.value().asset.size().width(), 1);
    QCOMPARE(session->history.undoCount(), count + 1);
    // An existing mask is not overwritten.
    session->addLayerMask();
    QCOMPARE(session->history.undoCount(), count + 1);
    session->toggleLayerMask();
    QVERIFY(!session->activeLayer().value().mask.value().isEnabled);
    session->deleteLayerMask();
    QVERIFY(!session->activeLayer().value().mask.has_value() && !session->isMaskSelected());
    session->undo();
    QVERIFY(!session->activeLayer().value().mask.value().isEnabled);
    session->undo();
    QVERIFY(session->activeLayer().value().mask.value().isEnabled);
    session->undo();
    QVERIFY(!session->activeLayer().value().mask.has_value());
    session->redo();
    const QUuid id = session->activeLayerID().value();
    session->selectLayerTarget(id, true);
    QVERIFY(session->isMaskSelected());
    session->selectLayerTarget(id, false);
    QVERIFY(!session->isMaskSelected() && session->activeLayer().value().asset.value().identity() == original);
    session->addGroup();
    session->addLayerMask();
    QVERIFY(session->activeLayer().value().isGroup && session->activeLayer().value().mask.has_value());
}

void LayerMaskSessionTests::coverageOpacityAndDisabledMasksRenderCorrectly()
{
    const std::unique_ptr<EditorSession> session = redSession();
    const QUuid id = session->activeLayerID().value();
    session->addLayerMask();
    QCOMPARE(rendered(*session), (QList<int>{255, 255, 255, 255}));
    session->deleteLayerMask();
    session->addLayerMask(false);
    QCOMPARE(rendered(*session), (QList<int>{0, 0, 0, 0}));
    session->toggleLayerMask();
    QCOMPARE(rendered(*session), (QList<int>{255, 255, 255, 255}));
    rewrite(*session, [&](ProjectSnapshot &snapshot) {
        setMask(snapshot, id, coverage());
        record(snapshot, id).maskEnabled = true;
    });
    QCOMPARE(rendered(*session), (QList<int>{255, 0, 128, 255}));
    session->setLayerOpacity(0.5);
    QVERIFY2(within(rendered(*session), {128, 0, 64, 128}, 1), qPrintable(QString::number(rendered(*session)[2])));
}

void LayerMaskSessionTests::transformedMaskResizeAndCanvasChangesStayAligned()
{
    const std::unique_ptr<EditorSession> session = redSession();
    const QUuid id = session->activeLayerID().value();
    rewrite(*session, [&](ProjectSnapshot &snapshot) {
        setMask(snapshot, id, coverage());
        record(snapshot, id).transform.flipX = true;
    });
    const ProjectSnapshot input = session->projectSnapshot().value();
    QCOMPARE(alphas(ImageExporter::render(input).image), (QList<int>{0, 255, 255, 128}));
    const ProjectSnapshot resized = ImageResizer::resize(input, {.width = 4, .height = 4, .resolution = 72, .sampling = LayerSampling::nearest});
    QCOMPARE(alphas(ImageExporter::render(resized).image),
             (QList<int>{0, 0, 255, 255, 0, 0, 255, 255, 255, 255, 128, 128, 255, 255, 128, 128}));
    const ProjectSnapshot canvas = CanvasResizer::resize(resized, {.width = 6, .height = 6});
    QVERIFY(canvas.masks.at(id).identity() == resized.masks.at(id).identity());
    session->applyDocumentSize(canvas, "Canvas Size");
    QVERIFY(session->activeLayer().value().mask.has_value());
    session->undo();
    QCOMPARE(session->document().value().width, 2);
    QCOMPARE(session->activeLayer().value().mask.value().asset.size().width(), 2);
    // Turned, the same coverage falls on other pixels.
    rewrite(*session, [&](ProjectSnapshot &snapshot) { record(snapshot, id).transform.rotation = 90; });
    QList<int> turned = rendered(*session);
    std::sort(turned.begin(), turned.end());
    QCOMPARE(turned, (QList<int>{0, 128, 255, 255}));
}

void LayerMaskSessionTests::projectAndPNGPreserveCoverageAndDisabledState()
{
    const std::unique_ptr<EditorSession> session = redSession();
    const QUuid id = session->activeLayerID().value();
    rewrite(*session, [&](ProjectSnapshot &snapshot) { setMask(snapshot, id, coverage()); });
    QTemporaryDir folder;
    const QString path = folder.filePath("Masks.comp");
    for (const bool enabled : {true, false}) {
        rewrite(*session, [&](ProjectSnapshot &snapshot) { record(snapshot, id).maskEnabled = enabled; });
        ProjectStore::save(session->projectSnapshot().value(), path);
        const ProjectSnapshot loaded = ProjectStore::load(path);
        QCOMPARE(loaded.manifest.version, qint64(10));
        session->installProject(loaded, path);
        QCOMPARE(session->activeLayer().value().mask.value().isEnabled, enabled);
        const QImage image = QImage::fromData(ImageExporter::pngData(loaded), "PNG");
        QCOMPARE(alphas(image), enabled ? (QList<int>{255, 0, 128, 255}) : (QList<int>{255, 255, 255, 255}));
    }
    // A missing mask file fails; it never reveals the image.
    const QString maskFile = session->projectSnapshot().value().manifest.layers.front().maskFile.value();
    QVERIFY(QFile::remove(path + "/images/" + maskFile));
    QVERIFY(isProjectError([&] { ProjectStore::load(path); }));
}

void LayerMaskSessionTests::masksRejectOlderSchemaAndUnsafePaths()
{
    const std::unique_ptr<EditorSession> session = redSession();
    session->addLayerMask();
    const ProjectSnapshot snapshot = session->projectSnapshot().value();
    QTemporaryDir folder;
    const QString path = folder.filePath("InvalidMask.comp");
    ProjectSnapshot old = snapshot;
    old.manifest.version = 3;
    QVERIFY(isProjectError([&] { ProjectStore::save(old, path); }));
    old.manifest.version = 4;
    old.manifest.layers[0].maskFile = "../outside.png";
    QVERIFY(isProjectError([&] { ProjectStore::save(old, path); }));
    old.manifest.layers[0].maskFile = snapshot.manifest.layers[0].maskFile;
    ProjectStore::save(old, path);
}

void LayerMaskSessionTests::folderMasksClipEveryLayerInsideAndMultiplyWithTheirOwnMasks()
{
    const std::unique_ptr<EditorSession> session = redSession();
    const QUuid red = session->activeLayerID().value();
    session->groupSelectedLayers();
    const QUuid folder = session->activeLayerID().value();
    rewrite(*session, [&](ProjectSnapshot &snapshot) { record(snapshot, folder).transform.sampling = LayerSampling::nearest; });
    QVERIFY(session->activeLayer().value().isGroup && session->canEditMask());
    const int count = session->history.undoCount();
    session->addLayerMask(false);
    QVERIFY(session->isMaskSelected() && session->activeLayer().value().mask.has_value());
    QCOMPARE(session->history.undoCount(), count + 1);
    QCOMPARE(rendered(*session), (QList<int>{0, 0, 0, 0}));
    session->toggleLayerMask();
    QCOMPARE(rendered(*session), (QList<int>{255, 255, 255, 255}));
    session->toggleLayerMask();
    // Soft folder coverage, then times the layer's own soft mask.
    rewrite(*session, [&](ProjectSnapshot &snapshot) { setMask(snapshot, folder, coverage()); });
    QCOMPARE(rendered(*session), (QList<int>{255, 0, 128, 255}));
    rewrite(*session, [&](ProjectSnapshot &snapshot) { setMask(snapshot, red, coverage()); });
    QVERIFY2(within(rendered(*session), {255, 0, 64, 255}, 1), qPrintable(QString::number(rendered(*session)[2])));
    // A folder around the folder masks them all as well.
    rewrite(*session, [&](ProjectSnapshot &snapshot) {
        record(snapshot, red).maskFile = std::nullopt;
        snapshot.masks.erase(red);
    });
    session->selectLayer(folder);
    session->groupSelectedLayers();
    QVERIFY(session->activeLayerID() != std::optional(folder) && session->activeLayer().value().isGroup);
    session->addLayerMask(false);
    QCOMPARE(rendered(*session), (QList<int>{0, 0, 0, 0}));
    session->deleteLayerMask();
    QCOMPARE(rendered(*session), (QList<int>{255, 0, 128, 255}));
    session->undo();
    QCOMPARE(rendered(*session), (QList<int>{0, 0, 0, 0}));
}

void LayerMaskSessionTests::folderMasksSaveResizeAndNeedTheNewFormat()
{
    const std::unique_ptr<EditorSession> session = redSession();
    session->groupSelectedLayers();
    const QUuid folder = session->activeLayerID().value();
    rewrite(*session, [&](ProjectSnapshot &snapshot) {
        record(snapshot, folder).transform.sampling = LayerSampling::nearest;
        setMask(snapshot, folder, coverage());
    });
    QTemporaryDir directory;
    const QString path = directory.filePath("FolderMask.comp");
    const ProjectSnapshot snapshot = session->projectSnapshot().value();
    ProjectStore::save(snapshot, path);
    const ProjectSnapshot loaded = ProjectStore::load(path);
    QVERIFY(loaded.manifest.version == 10 && loaded.masks.count(folder) == 1);
    QCOMPARE(alphas(ImageExporter::render(loaded).image), (QList<int>{255, 0, 128, 255}));
    const ProjectSnapshot resized = ImageResizer::resize(loaded, {.width = 4, .height = 4, .resolution = 72, .sampling = LayerSampling::nearest});
    QCOMPARE(alphas(ImageExporter::render(resized).image),
             (QList<int>{255, 255, 0, 0, 255, 255, 0, 0, 128, 128, 255, 255, 128, 128, 255, 255}));
    // Earlier formats had no folder masks: such files are refused.
    ProjectSnapshot old = snapshot;
    old.manifest.version = 5;
    QVERIFY(isProjectError([&] { ProjectStore::save(old, path); }));
}

void LayerMaskSessionTests::folderMaskCanBePaintedInvertedAndLoadedAsASelection()
{
    EditorSession session;
    session.createDocument(40, 20);
    QImage red = BrushRaster::context(40, 20, false);
    red.fill(Qt::red);
    session.insert(ImportedImage(red, red, "Red"));
    session.groupSelectedLayers();
    const QUuid folder = session.activeLayerID().value();
    session.addLayerMask();
    QVERIFY(session.canPaint() && session.canInvert() && session.canCopyPixels());
    // Black on the folder's mask hides the layer there alone.
    session.selectTool(NavigationTool::brush);
    session.setBrushSettings(BrushSettings{.diameter = 8, .hardness = 1, .red = 0, .green = 0, .blue = 0});
    session.setMaskPaintWhite(false);
    session.beginBrush(QPointF(10, 10));
    session.continueBrush(QPointF(11, 10));
    session.finishBrush();
    QList<int> values = alphas(ImageExporter::render(session.projectSnapshot().value()).image);
    QVERIFY(values[10 * 40 + 10] == 0 && values[10 * 40 + 30] == 255);
    QVERIFY(session.activeLayerID() == folder && session.isMaskSelected());
    // Invert swaps what the folder hides.
    bool done = false;
    session.invertPixels([&] { done = true; });
    QTRY_VERIFY(done);
    values = alphas(ImageExporter::render(session.projectSnapshot().value()).image);
    QVERIFY(values[10 * 40 + 10] == 255 && values[10 * 40 + 30] == 0);
    // Ctrl-click on the folder's mask thumbnail selects its black.
    session.loadMaskSelection(folder);
    QVERIFY(session.selection().value().path.boundingRect().width() > 30);
}

QTEST_GUILESS_MAIN(LayerMaskSessionTests)
#include "LayerMaskSessionTests.moc"
