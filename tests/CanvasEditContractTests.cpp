#include "CanvasContractFixtures.h"

// Compositor 1.4's GPU canvas contracts on the CPU canvas: edits.
namespace {
QUuid insertNoise(ContractScene &scene, int width, int height, quint32 seed, QPointF centre, int alpha = 255)
{
    const QImage image = noise(width, height, seed, alpha);
    scene.session.insert(ImportedImage(image, image, QStringLiteral("Noise")), centre);
    return scene.session.activeLayerID().value();
}

QPainterPath ellipse(const QRectF &rect)
{
    QPainterPath path;
    path.addEllipse(rect);
    return path;
}

bool finished(const std::function<void(std::function<void()>)> &start)
{
    bool done = false;
    start([&done] { done = true; });
    return QTest::qWaitFor([&done] { return done; }, 20000);
}
}

class CanvasEditContractTests : public QObject {
    Q_OBJECT
private slots:
    void aNewMaskRevealsTheWholeLayer_data();
    void aNewMaskRevealsTheWholeLayer();
    void movingPixelsShowAsTheirCommit_data();
    void movingPixelsShowAsTheirCommit();
    void aGradientInASelectionShowsAsItsCommit();
    void aGradientThatCannotFillCancels();
    void aLiveMaskFromFurtherDownShowsAsTheExport();
    void aDraftOnAStacksBaseSitsInsideTheStack();
};

void CanvasEditContractTests::aNewMaskRevealsTheWholeLayer_data()
{
    QTest::addColumn<double>("rotation");
    QTest::newRow("upright") << 0.0;
    QTest::newRow("turned") << 25.0;
}

// Swift's aNewMaskRevealsTheWholeLayer: one white pixel, stretched.
void CanvasEditContractTests::aNewMaskRevealsTheWholeLayer()
{
    QFETCH(double, rotation);
    ContractScene scene(80, 70, 80, 70);
    const QUuid id = insertNoise(scene, 30, 20, 2, QPointF(40, 35));
    rewrite(scene.session, [&](ProjectSnapshot &snapshot) {
        record(snapshot, id).transform = LayerTransform{.origin = {10, 12}, .size = {38, 30}, .rotation = rotation};
    });
    scene.session.zoom(1);
    scene.session.selectLayer(id);
    const QImage before = scene.exported();
    scene.session.addLayerMask();
    QCOMPARE(scene.session.activeLayer().value().mask.value().asset.size(), QSize(1, 1));
    const QImage after = scene.exported();
    // Wherever the layer covered its pixel, nothing changes.
    int covered = 0;
    for (int y = 0; y < after.height(); ++y) {
        for (int x = 0; x < after.width(); ++x) {
            if (qAlpha(before.pixel(x, y)) < 255)
                continue;
            ++covered;
            QVERIFY2(ContractScene::gap(before.copy(x - 1, y - 1, 3, 3), after.copy(x - 1, y - 1, 3, 3)) <= 1,
                     qPrintable(QStringLiteral("%1, %2").arg(x).arg(y)));
        }
    }
    QVERIFY(covered > 900);
    QVERIFY2(scene.gapToExport() <= 1, qPrintable(QString::number(scene.gapToExport())));
}

void CanvasEditContractTests::movingPixelsShowAsTheirCommit_data()
{
    QTest::addColumn<bool>("duplicate");
    QTest::newRow("moving") << false;
    QTest::newRow("duplicating") << true;
}

// Swift's matchesWhileMovingPixels, on a masked layer at 60%.
void CanvasEditContractTests::movingPixelsShowAsTheirCommit()
{
    QFETCH(bool, duplicate);
    ContractScene scene(80, 60, 80, 60);
    const QUuid id = insertNoise(scene, 80, 60, 2, QPointF(40, 30));
    rewrite(scene.session, [&](ProjectSnapshot &snapshot) {
        setMask(snapshot, id, rampMask(80, 60));
        record(snapshot, id).opacity = 0.6;
    });
    scene.session.zoom(1);
    scene.session.selectLayer(id);
    const QImage still = scene.documentShot();
    scene.session.applySelection(ellipse(QRectF(20, 15, 30, 24)), SelectionMode::replace, QStringLiteral("Select"));
    QVERIFY(scene.session.beginPixelMove(duplicate));
    scene.session.movePixels(QSizeF(9.4, 5.6));
    const QImage moving = scene.documentShot();
    QVERIFY(finished([&](std::function<void()> done) { scene.session.finishPixelMove(std::move(done)); }));
    const QImage moved = scene.documentShot();
    // The ants run round the moved outline: left out.
    const QPainterPath ants = ContractScene::band(ellipse(QRectF(29, 21, 30, 24)));
    QVERIFY(ContractScene::gap(still, moving, ants) > 20);
    QVERIFY2(ContractScene::gap(moving, moved, ants) <= 1, qPrintable(QString::number(ContractScene::gap(moving, moved, ants))));
    QVERIFY(scene.gapToExport(ants) <= 1);
    // A move leaves a hole; a duplicate leaves its pixels.
    const QRgb left = scene.exported().pixel(24, 27);
    QCOMPARE(qAlpha(left) == 0, !duplicate);
}

