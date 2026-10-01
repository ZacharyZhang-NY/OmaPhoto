#include "Document/DocumentLimits.h"
#include "Document/EditorSession.h"
#include <QtTest>

namespace {
const LayerTransform transform{.origin = {0, 0}, .size = {10, 10}};

ProjectLayerRecord record(const QString &name, std::optional<QUuid> parent = std::nullopt, bool isGroup = false,
                          bool isVisible = true)
{
    return {.id = QUuid::createUuid(), .name = name, .isVisible = isVisible, .transform = transform,
            .imageFile = isGroup ? std::nullopt : std::optional<QString>(name + ".png"),
            .parentID = parent, .isGroup = isGroup, .opacity = std::nullopt, .blendMode = std::nullopt};
}

QStringList names(const std::vector<LayerHierarchy::Entry> &entries)
{
    QStringList result;
    for (const LayerHierarchy::Entry &entry : entries)
        result << QString("%1:%2:%3").arg(entry.layer.name).arg(entry.depth).arg(entry.visible ? "shown" : "hidden");
    return result;
}

bool isInvalid(const std::vector<ProjectLayerRecord> &layers)
{
    try {
        LayerHierarchy::validate(layers);
    } catch (const ProjectError &error) {
        return error.kind == ProjectError::Kind::invalid;
    }
    return false;
}

// A chain of nested folders, outermost first.
std::vector<ProjectLayerRecord> chain(int count)
{
    std::vector<ProjectLayerRecord> layers;
    for (int index = 0; index < count; ++index)
        layers.push_back(record(QString::number(index), index == 0 ? std::nullopt : std::optional(layers.back().id), true));
    return layers;
}
}

class GroupTests : public QObject {
    Q_OBJECT
private slots:
    void malformedParentLinksAndCyclesAreRejected();
    void duplicateIdsAndFoldersWithPixelsAreRejected();
    void nestingStopsAtSixtyFourLevels();
    void entriesWalkFoldersBottomToTop();
    void topFirstReversesEveryLevel();
    void collapsedFoldersHideTheirContents();
    void hiddenParentOverridesChildren();
    void anAbsentFolderFlagMeansALeaf();
    void entriesStopBelowSixtyFourLevels();
    void documentHierarchyFollowsItsLayers();
    void hierarchyRecordNamesFilesLikeTheMacApp();
    void projectErrorsCarryTheirMessages();
};

void GroupTests::malformedParentLinksAndCyclesAreRejected()
{
    ProjectLayerRecord group = record("Group", std::nullopt, true);
    ProjectLayerRecord nested = record("Nested", group.id, true);
    LayerHierarchy::validate({group, nested});
    group.parentID = nested.id;
    QVERIFY(isInvalid({group, nested}));
    QVERIFY(isInvalid({group}));
    ProjectLayerRecord rasterParent = group;
    rasterParent.parentID = std::nullopt;
    rasterParent.isGroup = false;
    QVERIFY(isInvalid({rasterParent, nested}));
    rasterParent.isGroup = std::nullopt;
    QVERIFY(isInvalid({rasterParent, nested}));
    ProjectLayerRecord selfParent = record("Loop", std::nullopt, true);
    selfParent.parentID = selfParent.id;
    QVERIFY(isInvalid({selfParent}));
}

void GroupTests::duplicateIdsAndFoldersWithPixelsAreRejected()
{
    const ProjectLayerRecord layer = record("Layer");
    LayerHierarchy::validate({layer, record("Other")});
    QVERIFY(isInvalid({layer, layer}));
    ProjectLayerRecord folder = record("Folder", std::nullopt, true);
    LayerHierarchy::validate({folder});
    folder.imageFile = "Folder.png";
    QVERIFY(isInvalid({folder}));
}

void GroupTests::nestingStopsAtSixtyFourLevels()
{
    const std::vector<ProjectLayerRecord> folders = chain(64);
    LayerHierarchy::validate(folders);
    std::vector<ProjectLayerRecord> withLeaf = folders;
    withLeaf.push_back(record("Leaf", folders[63].id));
    LayerHierarchy::validate(withLeaf);
    std::vector<ProjectLayerRecord> withFolder = folders;
    withFolder.push_back(record("Folder", folders[63].id, true));
    QVERIFY(isInvalid(withFolder));
}

void GroupTests::entriesWalkFoldersBottomToTop()
{
    const ProjectLayerRecord bottom = record("Bottom");
    const ProjectLayerRecord folder = record("Folder", std::nullopt, true);
    const ProjectLayerRecord inner = record("Inner", folder.id, true);
    const ProjectLayerRecord deep = record("Deep", inner.id);
    const ProjectLayerRecord child = record("Child", folder.id);
    const ProjectLayerRecord top = record("Top");
    const auto entries = LayerHierarchy::entries({bottom, deep, folder, child, inner, top});
    QCOMPARE(names(entries), QStringList({"Bottom:0:shown", "Folder:0:shown", "Child:1:shown", "Inner:1:shown",
                                          "Deep:2:shown", "Top:0:shown"}));
    QCOMPARE(entries[4].layer.id, deep.id);
    const ProjectLayerRecord raster = record("Raster");
    QCOMPARE(names(LayerHierarchy::entries({raster, record("Stray", raster.id)})), QStringList({"Raster:0:shown"}));
}

