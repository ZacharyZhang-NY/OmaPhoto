#include "Document/BrushStroke.h"
#include "SessionFixtures.h"
#include <QtTest>

// LayerMerge: what Ctrl+E merges and what it bakes.
namespace {
QUuid insertColor(EditorSession &session, int width, int height, QPointF center, QColor color, const QString &name)
{
    QImage image(width, height, QImage::Format_RGBA8888_Premultiplied);
    image.fill(color);
    session.insert(ImportedImage(image, image, name), center);
    return session.activeLayerID().value();
}

int layerCount(const EditorSession &session)
{
    return int(session.document().value().layers.size());
}

bool near(QColor lhs, QColor rhs)
{
    return std::abs(lhs.red() - rhs.red()) <= 1 && std::abs(lhs.green() - rhs.green()) <= 1 && std::abs(lhs.blue() - rhs.blue()) <= 1
        && std::abs(lhs.alpha() - rhs.alpha()) <= 1;
}

// The document as the canvas and the export draw it.
QImage composite(const EditorSession &session)
{
    QImage full = BrushRaster::context(8, 8, false);
    QPainter painter(&full);
    session.drawLiveComposite(session.document().value(), painter);
    return full;
}

bool sameComposite(const QImage &before, const QImage &after)
{
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            if (!near(before.pixelColor(x, y), after.pixelColor(x, y)))
                return false;
        }
    }
    return true;
}

// Eight by eight, the left half revealed.
ImportedImage halfMask()
{
    QImage image(8, 8, QImage::Format_Grayscale8);
    image.fill(0);
    for (int y = 0; y < 8; ++y)
        std::fill_n(image.scanLine(y), 4, uchar(255));
    return LayerMask::assetFrom(image);
}
}

class LayerMergeTests : public QObject {
    Q_OBJECT
private slots:
    void mergeDownBakesTheLayerBeneathAsOneStep();
    void thePlanNamesWhatMergesAndRefusesTheRest();
    void selectedLayersMergeAtTheTopsPlaceAndKeepClips();
    void aFolderMergesItsContentsAndGoes();
    void hiddenLayersAndAnOpenTransformGoThroughTheCanvas();
    void masksBlendsAndClipsBakeAsTheCanvasShowsThem();
};

void LayerMergeTests::mergeDownBakesTheLayerBeneathAsOneStep()
{
    EditorSession session;
    session.createDocument(8, 8);
    const QUuid lower = insertColor(session, 4, 4, {2, 2}, Qt::blue, "Lower");
    const QUuid upper = insertColor(session, 4, 4, {4, 4}, Qt::red, "Upper");
    session.setLayerOpacity(0.5);
    QCOMPARE(session.mergeTitle(), QString("Merge Down"));
    QVERIFY(session.canMergeLayers());
    const int steps = session.history.undoCount();
    session.mergeLayers();
    QCOMPARE(layerCount(session), 1);
    const ImageLayer merged = session.activeLayer().value();
    QVERIFY(merged.id != lower && merged.id != upper);
    QCOMPARE(merged.name, QString("Lower"));
    QCOMPARE(merged.opacity, 1.0);
    // Trimmed to the six by six both cover, in place.
    QCOMPARE(merged.transform, (LayerTransform{.origin = {0, 0}, .size = {6, 6}}));
    const QImage pixels = merged.asset.value().image();
    QCOMPARE(pixels.size(), QSize(6, 6));
    QCOMPARE(pixels.pixelColor(0, 0), QColor(Qt::blue));
    QVERIFY(near(pixels.pixelColor(3, 3), QColor(128, 0, 127)));
    QVERIFY(near(pixels.pixelColor(5, 5), QColor(255, 0, 0, 128)));
    QCOMPARE(pixels.pixelColor(5, 0).alpha(), 0);
    QCOMPARE(merged.asset.value().thumbnail.size(), QSize(6, 6));
    QCOMPARE(session.history.undoCount(), steps + 1);
    QCOMPARE(session.history.undoName(), QString("Merge Down"));
    session.undo();
    QCOMPARE(layerCount(session), 2);
    QCOMPARE(session.document().value().layers[1].id, upper);
    session.redo();
    QCOMPARE(layerCount(session), 1);
}

