#include "BrushFixtures.h"
#include "SelectionFixtures.h"
#include "Document/EditorSession.h"
#include "IO/CanvasResizer.h"
#include "IO/ImageExporter.h"
#include "IO/ImageResizer.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// A shape layer's life: resized, previewed, repainted, saved.
namespace {
// Red, 30 by 20 at (10, 10), radius 6.
std::unique_ptr<EditorSession> shapeSession()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(100, 80, true);
    session->selectTool(NavigationTool::shape);
    session->setForegroundColor(PaletteColor{1, 0, 0});
    session->setShapeCornerRadius(6);
    session->beginShape(QPointF(10, 10));
    session->dragShape(QPointF(40, 30), false, false);
    session->finishShape();
    return session;
}

int alphaAt(const EditorSession &session, int x, int y)
{
    return pixel(ImageExporter::render(session.projectSnapshot().value()).image, x, y)[3];
}

// An independent stroke, to hold a redrawn line against.
QImage stroked(QSize size, QPointF from, QPointF to, double width)
{
    QImage image = BrushRaster::context(size.width(), size.height(), false);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(0, 0, 255), width, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(from, to);
    painter.end();
    return image;
}

// Swift's await: true once the call's callback ran.
bool landed(const std::function<void(std::function<void()>)> &start)
{
    bool done = false;
    start([&] { done = true; });
    return QTest::qWaitFor([&] { return done; }, 20000);
}

void resize(EditorSession &session, QSizeF size)
{
    session.selectTool(NavigationTool::move);
    session.beginTransform();
    LayerTransform draft = session.transformEdit().value().draft;
    draft.size = size;
    session.previewTransform(draft);
}

QJsonObject firstShape(const ProjectSnapshot &snapshot, qsizetype index)
{
    return QJsonDocument::fromJson(snapshot.manifest.encoded()).object().value("layers").toArray().at(index).toObject().value("shape").toObject();
}

// The manifest with this layer's shape object replaced.
QByteArray withShape(const ProjectSnapshot &snapshot, const QJsonValue &shape)
{
    QJsonObject object = QJsonDocument::fromJson(snapshot.manifest.encoded()).object();
    QJsonArray layers = object.value("layers").toArray();
    QJsonObject layer = layers.at(1).toObject();
    layer.insert("shape", shape);
    layers.replace(1, layer);
    object.insert("layers", layers);
    return QJsonDocument(object).toJson();
}
}

class ShapeLayerTests : public QObject {
    Q_OBJECT
private slots:
    void resizingRedrawsTheShapeAtItsRadius();
    void aMaskOnTheOldGridStaysWhereItWas();
    void everyShapeInAGroupIsRedrawn();
    void whatIsNotRedrawn();
    void theMovePreviewKeepsTheCorners();
    void paintingLeavesPlainPixels();
    void fillsInvertsFiltersAndMergesDropTheShape();
    void aMaskStrokeOrADuplicateKeepsTheShape();
    void aProjectKeepsLiveShapesOnly();
    void theManifestRefusesAWrongShape();
    void resizersKeepOrDropTheShape();
    void aShapeNeedsItsImage();
    void anOlderLineRunsCornerToCorner();
    void aLineKeepsItsEndsWhenRedrawn();
    void aDistortedShapeStretches();
};

void ShapeLayerTests::resizingRedrawsTheShapeAtItsRadius()
{
    const auto session = shapeSession();
    const ImageLayer before = session->activeLayer().value();
    resize(*session, QSizeF(60, 40));
    session->commitTransform();
    QCOMPARE(session->history.undoName(), QString("Transform Layer"));
    const ImageLayer after = session->activeLayer().value();
    QCOMPARE(after.asset.value().size(), QSize(60, 40));
    QCOMPARE(after.asset.value().name, QString("Rectangle 1"));
    QCOMPARE(after.liveShape().value().style, before.shape.value().style);
    // Radius 6 fills (13, 13); a stretched 12 would not.
    QCOMPARE(alphaAt(*session, 13, 13), 255);
    QCOMPARE(alphaAt(*session, 10, 10), 0);
    QCOMPARE(alphaAt(*session, 69, 49), 0);
    QCOMPARE(alphaAt(*session, 40, 30), 255);
    session->undo();
    QCOMPARE(session->activeLayer().value().asset.value().identity(), before.asset.value().identity());
    QVERIFY(session->activeLayer().value().liveShape());
    // Whole pixels, rounded; one side alone redraws too.
    resize(*session, QSizeF(60.6, 40));
    session->commitTransform();
    QCOMPARE(session->activeLayer().value().asset.value().size(), QSize(61, 40));
    resize(*session, QSizeF(61, 30));
    session->commitTransform();
    QCOMPARE(session->activeLayer().value().asset.value().size(), QSize(61, 30));
}

