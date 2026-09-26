#include "Document/EditorSession.h"
#include "Document/ProjectWorkspace.h"
#include <QSignalSpy>
#include <QThreadPool>
#include <QtTest>

// Hue/Saturation in the session: gates, previews, commits, signals.
namespace {
std::unique_ptr<EditorSession> filled(int width, int height, QColor colour)
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(width, height);
    QImage image(width, height, QImage::Format_RGBA8888_Premultiplied);
    image.fill(colour);
    session->insert(ImportedImage(image, image, QStringLiteral("Filled")));
    return session;
}

// 3000 by 2000 red: long enough runs to queue behind.
std::unique_ptr<EditorSession> big()
{
    return filled(3000, 2000, Qt::red);
}

bool settled(const EditorSession &session, int revision)
{
    return QTest::qWaitFor([&] { return session.brushRevision() > revision && !session.hueSaturationPending(); });
}

std::optional<QImage> shown(const EditorSession &session)
{
    const HueSaturationEdit &edit = session.hueSaturation().value();
    return edit.previewImage(edit.layerID);
}

QColor first(const QImage &image)
{
    return image.pixelColor(0, 0);
}
}

class HueSaturationSessionTests : public QObject {
    Q_OBJECT
private slots:
    void beginRefusesAndLandsAPendingGradientFirst();
    void otherEditsWaitWhileItIsOpen();
    void previewsQueueAndDropWhenCancelled();
    void commitsRunOnceAndSkipAChangedLayer();
    void signalsFollowEveryStep();
    void samplesAndTargetsNeedAColour();
    void beginCommitsAnOpenTransform();
    void thePreviewFlagStaysAndIdentityClearsAtOnce();
    void aSelectionLimitsThePreview();
    void aCommitDropsTheQueueAndLandsOnce();
    void theRebuildDropsAShape();
    void samplesSkipGrayAndReadPaleColours();
    void aDoneRenderLandsBeforeTheNextStarts();
};

void HueSaturationSessionTests::beginRefusesAndLandsAPendingGradientFirst()
{
    const std::unique_ptr<EditorSession> session = filled(6, 1, Qt::red);
    session->beginHueSaturation();
    const QUuid opened = session->hueSaturation().value().id;
    session->beginHueSaturation();
    QCOMPARE(session->hueSaturation().value().id, opened);
    session->cancelHueSaturation();
    // A blank layer holds nothing to adjust.
    session->addBlankLayer();
    session->beginHueSaturation();
    QVERIFY(!session->hueSaturation());
    session->undo();
    // The gradient lands, then the edit opens on its pixels.
    session->selectTool(NavigationTool::gradient);
    session->beginGradient(QPointF(0, 0.5));
    session->moveGradient(std::nullopt, QPointF(6, 0.5));
    const int count = session->history.undoCount();
    session->beginHueSaturation();
    QTRY_VERIFY(session->hueSaturation());
    QVERIFY(!session->gradientEdit());
    QCOMPARE(session->history.undoCount(), count + 1);
    QCOMPARE(session->hueSaturation().value().original.identity(), session->activeLayer().value().asset.value().identity());
}

void HueSaturationSessionTests::otherEditsWaitWhileItIsOpen()
{
    ProjectWorkspace workspace;
    EditorSession &session = workspace.current().session;
    session.createDocument(8, 8);
    QImage image(8, 8, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::red);
    session.insert(ImportedImage(image, image, QStringLiteral("Red")));
    QPainterPath corner;
    corner.addRect(0, 0, 4, 4);
    session.applySelection(corner, SelectionMode::replace, QStringLiteral("Select"));
    QVERIFY(session.canEditLayers() && session.canContentAwareFill() && workspace.canSwitch());
    session.beginHueSaturation();
    QVERIFY(!session.canEditLayers() && !session.canContentAwareFill() && !workspace.canSwitch());
    session.beginLevels();
    QVERIFY(!session.levels());
    session.cancelHueSaturation();
    QVERIFY(session.canEditLayers() && session.canContentAwareFill() && workspace.canSwitch());
}