// Swift's matchesWhileDraggingAGradient: inside a selection, at 70%.
void CanvasEditContractTests::aGradientInASelectionShowsAsItsCommit()
{
    ContractScene scene(80, 60, 80, 60);
    insertNoise(scene, 80, 60, 2, QPointF(40, 30));
    scene.session.zoom(1);
    const QImage still = scene.documentShot();
    const QPainterPath outline = ellipse(QRectF(15, 10, 40, 30));
    scene.session.applySelection(outline, SelectionMode::replace, QStringLiteral("Select"));
    GradientSettings settings;
    settings.opacity = 0.7;
    settings.style = GradientStyle::foregroundToBackground;
    scene.session.setGradientSettings(settings);
    scene.session.selectTool(NavigationTool::gradient);
    scene.session.beginGradient(QPointF(10, 30));
    scene.session.moveGradient(std::nullopt, QPointF(70, 30));
    scene.session.endGradientDrag();
    const QImage pending = scene.documentShot();
    QVERIFY(finished([&](std::function<void()> done) { scene.session.commitGradient(std::move(done)); }));
    const QImage committed = scene.documentShot();
    // The ants, the gradient's line and its ends: overlays.
    QPainterPath line;
    line.moveTo(10, 30);
    line.lineTo(70, 30);
    const QPainterPath overlays = ContractScene::band(outline).united(ContractScene::band(line, 30));
    QVERIFY(ContractScene::gap(still, pending, overlays) > 20);
    QVERIFY2(ContractScene::gap(pending, committed, overlays) <= 1, qPrintable(QString::number(ContractScene::gap(pending, committed, overlays))));
    // Outside the selection the photo stays.
    QCOMPARE(scene.exported().pixel(3, 3), still.pixel(3, 3));
}

// Swift's 5708ed3: a fill that fails cancels with the error.
void CanvasEditContractTests::aGradientThatCannotFillCancels()
{
    ContractScene scene(80, 60, 80, 60);
    insertNoise(scene, 80, 60, 2, QPointF(40, 30));
    const QImage before = scene.exported();
    const int count = scene.session.history.undoCount();
    scene.session.selectTool(NavigationTool::gradient);
    scene.session.beginGradient(QPointF(10, 30));
    QVERIFY(scene.session.gradientEdit());
    scene.session.gradientEdit()->raster->pixelLimit = 100;
    scene.session.moveGradient(std::nullopt, QPointF(70, 30));
    QVERIFY(!scene.session.gradientEdit() && scene.session.brushError().has_value());
    QCOMPARE(scene.session.history.undoCount(), count);
    QCOMPARE(scene.exported(), before);
}

// Swift's matchesALiveMaskOutsideAClippingStack: a turned, distant source.
void CanvasEditContractTests::aLiveMaskFromFurtherDownShowsAsTheExport()
{
    ContractScene scene(80, 60, 80, 60);
    insertNoise(scene, 80, 60, 2, QPointF(40, 30));
    const QUuid source = insertNoise(scene, 40, 30, 4, QPointF(35, 30), 140);
    rewrite(scene.session, [&](ProjectSnapshot &snapshot) { record(snapshot, source).transform.rotation = 25; });
    insertNoise(scene, 16, 16, 5, QPointF(60, 20));
    const QUuid target = insertNoise(scene, 50, 40, 9, QPointF(40, 30));
    const QImage unlinked = scene.exported();
    QVERIFY(scene.session.linkMask(source, target));
    scene.session.zoom(1);
    QVERIFY(ContractScene::gap(unlinked, scene.exported()) > 20);
    QVERIFY2(scene.gapToExport() <= 1, qPrintable(QString::number(scene.gapToExport())));
}

// The draft follows its active base into the stack's group.
void CanvasEditContractTests::aDraftOnAStacksBaseSitsInsideTheStack()
{
    ContractScene scene(60, 40, 60, 40);
    const QImage white = solid(30, 40, qRgba(255, 255, 255, 255));
    scene.session.insert(ImportedImage(white, white, QStringLiteral("Base")), QPointF(15, 20));
    const QUuid base = scene.session.activeLayerID().value();
    const QImage red = solid(10, 40, qRgba(255, 0, 0, 255));
    scene.session.insert(ImportedImage(red, red, QStringLiteral("Clipped")), QPointF(5, 20));
    QVERIFY(scene.session.linkMask(base, scene.session.activeLayerID().value()));
    scene.session.zoom(1);
    scene.session.selectLayer(base);
    scene.session.selectTool(NavigationTool::shape);
    scene.session.beginShape(QPointF(5, 10));
    scene.session.dragShape(QPointF(55, 30), false, false);
    const QImage shown = scene.documentShot();
    // The draft joins the base, its alpha included, as Swift's.
    QCOMPARE(shown.pixel(20, 20), qRgb(0, 0, 0));
    QCOMPARE(shown.pixel(45, 20), qRgb(0, 0, 0));
    // The clipped layer stays over it.
    QCOMPARE(shown.pixel(5, 20), qRgb(255, 0, 0));
    scene.session.cancelShape();
}

QTEST_MAIN(CanvasEditContractTests)
#include "CanvasEditContractTests.moc"
