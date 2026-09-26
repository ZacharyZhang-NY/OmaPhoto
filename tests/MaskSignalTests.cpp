#include "SessionFixtures.h"
#include "SessionRecord.h"
#include <QSignalSpy>
#include <QtTest>

// The `changed()` contract for masks in the session.
namespace {
std::unique_ptr<EditorSession> twoLayers(QUuid &lower, QUuid &upper)
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(8, 8);
    for (QUuid *id : {&lower, &upper}) {
        session->insert(ImportedImage(QImage(4, 4, QImage::Format_RGBA8888_Premultiplied), QImage(), "Image"));
        *id = session->activeLayerID().value();
    }
    return session;
}
}

class MaskSignalTests : public QObject {
    Q_OBJECT
private slots:
    void masksAnnounceAndRefuseInSilence();
    void theLastSignalOfEveryMaskChangeSeesWhatItLeaves();
    void clippingAnnouncesAndRefusesInSilence();
    void theLastSignalOfEveryClippingChangeSeesWhatItLeaves();
};

void MaskSignalTests::masksAnnounceAndRefuseInSilence()
{
    QUuid lower, upper;
    const std::unique_ptr<EditorSession> session = twoLayers(lower, upper);
    QSignalSpy changes(session.get(), &EditorSession::changed);
    const auto announced = [&](const std::function<void()> &change) {
        const qsizetype before = changes.count();
        change();
        return changes.count() > before;
    };
    // No mask yet: nothing to toggle, delete, unlink or copy.
    QVERIFY(!announced([&] { session->toggleLayerMask(); }));
    QVERIFY(!announced([&] { session->deleteLayerMask(); }));
    QVERIFY(!announced([&] { session->toggleMaskLink(upper); }));
    QVERIFY(!announced([&] { session->copyMask(upper, lower); }));
    QVERIFY(announced([&] { session->addLayerMask(); }));
    QVERIFY(!announced([&] { session->addLayerMask(); }));
    QVERIFY(announced([&] { session->toggleLayerMask(); }));
    QVERIFY(announced([&] { session->toggleMaskLink(upper); }));
    QVERIFY(!announced([&] { session->toggleMaskLink(QUuid::createUuid()); }));
    QVERIFY(announced([&] { session->selectLayerTarget(upper, false); }));
    QVERIFY(announced([&] { session->selectLayerTarget(upper, true); }));
    QVERIFY(announced([&] { session->copyMask(upper, lower); }));
    QVERIFY(!announced([&] { session->copyMask(upper, upper); }));
    QVERIFY(announced([&] { session->deleteLayerMask(); }));
    session->setIsImporting(true);
    QVERIFY(!announced([&] { session->selectLayerTarget(upper, true); }));
    QVERIFY(!announced([&] { session->addLayerMask(); }));
    QVERIFY(!announced([&] { session->toggleMaskLink(upper); }));
}

void MaskSignalTests::theLastSignalOfEveryMaskChangeSeesWhatItLeaves()
{
    QUuid lower, upper;
    const std::unique_ptr<EditorSession> session = twoLayers(lower, upper);
    QStringList seen;
    connect(session.get(), &EditorSession::changed, this, [&] { seen = described(*session); });
    const auto stale = [&](const std::function<void()> &change) {
        seen.clear();
        change();
        const QStringList left = described(*session);
        return seen == left ? QString() : seen.join("; ") + " != " + left.join("; ");
    };
    QCOMPARE(stale([&] { session->addLayerMask(false); }), QString());
    QVERIFY(seen.contains("mask target 1") && seen.filter("  mask ").size() == 1);
    QCOMPARE(stale([&] { session->toggleLayerMask(); }), QString());
    QCOMPARE(stale([&] { session->selectLayerTarget(upper, false); }), QString());
    QVERIFY(seen.contains("mask target 0"));
    QCOMPARE(stale([&] { session->selectLayerTarget(upper, true); }), QString());
    QVERIFY(seen.contains("mask target 1"));
    QCOMPARE(stale([&] { session->toggleMaskLink(upper); }), QString());
    // The mask alone: begun, moved, committed.
    QCOMPARE(stale([&] { session->beginTransform(); }), QString());
    QVERIFY(seen.filter(" mask").filter("edit ").size() == 1);
    QCOMPARE(stale([&] { session->nudgeLayer(2, 1); }), QString());
    QCOMPARE(stale([&] { session->commitTransform(); }), QString());
    QVERIFY(seen.contains("names Transform Layer Mask/") && seen.filter("edit ").isEmpty());
    QCOMPARE(stale([&] { session->copyMask(upper, lower); }), QString());
    QVERIFY(seen.contains("active " + lower.toString()) && seen.contains("mask target 1") && seen.filter("  mask ").size() == 2);
    // Another layer, an undo, a project: the target is dropped.
    QCOMPARE(stale([&] { session->selectLayer(upper); }), QString());
    QVERIFY(seen.contains("mask target 0"));
    QCOMPARE(stale([&] { session->selectLayerTarget(upper, true); }), QString());
    QCOMPARE(stale([&] { session->deleteLayerMask(); }), QString());
    QVERIFY(seen.contains("mask target 0") && seen.filter("  mask ").size() == 1);
    QCOMPARE(stale([&] { session->undo(); }), QString());
    QVERIFY(seen.filter("  mask ").size() == 2);
    QCOMPARE(stale([&] { session->selectLayerTarget(upper, true); }), QString());
    const ProjectSnapshot snapshot = session->projectSnapshot().value();
    QCOMPARE(stale([&] { session->installProject(snapshot, "masks.comp"); }), QString());
    QVERIFY(seen.contains("mask target 0"));
    QCOMPARE(stale([&] { session->selectLayerTarget(upper, true); }), QString());
    QCOMPARE(stale([&] { session->clearProject(); }), QString());
    QVERIFY(seen.contains("mask target 0"));
}