void HueSaturationSessionTests::previewsQueueAndDropWhenCancelled()
{
    const std::unique_ptr<EditorSession> session = big();
    session->beginHueSaturation();
    int revision = session->brushRevision();
    session->updateHueSaturation(HueSaturationSettings(120), true);
    session->updateHueSaturation(HueSaturationSettings(240), true);
    // The newest waits for the running one, then shows.
    QVERIFY(session->hueSaturationPending());
    QVERIFY(settled(*session, revision + 1));
    QCOMPARE(first(shown(*session).value()), QColor(Qt::blue));
    // Turned off mid-render, the queued and running drop.
    session->updateHueSaturation(HueSaturationSettings(120), true);
    session->updateHueSaturation(HueSaturationSettings(240), true);
    QVERIFY(session->hueSaturationPending());
    session->updateHueSaturation(HueSaturationSettings(120), false);
    QVERIFY(!session->hueSaturationPending() && !shown(*session));
    revision = session->brushRevision();
    session->updateHueSaturation(HueSaturationSettings(60), true);
    QVERIFY(settled(*session, revision));
    QCOMPARE(session->brushRevision(), revision + 1);
    QCOMPARE(first(shown(*session).value()), QColor(Qt::yellow));
    // Cancelled mid-render, it lands on no later edit.
    session->updateHueSaturation(HueSaturationSettings(120), true);
    session->updateHueSaturation(HueSaturationSettings(240), true);
    session->cancelHueSaturation();
    QVERIFY(!session->hueSaturationPending());
    session->beginHueSaturation();
    revision = session->brushRevision();
    session->updateHueSaturation(HueSaturationSettings(180), true);
    QVERIFY(settled(*session, revision));
    QCOMPARE(session->brushRevision(), revision + 1);
    QCOMPARE(first(shown(*session).value()), QColor(Qt::cyan));
    QVERIFY(!session->hueSaturation().value().previewImage(QUuid::createUuid()));
    session->cancelHueSaturation();
}

void HueSaturationSessionTests::commitsRunOnceAndSkipAChangedLayer()
{
    const std::unique_ptr<EditorSession> session = big();
    session->beginHueSaturation();
    session->setHueSampleMode(HueSampleMode::add);
    session->setHueTargeting(true);
    session->updateHueSaturation(HueSaturationSettings(120), false);
    const int count = session->history.undoCount();
    bool once = false, twice = false;
    session->commitHueSaturation([&once] { once = true; });
    QVERIFY(session->isProjectBusy() && !session->hueSampleMode() && !session->hueTargeting());
    session->commitHueSaturation([&twice] { twice = true; });
    QTRY_VERIFY(once && twice);
    QVERIFY(!session->hueSaturation() && !session->isProjectBusy());
    QCOMPARE(session->history.undoCount(), count + 1);
    const ImportedImage asset = session->activeLayer().value().asset.value();
    QCOMPARE(asset.name, QString("Filled"));
    QCOMPARE(asset.thumbnail.size(), QSize(96, 64));
    QCOMPARE(first(asset.image()), QColor(Qt::green));
    // The layer changed under the edit: the commit lands nothing.
    session->beginHueSaturation();
    session->updateHueSaturation(HueSaturationSettings(60), false);
    session->undo();
    bool done = false;
    session->commitHueSaturation([&done] { done = true; });
    QTRY_VERIFY(done);
    QVERIFY(!session->hueSaturation() && !session->isProjectBusy());
    QCOMPARE(session->history.undoCount(), count);
    QCOMPARE(first(session->activeLayer().value().asset.value().image()), QColor(Qt::red));
    // Nothing to change closes at once, redrawn.
    session->beginHueSaturation();
    const int revision = session->brushRevision();
    done = false;
    session->commitHueSaturation([&done] { done = true; });
    QVERIFY(!session->hueSaturation() && !session->isProjectBusy());
    QCOMPARE(session->brushRevision(), revision + 1);
    QTRY_VERIFY(done);
    done = false;
    session->commitHueSaturation([&done] { done = true; });
    QTRY_VERIFY(done);
}

void HueSaturationSessionTests::signalsFollowEveryStep()
{
    const std::unique_ptr<EditorSession> session = filled(4, 4, Qt::red);
    QSignalSpy changed(session.get(), &EditorSession::changed);
    session->beginHueSaturation();
    QCOMPARE(changed.count(), 1);
    changed.clear();
    const int revision = session->brushRevision();
    session->updateHueSaturation(HueSaturationSettings(120), true);
    QCOMPARE(changed.count(), 1);
    QVERIFY(settled(*session, revision));
    QCOMPARE(session->brushRevision(), revision + 1);
    QCOMPARE(changed.count(), 2);
    changed.clear();
    session->setHueSampleMode(HueSampleMode::remove);
    session->setHueSampleMode(HueSampleMode::remove);
    session->setHueTargeting(true);
    session->setHueTargeting(true);
    QCOMPARE(changed.count(), 2);
    changed.clear();
    session->updateHueSaturation(HueSaturationSettings(120), false);
    QCOMPARE(session->brushRevision(), revision + 2);
    session->cancelHueSaturation();
    QCOMPARE(changed.count(), 2);
    QCOMPARE(session->brushRevision(), revision + 3);
    QVERIFY(!session->hueSampleMode() && !session->hueTargeting());
    // Without an edit, cancelling drops the armed tools alone.
    changed.clear();
    session->cancelHueSaturation();
    QCOMPARE(changed.count(), 0);
    session->setHueTargeting(true);
    changed.clear();
    session->cancelHueSaturation();
    QCOMPARE(changed.count(), 1);
    QVERIFY(!session->hueTargeting() && session->brushRevision() == revision + 3);
    session->setHueSampleMode(HueSampleMode::add);
    changed.clear();
    session->cancelHueSaturation();
    QCOMPARE(changed.count(), 1);
    QVERIFY(!session->hueSampleMode());
}

