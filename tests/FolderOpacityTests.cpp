#include "Document/LiveLayerMask.h"
#include "Rendering/EditorCanvas.h"
#include "IO/ImageExporter.h"
#include "RenderFixtures.h"
#include "SessionFixtures.h"
#include <QTemporaryDir>
#include <QtTest>

// A folder's opacity dims each layer inside it (Swift 1.1.6).
namespace {
ImportedImage filled(int width, int height, QRgb premultiplied, const QString &name)
{
    const QImage image = solid(width, height, premultiplied);
    return ImportedImage(image, image, name);
}

// The layer wrapped in a folder, the folder left active.
QUuid wrap(EditorSession &session, QUuid id)
{
    session.selectLayers({id}, id);
    session.groupSelectedLayers();
    return session.activeLayerID().value();
}

void dim(EditorSession &session, QUuid id, double opacity)
{
    session.selectLayer(id);
    session.setLayerOpacity(opacity);
}

QImage exported(const EditorSession &session)
{
    return ImageExporter::render(session.projectSnapshot().value()).image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
}

int alpha(const QImage &image, int x, int y)
{
    return image.constScanLine(y)[x * 4 + 3];
}

std::optional<std::pair<double, std::optional<QUuid>>> none(QUuid)
{
    return std::nullopt;
}
}

class FolderOpacityTests : public QObject {
    Q_OBJECT
private slots:
    void opacitiesMultiplyUpTheTree();
    void theExportAndTheCompositeDimInsideAFolder();
    void anAdjustmentDimsWithItsFolder();
    void effectsDimWithTheirFolder();
    void theCanvasShowsAndFollowsAFoldersOpacity();
    void theCanvasDimsEveryWayALayerDraws_data();
    void theCanvasDimsEveryWayALayerDraws();
    void aClippingSourceBakesDimmed();
    void aDimmedFolderSavesAndReopens();
};

void FolderOpacityTests::opacitiesMultiplyUpTheTree()
{
    const QUuid inner = QUuid::createUuid(), outer = QUuid::createUuid();
    const auto tree = [&](QUuid id) -> std::optional<std::pair<double, std::optional<QUuid>>> {
        if (id == inner)
            return std::pair(0.5, std::optional(outer));
        if (id == outer)
            return std::pair(0.8, std::optional<QUuid>());
        return std::nullopt;
    };
    QCOMPARE(LayerOpacity::effective(0.5, inner, tree), 0.2);
    QCOMPARE(LayerOpacity::effective(0.5, std::nullopt, tree), 0.5);
    // A parent nobody knows ends the walk.
    QCOMPARE(LayerOpacity::effective(0.5, QUuid::createUuid(), none), 0.5);
    // A loop stops after 64 folders, as Swift's does.
    const QUuid loop = QUuid::createUuid();
    const auto ring = [&](QUuid) -> std::optional<std::pair<double, std::optional<QUuid>>> { return std::pair(0.9, std::optional(loop)); };
    QCOMPARE(LayerOpacity::effective(1, loop, ring), std::pow(0.9, 64));
    // A document that repeats an id is a bug.
    CanvasDocument twice(4, 4);
    twice.layers = {ImageLayer(QStringLiteral("One"), QSizeF(4, 4))};
    twice.layers.push_back(twice.layers.front());
    QVERIFY_THROWS_EXCEPTION(std::logic_error, twice.effectiveOpacities());
    // The document's map and each record's agree.
    EditorSession session;
    session.createDocument(4, 4);
    session.insert(filled(4, 4, qRgba(255, 0, 0, 255), "Red"));
    const QUuid red = session.activeLayerID().value();
    dim(session, red, 0.5);
    const QUuid folder = wrap(session, red);
    const QUuid top = wrap(session, folder);
    dim(session, folder, 0.5);
    dim(session, top, 0.8);
    const QHash<QUuid, double> opacities = session.document().value().effectiveOpacities();
    QCOMPARE(opacities.value(red), 0.2);
    QCOMPARE(opacities.value(folder), 0.4);
    QCOMPARE(opacities.value(top), 0.8);
    std::map<QUuid, ProjectLayerRecord> records;
    const ProjectSnapshot snapshot = session.projectSnapshot().value();
    for (const ProjectLayerRecord &record : snapshot.manifest.layers)
        records.emplace(record.id, record);
    QCOMPARE(records.at(red).effectiveOpacity(records), 0.2);
    // The layer itself still reads its own opacity.
    QCOMPARE(layerWith(session, red).opacity, 0.5);
}

