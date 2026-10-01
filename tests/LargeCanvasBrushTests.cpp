#include "BrushFixtures.h"
#include "Document/EditorSession.h"
#include "IO/ImageExporter.h"
#include "SessionFixtures.h"
#include <cmath>

// Swift's LargeCanvasBrushTests: the largest Smear on a big canvas.
namespace {
// Eight coloured bands across a square canvas.
void bands(EditorSession &session, int side)
{
    session.createDocument(side, side);
    QImage image = BrushRaster::context(side, side, false);
    {
        QPainter painter(&image);
        for (int band = 0; band < 8; ++band)
            painter.fillRect(QRect(band * side / 8, 0, side / 8 + 1, side), QColor::fromRgbF(band / 7.0, 0.4, 1 - band / 7.0));
    }
    session.insert(ImportedImage(image, image, QStringLiteral("Bands")));
    session.selectTool(NavigationTool::blur);
    BrushSettings settings = session.brushSettings();
    settings.diameter = 2000;
    settings.hardness = 0.5;
    session.setBrushSettings(settings);
}

void stroke(EditorSession &session, int side)
{
    const double y = side / 2.0;
    session.beginBrush(QPointF(1000, y));
    for (int step = 1; step <= 20; ++step)
        session.continueBrush(QPointF(1000 + step * 100.0, y + (step % 3) * 40.0));
    session.finishBrushImmediately();
}
}

class LargeCanvasBrushTests : public QObject {
    Q_OBJECT
private slots:
    void largestBrushCommits_data();
    void largestBrushCommits();
    void aThinnedCommitLandsAllTheWarpMoved();
    void blurPiecesMatchTheWhole_data();
    void blurPiecesMatchTheWhole();
    void piecesAreCutOnceWhereTheBrushReaches();
};

void LargeCanvasBrushTests::largestBrushCommits_data()
{
    QTest::addColumn<BlurToolMode>("mode");
    QTest::newRow("liquify") << BlurToolMode::liquify;
    QTest::newRow("smudge") << BlurToolMode::smudge;
    QTest::newRow("blur") << BlurToolMode::blur;
}

// The commit's tip runs past Size's 2000, once refused.
void LargeCanvasBrushTests::largestBrushCommits()
{
    QFETCH(BlurToolMode, mode);
    EditorSession session;
    bands(session, 5000);
    session.setBlurMode(mode);
    BrushSettings settings = session.brushSettings();
    settings.blurRadius = 20;
    session.setBrushSettings(settings);
    const ImageIdentity before = session.activeLayer().value().asset.value().identity();
    stroke(session, 5000);
    QCOMPARE(session.brushError(), std::optional<QString>());
    QVERIFY(session.activeLayer().value().asset.value().identity() != before);
    QCOMPARE(session.history.undoName(), rawValue(mode));
}

// A point every twentieth of the tip covers the warp.
void LargeCanvasBrushTests::aThinnedCommitLandsAllTheWarpMoved()
{
    EditorSession session;
    bands(session, 400);
    session.setBlurMode(BlurToolMode::liquify);
    BrushSettings settings = session.brushSettings();
    settings.diameter = 160;
    settings.hardness = 1;
    session.setBrushSettings(settings);
    // Hard to its rim, ending a rim before a band.
    session.beginBrush(QPointF(60, 200));
    session.continueBrush(QPointF(273, 213));
    const QImage live = session.warpStroke()->image().copy();
    QCOMPARE(int(session.warpStroke()->points().size()), 54);
    session.finishBrush();
    const QImage landed = ImageExporter::render(session.projectSnapshot().value()).image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    int apart = 0;
    for (int y = 0; y < 400; ++y)
        apart += std::memcmp(landed.constScanLine(y), live.constScanLine(y), 400 * 4) != 0;
    QCOMPARE(apart, 0);
    QCOMPARE(session.history.undoName(), QString("Liquify"));
    // A tight circle: chords between kept points stay close.
    session.beginBrush(QPointF(260, 200));
    for (int step = 1; step <= 72; ++step)
        session.continueBrush(QPointF(200 + 60 * std::cos(step * M_PI / 36), 200 + 60 * std::sin(step * M_PI / 36)));
    const QImage circled = session.warpStroke()->image().copy();
    session.finishBrush();
    const QImage turned = ImageExporter::render(session.projectSnapshot().value()).image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    for (int y = 0; y < 400; ++y)
        apart += std::memcmp(turned.constScanLine(y), circled.constScanLine(y), 400 * 4) != 0;
    QCOMPARE(apart, 0);
}