void MaskSignalTests::clippingAnnouncesAndRefusesInSilence()
{
    QUuid lower, upper;
    const std::unique_ptr<EditorSession> session = twoLayers(lower, upper);
    QSignalSpy changes(session.get(), &EditorSession::changed);
    const auto announced = [&](const std::function<void()> &change) {
        const qsizetype before = changes.count();
        change();
        return changes.count() > before;
    };
    // A layer nobody knows: nothing to clip, free or delete.
    const QUuid stranger = QUuid::createUuid();
    QVERIFY(!announced([&] { session->toggleClippingMask(stranger); }));
    QVERIFY(!announced([&] { session->removeLiveMask(stranger); }));
    QVERIFY(!announced([&] { session->finishDeletingLayer(stranger, {}); }));
    QVERIFY(!announced([&] { session->finishDeletingLayers({}, {}); }));
    QVERIFY(!session->canToggleClippingMask(stranger));
    // Nothing below to clip to; no link to free.
    QVERIFY(!announced([&] { session->toggleClippingMask(lower); }));
    QVERIFY(!announced([&] { session->removeLiveMask(upper); }));
    QVERIFY(!announced([&] { session->linkMask(upper, upper); }));
    QVERIFY(announced([&] { session->toggleClippingMask(upper); }));
    // The same link again is no change.
    QVERIFY(!announced([&] { session->linkMask(lower, upper); }));
    QVERIFY(!announced([&] { session->linkMask(upper, lower); }));
    QVERIFY(announced([&] { session->removeLiveMask(upper); }));
    QVERIFY(announced([&] { session->linkMask(lower, upper); }));
    QVERIFY(announced([&] { session->toggleClippingMask(upper); }));
    QVERIFY(announced([&] { session->setBrushError(QString("failed")); }));
    QVERIFY(announced([&] { session->setBrushError(std::nullopt); }));
    session->setIsImporting(true);
    QVERIFY(!announced([&] { session->toggleClippingMask(upper); }));
    QVERIFY(!announced([&] { session->linkMask(lower, upper); }));
}

void MaskSignalTests::theLastSignalOfEveryClippingChangeSeesWhatItLeaves()
{
    QUuid lower, upper;
    const std::unique_ptr<EditorSession> session = twoLayers(lower, upper);
    QStringList seen;
    connect(session.get(), &EditorSession::changed, this, [&] { seen = described(*session); });
    const auto stale = [&](const std::function<void()> &change) {
        seen.clear();
        change();
        const QStringList left = described(*session);
        return seen == left ? QString() : seen.join("; ") + " != " + left.join("; ");
    };
    QCOMPARE(stale([&] { session->toggleClippingMask(upper); }), QString());
    QVERIFY(seen.filter("clipped to " + lower.toString()).size() == 1);
    QCOMPARE(stale([&] { session->toggleClippingMask(upper); }), QString());
    QVERIFY(seen.filter("clipped to " + lower.toString()).isEmpty());
    QCOMPARE(stale([&] { QVERIFY(session->linkMask(lower, upper)); }), QString());
    QCOMPARE(stale([&] { session->removeLiveMask(upper); }), QString());
    QCOMPARE(stale([&] { QVERIFY(session->linkMask(lower, upper)); }), QString());
    // A move that breaks the stack frees it at once.
    QCOMPARE(stale([&] { QVERIFY(session->placeLayer(lower, std::nullopt)); }), QString());
    QVERIFY(seen.filter("clipped to none").size() == 2);
    QCOMPARE(stale([&] { session->undo(); }), QString());
    // Base gone, baked pixels in, link gone: one state.
    const ImportedImage baked(QImage(4, 4, QImage::Format_RGBA8888_Premultiplied), QImage(), "Baked");
    QCOMPARE(stale([&] { session->finishDeletingLayer(lower, {{upper, baked}}); }), QString());
    QVERIFY(seen.filter(QStringLiteral("pixels %1").arg(baked.identity().cacheKey)).size() == 1);
    QCOMPARE(stale([&] { session->setBrushError(QString("failed")); }), QString());
    QVERIFY(seen.contains("tool error failed"));
}

QTEST_GUILESS_MAIN(MaskSignalTests)
#include "MaskSignalTests.moc"