void FolderOpacityTests::theExportAndTheCompositeDimInsideAFolder()
{
    EditorSession session;
    session.createDocument(4, 4);
    session.insert(filled(4, 4, qRgba(255, 0, 0, 255), "Red"));
    const QUuid red = session.activeLayerID().value();
    const QUuid folder = wrap(session, red);
    QCOMPARE(alpha(exported(session), 1, 1), 255);
    dim(session, folder, 0.5);
    QCOMPARE(alpha(exported(session), 1, 1), 127);
    // Nested, each folder multiplies in.
    const QUuid top = wrap(session, folder);
    dim(session, top, 0.5);
    QCOMPARE(alpha(exported(session), 1, 1), 63);
    QImage composite = BrushRaster::context(4, 4, false);
    QPainter painter(&composite);
    session.drawLiveComposite(session.document().value(), painter);
    painter.end();
    QCOMPARE(alpha(composite, 1, 1), 63);
}

void FolderOpacityTests::anAdjustmentDimsWithItsFolder()
{
    // Half an adjustment equals a whole one, half folded.
    const auto scene = [](bool folded) {
        auto session = std::make_unique<EditorSession>();
        session->createDocument(4, 4);
        session->insert(filled(4, 4, qRgba(255, 0, 0, 255), "Red"));
        session->addAdjustment(AdjustmentKind::gradientMap);
        session->setAdjustmentEditingID(std::nullopt);
        const QUuid adjustment = session->activeLayerID().value();
        if (folded)
            dim(*session, wrap(*session, adjustment), 0.5);
        else
            dim(*session, adjustment, 0.5);
        QImage composite = BrushRaster::context(4, 4, false);
        QPainter painter(&composite);
        session->drawLiveComposite(session->document().value(), painter);
        painter.end();
        return std::pair(exported(*session), composite);
    };
    const auto half = scene(false), folded = scene(true);
    QCOMPARE(folded.first, half.first);
    QCOMPARE(folded.second, half.second);
    QVERIFY(half.first.pixelColor(1, 1) != QColor(Qt::red) && half.second.pixelColor(1, 1) != QColor(Qt::red));
}

void FolderOpacityTests::effectsDimWithTheirFolder()
{
    // A stroked layer: half alone, or whole, half folded.
    const auto scene = [](bool folded) {
        auto session = std::make_unique<EditorSession>();
        session->createDocument(20, 20);
        session->insert(filled(10, 10, qRgba(255, 0, 0, 255), "Red"));
        const QUuid red = session->activeLayerID().value();
        LayerEffects effects;
        effects.stroke = StrokeEffect();
        session->setEffects(effects, red);
        if (folded)
            dim(*session, wrap(*session, red), 0.5);
        else
            dim(*session, red, 0.5);
        QImage composite = BrushRaster::context(20, 20, false);
        QPainter painter(&composite);
        session->drawLiveComposite(session->document().value(), painter);
        painter.end();
        return std::pair(exported(*session), composite);
    };
    const auto half = scene(false), folded = scene(true);
    QCOMPARE(folded.first, half.first);
    QCOMPARE(folded.second, half.second);
    QVERIFY(alpha(half.first, 10, 10) == 127 && alpha(half.first, 4, 10) > 0);
}

void FolderOpacityTests::theCanvasShowsAndFollowsAFoldersOpacity()
{
    EditorSession session;
    CanvasView canvas(session);
    session.createDocument(20, 20);
    session.setShowsTransformControls(false);
    canvas.resize(40, 40);
    session.viewport.resize(QSizeF(40, 40), 1, QSizeF(20, 20));
    session.zoom(1);
    // White below, so canvas and export both show opaque.
    session.insert(filled(20, 20, qRgba(255, 255, 255, 255), "White"));
    session.insert(filled(20, 20, qRgba(255, 0, 0, 255), "Red"));
    const QUuid folder = wrap(session, session.activeLayerID().value());
    canvas.synchronizeDisplay();
    const QPoint middle = session.viewport.viewPoint(QPointF(10, 10), QSizeF(20, 20)).toPoint();
    QCOMPARE(canvas.grab().toImage().pixelColor(middle), QColor(Qt::red));
    // A folder holds no pixels, yet its opacity redraws.
    dim(session, folder, 0.5);
    QVERIFY(canvas.synchronizeDisplay());
    const QColor shown = canvas.grab().toImage().pixelColor(middle);
    const QColor expected = exported(session).pixelColor(10, 10);
    QVERIFY2(std::abs(shown.green() - expected.green()) <= 1 && std::abs(expected.green() - 128) <= 1 && shown.red() == 255,
             qPrintable(shown.name() + expected.name()));
}