void LayerMergeTests::thePlanNamesWhatMergesAndRefusesTheRest()
{
    EditorSession session;
    QVERIFY(!session.canMergeLayers());
    QCOMPARE(session.mergeTitle(), QString("Merge Down"));
    session.createDocument(8, 8);
    const QUuid bottom = insertColor(session, 2, 2, {1, 1}, Qt::red, "Bottom");
    // The bottom layer has nothing beneath it.
    QVERIFY(!session.canMergeLayers());
    session.mergeLayers();
    QCOMPARE(layerCount(session), 1);
    session.addGroup();
    const QUuid empty = session.activeLayerID().value();
    QVERIFY(!session.canMergeLayers());
    // Inserted while the folder is active, the layer lands inside.
    const QUuid above = insertColor(session, 2, 2, {1, 1}, Qt::red, "Above");
    QCOMPARE(layerWith(session, above).parentID, std::optional(empty));
    QVERIFY(!session.canMergeLayers());
    session.selectLayer(empty);
    QCOMPARE(session.mergeTitle(), QString("Merge Group"));
    // A folder in the selection brings what it holds.
    session.selectLayers({bottom, empty}, empty);
    QCOMPARE(session.mergeTitle(), QString("Merge Layers"));
    session.mergeLayers();
    QCOMPARE(layerCount(session), 1);
    QCOMPARE(session.activeLayer().value().name, QString("Folder 1"));
    session.undo();
    // Folder and child together: the result has no folder.
    session.selectLayers({empty, above}, above);
    QCOMPARE(session.mergeTitle(), QString("Merge Layers"));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("merge: the result is no valid hierarchy"));
    session.mergeLayers();
    QCOMPARE(layerCount(session), 3);
    // Outside its folder, the folder beneath is no partner.
    QVERIFY(session.placeLayer(above, std::nullopt));
    session.selectLayer(above);
    QCOMPARE(session.document().value().layers[1].id, empty);
    QVERIFY(!session.canMergeLayers());
    session.mergeLayers();
    QCOMPARE(layerCount(session), 3);
    // A root layer beneath in the list: another folder.
    QVERIFY(session.placeLayer(bottom, std::nullopt) && session.placeLayer(above, empty));
    QCOMPARE(session.document().value().layers[1].id, bottom);
    QCOMPARE(session.document().value().layers[2].id, above);
    session.selectLayer(above);
    QVERIFY(!session.canMergeLayers());
    // Folders without pixels have nothing to bake.
    session.deleteActiveLayer();
    session.addGroup();
    const QUuid other = session.activeLayerID().value();
    session.selectLayers({empty, other}, empty);
    QVERIFY(!session.canMergeLayers());
    QVERIFY(session.placeLayer(other, empty));
    session.selectLayer(empty);
    QVERIFY(!session.canMergeLayers());
    session.selectLayers({bottom, empty}, empty);
    QVERIFY(session.canMergeLayers());
    session.setIsProjectBusy(true);
    QVERIFY(!session.canMergeLayers());
}

