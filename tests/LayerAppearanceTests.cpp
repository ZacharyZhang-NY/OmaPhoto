#include "Document/EditorSession.h"
#include "IO/CanvasResizer.h"
#include "IO/ImageExporter.h"
#include "IO/ImageResizer.h"
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
ImportedImage asset(double gray)
{
    QImage image(4, 4, QImage::Format_RGBA8888_Premultiplied);
    image.fill(QColor::fromRgbF(gray, gray, gray));
    return ImportedImage(image, image, "Gray");
}

// The flattened canvas's first pixel: gray level and alpha.
std::pair<double, double> pixel(const EditorSession &session)
{
    const QImage image = ImageExporter::render(session.projectSnapshot().value()).image.convertToFormat(QImage::Format_RGBA8888);
    return {image.constScanLine(0)[0] / 255.0, image.constScanLine(0)[3] / 255.0};
}

ImageLayer layerWith(const EditorSession &session, QUuid id)
{
    for (const ImageLayer &layer : session.document().value().layers) {
        if (layer.id == id)
            return layer;
    }
    throw std::runtime_error("no such layer");
}
}

class LayerAppearanceTests : public QObject {
    Q_OBJECT
private slots:
    void hoverPreviewIsTemporaryAndNeverChangesSavedState();
    void blendModesAndOpacityMatchKnownPixels();
    void opacityDragIsOneUndoAndKeepsSources();
    void appearancePersistsThroughSaveResizeAndTransparentExport();
    void thePreviewBelongsToTheActiveLayerAlone();
    void opacityIsClampedAndRefusesWhatIsNoNumber();
    void selectedLayersTakeOneOpacityInOneStep();
    void theBlendModeCyclesBothWaysAndWraps();
    void onlyOnePixelLayerHasAnAppearanceToEdit();
    void whatEndsATransformEndsThePreviewAndTheDrag();
    void moveToolNumberKeysSetSelectedLayersOpacityAsOneUndo();
};

void LayerAppearanceTests::hoverPreviewIsTemporaryAndNeverChangesSavedState()
{
    EditorSession session;
    session.createDocument(4, 4);
    session.insert(asset(0.8));
    session.history.markSaved();
    const QUuid id = session.activeLayerID().value();
    const int count = session.history.undoCount();
    for (const LayerBlendMode mode : allLayerBlendModes) {
        session.previewBlendMode(mode, id);
        QCOMPARE(session.displayedBlendMode(session.activeLayer().value()), mode);
        QCOMPARE(session.activeLayer().value().blendMode, LayerBlendMode::normal);
        QCOMPARE(session.projectSnapshot().value().manifest.layers.back().blendMode, std::optional(LayerBlendMode::normal));
        QVERIFY(!session.isModified() && session.history.undoCount() == count);
    }
    session.previewBlendMode(std::nullopt, std::nullopt);
    QCOMPARE(session.displayedBlendMode(session.activeLayer().value()), LayerBlendMode::normal);
    session.previewBlendMode(LayerBlendMode::multiply, id);
    session.setLayerBlendMode(LayerBlendMode::multiply);
    QVERIFY(!session.blendPreview().has_value());
    QCOMPARE(session.history.undoCount(), count + 1);
    QCOMPARE(session.history.undoName(), QString("Layer Blend Mode"));
    session.undo();
    QVERIFY(session.activeLayer().value().blendMode == LayerBlendMode::normal && !session.isModified());
    // With no preview a layer shows the mode it holds.
    session.redo();
    QVERIFY(!session.blendPreview().has_value());
    QCOMPARE(session.displayedBlendMode(session.activeLayer().value()), LayerBlendMode::multiply);
    session.previewBlendMode(LayerBlendMode::screen, id);
    session.previewBlendMode(std::nullopt, std::nullopt);
    QCOMPARE(session.displayedBlendMode(session.activeLayer().value()), LayerBlendMode::multiply);
}