void FolderOpacityTests::aClippingSourceBakesDimmed()
{
    EditorSession session;
    session.createDocument(4, 4);
    session.insert(filled(4, 4, qRgba(255, 0, 0, 255), "Base"));
    const QUuid base = session.activeLayerID().value();
    session.insert(filled(4, 4, qRgba(0, 255, 0, 255), "Clipped"));
    const QUuid clipped = session.activeLayerID().value();
    session.toggleClippingMask(clipped);
    session.selectLayers({base, clipped}, clipped);
    session.groupSelectedLayers();
    const QUuid folder = session.activeLayerID().value();
    const QImage whole = LiveMaskBaker::bake(session.projectSnapshot().value(), clipped).value().image();
    QCOMPARE(qAlpha(whole.pixel(1, 1)), 255);
    dim(session, folder, 0.5);
    const QImage dimmed = LiveMaskBaker::bake(session.projectSnapshot().value(), clipped).value().image();
    QCOMPARE(qAlpha(dimmed.pixel(1, 1)), 127);
    // A folder around the folder dims the source again.
    dim(session, wrap(session, folder), 0.5);
    const QImage twice = LiveMaskBaker::bake(session.projectSnapshot().value(), clipped).value().image();
    QCOMPARE(qAlpha(twice.pixel(1, 1)), 63);
}

void FolderOpacityTests::aDimmedFolderSavesAndReopens()
{
    QTemporaryDir root;
    EditorSession session;
    session.createDocument(4, 4);
    session.insert(filled(4, 4, qRgba(255, 0, 0, 255), "Red"));
    const QUuid folder = wrap(session, session.activeLayerID().value());
    dim(session, folder, 0.5);
    QCOMPARE(layerWith(session, folder).opacity, 0.5);
    const QString path = root.filePath("Dimmed.comp");
    ProjectStore::save(session.projectSnapshot().value(), path);
    const ProjectSnapshot loaded = ProjectStore::load(path);
    QCOMPARE(loaded.manifest.version, qint64(8));
    for (const ProjectLayerRecord &record : loaded.manifest.layers) {
        if (record.isGroup == true)
            QCOMPARE(record.opacity, std::optional(0.5));
    }
}

void FolderOpacityTests::theCanvasDimsEveryWayALayerDraws_data()
{
    QTest::addColumn<QString>("way");
    QTest::newRow("adjustment") << "adjustment";
    QTest::newRow("effects") << "effects";
    QTest::newRow("warp") << "warp";
}

void FolderOpacityTests::theCanvasDimsEveryWayALayerDraws()
{
    QFETCH(QString, way);
    // The canvas, half alone against whole in a half folder.
    const auto scene = [&way](bool folded) {
        EditorSession session;
        CanvasView canvas(session);
        session.createDocument(20, 20);
        session.setShowsTransformControls(false);
        canvas.resize(40, 40);
        session.viewport.resize(QSizeF(40, 40), 1, QSizeF(20, 20));
        session.zoom(1);
        session.insert(filled(20, 20, qRgba(255, 255, 255, 255), "White"));
        session.insert(filled(10, 10, qRgba(255, 0, 0, 255), "Red"));
        QUuid target = session.activeLayerID().value();
        if (way == "adjustment") {
            session.addAdjustment(AdjustmentKind::gradientMap);
            session.setAdjustmentEditingID(std::nullopt);
            target = session.activeLayerID().value();
        } else if (way == "effects") {
            LayerEffects effects;
            effects.stroke = StrokeEffect();
            session.setEffects(effects, target);
        }
        dim(session, folded ? wrap(session, target) : target, 0.5);
        if (way == "warp") {
            session.selectLayer(target);
            session.selectTool(NavigationTool::blur);
            session.beginBrush(QPointF(10, 10));
            if (!session.warpStroke())
                throw std::runtime_error("no warp began");
        }
        canvas.synchronizeDisplay();
        if (way == "effects" && !QTest::qWaitFor([&] { canvas.grab(); return session.effectsPreviews.rendered(target).has_value(); }))
            throw std::runtime_error("the effects never rendered");
        return canvas.grab().toImage();
    };
    const QImage half = scene(false), folded = scene(true);
    QCOMPARE(folded, half);
    const QPoint inside(20, 20);
    QVERIFY2(half.pixelColor(inside) != QColor(Qt::red) && half.pixelColor(inside) != QColor(Qt::white), qPrintable(half.pixelColor(inside).name()));
}

QTEST_MAIN(FolderOpacityTests)
#include "FolderOpacityTests.moc"
