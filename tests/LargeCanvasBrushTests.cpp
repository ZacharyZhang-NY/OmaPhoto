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
};

void LargeCanvasBrushTests::largestBrushCommits_data()
{
    QTest::addColumn<BlurToolMode>("mode");
    QTest::newRow("liquify") << BlurToolMode::liquify;
    QTest::newRow("smudge") << BlurToolMode::smudge;
}

// The commit's tip runs past Size's 2000, once refused.
void LargeCanvasBrushTests::largestBrushCommits()
{
    QFETCH(BlurToolMode, mode);
    EditorSession session;
    bands(session, 5000);
    session.setBlurMode(mode);
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

QTEST_GUILESS_MAIN(LargeCanvasBrushTests)
#include "LargeCanvasBrushTests.moc"