void ShapeLayerTests::aMaskOnTheOldGridStaysWhereItWas()
{
    const auto session = shapeSession();
    session->addLayerMask(true);
    const ImageIdentity mask = session->activeLayer().value().mask.value().asset.identity();
    resize(*session, QSizeF(60, 40));
    session->commitTransform();
    const ImageLayer after = session->activeLayer().value();
    QCOMPARE(after.mask.value().placement, std::optional(after.transform));
    QVERIFY(after.mask.value().asset.identity() == mask);
    // Undone, the mask follows the grid again.
    session->undo();
    QVERIFY(!session->activeLayer().value().mask.value().placement);
}

void ShapeLayerTests::everyShapeInAGroupIsRedrawn()
{
    const auto session = shapeSession();
    const QUuid rectangle = session->activeLayerID().value();
    session->setShapeKind(ShapeKind::ellipse);
    session->beginShape(QPointF(50, 10));
    session->dragShape(QPointF(70, 30), false, false);
    session->finishShape();
    const QUuid ellipse = session->activeLayerID().value();
    session->selectLayers({rectangle, ellipse}, ellipse);
    session->selectTool(NavigationTool::move);
    session->beginTransform();
    QVERIFY(session->transformEdit().value().group);
    LayerTransform box = session->transformEdit().value().draft;
    box.size = QSizeF(box.size.width() * 2, box.size.height() * 2);
    session->previewTransform(box);
    session->commitTransform();
    QCOMPARE(session->history.undoName(), QString("Transform Layers"));
    for (const QUuid id : {rectangle, ellipse}) {
        const ImageLayer layer = layerWith(*session, id);
        QVERIFY(layer.liveShape());
        QCOMPARE(layer.asset.value().size(), QSize(int(std::round(layer.transform.size.width())), int(std::round(layer.transform.size.height()))));
    }
    QCOMPARE(layerWith(*session, ellipse).asset.value().size(), QSize(40, 40));
}

void ShapeLayerTests::whatIsNotRedrawn()
{
    const auto session = shapeSession();
    const ImportedImage asset = session->activeLayer().value().asset.value();
    // Sizes that round to the same pixels keep the pixels.
    resize(*session, QSizeF(30.4, 19.6));
    session->commitTransform();
    QCOMPARE(session->activeLayer().value().transform.size, QSizeF(30.4, 19.6));
    QCOMPARE(session->activeLayer().value().asset.value().identity(), asset.identity());
    QVERIFY(session->activeLayer().value().liveShape());
    // Past 100 megapixels it stretches as it is.
    resize(*session, QSizeF(20'001, 5'000));
    session->commitTransform();
    QCOMPARE(session->activeLayer().value().asset.value().identity(), asset.identity());
    session->undo();
    // Painted over, it stretches like any pixels.
    session->selectTool(NavigationTool::brush);
    session->beginBrush(QPointF(20, 20));
    session->finishBrush();
    const ImportedImage painted = session->activeLayer().value().asset.value();
    resize(*session, QSizeF(60, 40));
    session->commitTransform();
    QCOMPARE(session->activeLayer().value().asset.value().identity(), painted.identity());
    QVERIFY(!session->activeLayer().value().shape);
}

