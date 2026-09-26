#include "Document/EditorSession.h"
#include "Document/LiveLayerMask.h"
#include <QtTest>

namespace {
ProjectLayerRecord record(const QString &name, std::optional<QUuid> source = std::nullopt)
{
    return {.id = QUuid::createUuid(), .name = name, .isVisible = true,
            .transform = LayerTransform{.origin = {0, 0}, .size = {10, 10}}, .imageFile = name + ".png",
            .maskSourceID = source};
}

bool isInvalid(const std::vector<ProjectLayerRecord> &layers)
{
    try {
        LiveMaskGraph::validate(layers);
    } catch (const ProjectError &error) {
        return error.kind == ProjectError::Kind::invalid;
    }
    return false;
}

// Each record clips to the one before it.
std::vector<ProjectLayerRecord> chain(int count)
{
    std::vector<ProjectLayerRecord> layers;
    for (int index = 0; index < count; ++index)
        layers.push_back(record(QString::number(index), index == 0 ? std::nullopt : std::optional(layers.back().id)));
    return layers;
}
}

class LiveMaskTests : public QObject {
    Q_OBJECT
private slots:
    void sharedBasesAndChainsAreValid();
    void cyclesAndSelfLinksAreRejected();
    void missingSourcesAndDuplicateIdsAreRejected();
    void foldersNeitherClipNorSupplyAMask();
    void chainsStopAtTwoHundredFiftySixLayers();
    void layersCompareAndRecordTheirClippingBase();
};

void LiveMaskTests::sharedBasesAndChainsAreValid()
{
    const ProjectLayerRecord base = record("Base");
    const ProjectLayerRecord first = record("First", base.id), second = record("Second", base.id);
    const ProjectLayerRecord chained = record("Chained", first.id);
    LiveMaskGraph::validate({base, first, second, chained});
    LiveMaskGraph::validate({chained, second, first, base});
    LiveMaskGraph::validate({});
}

void LiveMaskTests::cyclesAndSelfLinksAreRejected()
{
    ProjectLayerRecord target = record("Target"), source = record("Source");
    target.maskSourceID = source.id;
    LiveMaskGraph::validate({target, source});
    source.maskSourceID = target.id;
    QVERIFY(isInvalid({target, source}));
    ProjectLayerRecord loop = record("Loop");
    loop.maskSourceID = loop.id;
    QVERIFY(isInvalid({loop}));
    ProjectLayerRecord a = record("A"), b = record("B"), c = record("C");
    a.maskSourceID = b.id;
    b.maskSourceID = c.id;
    c.maskSourceID = a.id;
    QVERIFY(isInvalid({a, b, c}));
}

void LiveMaskTests::missingSourcesAndDuplicateIdsAreRejected()
{
    QVERIFY(isInvalid({record("Orphan", QUuid::createUuid())}));
    const ProjectLayerRecord layer = record("Layer");
    QVERIFY(isInvalid({layer, layer}));
}

void LiveMaskTests::foldersNeitherClipNorSupplyAMask()
{
    ProjectLayerRecord base = record("Base");
    ProjectLayerRecord clipped = record("Clipped", base.id);
    LiveMaskGraph::validate({base, clipped});
    clipped.isGroup = false;
    base.isGroup = false;
    LiveMaskGraph::validate({base, clipped});
    clipped.isGroup = true;
    QVERIFY(isInvalid({base, clipped}));
    clipped.isGroup = std::nullopt;
    base.isGroup = true;
    QVERIFY(isInvalid({base, clipped}));
}

void LiveMaskTests::chainsStopAtTwoHundredFiftySixLayers()
{
    LiveMaskGraph::validate(chain(256));
    QVERIFY(isInvalid(chain(257)));
}

void LiveMaskTests::layersCompareAndRecordTheirClippingBase()
{
    ImageLayer layer("Layer 1", QSizeF(10, 10));
    QCOMPARE(layer.hierarchyRecord().maskSourceID, std::nullopt);
    ImageLayer clipped = layer;
    clipped.maskSourceID = QUuid::createUuid();
    QVERIFY(!(clipped == layer));
    QCOMPARE(clipped, ImageLayer(clipped));
    QCOMPARE(clipped.hierarchyRecord().maskSourceID, clipped.maskSourceID);
    ImageLayer rebased = clipped;
    rebased.maskSourceID = QUuid::createUuid();
    QVERIFY(!(rebased == clipped));
    QCOMPARE(rebased.hierarchyRecord().maskSourceID, rebased.maskSourceID);
}

QTEST_APPLESS_MAIN(LiveMaskTests)
#include "LiveMaskTests.moc"