void GroupTests::topFirstReversesEveryLevel()
{
    const ProjectLayerRecord bottom = record("Bottom");
    const ProjectLayerRecord folder = record("Folder", std::nullopt, true);
    const ProjectLayerRecord low = record("Low", folder.id);
    const ProjectLayerRecord high = record("High", folder.id);
    const ProjectLayerRecord top = record("Top");
    QCOMPARE(names(LayerHierarchy::entries({bottom, folder, low, high, top}, true)),
             QStringList({"Top:0:shown", "Folder:0:shown", "High:1:shown", "Low:1:shown", "Bottom:0:shown"}));
}

void GroupTests::collapsedFoldersHideTheirContents()
{
    const ProjectLayerRecord folder = record("Folder", std::nullopt, true);
    const ProjectLayerRecord child = record("Child", folder.id);
    const ProjectLayerRecord other = record("Other", std::nullopt, true);
    const ProjectLayerRecord kept = record("Kept", other.id);
    QCOMPARE(names(LayerHierarchy::entries({folder, child, other, kept}, false, {folder.id})),
             QStringList({"Folder:0:shown", "Other:0:shown", "Kept:1:shown"}));

    const ProjectLayerRecord outer = record("Outer", std::nullopt, true);
    const ProjectLayerRecord inner = record("Inner", outer.id, true);
    const ProjectLayerRecord deep = record("Deep", inner.id);
    const ProjectLayerRecord sibling = record("Sibling", outer.id);
    const ProjectLayerRecord empty = record("Empty", std::nullopt, true);
    QCOMPARE(names(LayerHierarchy::entries({outer, inner, deep, sibling, empty}, false, {inner.id})),
             QStringList({"Outer:0:shown", "Inner:1:shown", "Sibling:1:shown", "Empty:0:shown"}));
    QCOMPARE(names(LayerHierarchy::entries({outer, inner, deep, sibling, empty})),
             QStringList({"Outer:0:shown", "Inner:1:shown", "Deep:2:shown", "Sibling:1:shown", "Empty:0:shown"}));
}

void GroupTests::hiddenParentOverridesChildren()
{
    const ProjectLayerRecord folder = record("Folder", std::nullopt, true, false);
    const ProjectLayerRecord inner = record("Inner", folder.id, true);
    const ProjectLayerRecord deep = record("Deep", inner.id);
    const ProjectLayerRecord hidden = record("Hidden", std::nullopt, false, false);
    const ProjectLayerRecord shown = record("Shown");
    const std::vector<ProjectLayerRecord> layers{folder, inner, deep, hidden, shown};
    const auto entries = LayerHierarchy::entries(layers);
    QCOMPARE(names(entries), QStringList({"Folder:0:hidden", "Inner:1:hidden", "Deep:2:hidden", "Hidden:0:hidden",
                                          "Shown:0:shown"}));
    QVERIFY(entries[2].layer.isVisible);
    const auto visible = LayerHierarchy::visibleLayers(layers);
    QCOMPARE(int(visible.size()), 1);
    QCOMPARE(visible[0].id, shown.id);
    const ProjectLayerRecord low = record("Low");
    const ProjectLayerRecord openFolder = record("Open", std::nullopt, true);
    const ProjectLayerRecord inside = record("Inside", openFolder.id);
    const ProjectLayerRecord high = record("High");
    const auto ordered = LayerHierarchy::visibleLayers({low, openFolder, inside, high});
    QCOMPARE(int(ordered.size()), 3);
    QCOMPARE(ordered[0].id, low.id);
    QCOMPARE(ordered[1].id, inside.id);
    QCOMPARE(ordered[2].id, high.id);
}

void GroupTests::anAbsentFolderFlagMeansALeaf()
{
    ProjectLayerRecord legacy = record("Legacy");
    legacy.isGroup = std::nullopt;
    const ProjectLayerRecord stray = record("Stray", legacy.id);
    QCOMPARE(names(LayerHierarchy::entries({legacy, stray})), QStringList({"Legacy:0:shown"}));
    const auto visible = LayerHierarchy::visibleLayers({legacy});
    QCOMPARE(int(visible.size()), 1);
    QCOMPARE(visible[0].id, legacy.id);

    QVERIFY(legacy.imageFile.has_value());
    LayerHierarchy::validate({legacy});
    std::vector<ProjectLayerRecord> deepest = chain(64);
    ProjectLayerRecord leaf = record("Leaf", deepest[63].id);
    leaf.isGroup = std::nullopt;
    deepest.push_back(leaf);
    LayerHierarchy::validate(deepest);
}