void ShapeLayerTests::theMovePreviewKeepsTheCorners()
{
    const auto session = shapeSession();
    const ImageLayer layer = session->activeLayer().value();
    LayerTransform transform = layer.transform;
    transform.size = QSizeF(60, 40);
    QVERIFY(session->shapeTransformPreview(layer, transform).isNull());
    resize(*session, QSizeF(60, 40));
    const QImage preview = session->shapeTransformPreview(layer, transform);
    QCOMPARE(preview.size(), QSize(60, 40));
    QVERIFY(alpha(preview, 3, 3) == 255 && alpha(preview, 0, 0) == 0);
    QCOMPARE(session->shapeTransformPreview(layer, transform).cacheKey(), preview.cacheKey());
    transform.size = QSizeF(61, 40);
    QVERIFY(session->shapeTransformPreview(layer, transform).cacheKey() != preview.cacheKey());
    // At most 2048 across; the radius shrinks alike.
    transform.size = QSizeF(4096, 40);
    const QImage wide = session->shapeTransformPreview(layer, transform);
    QCOMPARE(wide.size(), QSize(2048, 20));
    QVERIFY(alpha(wide, 1, 0) > 30 && alpha(wide, 0, 0) < 64);
    // Within half a pixel, or under one, there is none.
    transform.size = QSizeF(30.4, 20);
    QVERIFY(session->shapeTransformPreview(layer, transform).isNull());
    transform.size = QSizeF(30.5, 20);
    QCOMPARE(session->shapeTransformPreview(layer, transform).size(), QSize(31, 20));
    transform.size = QSizeF(0.9, 40);
    QVERIFY(session->shapeTransformPreview(layer, transform).isNull());
    // An ended edit empties the cache; the next draws afresh.
    transform.size = QSizeF(60, 40);
    const qint64 kept = session->shapeTransformPreview(layer, transform).cacheKey();
    session->cancelTransform();
    QVERIFY(session->shapeTransformPreview(layer, transform).isNull());
    resize(*session, QSizeF(60, 40));
    QVERIFY(session->shapeTransformPreview(layer, transform).cacheKey() != kept);
    session->cancelTransform();
    // Square corners and ellipses stretch with no preview.
    ImageLayer square = layer;
    square.shape.value().style.cornerRadius = 0;
    ImageLayer round = layer;
    round.shape.value().style.kind = ShapeKind::ellipse;
    resize(*session, QSizeF(60, 40));
    QVERIFY(session->shapeTransformPreview(square, transform).isNull() && session->shapeTransformPreview(round, transform).isNull());
}

void ShapeLayerTests::paintingLeavesPlainPixels()
{
    const auto session = shapeSession();
    session->selectTool(NavigationTool::brush);
    session->beginBrush(QPointF(20, 20));
    session->finishBrush();
    QCOMPARE(session->history.undoName(), QString("Brush Stroke"));
    QVERIFY(!session->activeLayer().value().shape);
    QVERIFY(!session->projectSnapshot().value().manifest.layers.back().shape);
    session->undo();
    QVERIFY(session->activeLayer().value().liveShape());
}

void ShapeLayerTests::fillsInvertsFiltersAndMergesDropTheShape()
{
    const std::vector<std::function<bool(EditorSession &)>> edits = {
        [](EditorSession &session) {
            return landed([&](std::function<void()> done) { session.fillSelection(EditorSession::FillSource::foreground, done); });
        },
        [](EditorSession &session) { return landed([&](std::function<void()> done) { session.invertPixels(done); }); },
        [](EditorSession &session) {
            session.beginFilter(FilterKind::contentAwareFill);
            return landed([&](std::function<void()> done) { session.commitFilter(done); });
        },
        [](EditorSession &session) {
            if (!landed([&](std::function<void()> done) { session.beginSelectionTransform(done); }))
                return false;
            LayerTransform draft = session.transformEdit().value().draft;
            draft.origin += QPointF(3, 2);
            session.previewTransform(draft);
            session.commitTransform();
            return true;
        },
    };
    const std::vector<QString> names{"Fill", "Invert", "Content-Aware Fill", "Transform Selection"};
    // A selection past the shape's pixels floats nothing out first.
    {
        const auto session = shapeSession();
        const QUuid id = session->activeLayerID().value();
        session->applySelection(rectPath(QRectF(60, 50, 10, 10)), SelectionMode::replace, "Select");
        QVERIFY(edits[3](*session));
        QVERIFY(!layerWith(*session, id).shape);
    }
    for (size_t index = 0; index < edits.size(); ++index) {
        const auto session = shapeSession();
        const QUuid id = session->activeLayerID().value();
        session->applySelection(rectPath(QRectF(20, 14, 8, 6)), SelectionMode::replace, "Select");
        QVERIFY(edits[index](*session));
        QCOMPARE(session->history.undoName(), names[index]);
        QVERIFY2(!layerWith(*session, id).shape, qPrintable(names[index]));
    }
}

void ShapeLayerTests::aMaskStrokeOrADuplicateKeepsTheShape()
{
    const auto session = shapeSession();
    const QUuid id = session->activeLayerID().value();
    session->addLayerMask(true);
    session->selectLayerTarget(id, true);
    session->selectTool(NavigationTool::brush);
    session->beginBrush(QPointF(20, 20));
    session->finishBrush();
    QCOMPARE(session->history.undoName(), QString("Paint Mask"));
    QVERIFY(session->activeLayer().value().liveShape());
    session->selectLayerTarget(id, false);
    session->duplicateActiveLayer();
    QCOMPARE(session->activeLayer().value().name, QString("Rectangle 1 copy"));
    QCOMPARE(session->activeLayer().value().liveShape(), layerWith(*session, id).liveShape());
}

