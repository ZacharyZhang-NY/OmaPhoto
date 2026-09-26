#include "Document/BrushStroke.h"
#include "Document/Distort.h"
#include "IO/ImageExporter.h"
#include "SelectionFixtures.h"
#include <QRegularExpression>

// Ctrl+T on selected pixels: they float, transform and merge back.
namespace {
// A 100×40 layer: red on the left, blue right.
std::unique_ptr<EditorSession> makeSession()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(100, 40, true);
    QImage image = BrushRaster::context(100, 40, false);
    QPainter painter(&image);
    painter.fillRect(QRect(0, 0, 50, 40), Qt::red);
    painter.fillRect(QRect(50, 0, 50, 40), Qt::blue);
    painter.end();
    session->insert(ImportedImage(image, image, "Halves"));
    session->deleteLayer(session->document().value().layers[0].id);
    return session;
}

void select(EditorSession &session, const QRectF &rect)
{
    session.applySelection(rectPath(rect), SelectionMode::replace, "Select");
}

std::vector<int> pixel(const QImage &image, int x, int y)
{
    const QImage bytes = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    const uchar *at = bytes.constScanLine(y) + x * 4;
    return {at[0], at[1], at[2], at[3]};
}

QImage render(const EditorSession &session)
{
    return ImageExporter::render(session.projectSnapshot().value()).image;
}

// Waits for the selected pixels to float.
bool floated(EditorSession &session)
{
    bool done = false;
    session.beginSelectionTransform([&] { done = true; });
    return QTest::qWaitFor([&] { return done; }, 10000) && !session.isProjectBusy();
}
}

class FloatingSelectionTests : public QObject {
    Q_OBJECT
private slots:
    void transformSelectionMovesPixelsAndOutlineAsOneUndo();
    void escapeRestoresExactlyWithoutAnUndoStep();
    void applyingAnUnchangedTransformLeavesSoftEdgesUntouched();
    void scalingAndMovingPastTheLayerEdgeGrowsTheLayer();
    void aDistortedSelectionWarpsItsPixelsAndItsOutline();
    void aMergeThatCannotGrowRestoresTheDocument();
    void transformSelectionIsRefusedWhereSwiftRefusesIt();
    void theFloatingLayerTakesItsSourcesPlaceAndLook();
    void aSourceGoneMidLiftPutsTheDocumentBack();
    void aMaskGrowsWithTheMergedLayer();
    void aTransformedSourceTakesTheMergeInItsOwnGrid();
    void aFoldedDistortionDropsTheOutline();
};

void FloatingSelectionTests::transformSelectionMovesPixelsAndOutlineAsOneUndo()
{
    const auto session = makeSession();
    const QUuid source = session->activeLayerID().value();
    select(*session, QRectF(10, 10, 10, 10));
    const CanvasDocument before = session->document().value();
    const int count = session->history.undoCount();
    QVERIFY(session->canTransformSelection());
    QVERIFY(floated(*session));
    const TransformEdit edit = session->transformEdit().value();
    QVERIFY(edit.floating && session->tool() == NavigationTool::move);
    QCOMPARE(session->activeLayer().value().name, QString("Floating Selection"));
    QCOMPARE(session->transformPixelSize(), std::optional(QSizeF(10, 10)));
    LayerTransform draft = edit.draft;
    draft.origin = QPointF(70, 20);
    session->previewTransform(draft);
    QCOMPARE(session->displayedSelection().value().path.boundingRect(), QRectF(70, 20, 10, 10));
    session->commitTransform();
    QCOMPARE(session->history.undoCount(), count + 1);
    QCOMPARE(session->history.undoName(), QString("Transform Selection"));
    QVERIFY(session->document().value().layers.size() == 1 && session->activeLayerID() == source);
    QCOMPARE(session->selection().value().path.boundingRect(), QRectF(70, 20, 10, 10));
    const QImage result = render(*session);
    QCOMPARE(pixel(result, 15, 15)[3], 0);
    QCOMPARE(pixel(result, 75, 25), (std::vector<int>{255, 0, 0, 255}));
    QCOMPARE(pixel(result, 85, 25), (std::vector<int>{0, 0, 255, 255}));
    session->undo();
    QVERIFY(session->document().value() == before);
}

