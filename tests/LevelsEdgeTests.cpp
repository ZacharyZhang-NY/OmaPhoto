#include "LevelsFixtures.h"
#include "Rendering/RasterSnapshot.h"
#include <QSignalSpy>
#include <QThreadPool>

// Levels at its edges: order, signals, stale runs, sizes, samples.
namespace {
LevelsSettings inverted()
{
    LevelsSettings settings;
    settings.setCurrent(LevelRange{0, 1, 255, 255, 0});
    return settings;
}

LevelsSettings brighter()
{
    LevelsSettings settings;
    settings.setCurrent(LevelRange{0, 2, 255, 0, 255});
    return settings;
}
}

class LevelsEdgeTests : public QObject {
    Q_OBJECT
private slots:
    void beginEndsATransformAndALasso();
    void signalsFollowEveryStep();
    void turningPreviewOffDropsTheQueue();
    void anIdentityCommitIsImmediate();
    void stalePreviewsNeverLand();
    void aDoneRunLandsBeforeTheNextStarts();
    void theRebuildDropsText();
    void thePreviewIsTheEditsLayerAlone();
    void scaledSidesTruncateAsSwifts();
    void samplesStopAtTheDocumentAndClearPixels();
};

void LevelsEdgeTests::beginEndsATransformAndALasso()
{
    const std::unique_ptr<EditorSession> session = rampSession();
    session->selectTool(NavigationTool::move);
    session->beginTransform();
    LayerTransform moved = session->activeLayer().value().transform;
    moved.origin = QPointF(1, 0);
    session->previewTransform(moved);
    session->beginLevels();
    QVERIFY(!session->transformEdit());
    QCOMPARE(session->levels().value().transform.origin, QPointF(1, 0));
    // Placed: its pixels map through the new place.
    QCOMPARE(session->levels().value().mapping.map(QPointF(0, 0)), QPointF(1, 0));
    QCOMPARE(session->levels().value().previewMapping, session->levels().value().mapping);
    session->cancelLevels();
    session->selectTool(NavigationTool::lasso);
    session->beginLasso(QPointF(0, 0), SelectionMode::replace);
    QVERIFY(session->lassoDraft());
    session->beginLevels();
    QVERIFY(!session->lassoDraft());
    session->cancelLevels();
}

void LevelsEdgeTests::signalsFollowEveryStep()
{
    const std::unique_ptr<EditorSession> session = rampSession();
    QSignalSpy changed(session.get(), &EditorSession::changed);
    session->beginLevels();
    QCOMPARE(changed.count(), 1);
    QTRY_VERIFY(session->levels().value().histogramReady);
    QCOMPARE(changed.count(), 2);
    changed.clear();
    session->updateLevels(inverted(), true);
    QCOMPARE(changed.count(), 1);
    // The preview lands with a redraw and a signal.
    const int revision = session->brushRevision();
    QTRY_VERIFY(session->levels().value().preparedPreview);
    QCOMPARE(session->brushRevision(), revision + 1);
    QCOMPARE(changed.count(), 2);
    changed.clear();
    session->updateLevels(inverted(), false);
    QCOMPARE(changed.count(), 1);
    QVERIFY(!session->levels().value().preview);
    const int shown = session->brushRevision();
    changed.clear();
    session->cancelLevels();
    QCOMPARE(changed.count(), 1);
    QCOMPARE(session->brushRevision(), shown + 1);
    session->beginLevels();
    session->updateLevels(inverted(), false);
    const int committing = session->brushRevision();
    commit(*session);
    QCOMPARE(session->brushRevision(), committing + 1);
}

void LevelsEdgeTests::turningPreviewOffDropsTheQueue()
{
    const std::unique_ptr<EditorSession> session = bigSession();
    session->beginLevels();
    session->updateLevels(brighter(), true);
    session->updateLevels(inverted(), true);
    QVERIFY(session->levels().value().pending);
    session->updateLevels(inverted(), false);
    QVERIFY(!session->levels().value().pending);
    QTest::qWait(200);
    QVERIFY(!session->levels().value().preparedPreview);
    session->cancelLevels();
}

void LevelsEdgeTests::anIdentityCommitIsImmediate()
{
    const std::unique_ptr<EditorSession> session = rampSession();
    session->beginLevels();
    bool done = false;
    session->commitLevels([&done] { done = true; });
    QVERIFY(!session->levels() && !session->isProjectBusy());
    QTRY_VERIFY(done);
}

