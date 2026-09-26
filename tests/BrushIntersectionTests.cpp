#include "BrushFixtures.h"
#include "Document/EditorSession.h"
#include "IO/ImageExporter.h"
#include <QDir>

// Self-crossings, spacing by distance, the opacity cap, idempotent flush.
namespace {
std::unique_ptr<BrushStroke> stroke(double diameter = 120, double opacity = 1)
{
    return std::make_unique<BrushStroke>(blankLayer(800, 800), false, brush(diameter, 0, 1, 1, 1, opacity), QSizeF(800, 800));
}

void trace(BrushStroke &stroke, const std::vector<QPointF> &points, double step = 12)
{
    stroke.append(points[0]);
    for (size_t i = 1; i < points.size(); ++i) {
        const QPointF a = points[i - 1], b = points[i];
        const int count = std::max(1, int(std::ceil(std::hypot(b.x() - a.x(), b.y() - a.y()) / step)));
        for (int index = 1; index <= count; ++index) {
            const double t = double(index) / count;
            stroke.append(QPointF(a.x() + (b.x() - a.x()) * t, a.y() + (b.y() - a.y()) * t));
        }
    }
    stroke.flush();
}

QImage raster(const BrushStroke &stroke)
{
    return preview(stroke, QSizeF(800, 800));
}
}

class BrushIntersectionTests : public QObject {
    Q_OBJECT
private slots:
    void selfCrossingsBlendInsteadOfTakingTheStrongestEdge();
    void accumulationDependsOnDistanceNotEventCount();
    void softCrossingsRespectStrokeOpacityAndFlushIsIdempotent();
    void exportCrossingExample();
};

void BrushIntersectionTests::selfCrossingsBlendInsteadOfTakingTheStrongestEdge()
{
    const auto vertical = stroke(), horizontal = stroke(), crossing = stroke();
    trace(*vertical, {QPointF(400, 100), QPointF(400, 700)});
    trace(*horizontal, {QPointF(700, 400), QPointF(100, 400)});
    trace(*crossing, {QPointF(400, 100), QPointF(400, 700), QPointF(700, 700), QPointF(700, 400), QPointF(100, 400)});
    const QImage v = raster(*vertical), h = raster(*horizontal), c = raster(*crossing);
    for (int offset : {45, 48, 51}) {
        const int a = alpha(v, 400 + offset, 400 + offset), b = alpha(h, 400 + offset, 400 + offset);
        const int actual = alpha(c, 400 + offset, 400 + offset);
        const int expected = 255 - (255 - a) * (255 - b) / 255;
        QVERIFY2(actual > std::max(a, b) + 10, qPrintable(QStringLiteral("offset %1: %2 over %3 and %4").arg(offset).arg(actual).arg(a).arg(b)));
        QVERIFY2(std::abs(actual - expected) <= 3, qPrintable(QStringLiteral("offset %1: %2 vs %3").arg(offset).arg(actual).arg(expected)));
    }
}

void BrushIntersectionTests::accumulationDependsOnDistanceNotEventCount()
{
    for (double diameter : {12.0, 120.0, 520.0}) {
        const auto sparse = stroke(diameter), dense = stroke(diameter);
        const std::vector<QPointF> points{QPointF(60, 400), QPointF(740, 400)};
        trace(*sparse, points, 1000);
        trace(*dense, points, 5);
        const QImage a = raster(*sparse), b = raster(*dense);
        for (int y = 400; y < std::min(800, 400 + int(diameter / 2)); ++y)
            QVERIFY2(std::abs(alpha(a, 400, y) - alpha(b, 400, y)) <= 2, qPrintable(QStringLiteral("diameter %1, row %2").arg(diameter).arg(y)));
    }
}

void BrushIntersectionTests::softCrossingsRespectStrokeOpacityAndFlushIsIdempotent()
{
    const auto paint = stroke(120, 0.4);
    trace(*paint, {QPointF(400, 100), QPointF(400, 700), QPointF(700, 700), QPointF(700, 400), QPointF(100, 400)});
    const QImage first = raster(*paint);
    QCOMPARE(alpha(first, 400, 400), 102);
    for (int y = 0; y < 800; ++y) {
        const uchar *row = first.constScanLine(y);
        for (int x = 0; x < 800; ++x)
            QVERIFY(row[x * 4 + 3] <= 102);
    }
    paint->flush();
    const QImage second = raster(*paint);
    QCOMPARE(second, first);
}

// Swift's picture of a 520 px crossing, with BRUSH_BENCHMARK=1.
void BrushIntersectionTests::exportCrossingExample()
{
    if (qEnvironmentVariable("BRUSH_BENCHMARK") != QStringLiteral("1"))
        QSKIP("BRUSH_BENCHMARK=1 exports the crossing");
    EditorSession session;
    session.createDocument(4000, 4000);
    QImage black = BrushRaster::context(4000, 4000, false);
    black.fill(Qt::black);
    session.insert(ImportedImage(black, black, "Crossing"));
    session.selectTool(NavigationTool::brush);
    session.setBrushSettings(brush(520, 0, 1, 1, 1));
    const std::vector<QPointF> path = {{1600, 700}, {1750, 3300}, {2900, 2400}, {3150, 1800}, {1600, 1700}, {450, 2000}};
    session.beginBrush(path[0]);
    for (size_t i = 1; i < path.size(); ++i) {
        for (int step = 1; step <= 60; ++step) {
            const double t = step / 60.0;
            session.continueBrush(path[i - 1] + (path[i] - path[i - 1]) * t);
        }
    }
    QVERIFY(session.finishBrushImmediately());
    ImageExporter::exportPNG(session.projectSnapshot().value(), QDir::temp().filePath(QStringLiteral("compositor-brush-crossing.png")));
}

QTEST_GUILESS_MAIN(BrushIntersectionTests)
#include "BrushIntersectionTests.moc"
