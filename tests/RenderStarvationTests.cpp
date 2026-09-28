#include "Document/BrushStroke.h"
#include "Document/LayerMask.h"
#include "Document/LiveLayerMask.h"
#include "IO/ProjectStore.h"
#include "Rendering/LiveMaskRenderer.h"
#include <QtTest>
#include <cstdlib>
#include <new>

// A render's own containers may fail to allocate; none terminates.
namespace {
// This thread's allocations left before one fails; below zero, none.
thread_local long allowed = -1;
}

void *operator new(std::size_t size)
{
    if (allowed == 0)
        throw std::bad_alloc();
    if (allowed > 0)
        --allowed;
    if (void *memory = std::malloc(size))
        return memory;
    throw std::bad_alloc();
}

void *operator new[](std::size_t size)
{
    return operator new(size);
}

// New is malloc, so free pairs; GCC 16 cannot tell.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
void operator delete(void *memory) noexcept
{
    std::free(memory);
}

void operator delete(void *memory, std::size_t) noexcept
{
    std::free(memory);
}

void operator delete[](void *memory) noexcept
{
    std::free(memory);
}

void operator delete[](void *memory, std::size_t) noexcept
{
    std::free(memory);
}
#pragma GCC diagnostic pop

namespace {
// Fails each allocation in turn; returns the call's count.
long starved(const std::function<void()> &call)
{
    for (long count = 0; count < 100000; ++count) {
        allowed = count;
        try {
            call();
            allowed = -1;
            return count;
        } catch (const std::bad_alloc &) {
            allowed = -1;
        }
    }
    throw std::logic_error("the call never finished");
}

// Layers in two folders, every third clipped below.
std::vector<ProjectLayerRecord> layers(int count)
{
    std::vector<ProjectLayerRecord> result;
    const QUuid folders[2]{QUuid::createUuid(), QUuid::createUuid()};
    for (const QUuid &folder : folders)
        result.push_back({.id = folder, .name = QStringLiteral("Folder"), .isVisible = true, .transform = {.origin = {0, 0}, .size = {4, 4}},
                          .imageFile = std::nullopt, .isGroup = true});
    for (int index = 0; index < count; ++index) {
        ProjectLayerRecord layer{.id = QUuid::createUuid(), .name = QStringLiteral("Layer"), .isVisible = true,
                                 .transform = {.origin = {0, 0}, .size = {4, 4}}, .imageFile = QStringLiteral("L.png"),
                                 .parentID = folders[index % 2]};
        if (index % 3 == 2)
            layer.maskSourceID = result.back().id;
        result.push_back(layer);
    }
    return result;
}
}

class RenderStarvationTests : public QObject {
    Q_OBJECT
private slots:
    void validatingTheGraphThrows();
    void preparingStacksThrows();
    void folderClipsThrow();
};

void RenderStarvationTests::validatingTheGraphThrows()
{
    const std::vector<ProjectLayerRecord> records = layers(64);
    QVERIFY(starved([&] { LiveMaskGraph::validate(records); }) > 64);
}

void RenderStarvationTests::preparingStacksThrows()
{
    const std::vector<ProjectLayerRecord> records = layers(64);
    std::vector<QUuid> ids;
    for (const ProjectLayerRecord &layer : records)
        ids.push_back(layer.id);
    const long made = starved([&] {
        LiveMaskRenderer live(
            [&records](QUuid id) {
                for (const ProjectLayerRecord &layer : records)
                    if (layer.id == id)
                        return layer.maskSourceID;
                return std::optional<QUuid>();
            },
            [](QUuid, QPainter &, const QImage &) {});
        live.prepareStacks(ids, [](QUuid) { return std::optional<QUuid>(); }, [](QUuid) { return LayerBlendMode::multiply; });
    });
    QVERIFY(made > 64);
}

void RenderStarvationTests::folderClipsThrow()
{
    const std::vector<ProjectLayerRecord> records = layers(6);
    std::vector<QUuid> ids;
    for (const ProjectLayerRecord &layer : records)
        ids.push_back(layer.id);
    QImage device = BrushRaster::context(4, 4, false);
    QPainter painter(&device);
    int drawn = 0;
    const long made = starved([&] {
        drawn = 0;
        FolderMaskClip::draw(
            ids,
            [&records](QUuid id) {
                for (const ProjectLayerRecord &layer : records)
                    if (layer.id == id)
                        return layer.parentID;
                return std::optional<QUuid>();
            },
            [](QUuid) { return std::optional<FolderMaskClip::Applier>([](const QPainter &, QImage &coverage) { coverage.fill(128); }); },
            painter, [&drawn](QUuid, const QImage &) { ++drawn; });
    });
    QVERIFY(made > 2);
    QCOMPARE(drawn, int(ids.size()));
}

QTEST_GUILESS_MAIN(RenderStarvationTests)
#include "RenderStarvationTests.moc"