void HueSaturationSessionTests::samplesAndTargetsNeedAColour()
{
    // Hue 90 exactly: halfway between Yellows and Greens.
    const std::unique_ptr<EditorSession> session = filled(4, 4, QColor(127, 254, 0));
    session->beginHueSaturation();
    session->updateHueSaturation(HueSaturationSettings(0, 0, 0, false, ColorRange::reds), false);
    const HueBand reds = session->hueSaturation().value().settings.band();
    // No eyedropper armed: nothing.
    session->sampleHueRange(QPointF(1, 1));
    QVERIFY(session->hueSaturation().value().settings.band() == reds);
    session->setHueSampleMode(HueSampleMode::replace);
    HueSaturationSettings colorized(0, 0, 0, true, ColorRange::reds);
    session->updateHueSaturation(colorized, false);
    session->sampleHueRange(QPointF(1, 1));
    QVERIFY(session->hueSaturation().value().settings.band() == reds);
    // Targeting needs its mode armed, no Colorize and colour.
    session->setHueTargeting(true);
    QVERIFY(!session->beginHueTargeting(QPointF(1, 1)));
    HueSaturationSettings start;
    start.adjustments[ColorRange::yellows] = RangeAdjustment{30, 10, 0};
    session->updateHueSaturation(start, false);
    session->setHueTargeting(false);
    QVERIFY(!session->beginHueTargeting(QPointF(1, 1)));
    session->setHueTargeting(true);
    QVERIFY(!session->beginHueTargeting(QPointF(9, 9)));
    // A tie goes to the first range, as Swift's max(by:).
    QVERIFY(session->beginHueTargeting(QPointF(1, 1)));
    QCOMPARE(session->hueSaturation().value().settings.range, ColorRange::yellows);
    // Drags start from the range's own numbers.
    session->dragHueTargeting(20, true);
    session->dragHueTargeting(-10, false);
    QCOMPARE(session->hueSaturation().value().settings.adjustments.at(ColorRange::yellows), (RangeAdjustment{40, 5, 0}));
    session->dragHueTargeting(1000, false);
    QCOMPARE(session->hueSaturation().value().settings.adjustments.at(ColorRange::yellows).saturation, 100.0);
    session->dragHueTargeting(-1000, false);
    QCOMPARE(session->hueSaturation().value().settings.adjustments.at(ColorRange::yellows).saturation, -100.0);
    session->dragHueTargeting(1000, true);
    QCOMPARE(session->hueSaturation().value().settings.adjustments.at(ColorRange::yellows).hue, 180.0);
    session->dragHueTargeting(-1000, true);
    QCOMPARE(session->hueSaturation().value().settings.adjustments.at(ColorRange::yellows).hue, -180.0);
    // Ended, drags change nothing.
    session->endHueTargeting();
    session->dragHueTargeting(20, true);
    QCOMPARE(session->hueSaturation().value().settings.adjustments.at(ColorRange::yellows).hue, -180.0);
    session->cancelHueSaturation();
}

void HueSaturationSessionTests::beginCommitsAnOpenTransform()
{
    const std::unique_ptr<EditorSession> session = filled(4, 4, Qt::red);
    session->selectTool(NavigationTool::move);
    session->beginTransform();
    LayerTransform moved = session->activeLayer().value().transform;
    moved.origin = QPointF(1, 0);
    session->previewTransform(moved);
    session->beginHueSaturation();
    QVERIFY(!session->transformEdit());
    // Its pixels map through the new place.
    QCOMPARE(session->hueSaturation().value().pixelToDocument.map(QPointF(0, 0)), QPointF(1, 0));
    session->cancelHueSaturation();
}

void HueSaturationSessionTests::thePreviewFlagStaysAndIdentityClearsAtOnce()
{
    const std::unique_ptr<EditorSession> session = filled(4, 4, Qt::red);
    session->beginHueSaturation();
    QVERIFY(session->hueSaturation().value().preview);
    session->updateHueSaturation(HueSaturationSettings(120), false);
    QVERIFY(!session->hueSaturation().value().preview);
    int revision = session->brushRevision();
    session->updateHueSaturation(HueSaturationSettings(120), true);
    QVERIFY(settled(*session, revision));
    // Nothing to show: the preview goes without a render.
    revision = session->brushRevision();
    session->updateHueSaturation(HueSaturationSettings(), true);
    QVERIFY(!shown(*session) && !session->hueSaturationPending() && session->hueSaturation().value().preview);
    QCOMPARE(session->brushRevision(), revision + 1);
    session->cancelHueSaturation();
}