void LargeCanvasBrushTests::blurPiecesMatchTheWhole_data()
{
    QTest::addColumn<bool>("mask");
    QTest::newRow("pixels") << false;
    QTest::newRow("mask") << true;
}

// Swift's test: each piece is its part of the whole.
void LargeCanvasBrushTests::blurPiecesMatchTheWhole()
{
    QFETCH(bool, mask);
    EditorSession session;
    bands(session, 600);
    const QUuid id = session.activeLayerID().value();
    QImage stripes(600, 600, QImage::Format_Grayscale8);
    for (int y = 0; y < 600; ++y)
        for (int x = 0; x < 600; ++x)
            stripes.scanLine(y)[x] = (x / 37 + y / 23) % 2 ? 255 : 0;
    rewrite(session, [&](ProjectSnapshot &snapshot) { setMask(snapshot, id, LayerMask::assetFrom(stripes)); });
    BrushSettings settings = session.brushSettings();
    settings.blurRadius = 12;
    session.setBrushSettings(settings);
    const ImageLayer layer = session.activeLayer().value();
    const std::unique_ptr<BrushStroke> stroke =
        mask ? std::make_unique<BrushStroke>(layer, true, settings, QSizeF(600, 600)) : session.makeRasterEdit(layer, settings);
    const BrushStroke::Clone blur = session.blurSample(*stroke).value();
    const QImage whole = blur.render(blur.image.rect());
    // The sample reaches three sigmas past the layer.
    QCOMPARE(whole.size(), QSize(672, 672));
    const int width = blur.image.width(), height = blur.image.height();
    for (const QRect part : {QRect(0, 0, 256, 256), QRect(250, 300, 260, 180), QRect(width - 100, height - 70, 100, 70)}) {
        const QImage piece = blur.render(part);
        QCOMPARE(piece.size(), part.size());
        QCOMPARE(piece, whole.copy(part));
    }
}

// Only tiles the brush reaches are blurred, each once.
void LargeCanvasBrushTests::piecesAreCutOnceWhereTheBrushReaches()
{
    QImage image = BrushRaster::context(1000, 1000, false);
    image.fill(Qt::red);
    const ImageLayer layer(ImportedImage(image, image, QStringLiteral("Red")), QPointF(0, 0));
    BrushStroke stroke(layer, false, brush(20, 1, 0, 0, 0), QSizeF(1000, 1000));
    std::vector<QRect> parts;
    stroke.setClone(BrushStroke::Clone{image, QRectF(0, 0, 1000, 1000), false, [&](const QRect &part) {
                                           parts.push_back(part);
                                           return image.copy(part);
                                       }});
    stroke.isBlur = true;
    stroke.append(QPointF(100, 100));
    stroke.append(QPointF(120, 100));
    stroke.flush();
    QCOMPARE(int(parts.size()), 1);
    QCOMPARE(parts[0], QRect(0, 0, 258, 258));
    // Over the same tile again: nothing new is cut.
    stroke.append(QPointF(100, 110));
    stroke.flush();
    QCOMPARE(int(parts.size()), 1);
    // Into the next tile: its piece, two pixels round.
    stroke.append(QPointF(300, 110));
    stroke.flush();
    QCOMPARE(int(parts.size()), 2);
    QCOMPARE(parts[1], QRect(254, 0, 260, 258));
    // A new clone drops what was cut.
    stroke.setClone(stroke.clone());
    stroke.append(QPointF(310, 110));
    stroke.flush();
    QCOMPARE(int(parts.size()), 3);
    QCOMPARE(parts[2], parts[1]);
    // Past the clone, a tile is neither cut nor painted.
    BrushStroke past(layer, false, brush(20, 1, 0, 0, 0), QSizeF(1000, 1000));
    QImage blue = BrushRaster::context(200, 200, false);
    blue.fill(Qt::blue);
    parts.clear();
    past.setClone(BrushStroke::Clone{blue, QRectF(0, 0, 200, 200), false, [&](const QRect &part) {
                                         parts.push_back(part);
                                         return blue.copy(part);
                                     }});
    past.isBlur = true;
    past.append(QPointF(400, 400));
    past.append(QPointF(420, 400));
    past.flush();
    QVERIFY(parts.empty());
    QVERIFY(!past.patches().empty());
    QCOMPARE(pixel(preview(past, QSizeF(1000, 1000)), 410, 400), (std::vector<int>{255, 0, 0, 255}));
}

QTEST_GUILESS_MAIN(LargeCanvasBrushTests)
#include "LargeCanvasBrushTests.moc"