void FloatingSelectionTests::escapeRestoresExactlyWithoutAnUndoStep()
{
    const auto session = makeSession();
    select(*session, QRectF(10, 10, 10, 10));
    const CanvasDocument before = session->document().value();
    const std::optional<QUuid> active = session->activeLayerID();
    const int count = session->history.undoCount();
    QVERIFY(floated(*session));
    LayerTransform draft = session->transformEdit().value().draft;
    draft.origin.rx() += 30;
    session->previewTransform(draft);
    session->cancelTransform();
    QVERIFY(session->document().value() == before && session->activeLayerID() == active);
    QVERIFY(!session->transformEdit().has_value() && session->history.undoCount() == count);
}

void FloatingSelectionTests::applyingAnUnchangedTransformLeavesSoftEdgesUntouched()
{
    const auto session = makeSession();
    session->setSelectionAntialiased(true);
    QPainterPath ellipse;
    ellipse.addEllipse(QRectF(10.3, 5.7, 30, 25));
    session->applySelection(ellipse, SelectionMode::replace, "Select");
    const CanvasDocument before = session->document().value();
    const int count = session->history.undoCount();
    QVERIFY(floated(*session));
    session->commitTransform();
    QVERIFY(session->document().value() == before && session->history.undoCount() == count);
}

void FloatingSelectionTests::scalingAndMovingPastTheLayerEdgeGrowsTheLayer()
{
    const auto session = makeSession();
    const QUuid source = session->activeLayerID().value();
    select(*session, QRectF(0, 0, 10, 10));
    QVERIFY(floated(*session));
    LayerTransform draft = session->transformEdit().value().draft;
    // Twice as large, and hanging off the canvas corner.
    draft.size = QSizeF(20, 20);
    draft.origin = QPointF(90, 30);
    session->previewTransform(draft);
    // Switching tools applies it.
    session->selectTool(NavigationTool::lasso);
    QVERIFY(!session->transformEdit().has_value() && session->history.undoName() == "Transform Selection");
    QCOMPARE(layerWith(*session, source).size(), QSizeF(110, 50));
    const QImage result = render(*session);
    QCOMPARE(pixel(result, 95, 35), (std::vector<int>{255, 0, 0, 255}));
    QCOMPARE(pixel(result, 5, 5)[3], 0);
    QCOMPARE(session->selection().value().path.boundingRect(), QRectF(90, 30, 20, 20));
}

void FloatingSelectionTests::aDistortedSelectionWarpsItsPixelsAndItsOutline()
{
    const auto session = makeSession();
    session->setSelectionAntialiased(false);
    select(*session, QRectF(10, 10, 20, 20));
    QVERIFY(floated(*session));
    session->beginDistort();
    // One corner pulled out, one pushed in: still convex.
    Corners corners = session->transformEdit().value().corners.value();
    corners[1] = QPointF(40, 5);
    corners[2] = QPointF(22, 22);
    session->previewCorners(corners);
    session->commitTransform();
    QCOMPARE(session->history.undoName(), QString("Transform Selection"));
    const QRectF bounds = session->selection().value().path.boundingRect();
    QVERIFY2(std::abs(bounds.left() - 10) < 0.01 && std::abs(bounds.top() - 5) < 0.01 && std::abs(bounds.right() - 40) < 0.01
                 && std::abs(bounds.bottom() - 30) < 0.01,
             qPrintable(QStringLiteral("%1 %2 %3 %4").arg(bounds.left()).arg(bounds.top()).arg(bounds.right()).arg(bounds.bottom())));
    // In the old square, outside the new shape: a hole.
    const QImage result = render(*session);
    QCOMPARE(pixel(result, 28, 28)[3], 0);
    QCOMPARE(pixel(result, 14, 14), (std::vector<int>{255, 0, 0, 255}));
    // Hard outlines stay hard, warped or moved.
    QVERIFY(!session->selection().value().antialiased);
    QVERIFY(floated(*session));
    LayerTransform draft = session->transformEdit().value().draft;
    draft.origin.rx() += 5;
    session->previewTransform(draft);
    QVERIFY(!session->displayedSelection().value().antialiased);
    session->commitTransform();
    QVERIFY(!session->selection().value().antialiased);
}