void HueSaturationSessionTests::aSelectionLimitsThePreview()
{
    const std::unique_ptr<EditorSession> session = filled(4, 1, Qt::red);
    QPainterPath left;
    left.addRect(0, 0, 2, 1);
    session->applySelection(left, SelectionMode::replace, QStringLiteral("Select"));
    session->beginHueSaturation();
    const int revision = session->brushRevision();
    session->updateHueSaturation(HueSaturationSettings(120), true);
    QVERIFY(settled(*session, revision));
    const QImage preview = shown(*session).value();
    QVERIFY(preview.pixelColor(1, 0) == QColor(Qt::green) && preview.pixelColor(2, 0) == QColor(Qt::red));
    session->cancelHueSaturation();
}

void HueSaturationSessionTests::aCommitDropsTheQueueAndLandsOnce()
{
    const std::unique_ptr<EditorSession> session = big();
    session->beginHueSaturation();
    session->updateHueSaturation(HueSaturationSettings(120), true);
    session->updateHueSaturation(HueSaturationSettings(240), true);
    QVERIFY(session->hueSaturationPending());
    const int revision = session->brushRevision();
    bool done = false;
    session->commitHueSaturation([&done] { done = true; });
    QVERIFY(!session->hueSaturationPending());
    QTRY_VERIFY(done);
    // The running preview dropped: one redraw, the commit's.
    QCOMPARE(session->brushRevision(), revision + 1);
    QCOMPARE(first(session->activeLayer().value().asset.value().image()), QColor(Qt::blue));
}

void HueSaturationSessionTests::theRebuildDropsAShape()
{
    EditorSession session;
    session.createDocument(20, 20);
    session.selectTool(NavigationTool::shape);
    session.beginShape(QPointF(2, 2));
    session.dragShape(QPointF(12, 12), false, false);
    session.finishShape();
    QVERIFY(session.activeLayer().value().shape);
    session.beginHueSaturation();
    session.updateHueSaturation(HueSaturationSettings(120), false);
    bool done = false;
    session.commitHueSaturation([&done] { done = true; });
    QTRY_VERIFY(done);
    QVERIFY(!session.activeLayer().value().shape);
    QCOMPARE(session.history.undoName(), QString("Hue/Saturation"));
}

void HueSaturationSessionTests::samplesSkipGrayAndReadPaleColours()
{
    // Yellows centred on red moves: HSB saturation 0.1 samples.
    const HueSaturationSettings yellows(0, 0, 0, false, ColorRange::yellows);
    const std::unique_ptr<EditorSession> pale = filled(4, 4, QColor(255, 230, 230));
    pale->beginHueSaturation();
    pale->updateHueSaturation(yellows, false);
    pale->setHueSampleMode(HueSampleMode::replace);
    pale->sampleHueRange(QPointF(1, 1));
    QCOMPARE(pale->hueSaturation().value().settings.band(), (HueBand{315, 345, 15, 45}));
    // Gray has no hue: the band stays.
    const std::unique_ptr<EditorSession> gray = filled(4, 4, QColor(128, 128, 128));
    gray->beginHueSaturation();
    gray->updateHueSaturation(yellows, false);
    gray->setHueSampleMode(HueSampleMode::replace);
    gray->sampleHueRange(QPointF(1, 1));
    QCOMPARE(gray->hueSaturation().value().settings.band(), defaultBand(ColorRange::yellows));
}

void HueSaturationSessionTests::aDoneRenderLandsBeforeTheNextStarts()
{
    const std::unique_ptr<EditorSession> session = filled(4, 4, Qt::red);
    session->beginHueSaturation();
    const int revision = session->brushRevision();
    session->updateHueSaturation(HueSaturationSettings(120), true);
    // Done but undelivered: Qt would drop it for the next.
    QThreadPool::globalInstance()->waitForDone();
    session->updateHueSaturation(HueSaturationSettings(240), true);
    QVERIFY(session->hueSaturationPending());
    QVERIFY(settled(*session, revision + 1));
    QCOMPARE(session->brushRevision(), revision + 2);
    QCOMPARE(first(shown(*session).value()), QColor(Qt::blue));
    session->cancelHueSaturation();
}

QTEST_GUILESS_MAIN(HueSaturationSessionTests)
#include "HueSaturationSessionTests.moc"