void LevelsEdgeTests::stalePreviewsNeverLand()
{
    const std::unique_ptr<EditorSession> session = bigSession();
    // A closed edit's run never lands on the next one.
    session->beginLevels();
    session->updateLevels(brighter(), true);
    session->cancelLevels();
    session->beginLevels();
    session->updateLevels(inverted(), true);
    int revision = session->brushRevision();
    QTRY_VERIFY(session->levels().value().preparedPreview);
    QCOMPARE(session->brushRevision(), revision + 1);
    QCOMPARE(session->levels().value().preparedPreview.value().pixelColor(0, 0), QColor(Qt::cyan));
    // Nor one turned off, then on again.
    session->updateLevels(brighter(), true);
    session->updateLevels(brighter(), false);
    session->updateLevels(inverted(), true);
    revision = session->brushRevision();
    QTRY_VERIFY(session->levels().value().preparedPreview && !session->levels().value().pending);
    QCOMPARE(session->brushRevision(), revision + 1);
    // Nor one a commit overtook, nor the queued one.
    session->updateLevels(brighter(), true);
    session->updateLevels(inverted(), true);
    QVERIFY(session->levels().value().pending);
    revision = session->brushRevision();
    bool done = false;
    session->commitLevels([&done] { done = true; });
    QVERIFY(!session->levels().value().pending);
    QTRY_VERIFY(done);
    QCOMPARE(session->brushRevision(), revision + 1);
}

void LevelsEdgeTests::aDoneRunLandsBeforeTheNextStarts()
{
    const std::unique_ptr<EditorSession> session = rampSession();
    session->beginLevels();
    QTRY_VERIFY(session->levels().value().histogramReady);
    const int revision = session->brushRevision();
    session->updateLevels(brighter(), true);
    // Done but undelivered: Qt would drop it for the next.
    QThreadPool::globalInstance()->waitForDone();
    session->updateLevels(inverted(), true);
    QVERIFY(session->levels().value().pending);
    QTRY_VERIFY(!session->levels().value().pending && session->brushRevision() == revision + 2);
    QCOMPARE(session->levels().value().preparedPreview.value().pixelColor(0, 0), QColor(Qt::white));
    session->cancelLevels();
}

void LevelsEdgeTests::theRebuildDropsText()
{
    EditorSession session;
    session.createDocument(200, 100);
    session.selectTool(NavigationTool::type);
    session.beginText(QPointF(10, 10), true);
    TextDraft draft = session.textDraft().value();
    draft.style.content = QStringLiteral("Hi");
    session.setTextDraft(draft);
    QVERIFY(session.finishText());
    QVERIFY(session.activeLayer().value().liveText());
    session.beginLevels();
    session.updateLevels(inverted(), false);
    commit(session);
    QVERIFY(!session.activeLayer().value().text);
}

void LevelsEdgeTests::thePreviewIsTheEditsLayerAlone()
{
    const std::unique_ptr<EditorSession> session = rampSession();
    session->beginLevels();
    session->updateLevels(inverted(), true);
    QTRY_VERIFY(session->levels().value().preparedPreview);
    const LevelsEdit &edit = session->levels().value();
    QVERIFY(edit.previewImage(edit.layerID));
    QVERIFY(!edit.previewImage(QUuid::createUuid()));
    session->cancelLevels();
}

void LevelsEdgeTests::scaledSidesTruncateAsSwifts()
{
    // The long side sets the factor, tall or wide.
    for (const auto &[size, preview] : {std::pair(QSize(2, 9000), QSize(1, 8000)), std::pair(QSize(8311, 2), QSize(7999, 1))}) {
        EditorSession session;
        session.createDocument(size.width(), size.height());
        QImage image(size, QImage::Format_RGBA8888_Premultiplied);
        image.fill(Qt::red);
        session.insert(ImportedImage(image, image, QStringLiteral("Red")));
        session.beginLevels();
        QCOMPARE(session.levels().value().previewSource.size(), preview);
        session.cancelLevels();
    }
}

void LevelsEdgeTests::samplesStopAtTheDocumentAndClearPixels()
{
    // The ramp at twice its width reaches past the document.
    EditorSession session;
    session.createDocument(6, 1);
    const QImage source = image(ramp);
    session.insert(ImportedImage(source, source, QStringLiteral("Ramp")));
    LayerTransform wide = session.activeLayer().value().transform;
    wide.size = QSizeF(12, 1);
    session.beginTransform();
    session.previewTransform(wide);
    session.commitTransform();
    session.beginLevels();
    session.setLevelsSampleMode(LevelsSample::black);
    session.sampleLevels(QPointF(6, 0.5));
    QVERIFY(session.levels().value().settings.isIdentity());
    session.cancelLevels();
    // A clear pixel sets nothing; a soft one reads unpremultiplied.
    const std::unique_ptr<EditorSession> plain = rampSession();
    plain->beginLevels();
    plain->setLevelsSampleMode(LevelsSample::black);
    plain->sampleLevels(QPointF(5.5, 0.5));
    QVERIFY(plain->levels().value().settings.isIdentity());
    plain->sampleLevels(QPointF(4.5, 0.5));
    const LevelsSettings soft = plain->levels().value().settings;
    QVERIFY(soft.ranges[1].black == 127.5 && soft.ranges[2].black == 63.75 && soft.ranges[3].black == 0);
    plain->cancelLevels();
}

QTEST_MAIN(LevelsEdgeTests)
#include "LevelsEdgeTests.moc"