void ShapeLayerTests::aProjectKeepsLiveShapesOnly()
{
    const auto session = shapeSession();
    session->setShapeKind(ShapeKind::line);
    session->setShapeLineWidth(3);
    session->setForegroundColor(PaletteColor{0.2, 0.4, 0.6});
    session->beginShape(QPointF(60, 60));
    session->dragShape(QPointF(80, 70), false, false);
    session->finishShape();
    const ProjectSnapshot snapshot = session->projectSnapshot().value();
    QCOMPARE(snapshot.manifest.layers[1].shape.value().cornerRadius, 6.0);
    const LayerShapeStyle line = snapshot.manifest.layers[2].shape.value();
    QCOMPARE(line.lineWidth.value(), 3.0);
    // Swift's keys; a rectangle writes no line fields.
    QCOMPARE(firstShape(snapshot, 1).keys(), (QStringList{"blue", "cornerRadius", "green", "kind", "red"}));
    QCOMPARE(firstShape(snapshot, 1).value("kind").toString(), QString("Rectangle"));
    QCOMPARE(firstShape(snapshot, 2).value("start").toArray(), (QJsonArray{line.start->x(), line.start->y()}));
    QCOMPARE(firstShape(snapshot, 2).value("lineWidth").toDouble(), 3.0);
    const ProjectManifest decoded = ProjectManifest::decoded(snapshot.manifest.encoded());
    QCOMPARE(decoded.layers[1].shape, snapshot.manifest.layers[1].shape);
    QCOMPARE(decoded.layers[2].shape, snapshot.manifest.layers[2].shape);
    QVERIFY(!decoded.layers[0].shape);
    // Installed, the shapes are live and redraw.
    EditorSession opened;
    opened.installProject(snapshot, QStringLiteral("shapes.comp"));
    QCOMPARE(opened.document().value().layers[2].liveShape().value().style, line);
    opened.selectLayer(opened.document().value().layers[1].id);
    resize(opened, QSizeF(60, 40));
    opened.commitTransform();
    QCOMPARE(opened.activeLayer().value().asset.value().size(), QSize(60, 40));
    // A shape whose image is gone saves as pixels alone.
    ImageLayer stale = session->document().value().layers[2];
    stale.shape.value().image = ImageIdentity{};
    QVERIFY(!stale.liveShape() && !stale.hierarchyRecord().shape);
}

void ShapeLayerTests::theManifestRefusesAWrongShape()
{
    const auto session = shapeSession();
    const ProjectSnapshot snapshot = session->projectSnapshot().value();
    const QJsonObject good = firstShape(snapshot, 1);
    QVERIFY(!ProjectManifest::decoded(withShape(snapshot, QJsonValue::Null)).layers[1].shape);
    QJsonObject nulled = good;
    nulled.insert("lineWidth", QJsonValue::Null);
    QVERIFY(!ProjectManifest::decoded(withShape(snapshot, nulled)).layers[1].shape.value().lineWidth);
    const auto refused = [&](const QJsonValue &shape) {
        try {
            ProjectManifest::decoded(withShape(snapshot, shape));
        } catch (const ProjectError &error) {
            return error.kind == ProjectError::Kind::invalid;
        }
        return false;
    };
    for (const auto &[key, value] : std::vector<std::pair<QString, QJsonValue>>{
             {"kind", "Star"}, {"kind", 1}, {"red", "1"}, {"cornerRadius", true}, {"lineWidth", "4"}, {"start", QJsonArray{1}}, {"end", 2}}) {
        QJsonObject wrong = good;
        wrong.insert(key, value);
        QVERIFY2(refused(wrong), qPrintable(key));
    }
    for (const char *key : {"kind", "red", "green", "blue", "cornerRadius"}) {
        QJsonObject missing = good;
        missing.remove(key);
        QVERIFY2(refused(missing), key);
    }
    QVERIFY(refused(QJsonValue(5)));
}