void FloatingSelectionTests::aMergeThatCannotGrowRestoresTheDocument()
{
    const auto session = makeSession();
    select(*session, QRectF(10, 10, 10, 10));
    const CanvasDocument before = session->document().value();
    const int count = session->history.undoCount();
    QVERIFY(floated(*session));
    // Wider than a layer may grow: refused, the document restored.
    LayerTransform draft = session->transformEdit().value().draft;
    draft.size = QSizeF(35000, 10);
    session->previewTransform(draft);
    QCOMPARE(session->transformEdit().value().draft.size, QSizeF(35000, 10));
    session->commitTransform();
    QVERIFY(session->document().value() == before);
    QCOMPARE(session->history.undoCount(), count);
    QVERIFY(session->brushError().has_value());
    // A source without pixels cannot take them.
    const QImage dot = BrushRaster::context(4, 4, false);
    QVERIFY_THROWS_EXCEPTION(ProjectError, FloatingMerge::merge(dot, LayerTransform{.origin = {0, 0}, .size = {4, 4}}, ImageLayer("Blank", QSizeF(10, 10))));
}

void FloatingSelectionTests::transformSelectionIsRefusedWhereSwiftRefusesIt()
{
    const auto session = makeSession();
    const QUuid id = session->activeLayerID().value();
    // Without a selection Ctrl+T transforms the layer.
    QVERIFY(!session->canTransformSelection());
    session->transformCommand();
    QVERIFY(session->transformEdit().has_value() && !session->transformEdit().value().floating);
    session->cancelTransform();
    // An empty selection, a mask target, a blank layer: no.
    select(*session, QRectF(0, 0, 10, 10));
    session->applySelection(rectPath(QRectF(0, 0, 100, 40)), SelectionMode::subtract, "Subtract");
    QVERIFY(!session->canTransformSelection());
    select(*session, QRectF(0, 0, 10, 10));
    session->addLayerMask(true);
    session->selectLayerTarget(id, true);
    QVERIFY(!session->canTransformSelection());
    session->selectLayerTarget(id, false);
    QVERIFY(session->canTransformSelection());
    // Hidden, the layer takes no edit.
    session->toggleLayerVisibility(id);
    QVERIFY(!session->canTransformSelection());
    session->toggleLayerVisibility(id);
    session->addBlankLayer();
    select(*session, QRectF(0, 0, 10, 10));
    QVERIFY(!session->canTransformSelection());
    // Refused, the callback still comes and nothing changes.
    const CanvasDocument before = session->document().value();
    QVERIFY(floated(*session));
    QVERIFY(session->document().value() == before && !session->transformEdit().has_value());
    // Pixels with a selection: Ctrl+T floats them.
    session->selectLayer(id);
    select(*session, QRectF(0, 0, 10, 10));
    session->transformCommand();
    QTRY_VERIFY(session->transformEdit().has_value() && session->transformEdit().value().floating);
    QVERIFY(!session->canTransformSelection());
    session->cancelTransform();
    // A sliver under the copy's rounding: nothing to lift.
    select(*session, QRectF(10, 10, 0.0005, 10));
    QVERIFY(session->canTransformSelection());
    const CanvasDocument sliver = session->document().value();
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("no selected pixels to lift")));
    QVERIFY(floated(*session));
    QVERIFY(session->document().value() == sliver && !session->transformEdit().has_value());
}

void FloatingSelectionTests::theFloatingLayerTakesItsSourcesPlaceAndLook()
{
    const auto session = makeSession();
    const QUuid source = session->activeLayerID().value();
    session->groupSelectedLayers();
    const std::optional<QUuid> folder = layerWith(*session, source).parentID;
    QVERIFY(folder.has_value());
    // A layer above, so the floating one's place shows.
    session->selectLayer(source);
    session->addBlankLayer();
    session->selectLayer(source);
    session->setLayerOpacity(0.5);
    session->setLayerBlendMode(LayerBlendMode::multiply);
    session->selectTool(NavigationTool::marquee);
    select(*session, QRectF(10, 10, 10, 10));
    QVERIFY(floated(*session));
    QCOMPARE(session->tool(), NavigationTool::move);
    const ImageLayer floating = session->activeLayer().value();
    const std::vector<ImageLayer> &layers = session->document().value().layers;
    QCOMPARE(indexOf(layers, floating.id), indexOf(layers, source) + 1);
    QCOMPARE(floating.parentID, folder);
    QCOMPARE(floating.opacity, 0.5);
    QCOMPARE(floating.blendMode, LayerBlendMode::multiply);
    QCOMPARE(floating.transform.origin, QPointF(10, 10));
    session->cancelTransform();
}

