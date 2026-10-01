#include "CanvasContractFixtures.h"

// Compositor 1.4's GPU canvas contracts on the CPU canvas: adjustments.
namespace {
// A canvas over a photo and a smaller, translucent layer.
struct Scene : ContractScene {
    Scene(int width, int height, int canvasWidth, int canvasHeight) : ContractScene(width, height, canvasWidth, canvasHeight)
    {
        const QImage photo = noise(width, height, 3);
        session.insert(ImportedImage(photo, photo, QStringLiteral("Photo")));
        const QImage top = noise(width / 2, height / 2, 5, 160);
        session.insert(ImportedImage(top, top, QStringLiteral("Top")));
    }
    // An adjustment layer of this kind on top, at 80%.
    QUuid adjust(const LayerAdjustment &adjustment)
    {
        session.addAdjustment(adjustment.kind);
        session.setAdjustmentEditingID(std::nullopt);
        const QUuid id = session.activeLayerID().value();
        session.updateAdjustment(id, adjustment);
        session.setLayerOpacity(0.8);
        return id;
    }
};

LayerAdjustment visible(AdjustmentKind kind)
{
    LayerAdjustment adjustment{kind};
    switch (kind) {
    case AdjustmentKind::hsv: adjustment.hsvSettings = HueSaturationSettings(120, 30); break;
    case AdjustmentKind::levels: adjustment.levels.ranges[0].outputWhite = 160; break;
    case AdjustmentKind::curves: adjustment.curves.channels[0] = {{0, 20}, {120, 170}, {255, 235}}; break;
    case AdjustmentKind::exposure: adjustment.setExposure(ExposureSettings{.exposure = 0.8, .gamma = 1.2}); break;
    case AdjustmentKind::colorBalance: adjustment.setColorBalance(ColorBalanceSettings{.shadowYellowBlue = -30, .midCyanRed = 40}); break;
    case AdjustmentKind::gaussianBlur: adjustment.setGaussianRadius(6); break;
    case AdjustmentKind::motionBlur:
        adjustment.setResolvedMotionDistance(30);
        adjustment.setResolvedMotionAngle(30);
        break;
    case AdjustmentKind::addNoise:
        adjustment.setResolvedNoiseAmount(25);
        adjustment.setResolvedNoiseSeed(7);
        break;
    case AdjustmentKind::grain: adjustment.setGrain(GrainSettings{.amount = 60, .size = 3, .seed = 11}); break;
    default: break;
    }
    return adjustment;
}

}

class AdjustmentCanvasTests : public QObject {
    Q_OBJECT
private slots:
    void everyAdjustmentShowsAsTheExport_data();
    void everyAdjustmentShowsAsTheExport();
    void patternsStayWithTheDocument_data();
    void patternsStayWithTheDocument();
};

void AdjustmentCanvasTests::everyAdjustmentShowsAsTheExport_data()
{
    QTest::addColumn<AdjustmentKind>("kind");
    for (const AdjustmentKind kind : allAdjustmentKinds)
        QTest::newRow(qPrintable(rawValue(kind))) << kind;
}

// Swift's matchesEveryAdjustment: a mask, 80%, one to one.
void AdjustmentCanvasTests::everyAdjustmentShowsAsTheExport()
{
    QFETCH(AdjustmentKind, kind);
    Scene scene(60, 50, 60, 50);
    const QImage before = scene.exported();
    const QUuid id = scene.adjust(visible(kind));
    rewrite(scene.session, [&](ProjectSnapshot &snapshot) { setMask(snapshot, id, rampMask(60, 50)); });
    scene.session.zoom(1);
    QVERIFY(scene.exported() != before);
    QVERIFY2(scene.gapToExport() <= 1, qPrintable(QString::number(scene.gapToExport())));
}

void AdjustmentCanvasTests::patternsStayWithTheDocument_data()
{
    QTest::addColumn<AdjustmentKind>("kind");
    QTest::newRow("addNoise") << AdjustmentKind::addNoise;
    QTest::newRow("grain") << AdjustmentKind::grain;
}

// Swift's patternsStayWithTheDocument: wherever it sits, however zoomed.
void AdjustmentCanvasTests::patternsStayWithTheDocument()
{
    QFETCH(AdjustmentKind, kind);
    Scene scene(60, 50, 140, 120);
    scene.adjust(visible(kind));
    // One to one, and the crisp path's document pixels.
    for (const double zoom : {1.0, 2.0}) {
        scene.session.zoom(zoom);
        QVERIFY2(scene.gapToExport() <= 1, qPrintable(QStringLiteral("%1: %2").arg(zoom).arg(scene.gapToExport())));
        scene.session.viewport.translate(QSizeF(3, -2));
        QVERIFY2(scene.gapToExport() <= 1, qPrintable(QStringLiteral("%1 panned: %2").arg(zoom).arg(scene.gapToExport())));
    }
    // Reduced: a whole-pixel pan moves the pattern with the image.
    scene.session.zoom(0.8);
    const QImage before = scene.shot();
    const QPoint corner = scene.origin().toPoint();
    scene.session.viewport.translate(QSizeF(5, 3));
    const QImage after = scene.shot();
    QCOMPARE(scene.origin().toPoint(), corner + QPoint(5, 3));
    int worst = 0;
    for (int y = corner.y() + 2; y < corner.y() + 36; ++y) {
        for (int x = corner.x() + 2; x < corner.x() + 44; ++x) {
            const QRgb was = before.pixel(x, y), now = after.pixel(x + 5, y + 3);
            worst = std::max({worst, std::abs(qRed(was) - qRed(now)), std::abs(qGreen(was) - qGreen(now)), std::abs(qBlue(was) - qBlue(now))});
        }
    }
    QVERIFY2(worst <= 1, qPrintable(QString::number(worst)));
    // A partial repaint shows the whole's pattern, resampled alike.
    const QRect part(corner + QPoint(18, 12), QSize(20, 15));
    const QImage piece = scene.canvas.grab(part).toImage().convertToFormat(QImage::Format_ARGB32);
    QVERIFY(ContractScene::gap(piece, after.copy(part)) <= 2);
}

QTEST_MAIN(AdjustmentCanvasTests)
#include "AdjustmentCanvasTests.moc"