void LayerAppearanceTests::blendModesAndOpacityMatchKnownPixels()
{
    EditorSession session;
    session.createDocument(4, 4);
    session.insert(asset(0.4));
    session.insert(asset(0.8));
    const std::vector<std::pair<LayerBlendMode, double>> expected{
        {LayerBlendMode::normal, 0.8}, {LayerBlendMode::multiply, 0.32}, {LayerBlendMode::screen, 0.88},
        {LayerBlendMode::overlay, 0.64}, {LayerBlendMode::darken, 0.4}, {LayerBlendMode::lighten, 0.8},
        {LayerBlendMode::difference, 0.4}, {LayerBlendMode::colorDodge, 1}, {LayerBlendMode::colorBurn, 0.25}};
    for (const auto &[mode, value] : expected) {
        session.setLayerBlendMode(mode);
        QCOMPARE(layerBlendMode(rawValue(mode)), std::optional(mode));
        const auto [gray, alpha] = pixel(session);
        QVERIFY2(std::abs(gray - value) < 0.02, qPrintable(rawValue(mode) + " " + QString::number(gray)));
        QCOMPARE(alpha, 1.0);
    }
    session.setLayerBlendMode(LayerBlendMode::normal);
    session.setLayerOpacity(0.5);
    QVERIFY2(std::abs(pixel(session).first - 0.6) < 0.02, qPrintable(QString::number(pixel(session).first)));
    session.setLayerOpacity(0);
    QVERIFY2(std::abs(pixel(session).first - 0.4) < 0.02, qPrintable(QString::number(pixel(session).first)));
}

void LayerAppearanceTests::appearancePersistsThroughSaveResizeAndTransparentExport()
{
    EditorSession session;
    session.createDocument(4, 4);
    session.addGroup();
    session.insert(asset(0.8));
    const QUuid id = session.activeLayerID().value();
    session.setLayerOpacity(0.25);
    session.setLayerBlendMode(LayerBlendMode::multiply);
    const ProjectSnapshot snapshot = session.projectSnapshot().value();
    QVERIFY2(std::abs(pixel(session).second - 0.25) < 0.01, qPrintable(QString::number(pixel(session).second)));
    QTemporaryDir folder;
    const QString path = folder.filePath("Appearance.comp");
    ProjectStore::save(snapshot, path);
    const ProjectSnapshot loaded = ProjectStore::load(path);
    QCOMPARE(loaded.manifest.version, qint64(9));
    const ProjectSnapshot resized = ImageResizer::resize(loaded, {.width = 8, .height = 8, .resolution = 72});
    const ProjectSnapshot canvas = CanvasResizer::resize(resized, {.width = 12, .height = 12});
    const auto record = std::find_if(canvas.manifest.layers.begin(), canvas.manifest.layers.end(),
                                     [&](const ProjectLayerRecord &layer) { return layer.id == id; });
    QVERIFY(record != canvas.manifest.layers.end());
    QCOMPARE(record->opacity, std::optional(0.25));
    QCOMPARE(record->blendMode, std::optional(LayerBlendMode::multiply));
    QVERIFY(record->parentID.has_value());
}

void LayerAppearanceTests::opacityDragIsOneUndoAndKeepsSources()
{
    EditorSession session;
    session.createDocument(4, 4);
    const ImportedImage source = asset(0.8);
    session.insert(source);
    const int count = session.history.undoCount();
    session.beginOpacityEdit();
    for (int value = 9; value >= 2; --value)
        session.setLayerOpacity(value / 10.0);
    session.finishOpacityEdit();
    QCOMPARE(session.history.undoCount(), count + 1);
    QCOMPARE(session.history.undoName(), QString("Layer Opacity"));
    QVERIFY(session.activeLayer().value().asset.value().identity() == source.identity());
    session.undo();
    QCOMPARE(session.activeLayer().value().opacity, 1.0);
    session.redo();
    QVERIFY(std::abs(session.activeLayer().value().opacity - 0.2) < 0.001);
}