void GroupTests::entriesStopBelowSixtyFourLevels()
{
    std::vector<ProjectLayerRecord> layers = chain(70);
    const auto entries = LayerHierarchy::entries(layers);
    QCOMPARE(int(entries.size()), 65);
    QCOMPARE(entries.back().depth, 64);
}

void GroupTests::documentHierarchyFollowsItsLayers()
{
    CanvasDocument document(100, 100);
    ImageLayer folder("Folder", document.size());
    folder.isGroup = true;
    folder.isVisible = false;
    ImageLayer child(ImportedImage(QImage(4, 4, QImage::Format_RGBA8888_Premultiplied), QImage(), "Child"), QPointF(0, 0));
    child.parentID = folder.id;
    ImageLayer open("Open", document.size());
    open.isGroup = true;
    ImageLayer shown("Shown", document.size());
    shown.parentID = open.id;
    ImageLayer top("Top", document.size());
    document.layers = {folder, child, open, shown, top};
    QCOMPARE(document.hierarchy().order, (std::vector<QUuid>{folder.id, child.id, open.id, shown.id, top.id}));
    QCOMPARE(document.hierarchy().drawn, (std::vector<QUuid>{shown.id, top.id}));
    QCOMPARE(document.effectiveVisibleIDs(), QSet<QUuid>({open.id, shown.id, top.id}));
    const std::vector<ImageLayer> rendered = document.renderLayers();
    QCOMPARE(int(rendered.size()), 2);
    QCOMPARE(rendered[0], shown);
    QCOMPARE(rendered[1], top);

    ImageLayer twin("Twin", document.size());
    twin.id = top.id;
    document.layers.push_back(twin);
    QVERIFY_THROWS_EXCEPTION(std::logic_error, document.renderLayers());
}

void GroupTests::hierarchyRecordNamesFilesLikeTheMacApp()
{
    ImageLayer layer(ImportedImage(QImage(4, 4, QImage::Format_RGBA8888_Premultiplied), QImage(), "Photo"), QPointF(3, 4));
    layer.id = QUuid("e621e1f8-c36c-495a-93fc-0c247a3e6e5f");
    layer.parentID = QUuid::createUuid();
    layer.isVisible = false;
    layer.opacity = 0.25;
    layer.blendMode = LayerBlendMode::screen;
    const ProjectLayerRecord record = layer.hierarchyRecord();
    QCOMPARE(record.id, layer.id);
    QCOMPARE(record.name, QString("Photo"));
    QCOMPARE(record.isVisible, false);
    QCOMPARE(record.transform, layer.transform);
    QCOMPARE(record.imageFile, std::optional<QString>("E621E1F8-C36C-495A-93FC-0C247A3E6E5F.png"));
    QCOMPARE(record.parentID, layer.parentID);
    QCOMPARE(record.isGroup, std::optional<bool>(false));
    QCOMPARE(record.opacity, std::optional<double>(0.25));
    QCOMPARE(record.blendMode, std::optional<LayerBlendMode>(LayerBlendMode::screen));
    ImageLayer folder("Folder", QSizeF(10, 10));
    folder.isGroup = true;
    QCOMPARE(folder.hierarchyRecord().imageFile, std::nullopt);
    QCOMPARE(folder.hierarchyRecord().isGroup, std::optional<bool>(true));
}

void GroupTests::projectErrorsCarryTheirMessages()
{
    const ProjectError invalid(ProjectError::Kind::invalid);
    QCOMPARE(invalid.kind, ProjectError::Kind::invalid);
    QCOMPARE(invalid.version, std::nullopt);
    QCOMPARE(QString(invalid.what()), QString("This is not a valid OmaPhoto project, or its metadata is damaged."));

    const ProjectError version = ProjectError::unsupportedVersion(12);
    QCOMPARE(version.kind, ProjectError::Kind::version);
    QCOMPARE(version.version, std::optional<int>(12));
    QCOMPARE(QString(version.what()), QString("This project uses format version 12. This app supports versions 1–11."));
    QVERIFY_THROWS_EXCEPTION(std::logic_error, ProjectError(ProjectError::Kind::version));

    const ProjectError missingImage(ProjectError::Kind::missingImage);
    QCOMPARE(missingImage.kind, ProjectError::Kind::missingImage);
    QCOMPARE(QString(missingImage.what()),
             QString("An image inside the project is missing or damaged. The current document has not been replaced."));

    const ProjectError tooLarge(ProjectError::Kind::tooLarge);
    QCOMPARE(tooLarge.kind, ProjectError::Kind::tooLarge);
    QCOMPARE(QString(tooLarge.what()),
             QStringLiteral("This project exceeds the supported canvas, layer, file-size, or %1-megapixel document limit.").arg(DocumentLimits::documentBudgetMegapixels()));

    const ProjectError encode(ProjectError::Kind::encode);
    QCOMPARE(encode.kind, ProjectError::Kind::encode);
    QCOMPARE(QString(encode.what()), QString("An image could not be saved. The previous project has not been replaced."));
}

QTEST_APPLESS_MAIN(GroupTests)
#include "GroupTests.moc"