void FloatingSelectionTests::aSourceGoneMidLiftPutsTheDocumentBack()
{
    const auto session = makeSession();
    select(*session, QRectF(10, 10, 10, 10));
    const CanvasDocument before = session->document().value();
    bool done = false;
    session->beginSelectionTransform([&] { done = true; });
    QVERIFY(session->isProjectBusy());
    // The layer goes mid-clear, as a load would take it.
    rewrite(*session, [&](ProjectSnapshot &snapshot) {
        snapshot.manifest.layers.clear();
        snapshot.images.clear();
        snapshot.manifest.activeLayerID = std::nullopt;
    });
    QTRY_VERIFY(done);
    // Swift puts the document from before back.
    QVERIFY(session->document().value() == before);
    QVERIFY(!session->transformEdit().has_value() && !session->isProjectBusy());
}

void FloatingSelectionTests::aMaskGrowsWithTheMergedLayer()
{
    const auto session = makeSession();
    const QUuid source = session->activeLayerID().value();
    // A black block in the white mask marks its place.
    QImage drawn = BrushRaster::context(100, 40, true);
    drawn.fill(Qt::white);
    for (int y = 20; y < 30; ++y)
        std::fill_n(drawn.scanLine(y) + 60, 10, uchar(0));
    rewrite(*session, [&](ProjectSnapshot &snapshot) { setMask(snapshot, source, LayerMask::assetFrom(drawn)); });
    session->selectLayer(source);
    const ImageIdentity mask = layerWith(*session, source).mask.value().asset.identity();
    // Moved within the layer, the mask stays as it was.
    select(*session, QRectF(10, 10, 10, 10));
    QVERIFY(floated(*session));
    LayerTransform draft = session->transformEdit().value().draft;
    draft.origin = QPointF(30, 10);
    session->previewTransform(draft);
    session->commitTransform();
    QCOMPARE(layerWith(*session, source).mask.value().asset.identity(), mask);
    // Past the right edge, the mask grows white beside it.
    select(*session, QRectF(30, 10, 10, 10));
    QVERIFY(floated(*session));
    draft = session->transformEdit().value().draft;
    draft.origin = QPointF(95, 10);
    session->previewTransform(draft);
    session->commitTransform();
    const ImageLayer grown = layerWith(*session, source);
    QCOMPARE(grown.size(), QSizeF(105, 40));
    const QImage grownMask = grown.mask.value().asset.image();
    QCOMPARE(grownMask.size(), QSize(105, 40));
    QCOMPARE(qGray(grownMask.pixel(102, 5)), 255);
    QCOMPARE(qGray(grownMask.pixel(65, 25)), 0);
    QCOMPARE(qGray(grownMask.pixel(50, 5)), 255);
    // Past the left edge, the old mask shifts along.
    select(*session, QRectF(0, 20, 10, 10));
    QVERIFY(floated(*session));
    draft = session->transformEdit().value().draft;
    draft.origin = QPointF(-5, 20);
    session->previewTransform(draft);
    session->commitTransform();
    const ImageLayer left = layerWith(*session, source);
    QCOMPARE(left.transform.origin, QPointF(-5, 0));
    const QImage leftMask = left.mask.value().asset.image();
    QCOMPARE(leftMask.size(), QSize(110, 40));
    QCOMPARE(qGray(leftMask.pixel(66, 25)), 0);
    QCOMPARE(qGray(leftMask.pixel(62, 25)), 255);
    QCOMPARE(qGray(leftMask.pixel(2, 25)), 255);
    // A mask placed apart keeps its grid.
    rewrite(*session, [&](ProjectSnapshot &snapshot) { record(snapshot, source).maskPlacement = LayerTransform{.origin = {-5, 0}, .size = {110, 40}}; });
    session->selectLayer(source);
    const ImageIdentity placed = layerWith(*session, source).mask.value().asset.identity();
    select(*session, QRectF(95, 10, 10, 10));
    QVERIFY(floated(*session));
    draft = session->transformEdit().value().draft;
    draft.origin = QPointF(110, 10);
    session->previewTransform(draft);
    session->commitTransform();
    // The canvas clipped the selection: five columns float.
    QCOMPARE(layerWith(*session, source).size(), QSizeF(120, 40));
    QCOMPARE(layerWith(*session, source).mask.value().asset.identity(), placed);
}