void LayerAppearanceTests::thePreviewBelongsToTheActiveLayerAlone()
{
    EditorSession session;
    session.createDocument(4, 4);
    session.insert(asset(0.2));
    const QUuid below = session.activeLayerID().value();
    session.insert(asset(0.8));
    const QUuid above = session.activeLayerID().value();
    // Another layer's id clears what was shown.
    session.previewBlendMode(LayerBlendMode::screen, above);
    QCOMPARE(session.blendPreview(), std::optional(EditorSession::BlendPreview{above, LayerBlendMode::screen}));
    session.previewBlendMode(LayerBlendMode::multiply, below);
    QVERIFY(!session.blendPreview().has_value());
    // A preview left behind shows on no other layer.
    session.previewBlendMode(LayerBlendMode::screen, above);
    QCOMPARE(session.displayedBlendMode(layerWith(session, below)), LayerBlendMode::normal);
    session.setActiveLayerID(below);
    QCOMPARE(session.displayedBlendMode(layerWith(session, above)), LayerBlendMode::normal);
    QCOMPARE(session.displayedBlendMode(layerWith(session, below)), LayerBlendMode::normal);
    // No mode, or no id, clears it too.
    session.setActiveLayerID(above);
    session.previewBlendMode(std::nullopt, above);
    QVERIFY(!session.blendPreview().has_value());
    session.previewBlendMode(LayerBlendMode::screen, above);
    session.previewBlendMode(LayerBlendMode::screen, std::nullopt);
    QVERIFY(!session.blendPreview().has_value());
    // A refused mode change still drops the preview.
    session.previewBlendMode(LayerBlendMode::screen, above);
    session.setIsImporting(true);
    session.setLayerBlendMode(LayerBlendMode::overlay);
    QVERIFY(!session.blendPreview().has_value());
    QCOMPARE(layerWith(session, above).blendMode, LayerBlendMode::normal);
    session.previewBlendMode(LayerBlendMode::screen, above);
    QVERIFY(!session.blendPreview().has_value());
    // Each layer shows its own stored mode, active or not.
    session.setIsImporting(false);
    session.setLayerBlendMode(LayerBlendMode::screen);
    session.selectLayer(below);
    session.setLayerBlendMode(LayerBlendMode::multiply);
    QCOMPARE(session.displayedBlendMode(layerWith(session, above)), LayerBlendMode::screen);
    QCOMPARE(session.displayedBlendMode(layerWith(session, below)), LayerBlendMode::multiply);
    session.previewBlendMode(LayerBlendMode::hue, below);
    QCOMPARE(session.displayedBlendMode(layerWith(session, above)), LayerBlendMode::screen);
    QCOMPARE(session.displayedBlendMode(layerWith(session, below)), LayerBlendMode::hue);
}

void LayerAppearanceTests::opacityIsClampedAndRefusesWhatIsNoNumber()
{
    EditorSession session;
    session.setLayerOpacity(0.5);
    session.setSelectedLayersOpacity(0.5);
    session.createDocument(4, 4);
    session.insert(asset(0.8));
    const int count = session.history.undoCount();
    session.setLayerOpacity(1.5);
    QCOMPARE(session.activeLayer().value().opacity, 1.0);
    QCOMPARE(session.history.undoCount(), count);
    session.setLayerOpacity(-2);
    QCOMPARE(session.activeLayer().value().opacity, 0.0);
    QCOMPARE(session.history.undoCount(), count + 1);
    QCOMPARE(session.history.undoName(), QString("Layer Opacity"));
    // What is no finite number is refused by both setters.
    session.setLayerOpacity(0.5);
    QSignalSpy changes(&session, &EditorSession::changed);
    const double infinity = std::numeric_limits<double>::infinity();
    for (const double value : {std::nan(""), infinity, -infinity}) {
        session.setLayerOpacity(value);
        session.setSelectedLayersOpacity(value);
    }
    QCOMPARE(session.activeLayer().value().opacity, 0.5);
    QCOMPARE(session.history.undoCount(), count + 2);
    QCOMPARE(int(changes.count()), 0);
    session.setLayerOpacity(0);
    session.setSelectedLayersOpacity(7);
    QCOMPARE(session.activeLayer().value().opacity, 1.0);
    session.setSelectedLayersOpacity(-7);
    QCOMPARE(session.activeLayer().value().opacity, 0.0);
    session.setIsImporting(true);
    session.setLayerOpacity(0.5);
    session.setSelectedLayersOpacity(0.5);
    session.beginOpacityEdit();
    QCOMPARE(session.activeLayer().value().opacity, 0.0);
    session.setIsImporting(false);
    QVERIFY(session.canUndo());
}