void ShapeLayerTests::resizersKeepOrDropTheShape()
{
    const auto session = shapeSession();
    const ProjectSnapshot snapshot = session->projectSnapshot().value();
    const ProjectSnapshot canvas = CanvasResizer::resize(snapshot, {.width = 120, .height = 90});
    QCOMPARE(canvas.manifest.layers[1].shape, snapshot.manifest.layers[1].shape);
    const ProjectSnapshot image = ImageResizer::resize(snapshot, {.width = 200, .height = 160, .resolution = 72});
    QVERIFY(!image.manifest.layers[1].shape);
    // Swift's rebuild after a resize leaves every shape out.
    session->applyDocumentSize(canvas, QStringLiteral("Canvas Size"));
    QCOMPARE(session->history.undoName(), QString("Canvas Size"));
    QVERIFY(!session->document().value().layers[1].shape);
}

void ShapeLayerTests::aShapeNeedsItsImage()
{
    const auto session = shapeSession();
    const ImageLayer shape = session->activeLayer().value();
    ImageLayer bare = shape;
    bare.asset = std::nullopt;
    QVERIFY(!bare.liveShape());
    // Layers differ by their shapes too, as Swift compares them.
    ImageLayer plain = shape;
    plain.shape = std::nullopt;
    QVERIFY(!(plain == shape) && shape == session->activeLayer().value());
    // A style on a pixel-less layer loads as no shape.
    const QUuid blank = session->document().value().layers.front().id;
    rewrite(*session, [&](ProjectSnapshot &snapshot) { record(snapshot, blank).shape = shape.shape.value().style; });
    QVERIFY(!layerWith(*session, blank).shape);
    QVERIFY(layerWith(*session, shape.id).liveShape());
    QVERIFY(!LayerShape::loaded(std::nullopt, shape.asset));
}

void ShapeLayerTests::anOlderLineRunsCornerToCorner()
{
    const auto session = shapeSession();
    const QUuid id = session->activeLayerID().value();
    // Older lines kept no ends: redrawn inset corner to corner.
    rewrite(*session, [&](ProjectSnapshot &snapshot) {
        record(snapshot, id).shape = LayerShapeStyle{.kind = ShapeKind::line, .red = 0, .green = 0, .blue = 1, .cornerRadius = 0, .lineWidth = 4};
    });
    resize(*session, QSizeF(40, 40));
    session->commitTransform();
    QCOMPARE(session->activeLayer().value().asset.value().image(), stroked(QSize(40, 40), QPointF(2, 2), QPointF(38, 38), 4));
    // Thicker than its box: the inset stops at the middle.
    rewrite(*session, [&](ProjectSnapshot &snapshot) {
        record(snapshot, id).shape = LayerShapeStyle{.kind = ShapeKind::line, .red = 0, .green = 0, .blue = 1, .cornerRadius = 0, .lineWidth = 6};
    });
    resize(*session, QSizeF(4, 40));
    session->commitTransform();
    QCOMPARE(session->activeLayer().value().asset.value().image(), stroked(QSize(4, 40), QPointF(2, 3), QPointF(2, 37), 6));
}

void ShapeLayerTests::aLineKeepsItsEndsWhenRedrawn()
{
    const auto session = shapeSession();
    session->setShapeKind(ShapeKind::line);
    session->setForegroundColor(PaletteColor{0, 0, 1});
    session->beginShape(QPointF(10, 30));
    session->dragShape(QPointF(50, 10), false, false);
    session->finishShape();
    // Up-right, doubled: the ends scale, the width holds.
    resize(*session, QSizeF(88, 48));
    session->commitTransform();
    QCOMPARE(session->activeLayer().value().asset.value().image(),
             stroked(QSize(88, 48), QPointF(2.0 / 44 * 88, 22.0 / 24 * 48), QPointF(42.0 / 44 * 88, 2.0 / 24 * 48), 4));
}

void ShapeLayerTests::aDistortedShapeStretches()
{
    const auto session = shapeSession();
    session->selectTool(NavigationTool::move);
    session->beginTransform(false);
    session->beginDistort();
    session->previewCorners({QPointF(10, 10), QPointF(40, 10), QPointF(45, 30), QPointF(5, 30)});
    session->commitTransform();
    // Warped in place as Swift's: the shape stays, not live.
    const ImageLayer warped = session->activeLayer().value();
    QVERIFY(warped.shape && !warped.liveShape());
    resize(*session, warped.transform.size * 2);
    session->commitTransform();
    QCOMPARE(session->activeLayer().value().transform.size, warped.transform.size * 2);
    QCOMPARE(session->activeLayer().value().asset.value().identity(), warped.asset.value().identity());
}

QTEST_GUILESS_MAIN(ShapeLayerTests)
#include "ShapeLayerTests.moc"
