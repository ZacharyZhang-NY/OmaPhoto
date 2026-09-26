#include "CanvasFixtures.h"
#include "CropFixtures.h"
#include "IO/ProjectController.h"
#include <QTemporaryDir>
#include <QtTest>

// The crop in the session: failures, turns, signals, hooks, steps.
class CropSessionTests : public QObject {
    Q_OBJECT
private slots:
    void aFailedCropSaysWhy();
    void aCropWaitsItsTurn();
    void theSettersAnnounceChangesAlone();
    void otherWorkEndsTheFrame();
    void aFlippedFrameCropsAsCoreGraphicsReadsIt();
    void aCropIsOneStepThatEndsRedoAndMarksTheProject();
};

void CropSessionTests::aFailedCropSaysWhy()
{
    EditorSession session;
    session.createDocument(100, 100);
    session.insert(filled(10, 10, qRgba(255, 0, 0, 255), "Red"));
    LayerTransform far = session.activeLayer().value().transform;
    far.origin = QPointF(999'000, 0);
    session.beginTransform();
    session.previewTransform(far);
    session.commitTransform();
    const std::optional<CanvasDocument> before = session.document();
    session.selectTool(NavigationTool::crop);
    session.setCropRect(QRectF(-5'000, 0, 50, 50));
    committed(session);
    QVERIFY(session.document() == before);
    QCOMPARE(session.cropError(), std::optional(QString::fromUtf8(ProjectError(ProjectError::Kind::tooLarge).what())));
    // The frame stays for another try; the project is free.
    QCOMPARE(session.cropRect(), std::optional(QRectF(-5'000, 0, 50, 50)));
    QVERIFY(!session.isProjectBusy());
    QVERIFY(session.canStartProjectOperation());
}

void CropSessionTests::aCropWaitsItsTurn()
{
    EditorSession session;
    session.createDocument(100, 100);
    session.selectTool(NavigationTool::crop);
    // Nothing drawn, or busy: refused, the callback still comes.
    session.cancelCrop();
    const QString step = session.history.undoName();
    QCOMPARE(committed(session), QRectF(0, 0, 100, 100));
    QCOMPARE(session.history.undoName(), step);
    session.setCropRect(QRectF(0, 0, 0.5, 10));
    QCOMPARE(committed(session), QRectF(0, 0, 100, 100));
    QCOMPARE(session.cropError(), std::nullopt);
    session.setCropRect(QRectF(0, 0, 50, 50));
    session.setIsProjectBusy(true);
    bool done = false;
    session.commitCrop([&] { done = true; });
    QVERIFY(!done);
    QTRY_VERIFY(done);
    QCOMPARE(session.document().value().width, 100);
    session.setIsProjectBusy(false);
    // Running, it holds the project; the step lands before `done`.
    done = false;
    session.commitCrop([&] {
        QCOMPARE(session.document().value().width, 50);
        QVERIFY(!session.isProjectBusy());
        done = true;
    });
    QVERIFY(session.isProjectBusy());
    QTRY_VERIFY(done);
    QCOMPARE(session.cropRect(), std::nullopt);
    QCOMPARE(session.visibleCropRect(), std::optional(QRectF(0, 0, 50, 50)));
    QVERIFY(session.canEditLayers());
}

void CropSessionTests::theSettersAnnounceChangesAlone()
{
    EditorSession session;
    session.createDocument(10, 10);
    int announced = 0;
    QObject::connect(&session, &EditorSession::changed, [&] { ++announced; });
    session.setCropRect(QRectF(1, 1, 5, 5));
    session.setCropRect(QRectF(1, 1, 5, 5));
    session.setCropRatioChoice("1:1");
    session.setCropRatioChoice("1:1");
    session.setCropError(QString("no"));
    session.setCropError(QString("no"));
    QCOMPARE(announced, 3);
    session.cancelCrop();
    session.cancelCrop();
    session.setCropError(std::nullopt);
    QCOMPARE(announced, 5);
    QCOMPARE(session.cropRect(), std::nullopt);
    QCOMPARE(session.cropError(), std::nullopt);
}

void CropSessionTests::otherWorkEndsTheFrame()
{
    QTemporaryDir folder;
    EditorSession session;
    session.createDocument(100, 100);
    session.insert(filled(10, 10, qRgba(255, 0, 0, 255), "Red"));
    // The tool opens freely on the whole canvas; layers rest.
    session.setCropRatioChoice("1:1");
    session.selectTool(NavigationTool::crop);
    QCOMPARE(session.cropRatioChoice(), QString("Free"));
    QCOMPARE(session.cropRect(), std::optional(QRectF(0, 0, 100, 100)));
    QVERIFY(!session.canEditLayers());
    // Chosen again, a drawn frame and its ratio stay.
    session.setCropRect(QRectF(5, 5, 20, 20));
    session.setCropRatioChoice("4:3");
    session.selectTool(NavigationTool::crop);
    QCOMPARE(session.cropRect(), std::optional(QRectF(5, 5, 20, 20)));
    QCOMPARE(session.cropRatioChoice(), QString("4:3"));
    const auto ends = [&](const char *what, const std::function<void()> &work) {
        session.selectTool(NavigationTool::crop);
        session.setCropRect(QRectF(5, 5, 20, 20));
        work();
        QVERIFY2(!session.cropRect(), what);
    };
    ends("a transform", [&] {
        session.beginTransform();
        session.cancelTransform();
    });
    ends("Levels", [&] {
        session.beginLevels();
        QVERIFY(session.levels());
        session.cancelLevels();
    });
    ends("a filter", [&] {
        session.beginFilter(FilterKind::gaussianBlur);
        QVERIFY(session.filterEdit());
        session.cancelFilter();
    });
    QImage picture(4, 4, QImage::Format_RGBA8888);
    picture.fill(Qt::blue);
    QVERIFY(picture.save(folder.filePath("Blue.png")));
    ends("an import", [&] {
        bool done = false;
        session.importImages({QUrl::fromLocalFile(folder.filePath("Blue.png"))}, std::nullopt, [&] { done = true; });
        QVERIFY(QTest::qWaitFor([&] { return done; }));
    });
    ProjectController controller(session);
    session.setProjectPath(folder.filePath("Crop.comp"));
    ends("a save", [&] {
        std::optional<bool> saved;
        controller.save(false, [&](bool value) { saved = value; });
        QVERIFY(QTest::qWaitFor([&] { return saved.has_value(); }));
        QVERIFY(saved.value());
    });
    ends("an undo", [&] { session.undo(); });
    const ProjectSnapshot snapshot = session.projectSnapshot().value();
    ends("an install", [&] { session.installProject(snapshot, folder.filePath("Crop.comp")); });
    ends("a close", [&] { session.clearProject(); });
}

void CropSessionTests::aFlippedFrameCropsAsCoreGraphicsReadsIt()
{
    EditorSession session;
    session.createDocument(64, 32);
    session.addBlankLayer();
    session.selectTool(NavigationTool::crop);
    session.setCropRect(QRectF(40, 20, -32, -16));
    QCOMPARE(committed(session), QRectF(0, 0, 32, 16));
    QCOMPARE(session.document().value().layers.front().origin(), QPointF(-8, -4));
}

void CropSessionTests::aCropIsOneStepThatEndsRedoAndMarksTheProject()
{
    EditorSession session;
    session.createDocument(64, 32);
    session.addBlankLayer();
    const CanvasDocument before = session.document().value();
    session.renameLayer(session.activeLayerID().value(), QStringLiteral("Renamed"));
    session.undo();
    session.history.markSaved();
    QVERIFY(session.canRedo() && !session.history.isModified());
    session.selectTool(NavigationTool::crop);
    session.setCropRect(QRectF(8, 4, 32, 16));
    committed(session);
    QVERIFY(!session.canRedo());
    QVERIFY(session.history.isModified());
    QCOMPARE(session.history.undoName(), QString("Crop"));
    session.undo();
    QVERIFY(session.document().value() == before);
    QVERIFY(!session.history.isModified());
}

QTEST_GUILESS_MAIN(CropSessionTests)
#include "CropSessionTests.moc"