void LayerAppearanceTests::selectedLayersTakeOneOpacityInOneStep()
{
    EditorSession session;
    session.createDocument(4, 4);
    session.insert(asset(0.2));
    const QUuid first = session.activeLayerID().value();
    session.insert(asset(0.8));
    const QUuid second = session.activeLayerID().value();
    session.insert(asset(0.5));
    const QUuid apart = session.activeLayerID().value();
    session.addGroup();
    const QUuid folder = session.activeLayerID().value();
    session.selectLayers({first, second, folder}, second);
    const int count = session.history.undoCount();
    session.setSelectedLayersOpacity(0.5);
    QCOMPARE(layerWith(session, first).opacity, 0.5);
    QCOMPARE(layerWith(session, second).opacity, 0.5);
    // A selected folder takes it too.
    QCOMPARE(layerWith(session, folder).opacity, 0.5);
    QCOMPARE(layerWith(session, apart).opacity, 1.0);
    QCOMPARE(session.history.undoCount(), count + 1);
    QCOMPARE(session.history.undoName(), QString("Layer Opacity"));
    // The same value again is no step.
    session.setSelectedLayersOpacity(0.5);
    QCOMPARE(session.history.undoCount(), count + 1);
    // One layer that differs is enough for a step.
    session.selectLayer(first);
    session.setLayerOpacity(0.25);
    session.selectLayers({first, second}, first);
    session.setSelectedLayersOpacity(0.5);
    QCOMPARE(layerWith(session, first).opacity, 0.5);
    QCOMPARE(session.history.undoCount(), count + 3);
    session.undo();
    QCOMPARE(layerWith(session, first).opacity, 0.25);
    QCOMPARE(layerWith(session, second).opacity, 0.5);
    // Several layers have no single appearance to edit.
    session.selectLayers({first, second}, first);
    QVERIFY(!session.canEditAppearance());
    session.setLayerOpacity(0.9);
    QCOMPARE(layerWith(session, first).opacity, 0.25);
}

