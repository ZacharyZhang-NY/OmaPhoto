#include "LevelsFixtures.h"
#include "Rendering/RasterSnapshot.h"
#include <QSignalSpy>

// The session around an open Levels edit: gates, queue, sampling.
class LevelsSessionTests : public QObject {
    Q_OBJECT
private slots:
    void anOpenEditHoldsTheSession();
    void aCommitHoldsTheProjectBusy();
    void aPendingGradientLandsFirst();
    void bigLayersPreviewAScaledCopy();
    void theRebuildDropsShapeAndText();
    void previewsRunOneAtATime();
    void autoWaitsForTheHistogramAndEndsSampling();
    void sampleModeRedrawsAndAnnounces();
    void samplesComeFromTheLayersOwnPixel();
};

void LevelsSessionTests::anOpenEditHoldsTheSession()
{
    const std::unique_ptr<EditorSession> session = rampSession();
    session->beginLevels();
    QVERIFY(!session->canAdjustColors());
    // A second Levels waits for the first.
    const QUuid first = session->levels().value().id;
    session->beginLevels();
    QCOMPARE(session->levels().value().id, first);
    // No other tool or layer while it is open.
    const NavigationTool tool = session->tool();
    session->selectTool(NavigationTool::brush);
    QCOMPARE(session->tool(), tool);
    const std::optional<QUuid> layer = session->activeLayerID();
    session->selectLayer(std::nullopt);
    QCOMPARE(session->activeLayerID(), layer);
    // A file request waits, then runs once it closes.
    bool asked = false;
    session->waitForFileRequest([&asked] { asked = true; });
    QTest::qWait(20);
    QVERIFY(!asked);
    session->cancelLevels();
    QTRY_VERIFY(asked);
    QVERIFY(session->canAdjustColors());
}

void LevelsSessionTests::aCommitHoldsTheProjectBusy()
{
    const std::unique_ptr<EditorSession> session = rampSession();
    session->beginLevels();
    LevelsSettings settings;
    settings.setCurrent(LevelRange{0, 1, 255, 255, 0});
    session->updateLevels(settings, false);
    bool done = false;
    session->commitLevels([&done] { done = true; });
    QVERIFY(session->isProjectBusy() && session->levels().value().committing);
    // Meanwhile every change is refused; a second commit returns.
    session->updateLevels(LevelsSettings(), true);
    QVERIFY(session->levels().value().settings == settings);
    session->cancelLevels();
    QVERIFY(session->levels());
    bool again = false;
    session->commitLevels([&again] { again = true; });
    QTRY_VERIFY(done && again);
    QVERIFY(!session->isProjectBusy() && !session->levels());
    QCOMPARE(bytes(session->activeLayer().value().asset.value().image())[0], uchar(255));
}

void LevelsSessionTests::aPendingGradientLandsFirst()
{
    const std::unique_ptr<EditorSession> session = rampSession();
    session->selectTool(NavigationTool::gradient);
    session->beginGradient(QPointF(0, 0.5));
    session->moveGradient(std::nullopt, QPointF(6, 0.5));
    QVERIFY(session->gradientEdit());
    const int count = session->history.undoCount();
    session->beginLevels();
    QTRY_VERIFY(session->levels());
    QVERIFY(!session->gradientEdit());
    QCOMPARE(session->history.undoCount(), count + 1);
    QCOMPARE(session->history.undoName(), QString("Gradient"));
    session->cancelLevels();
}

void LevelsSessionTests::bigLayersPreviewAScaledCopy()
{
    EditorSession session;
    session.createDocument(9000, 2);
    QImage wide(9000, 2, QImage::Format_RGBA8888_Premultiplied);
    wide.fill(Qt::red);
    session.insert(ImportedImage(wide, wide, QStringLiteral("Wide")));
    session.beginLevels();
    // Past 8000 a side: 8000 across, the height truncated.
    const LevelsEdit &edit = session.levels().value();
    QCOMPARE(edit.previewSource.size(), QSize(8000, 1));
    QCOMPARE(edit.previewSource.pixelColor(4000, 0), QColor(Qt::red));
    QCOMPARE(edit.previewMapping.map(QPointF(8000, 1)), QPointF(9000, 2));
    QCOMPARE(edit.mapping.map(QPointF(9000, 2)), QPointF(9000, 2));
    session.cancelLevels();
    // A painted layer previews from its tiles, never flattened.
    session.selectTool(NavigationTool::brush);
    session.beginBrush(QPointF(100, 1));
    session.finishBrush();
    const ImportedImage painted = session.activeLayer().value().asset.value();
    QVERIFY(painted.raster && !painted.raster->hasMaterializedPixels());
    session.beginLevels();
    QVERIFY(!painted.raster->hasMaterializedPixels());
    QCOMPARE(session.levels().value().previewSource.pixelColor(89, 0), QColor(Qt::black));
    session.cancelLevels();
    // One pixel tall stays one pixel tall.
    EditorSession thin;
    thin.createDocument(9000, 1);
    QImage line(9000, 1, QImage::Format_RGBA8888_Premultiplied);
    line.fill(Qt::red);
    thin.insert(ImportedImage(line, line, QStringLiteral("Line")));
    thin.beginLevels();
    QCOMPARE(thin.levels().value().previewSource.size(), QSize(8000, 1));
    thin.cancelLevels();
    // Smaller layers preview their own pixels.
    const std::unique_ptr<EditorSession> small = rampSession();
    small->beginLevels();
    QCOMPARE(small->levels().value().previewSource.cacheKey(), small->activeLayer().value().asset.value().image().cacheKey());
    QCOMPARE(small->levels().value().previewMapping, small->levels().value().mapping);
}