void FloatingSelectionTests::aTransformedSourceTakesTheMergeInItsOwnGrid()
{
    const std::vector<int> red{255, 0, 0, 255}, blue{0, 0, 255, 255};
    QImage image = BrushRaster::context(50, 20, false);
    image.fill(Qt::blue);
    QPainter painter(&image);
    painter.fillRect(QRect(0, 0, 25, 20), Qt::red);
    painter.end();
    // Doubled at 20,10: red to 70, blue past it.
    EditorSession scaled;
    scaled.createDocument(200, 100, true);
    scaled.insert(ImportedImage(image, image, "Halves"));
    const QUuid id = scaled.activeLayerID().value();
    const LayerTransform doubled{.origin = {20, 10}, .size = {100, 40}};
    rewrite(scaled, [&](ProjectSnapshot &snapshot) { record(snapshot, id).transform = doubled; });
    scaled.selectLayer(id);
    select(scaled, QRectF(30, 20, 20, 20));
    QVERIFY(floated(scaled));
    LayerTransform draft = scaled.transformEdit().value().draft;
    draft.origin = QPointF(80, 20);
    scaled.previewTransform(draft);
    scaled.commitTransform();
    QCOMPARE(layerWith(scaled, id).transform, doubled);
    QCOMPARE(layerWith(scaled, id).asset.value().size(), QSize(50, 20));
    QImage result = render(scaled);
    QCOMPARE(pixel(result, 90, 30), red);
    QCOMPARE(pixel(result, 110, 30), blue);
    QCOMPARE(pixel(result, 40, 30)[3], 0);
    // Turned a quarter: the block lands where it was put.
    EditorSession turned;
    turned.createDocument(200, 100, true);
    turned.insert(ImportedImage(image, image, "Halves"));
    const QUuid other = turned.activeLayerID().value();
    const LayerTransform quarter{.origin = {20, 10}, .size = {100, 40}, .rotation = 90};
    rewrite(turned, [&](ProjectSnapshot &snapshot) { record(snapshot, other).transform = quarter; });
    turned.selectLayer(other);
    const QImage before = render(turned);
    const std::vector<int> above = pixel(before, 70, 20);
    QVERIFY(above[3] == 255 && above != pixel(before, 70, 50));
    select(turned, QRectF(66, 16, 8, 8));
    QVERIFY(floated(turned));
    draft = turned.transformEdit().value().draft;
    draft.origin.ry() += 30;
    turned.previewTransform(draft);
    turned.commitTransform();
    QCOMPARE(layerWith(turned, other).transform, quarter);
    result = render(turned);
    QCOMPARE(pixel(result, 70, 50), above);
    QCOMPARE(pixel(result, 70, 20)[3], 0);
}

void FloatingSelectionTests::aFoldedDistortionDropsTheOutline()
{
    const auto session = makeSession();
    select(*session, QRectF(10, 10, 20, 20));
    const DocumentSelection outline = session->selection().value();
    QVERIFY(floated(*session));
    session->beginDistort();
    // A bow tie is usable but folded: no outline carries.
    const Corners folded = {QPointF(10, 10), QPointF(30, 30), QPointF(30, 10), QPointF(10, 30)};
    session->previewCorners(folded);
    QVERIFY(session->transformEdit().value().corners.value() == folded);
    session->commitTransform();
    QCOMPARE(session->history.undoName(), QString("Transform Selection"));
    QVERIFY(!session->selection().has_value());
    session->undo();
    QVERIFY(session->selection().value() == outline);
}

QTEST_GUILESS_MAIN(FloatingSelectionTests)
#include "FloatingSelectionTests.moc"
