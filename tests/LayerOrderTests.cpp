#include "Document/EditorSession.h"
#include <QRandomGenerator>
#include <QtTest>

// Swift's LayerOrder: the hierarchy kept until what shapes it changes.
namespace {
// Nested folders, some hidden, built from a fixed seed.
std::vector<ImageLayer> tree(int count)
{
    QRandomGenerator random(11);
    std::vector<ImageLayer> layers;
    std::vector<QUuid> folders;
    for (int index = 0; index < count; ++index) {
        ImageLayer layer(QString::number(index), QSizeF(10, 10));
        layer.isGroup = random.bounded(3) == 0;
        layer.isVisible = random.bounded(4) != 0;
        if (!folders.empty() && random.bounded(3) != 0)
            layer.parentID = folders[size_t(random.bounded(int(folders.size())))];
        if (layer.isGroup)
            folders.push_back(layer.id);
        layers.push_back(layer);
    }
    return layers;
}

std::vector<ProjectLayerRecord> records(const std::vector<ImageLayer> &layers)
{
    std::vector<ProjectLayerRecord> result;
    for (const ImageLayer &layer : layers)
        result.push_back(layer.hierarchyRecord());
    return result;
}
}

class LayerOrderTests : public QObject {
    Q_OBJECT
private slots:
    void orderVisibilityAndDrawingFollowTheHierarchy();
    void eachThingThatShapesItIsSeen();
    void sixtyFourFoldersDeepAtMost();
    void nothingShownDrawsNothing();
    void opacitiesMultiplyUpKnownFoldersAlone();
};

void LayerOrderTests::orderVisibilityAndDrawingFollowTheHierarchy()
{
    const std::vector<ImageLayer> layers = tree(60);
    const LayerOrder::Result result = LayerOrder::resolve(layers);
    std::vector<QUuid> order, drawn;
    QSet<QUuid> visible;
    for (const LayerHierarchy::Entry &entry : LayerHierarchy::entries(records(layers))) {
        order.push_back(entry.layer.id);
        if (entry.visible)
            visible.insert(entry.layer.id);
    }
    for (const ProjectLayerRecord &record : LayerHierarchy::visibleLayers(records(layers)))
        drawn.push_back(record.id);
    QCOMPARE(result.order, order);
    QCOMPARE(result.visible, visible);
    QCOMPARE(result.drawn, drawn);
    // Hidden folders and hidden layers both play a part.
    QVERIFY(visible.size() > 5 && int(visible.size()) < 50 && drawn.size() > 5 && qsizetype(drawn.size()) < visible.size());
}

void LayerOrderTests::eachThingThatShapesItIsSeen()
{
    CanvasDocument document(10, 10);
    ImageLayer folder(QStringLiteral("Folder"), document.size());
    folder.isGroup = true;
    ImageLayer inside(QStringLiteral("Inside"), document.size());
    inside.parentID = folder.id;
    ImageLayer top(QStringLiteral("Top"), document.size());
    document.layers = {folder, inside, top};
    QCOMPARE(document.hierarchy().drawn, (std::vector<QUuid>{inside.id, top.id}));
    // A hidden folder hides what it holds.
    document.layers[0].isVisible = false;
    QCOMPARE(document.hierarchy().drawn, std::vector<QUuid>{top.id});
    QCOMPARE(document.effectiveVisibleIDs(), QSet<QUuid>{top.id});
    document.layers[0].isVisible = true;
    // Out of the folder: the order changes.
    document.layers[1].parentID = std::nullopt;
    QCOMPARE(document.hierarchy().order, (std::vector<QUuid>{folder.id, inside.id, top.id}));
    document.layers[1].parentID = top.id;
    document.layers[2].isGroup = true;
    QCOMPARE(document.hierarchy().order, (std::vector<QUuid>{folder.id, top.id, inside.id}));
    QCOMPARE(document.hierarchy().drawn, std::vector<QUuid>{inside.id});
    // A different layer of the same shape: its own id.
    document.layers[2].id = QUuid::createUuid();
    document.layers[1].parentID = document.layers[2].id;
    QCOMPARE(document.hierarchy().order, (std::vector<QUuid>{folder.id, document.layers[2].id, inside.id}));
}

void LayerOrderTests::sixtyFourFoldersDeepAtMost()
{
    std::vector<ImageLayer> layers;
    for (int index = 0; index < 70; ++index) {
        ImageLayer layer(QString::number(index), QSizeF(10, 10));
        layer.isGroup = index < 69;
        if (index > 0)
            layer.parentID = layers.back().id;
        layers.push_back(layer);
    }
    const LayerOrder::Result result = LayerOrder::resolve(layers);
    QCOMPARE(int(result.order.size()), 65);
    QVERIFY(result.drawn.empty());
}

void LayerOrderTests::nothingShownDrawsNothing()
{
    CanvasDocument document(10, 10);
    ImageLayer hidden(QStringLiteral("Hidden"), document.size());
    hidden.isVisible = false;
    // Two of one id: drawn, that throws; hidden, nothing draws.
    document.layers = {hidden, hidden};
    QVERIFY(document.renderLayers().empty());
    document.layers[1].isVisible = true;
    QVERIFY_THROWS_EXCEPTION(std::logic_error, document.renderLayers());
}

void LayerOrderTests::opacitiesMultiplyUpKnownFoldersAlone()
{
    CanvasDocument document(10, 10);
    ImageLayer folder(QStringLiteral("Folder"), document.size());
    folder.isGroup = true;
    folder.opacity = 0.5;
    ImageLayer inside(QStringLiteral("Inside"), document.size());
    inside.parentID = folder.id;
    inside.opacity = 0.8;
    // A folder nobody holds: the walk stops at its own.
    ImageLayer stray(QStringLiteral("Stray"), document.size());
    stray.parentID = QUuid::createUuid();
    stray.opacity = 0.6;
    document.layers = {folder, inside, stray};
    const QHash<QUuid, double> opacities = document.effectiveOpacities();
    QCOMPARE(opacities.value(folder.id), 0.5);
    QCOMPARE(opacities.value(inside.id), 0.4);
    QCOMPARE(opacities.value(stray.id), 0.6);
    document.layers.push_back(inside);
    QVERIFY_THROWS_EXCEPTION(std::logic_error, document.effectiveOpacities());
}

QTEST_GUILESS_MAIN(LayerOrderTests)
#include "LayerOrderTests.moc"