void LayerAppearanceTests::theBlendModeCyclesBothWaysAndWraps()
{
    EditorSession session;
    session.cycleBlendMode(true);
    session.createDocument(4, 4);
    session.insert(asset(0.2));
    const QUuid below = session.activeLayerID().value();
    session.insert(asset(0.8));
    const int count = session.history.undoCount();
    // Photoshop's order: the first darkening mode follows Normal.
    session.cycleBlendMode(true);
    QCOMPARE(session.activeLayer().value().blendMode, LayerBlendMode::darken);
    // The mode is the active layer's alone.
    QCOMPARE(layerWith(session, below).blendMode, LayerBlendMode::normal);
    session.cycleBlendMode(false);
    session.cycleBlendMode(false);
    QCOMPARE(session.activeLayer().value().blendMode, LayerBlendMode::luminosity);
    session.cycleBlendMode(true);
    QCOMPARE(session.activeLayer().value().blendMode, LayerBlendMode::normal);
    QCOMPARE(session.history.undoCount(), count + 4);
    // A cycle starts from the stored mode, not a preview.
    const QUuid top = session.activeLayerID().value();
    session.setLayerBlendMode(LayerBlendMode::screen);
    session.previewBlendMode(LayerBlendMode::hue, top);
    session.cycleBlendMode(true);
    QCOMPARE(session.activeLayer().value().blendMode, LayerBlendMode::colorDodge);
    QVERIFY(!session.blendPreview().has_value());
    // A refused cycle leaves a preview where it was.
    session.previewBlendMode(LayerBlendMode::hue, top);
    session.setIsImporting(true);
    QSignalSpy changes(&session, &EditorSession::changed);
    session.cycleBlendMode(true);
    QCOMPARE(session.blendPreview(), std::optional(EditorSession::BlendPreview{top, LayerBlendMode::hue}));
    QCOMPARE(int(changes.count()), 0);
    QCOMPARE(session.activeLayer().value().blendMode, LayerBlendMode::colorDodge);
    QCOMPARE(int(allLayerBlendModes.size()), 24);
    for (size_t index = 0; index < allLayerBlendModes.size(); ++index)
        QCOMPARE(int(allLayerBlendModes[index]), int(index));
}

void LayerAppearanceTests::onlyOnePixelLayerHasAnAppearanceToEdit()
{
    EditorSession session;
    QVERIFY(!session.canEditAppearance());
    session.createDocument(4, 4);
    QVERIFY(!session.canEditAppearance());
    session.insert(asset(0.8));
    QVERIFY(session.canEditAppearance());
    session.addGroup();
    const QUuid folder = session.activeLayerID().value();
    // A folder takes an opacity; blending stays per layer.
    QVERIFY(!session.canEditAppearance());
    QVERIFY(session.canEditOpacity());
    session.setLayerBlendMode(LayerBlendMode::screen);
    session.previewBlendMode(LayerBlendMode::screen, folder);
    QVERIFY(!session.blendPreview().has_value());
    QCOMPARE(layerWith(session, folder).blendMode, LayerBlendMode::normal);
    session.setLayerOpacity(0.5);
    QCOMPARE(layerWith(session, folder).opacity, 0.5);
    QCOMPARE(session.history.undoName(), QString("Layer Opacity"));
    session.beginOpacityEdit();
    session.setLayerOpacity(0.25);
    session.setLayerOpacity(0.2);
    session.finishOpacityEdit();
    QCOMPARE(layerWith(session, folder).opacity, 0.2);
    session.undo();
    QCOMPARE(layerWith(session, folder).opacity, 0.5);
    // Two layers selected: neither gate holds.
    session.selectLayers({folder, session.document().value().layers.front().id}, folder);
    QVERIFY(!session.canEditOpacity());
    session.selectLayer(folder);
    session.addBlankLayer();
    QVERIFY(session.canEditAppearance());
    session.setShowsImporter(true);
    QVERIFY(!session.canEditAppearance());
}