void LayerMergeTests::selectedLayersMergeAtTheTopsPlaceAndKeepClips()
{
    EditorSession session;
    session.createDocument(8, 8);
    const QUuid a = insertColor(session, 2, 2, {1, 1}, Qt::red, "A");
    const QUuid b = insertColor(session, 2, 2, {5, 1}, Qt::green, "B");
    const QUuid c = insertColor(session, 2, 2, {1, 5}, Qt::blue, "C");
    const QUuid d = insertColor(session, 2, 2, {5, 5}, Qt::white, "D");
    session.toggleClippingMask(d);
    QCOMPARE(layerWith(session, d).maskSourceID, std::optional(c));
    session.selectLayers({a, c}, a);
    session.mergeLayers();
    // The result takes the topmost selected layer's name and slot.
    QCOMPARE(layerCount(session), 3);
    const std::vector<ImageLayer> &layers = session.document().value().layers;
    QCOMPARE(layers[0].id, b);
    QCOMPARE(layers[1].name, QString("C"));
    QCOMPARE(layers[2].id, d);
    QCOMPARE(layers[2].maskSourceID, std::optional(layers[1].id));
    QCOMPARE(session.activeLayerID(), std::optional(layers[1].id));
    QCOMPARE(layers[1].transform, (LayerTransform{.origin = {0, 0}, .size = {2, 6}}));
    QCOMPARE(session.history.undoName(), QString("Merge Layers"));
    // Clipped to something outside the merge, a layer draws whole.
    session.selectLayers({b, d}, d);
    session.mergeLayers();
    QCOMPARE(layerCount(session), 2);
    const ImageLayer bd = session.activeLayer().value();
    QCOMPARE(bd.name, QString("D"));
    QCOMPARE(bd.maskSourceID, std::nullopt);
    QCOMPARE(bd.transform, (LayerTransform{.origin = {4, 0}, .size = {2, 6}}));
    QCOMPARE(bd.asset.value().image().pixelColor(0, 4), QColor(Qt::white));
    // Inside a folder the result stays inside, drawn whole.
    session.addGroup();
    const QUuid folder = session.activeLayerID().value();
    const QUuid lowerC = session.document().value().layers[0].id;
    QVERIFY(session.placeLayer(lowerC, folder) && session.placeLayer(bd.id, folder));
    session.selectLayer(bd.id);
    QCOMPARE(session.mergeTitle(), QString("Merge Down"));
    session.mergeLayers();
    QCOMPARE(session.activeLayer().value().parentID, std::optional(folder));
    QCOMPARE(session.activeLayer().value().name, QString("C"));
    QCOMPARE(session.activeLayer().value().transform, (LayerTransform{.origin = {0, 0}, .size = {6, 6}}));
}

void LayerMergeTests::aFolderMergesItsContentsAndGoes()
{
    EditorSession session;
    session.createDocument(8, 8);
    const QUuid under = insertColor(session, 2, 2, {1, 1}, Qt::red, "Under");
    const QUuid p = insertColor(session, 2, 2, {5, 1}, Qt::green, "P");
    const QUuid q = insertColor(session, 2, 2, {1, 5}, Qt::blue, "Q");
    session.addGroup();
    const QUuid outer = session.activeLayerID().value();
    session.addGroup();
    const QUuid inner = session.activeLayerID().value();
    QVERIFY(session.placeLayer(inner, outer) && session.placeLayer(p, outer) && session.placeLayer(q, inner));
    session.selectLayer(outer);
    QCOMPARE(session.mergeTitle(), QString("Merge Group"));
    session.mergeLayers();
    QCOMPARE(layerCount(session), 2);
    const std::vector<ImageLayer> &layers = session.document().value().layers;
    QCOMPARE(layers[0].id, under);
    const ImageLayer merged = layers[1];
    QCOMPARE(merged.name, QString("Folder 1"));
    QVERIFY(!merged.isGroup && merged.asset.has_value());
    QCOMPARE(merged.parentID, std::nullopt);
    QVERIFY(merged.id != outer && merged.id != inner);
    // Both children's pixels, the folder's box trimmed around them.
    QCOMPARE(merged.transform, (LayerTransform{.origin = {0, 0}, .size = {6, 6}}));
    QCOMPARE(merged.asset.value().image().pixelColor(5, 0), QColor(Qt::green));
    QCOMPARE(merged.asset.value().image().pixelColor(0, 5), QColor(Qt::blue));
    QCOMPARE(merged.asset.value().image().pixelColor(0, 0).alpha(), 0);
    QCOMPARE(session.history.undoName(), QString("Merge Group"));
    session.undo();
    QCOMPARE(layerCount(session), 5);
}

void LayerMergeTests::hiddenLayersAndAnOpenTransformGoThroughTheCanvas()
{
    EditorSession session;
    session.createDocument(8, 8);
    insertColor(session, 2, 2, {1, 1}, Qt::red, "Lower");
    const QUuid upper = insertColor(session, 2, 2, {5, 5}, Qt::blue, "Upper");
    // A hidden layer merges as shown: not at all.
    session.toggleLayerVisibility(upper);
    session.mergeLayers();
    QCOMPARE(layerCount(session), 1);
    QCOMPARE(session.activeLayer().value().transform.size, QSizeF(2, 2));
    session.undo();
    session.toggleLayerVisibility(upper);
    // An open transform commits first: the moved pixels merge.
    session.beginTransform();
    session.previewTransform({.origin = {2, 2}, .size = {2, 2}});
    session.mergeLayers();
    QCOMPARE(layerCount(session), 1);
    QCOMPARE(session.activeLayer().value().transform.size, QSizeF(4, 4));
    QVERIFY(!session.transformEdit().has_value());
}