void LevelsSessionTests::theRebuildDropsShapeAndText()
{
    EditorSession session;
    session.createDocument(20, 20);
    session.selectTool(NavigationTool::shape);
    session.beginShape(QPointF(2, 2));
    session.dragShape(QPointF(12, 12), false, false);
    session.finishShape();
    QVERIFY(session.activeLayer().value().shape);
    session.beginLevels();
    LevelsSettings settings;
    settings.setCurrent(LevelRange{0, 1, 255, 255, 0});
    session.updateLevels(settings, false);
    commit(session);
    QVERIFY(!session.activeLayer().value().shape);
    QCOMPARE(session.history.undoName(), QString("Levels"));
}

void LevelsSessionTests::previewsRunOneAtATime()
{
    const std::unique_ptr<EditorSession> session = bigSession();
    session->beginLevels();
    LevelsSettings brighter, inverted;
    brighter.setCurrent(LevelRange{0, 2, 255, 0, 255});
    inverted.setCurrent(LevelRange{0, 1, 255, 255, 0});
    // The second waits for the first; the newest lands last.
    session->updateLevels(brighter, true);
    session->updateLevels(inverted, true);
    QVERIFY(session->levels().value().pending);
    QTRY_VERIFY(!session->levels().value().pending && session->levels().value().preparedPreview
                && session->levels().value().preparedPreview.value().pixelColor(0, 0) == QColor(Qt::cyan));
    // Identity settings show the layer itself.
    const int revision = session->brushRevision();
    session->updateLevels(LevelsSettings(), true);
    QVERIFY(!session->levels().value().preparedPreview && session->brushRevision() > revision);
    session->cancelLevels();
}

void LevelsSessionTests::autoWaitsForTheHistogramAndEndsSampling()
{
    EditorSession session;
    session.createDocument(2, 1);
    const QImage source = image({{50, 50, 50, 255}, {200, 200, 200, 255}});
    session.insert(ImportedImage(source, source, QStringLiteral("Gray")));
    session.beginLevels();
    session.setLevelsSampleMode(LevelsSample::white);
    // Its count arrives from the event loop: not yet.
    session.autoLevels(LevelsAuto::contrast);
    QVERIFY(session.levels().value().settings.isIdentity() && session.levels().value().sampleMode);
    QTRY_VERIFY(session.levels().value().histogramReady);
    session.autoLevels(LevelsAuto::contrast);
    QVERIFY(!session.levels().value().sampleMode);
    QVERIFY(session.levels().value().settings.ranges[0].black == 50 && session.levels().value().settings.ranges[0].white == 200);
    session.cancelLevels();
}

void LevelsSessionTests::sampleModeRedrawsAndAnnounces()
{
    const std::unique_ptr<EditorSession> session = rampSession();
    QSignalSpy changed(session.get(), &EditorSession::changed);
    // Without an edit it is refused, silently.
    session->setLevelsSampleMode(LevelsSample::black);
    QCOMPARE(changed.count(), 0);
    session->beginLevels();
    changed.clear();
    const int revision = session->brushRevision();
    session->setLevelsSampleMode(LevelsSample::black);
    QCOMPARE(changed.count(), 1);
    QVERIFY(session->brushRevision() > revision);
    QCOMPARE(session->levels().value().sampleMode, std::optional(LevelsSample::black));
    session->setLevelsSampleMode(std::nullopt);
    QVERIFY(!session->levels().value().sampleMode);
    // No mode, no sample.
    session->sampleLevels(QPointF(2.5, 0.5));
    QVERIFY(session->levels().value().settings.isIdentity());
    session->cancelLevels();
}

void LevelsSessionTests::samplesComeFromTheLayersOwnPixel()
{
    EditorSession session;
    session.createDocument(8, 4);
    // Black then white, scaled twice and moved by one.
    const QImage source = image({{0, 0, 0, 255}, {255, 255, 255, 255}});
    session.insert(ImportedImage(source, source, QStringLiteral("Pair")));
    LayerTransform placed = session.activeLayer().value().transform;
    placed.origin = QPointF(1, 1);
    placed.size = QSizeF(4, 2);
    session.beginTransform();
    session.previewTransform(placed);
    session.commitTransform();
    session.beginLevels();
    session.setLevelsSampleMode(LevelsSample::white);
    // Outside the layer, inside the document: nothing.
    session.sampleLevels(QPointF(0.5, 0.5));
    QVERIFY(session.levels().value().settings.isIdentity());
    // Document point (2, 2) is black: white stops at 1.
    session.sampleLevels(QPointF(2, 2));
    QCOMPARE(session.levels().value().settings.ranges[1].white, 1.0);
    session.cancelLevels();
}

QTEST_GUILESS_MAIN(LevelsSessionTests)
#include "LevelsSessionTests.moc"