void LayerAppearanceTests::whatEndsATransformEndsThePreviewAndTheDrag()
{
    EditorSession session;
    session.createDocument(4, 4);
    session.insert(asset(0.2));
    const QUuid below = session.activeLayerID().value();
    session.insert(asset(0.8));
    const QUuid above = session.activeLayerID().value();
    const int count = session.history.undoCount();
    // Another layer ends the drag: its values are one step.
    session.beginOpacityEdit();
    session.beginOpacityEdit();
    session.setLayerOpacity(0.7);
    session.setLayerOpacity(0.6);
    session.previewBlendMode(LayerBlendMode::screen, above);
    QVERIFY(!session.canUndo());
    session.selectLayer(below);
    QVERIFY(!session.blendPreview().has_value());
    QVERIFY(session.canUndo());
    QCOMPARE(session.history.undoCount(), count + 1);
    QCOMPARE(layerWith(session, above).opacity, 0.6);
    // A mode, or all selected layers, end a drag too.
    session.beginOpacityEdit();
    session.setLayerOpacity(0.4);
    session.setLayerBlendMode(LayerBlendMode::darken);
    QCOMPARE(session.history.undoCount(), count + 3);
    session.beginOpacityEdit();
    session.setLayerOpacity(0.3);
    session.setSelectedLayersOpacity(0.9);
    QCOMPARE(session.history.undoCount(), count + 5);
    QVERIFY(session.canUndo());
    // Selected layers already at the value leave a drag open.
    session.beginOpacityEdit();
    session.setLayerOpacity(0.9);
    session.setSelectedLayersOpacity(0.9);
    QVERIFY(!session.canUndo());
    session.finishOpacityEdit();
    QCOMPARE(session.history.undoCount(), count + 5);
    // The drag keeps its layer though another turns active.
    session.beginOpacityEdit();
    session.setActiveLayerID(above);
    session.setLayerOpacity(0.1);
    QCOMPARE(layerWith(session, below).opacity, 0.1);
    QCOMPARE(layerWith(session, above).opacity, 0.6);
    session.finishOpacityEdit();
    session.finishOpacityEdit();
    QCOMPARE(session.history.undoCount(), count + 6);
    // A drag whose layer is deleted changes nothing more.
    session.selectLayer(std::nullopt);
    session.insert(asset(0.5));
    const QUuid doomed = session.activeLayerID().value();
    session.beginOpacityEdit();
    session.setLayerOpacity(0.8);
    session.setActiveLayerID(above);
    session.deleteLayer(doomed);
    const std::optional<CanvasDocument> left = session.document();
    const int steps = session.history.undoCount();
    QSignalSpy changes(&session, &EditorSession::changed);
    session.setLayerOpacity(0.2);
    QCOMPARE(session.document(), left);
    QCOMPARE(session.history.undoCount(), steps);
    QCOMPARE(int(changes.count()), 0);
    // The drag and the deletion inside it are one step.
    session.finishOpacityEdit();
    QCOMPARE(session.history.undoCount(), count + 8);
    session.undo();
    session.undo();
    QCOMPARE(session.history.undoCount(), count + 6);
    // A transform committed in a drag: the drag ends first.
    session.selectLayer(above);
    const LayerTransform placed = layerWith(session, above).transform;
    session.beginOpacityEdit();
    session.setLayerOpacity(0.35);
    session.beginTransform();
    session.nudgeLayer(3, 0);
    session.commitTransform();
    QVERIFY(session.canUndo());
    QCOMPARE(session.history.undoCount(), count + 8);
    QCOMPARE(session.history.undoName(), QString("Transform Layer"));
    session.undo();
    QCOMPARE(layerWith(session, above).transform, placed);
    QCOMPARE(layerWith(session, above).opacity, 0.35);
    QCOMPARE(session.history.undoName(), QString("Layer Opacity"));
    session.undo();
    QCOMPARE(layerWith(session, above).opacity, 0.6);
}

void LayerAppearanceTests::moveToolNumberKeysSetSelectedLayersOpacityAsOneUndo()
{
    EditorSession session;
    session.createDocument(4, 4);
    session.insert(asset(0.2));
    const QUuid first = session.activeLayerID().value();
    session.insert(asset(0.8));
    const QUuid second = session.activeLayerID().value();
    session.addGroup();
    const QUuid folder = session.activeLayerID().value();
    session.selectTool(NavigationTool::move);
    session.selectLayers({first, second, folder}, second);
    const int count = session.history.undoCount();
    session.typeOpacityDigit(5, 10);
    for (const ImageLayer &layer : session.document().value().layers) {
        // The folder takes it too, since 1.1.6.
        if (layer.id == first || layer.id == second || layer.id == folder)
            QCOMPARE(layer.opacity, 0.5);
    }
    QCOMPARE(session.history.undoCount(), count + 1);
}

QTEST_GUILESS_MAIN(LayerAppearanceTests)
#include "LayerAppearanceTests.moc"