void LayerMergeTests::masksBlendsAndClipsBakeAsTheCanvasShowsThem()
{
    // A layer's own mask: the right half shows blue.
    EditorSession masked;
    masked.createDocument(8, 8);
    insertColor(masked, 8, 8, {4, 4}, Qt::blue, "Blue");
    const QUuid red = insertColor(masked, 8, 8, {4, 4}, Qt::red, "Red");
    rewrite(masked, [&](ProjectSnapshot &snapshot) { setMask(snapshot, red, halfMask()); });
    masked.selectLayer(red);
    QImage before = composite(masked);
    masked.mergeLayers();
    QCOMPARE(layerCount(masked), 1);
    QVERIFY(sameComposite(before, composite(masked)));
    QImage pixels = masked.activeLayer().value().asset.value().image();
    QCOMPARE(pixels.pixelColor(1, 1), QColor(Qt::red));
    QCOMPARE(pixels.pixelColor(6, 6), QColor(Qt::blue));
    QVERIFY(!masked.activeLayer().value().mask.has_value());
    // A folder's mask clips what the folder bakes.
    EditorSession folder;
    folder.createDocument(8, 8);
    const QUuid inside = insertColor(folder, 8, 8, {4, 4}, Qt::red, "Inside");
    folder.groupSelectedLayers();
    const QUuid group = folder.activeLayerID().value();
    rewrite(folder, [&](ProjectSnapshot &snapshot) { setMask(snapshot, group, halfMask()); });
    folder.selectLayer(group);
    QCOMPARE(layerWith(folder, inside).parentID, std::optional(group));
    before = composite(folder);
    folder.mergeLayers();
    QCOMPARE(layerCount(folder), 1);
    QVERIFY(sameComposite(before, composite(folder)));
    pixels = folder.activeLayer().value().asset.value().image();
    QCOMPARE(pixels.size(), QSize(4, 8));
    QCOMPARE(pixels.pixelColor(3, 7), QColor(Qt::red));
    QCOMPARE(folder.activeLayer().value().transform.origin, QPointF(0, 0));
    // A Multiply pair bakes the product; the result blends normally.
    EditorSession multiply;
    multiply.createDocument(8, 8);
    insertColor(multiply, 8, 8, {4, 4}, QColor(255, 128, 0), "Base");
    insertColor(multiply, 8, 8, {4, 4}, QColor(128, 255, 255), "Top");
    multiply.setLayerBlendMode(LayerBlendMode::multiply);
    before = composite(multiply);
    multiply.mergeLayers();
    QVERIFY(sameComposite(before, composite(multiply)));
    pixels = multiply.activeLayer().value().asset.value().image();
    QVERIFY(near(pixels.pixelColor(4, 4), QColor(128, 128, 0)));
    QCOMPARE(multiply.activeLayer().value().blendMode, LayerBlendMode::normal);
    // Clipped to a merged layer: shown only over it.
    EditorSession clipped;
    clipped.createDocument(8, 8);
    insertColor(clipped, 2, 2, {4, 4}, Qt::blue, "Small");
    const QUuid wide = insertColor(clipped, 8, 8, {4, 4}, Qt::red, "Wide");
    clipped.toggleClippingMask(wide);
    before = composite(clipped);
    clipped.mergeLayers();
    QCOMPARE(layerCount(clipped), 1);
    QVERIFY(sameComposite(before, composite(clipped)));
    QCOMPARE(clipped.activeLayer().value().transform, (LayerTransform{.origin = {3, 3}, .size = {2, 2}}));
    QCOMPARE(clipped.activeLayer().value().asset.value().image().pixelColor(0, 0), QColor(Qt::red));
    QCOMPARE(clipped.activeLayer().value().maskSourceID, std::nullopt);
}

QTEST_GUILESS_MAIN(LayerMergeTests)
#include "LayerMergeTests.moc"
