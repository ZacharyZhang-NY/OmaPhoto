#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include "UI/CameraRawColorControls.h"
#include <QComboBox>
#include <QLabel>
#include <QToolButton>
#include <QtTest>

// Swift 1.4.5: the Camera Raw curve graph's one drag.
namespace {
struct Graph {
    EditorSession session;
    std::unique_ptr<CameraRawCurveControls> controls;
    QWidget *graph = nullptr;
    explicit Graph(bool point = false)
    {
        session.createDocument(8, 8);
        QImage image = BrushRaster::context(8, 8, false);
        image.fill(QColor(200, 120, 60));
        session.insert(ImportedImage(image, image, QStringLiteral("Warm")));
        session.beginFilter(FilterKind::cameraRaw);
        controls = std::make_unique<CameraRawCurveControls>(session);
        controls->resize(374, 600);
        controls->show();
        graph = controls->findChild<QWidget *>(QStringLiteral("cameraRawCurveGraph"));
        if (point)
            controls->findChild<QToolButton *>(QStringLiteral("curvePage1"))->click();
    }
    const CameraRawCurveSettings &curve() const { return session.filterEdit().value().settings.cameraRaw.curve; }
    void set(const std::function<void(CameraRawCurveSettings &)> &change)
    {
        FilterSettings settings = session.filterEdit().value().settings;
        change(settings.cameraRaw.curve);
        session.updateFilter(settings, true);
    }
    // A curve position in the graph's own points.
    QPoint at(double x, double y) const { return QPoint(int(std::lround(x * graph->width())), int(std::lround((1 - y) * graph->height()))); }
    void press(QPoint where, Qt::MouseButton button = Qt::LeftButton) const { QTest::mousePress(graph, button, {}, where); }
    void move(QPoint where, Qt::MouseButtons buttons = Qt::LeftButton) const
    {
        QMouseEvent event(QEvent::MouseMove, where, graph->mapToGlobal(where), Qt::NoButton, buttons, Qt::NoModifier);
        QApplication::sendEvent(graph, &event);
    }
    void release(QPoint where) const { QTest::mouseRelease(graph, Qt::LeftButton, {}, where); }
    QLabel &readout() const { return *controls->findChild<QLabel *>(QStringLiteral("curveSelectedPoint")); }
    QColor dot(double x, double y) const { return graph->grab().toImage().pixelColor(at(x, y)); }
};
}

class CameraRawCurveGraphTests : public QObject {
    Q_OBJECT
private slots:
    void aRegionDragLiftsOrLowersThePressedRegion();
    void theRegionIsPickedOnceByItsTone();
    void dividersMoveInTheBottomStripAndStayOrdered();
    void aPointIsPickedByDistanceAndKept();
    void aPressElsewhereAddsAndDragsAPoint();
    void addingIsRefusedNearAnEdgeAPointOrSixteen();
    void theChosenPointIsLitAndReadOut();
    void aNewChannelOrPresetDropsTheChoice();
    void aDoubleClickRemovesAnInnerPointAndTheChoice();
    void aDoubleClickReachesFourHundredthsByX();
    void aDoubleClickInTheStripMovesADivider();
    void onlyTheLeftButtonDragsAndAPressStartsAfresh();
    void theGraphSaysWhatADragDoes();
};

void CameraRawCurveGraphTests::aRegionDragLiftsOrLowersThePressedRegion()
{
    Graph graph;
    const int h = graph.graph->height();
    graph.set([](CameraRawCurveSettings &curve) { curve.lights = 10; });
    // Up lifts: a quarter height adds 50.
    graph.press(QPoint(int(graph.graph->width() * 0.6), 100));
    graph.move(QPoint(int(graph.graph->width() * 0.6), 100 - h / 4));
    QCOMPARE(graph.curve().lights, std::round(10 + double(h / 4) / h * 200));
    // Down lowers, clamped at −100, rounded to whole.
    graph.move(QPoint(int(graph.graph->width() * 0.6), 100 + h));
    QCOMPARE(graph.curve().lights, -100.0);
    graph.move(QPoint(10, 100 + 3));
    QCOMPARE(graph.curve().lights, std::round(10 - 3.0 / h * 200));
    graph.release(QPoint(10, 103));
    QVERIFY(graph.curve().shadows == 0 && graph.curve().darks == 0 && graph.curve().highlights == 0);
    // Far up, +100 at most.
    graph.press(QPoint(int(graph.graph->width() * 0.9), 120));
    graph.move(QPoint(int(graph.graph->width() * 0.9), 120 - 4 * h));
    QCOMPARE(graph.curve().highlights, 100.0);
}

void CameraRawCurveGraphTests::theRegionIsPickedOnceByItsTone()
{
    Graph graph;
    const int w = graph.graph->width();
    graph.set([](CameraRawCurveSettings &curve) { curve.shadowSplit = 20; });
    // A tone on a divider joins the region above.
    const std::pair<double, double CameraRawCurveSettings::*> regions[] = {
        {0.1, &CameraRawCurveSettings::shadows}, {0.2, &CameraRawCurveSettings::darks}, {0.5, &CameraRawCurveSettings::lights}, {0.75, &CameraRawCurveSettings::highlights}};
    for (const auto &[tone, key] : regions) {
        const CameraRawCurveSettings before = graph.curve();
        graph.press(QPoint(int(std::lround(tone * w)), 60));
        graph.move(QPoint(int(std::lround(tone * w)), 50));
        graph.release(QPoint(int(std::lround(tone * w)), 50));
        CameraRawCurveSettings expected = before;
        expected.*key = std::round(10.0 / graph.graph->height() * 200);
        QVERIFY2(graph.curve() == expected, qPrintable(QString::number(tone)));
    }
    // At tone 50 exactly, each divider hands it upward.
    QCOMPARE(w % 2, 0);
    const std::tuple<std::array<double, 3>, double CameraRawCurveSettings::*> onDividers[] = {
        {{50, 60, 80}, &CameraRawCurveSettings::darks}, {{20, 50, 80}, &CameraRawCurveSettings::lights}, {{20, 30, 50}, &CameraRawCurveSettings::highlights}};
    for (const auto &[splits, key] : onDividers) {
        graph.set([&splits](CameraRawCurveSettings &curve) {
            curve = CameraRawCurveSettings();
            curve.shadowSplit = splits[0];
            curve.darkSplit = splits[1];
            curve.lightSplit = splits[2];
        });
        graph.press(QPoint(w / 2, 60));
        graph.move(QPoint(w / 2, 50));
        graph.release(QPoint(w / 2, 50));
        QVERIFY(graph.curve().*key > 0);
    }
    // Kept through the drag: across another region, the same one.
    graph.set([](CameraRawCurveSettings &curve) { curve.shadows = curve.darks = curve.lights = curve.highlights = 0; });
    graph.press(QPoint(int(0.1 * w), 60));
    graph.move(QPoint(int(0.9 * w), 40));
    QVERIFY(graph.curve().shadows > 0 && graph.curve().highlights == 0);
}

void CameraRawCurveGraphTests::dividersMoveInTheBottomStripAndStayOrdered()
{
    Graph graph;
    const int w = graph.graph->width(), h = graph.graph->height();
    // The strip is the bottom 18 points.
    graph.press(QPoint(int(0.5 * w), h - 18));
    graph.move(QPoint(int(0.6 * w), h - 30));
    graph.release(QPoint(int(0.6 * w), h - 30));
    QCOMPARE(graph.curve().darkSplit, 50.0);
    QVERIFY(graph.curve().lights > 0);
    graph.set([](CameraRawCurveSettings &curve) { curve.lights = 0; });
    // Each divider, the nearest to the press, between its neighbours.
    const auto dragged = [&](double from, double to) {
        graph.press(QPoint(int(std::lround(from * w)), h - 17));
        graph.move(QPoint(int(std::lround(to * w)), 10));
        graph.release(QPoint(int(std::lround(to * w)), 10));
    };
    dragged(0.3, 0.9);
    QCOMPARE(graph.curve().shadowSplit, 48.0);
    // Past 2 and 98 the settings' own limits hold.
    dragged(0.47, 0.0);
    QCOMPARE(graph.curve().shadowSplit, 5.0);
    dragged(0.55, 0.9);
    QCOMPARE(graph.curve().darkSplit, 73.0);
    dragged(0.73, 0.01);
    QCOMPARE(graph.curve().darkSplit, 7.0);
    dragged(0.74, 0.0);
    QCOMPARE(graph.curve().lightSplit, 9.0);
    dragged(0.095, 1.2);
    QCOMPARE(graph.curve().lightSplit, 98.0);
    QVERIFY(graph.curve().shadows == 0 && graph.curve().darks == 0 && graph.curve().lights == 0 && graph.curve().highlights == 0);
    // At tone 50 exactly, between 40 and 60: the first.
    QCOMPARE(w % 2, 0);
    graph.set([](CameraRawCurveSettings &curve) {
        curve.shadowSplit = 40;
        curve.darkSplit = 60;
        curve.lightSplit = 80;
    });
    graph.press(QPoint(w / 2, h - 5));
    graph.move(QPoint(int(0.45 * w), h - 5));
    QVERIFY(std::abs(graph.curve().shadowSplit - 45) < 0.5 && graph.curve().darkSplit == 60);
}

void CameraRawCurveGraphTests::aPointIsPickedByDistanceAndKept()
{
    Graph graph(true);
    graph.set([](CameraRawCurveSettings &curve) { curve.rgb = {{0, 0}, {0.3, 0.3}, {0.36, 0.5}, {1, 1}}; });
    // Nearer by distance, not by x: the second.
    graph.press(graph.at(0.33, 0.48));
    graph.move(graph.at(0.33, 0.6));
    QCOMPARE(graph.curve().rgb.size(), size_t(4));
    QVERIFY(std::abs(graph.curve().rgb[2].y - 0.6) < 0.01 && graph.curve().rgb[1] == (CurvePoint{0.3, 0.3}));
    // Kept as it passes x 0.99, held before the end.
    graph.move(graph.at(1.2, 0.2));
    QVERIFY(std::abs(graph.curve().rgb[2].x - 0.99) < 1e-6 && graph.curve().rgb[1] == (CurvePoint{0.3, 0.3}));
    // Fix beyond Swift: onto its neighbour it stays, still dragged.
    graph.move(graph.at(0.1, 0.2));
    QCOMPARE(graph.curve().rgb.size(), size_t(4));
    QVERIFY(std::abs(graph.curve().rgb[2].x - 0.31) < 1e-6 && graph.curve().rgb[1] == (CurvePoint{0.3, 0.3}));
    graph.move(graph.at(0.6, 0.7));
    QCOMPARE(graph.curve().rgb.size(), size_t(4));
    QVERIFY(std::abs(graph.curve().rgb[2].x - 0.6) < 0.003 && graph.curve().rgb[3] == (CurvePoint{1, 1}));
    graph.release(graph.at(0.6, 0.7));
    // Onto the next point: a hundredth short; both stay.
    graph.set([](CameraRawCurveSettings &curve) { curve.rgb = {{0, 0}, {0.3, 0.3}, {0.5, 0.5}, {1, 1}}; });
    graph.press(graph.at(0.3, 0.3));
    graph.move(graph.at(0.8, 0.3));
    graph.release(graph.at(0.8, 0.3));
    QCOMPARE(graph.curve().rgb.size(), size_t(4));
    QVERIFY(std::abs(graph.curve().rgb[1].x - 0.49) < 1e-6 && graph.curve().rgb[2] == (CurvePoint{0.5, 0.5}));
    // 0.05 away takes the point; 0.06 away adds one.
    graph.set([](CameraRawCurveSettings &curve) { curve.rgb = {{0, 0}, {0.5, 0.5}, {1, 1}}; });
    graph.press(graph.at(0.53, 0.54));
    graph.release(graph.at(0.53, 0.54));
    QCOMPARE(graph.curve().rgb.size(), size_t(3));
    QVERIFY(std::abs(graph.curve().rgb[1].x - 0.53) < 0.003);
    graph.set([](CameraRawCurveSettings &curve) { curve.rgb = {{0, 0}, {0.5, 0.5}, {1, 1}}; });
    graph.press(graph.at(0.54, 0.545));
    graph.release(graph.at(0.54, 0.545));
    QCOMPARE(graph.curve().rgb.size(), size_t(4));
    QVERIFY(graph.curve().rgb[1] == (CurvePoint{0.5, 0.5}));
}

void CameraRawCurveGraphTests::aPressElsewhereAddsAndDragsAPoint()
{
    Graph graph(true);
    graph.press(graph.at(0.25, 0.6));
    QCOMPARE(graph.curve().rgb.size(), size_t(3));
    QVERIFY(std::abs(graph.curve().rgb[1].x - 0.25) < 0.01 && std::abs(graph.curve().rgb[1].y - 0.6) < 0.01);
    // The same press drags it on, in order.
    graph.move(graph.at(0.4, 0.7));
    QVERIFY(std::abs(graph.curve().rgb[1].x - 0.4) < 0.01 && std::abs(graph.curve().rgb[1].y - 0.7) < 0.01);
    graph.release(graph.at(0.4, 0.7));
    // Added below, sorted in at its x.
    graph.press(graph.at(0.8, 0.95));
    graph.move(graph.at(0.7, 1.4));
    graph.release(graph.at(0.7, 1.4));
    QCOMPARE(graph.curve().rgb.size(), size_t(4));
    QVERIFY(std::abs(graph.curve().rgb[2].x - 0.7) < 0.01 && graph.curve().rgb[2].y == 1);
    QVERIFY(std::abs(graph.curve().rgb[1].x - 0.4) < 0.01);
    // The ends move their output alone.
    graph.press(graph.at(1, 1));
    graph.move(graph.at(0.6, 0.8));
    graph.release(graph.at(0.6, 0.8));
    QVERIFY(graph.curve().rgb.back().x == 1 && std::abs(graph.curve().rgb.back().y - 0.8) < 0.01);
    graph.press(graph.at(0, 0));
    graph.move(graph.at(0.3, -0.5));
    graph.release(graph.at(0.3, -0.5));
    QVERIFY(graph.curve().rgb.front() == (CurvePoint{0, 0}));
}

void CameraRawCurveGraphTests::addingIsRefusedNearAnEdgeAPointOrSixteen()
{
    Graph graph(true);
    // Too near an end's x, or a point's.
    const int w = graph.graph->width();
    graph.press(QPoint(2, 20));
    graph.move(QPoint(40, 30));
    graph.release(QPoint(40, 30));
    graph.press(QPoint(w - 2, 140));
    graph.release(QPoint(w - 2, 140));
    QVERIFY(graph.curve().rgb == CameraRawCurveSettings::linear());
    graph.set([](CameraRawCurveSettings &curve) { curve.rgb = {{0, 0}, {0.5, 0.2}, {1, 1}}; });
    graph.press(graph.at(0.505, 0.9));
    graph.move(graph.at(0.6, 0.9));
    graph.release(graph.at(0.6, 0.9));
    QVERIFY(graph.curve().rgb == (std::vector<CurvePoint>{{0, 0}, {0.5, 0.2}, {1, 1}}));
    QVERIFY(!graph.readout().isVisible());
    // Sixteen points take no seventeenth.
    graph.set([](CameraRawCurveSettings &curve) {
        curve.rgb = {{0, 0}};
        for (int index = 1; index < 15; ++index)
            curve.rgb.push_back({index * 0.06, index * 0.06});
        curve.rgb.push_back({1, 1});
    });
    QCOMPARE(graph.curve().rgb.size(), size_t(16));
    graph.press(graph.at(0.93, 0.2));
    graph.release(graph.at(0.93, 0.2));
    QCOMPARE(graph.curve().rgb.size(), size_t(16));
    // Fifteen take a sixteenth.
    graph.set([](CameraRawCurveSettings &curve) { curve.rgb.erase(curve.rgb.begin() + 1); });
    graph.press(graph.at(0.93, 0.2));
    graph.release(graph.at(0.93, 0.2));
    QCOMPARE(graph.curve().rgb.size(), size_t(16));
}

void CameraRawCurveGraphTests::theChosenPointIsLitAndReadOut()
{
    Graph graph(true);
    graph.set([](CameraRawCurveSettings &curve) { curve.rgb = {{0, 0}, {0.3, 0.4}, {0.7, 0.6}, {1, 1}}; });
    const QColor accent = graph.graph->palette().color(QPalette::Highlight);
    QCOMPARE(graph.dot(0.3, 0.4), QColor(Qt::white));
    QVERIFY(!graph.readout().isVisible());
    const auto shown = [&graph](size_t index) {
        const CurvePoint point = graph.curve().rgb[index];
        return QStringLiteral("In %1   Out %2").arg(std::lround(point.x * 255)).arg(std::lround(point.y * 255));
    };
    // The press takes the point to the pointer.
    graph.press(graph.at(0.3, 0.4));
    QVERIFY(std::abs(graph.curve().rgb[1].x - 0.3) < 0.003 && std::abs(graph.curve().rgb[1].y - 0.4) < 0.007);
    QCOMPARE(graph.dot(0.3, 0.4), accent);
    QCOMPARE(graph.dot(0.7, 0.6), QColor(Qt::white));
    QCOMPARE(graph.readout().text(), shown(1));
    // The readout follows the drag.
    graph.move(graph.at(0.4, 0.5));
    QCOMPARE(graph.readout().text(), shown(1));
    QVERIFY(graph.readout().text().startsWith("In 102   Out 12"));
    graph.release(graph.at(0.4, 0.5));
    // An end point is chosen too.
    graph.press(graph.at(1, 1));
    graph.release(graph.at(1, 1));
    QCOMPARE(graph.readout().text(), QString("In 255   Out 255"));
    QCOMPARE(graph.graph->grab().toImage().pixelColor(graph.graph->width() - 2, 2), accent);
    // A new point is chosen as it is made.
    graph.press(graph.at(0.85, 0.2));
    graph.release(graph.at(0.85, 0.2));
    QCOMPARE(graph.readout().text(), shown(3));
    QVERIFY(std::abs(graph.curve().rgb[3].x - 0.85) < 0.003);
    QCOMPARE(graph.dot(0.85, 0.2), accent);
    // A choice past the points changed elsewhere hides.
    graph.press(graph.at(1, 1));
    graph.release(graph.at(1, 1));
    graph.set([](CameraRawCurveSettings &curve) { curve.rgb = CameraRawCurveSettings::linear(); });
    QVERIFY(!graph.readout().isVisible());
}

void CameraRawCurveGraphTests::aNewChannelOrPresetDropsTheChoice()
{
    Graph graph(true);
    graph.set([](CameraRawCurveSettings &curve) { curve.rgb = curve.red = {{0, 0}, {0.3, 0.4}, {1, 1}}; });
    graph.press(graph.at(0.3, 0.4));
    QVERIFY(graph.readout().isVisible());
    // Another channel: no choice, and the held drag lets go.
    graph.controls->findChild<QToolButton *>(QStringLiteral("pointChannel1"))->click();
    QVERIFY(!graph.readout().isVisible());
    QCOMPARE(graph.dot(0.3, 0.4), QColor(Qt::white));
    graph.move(graph.at(0.3, 0.9));
    QVERIFY(graph.curve().red == (std::vector<CurvePoint>{{0, 0}, {0.3, 0.4}, {1, 1}}));
    graph.release(graph.at(0.3, 0.9));
    // A preset's points drop it too; Custom keeps it.
    graph.press(graph.at(0.3, 0.4));
    graph.release(graph.at(0.3, 0.4));
    auto &preset = *graph.controls->findChild<QComboBox *>(QStringLiteral("curvePreset"));
    emit preset.activated(0);
    graph.set([](CameraRawCurveSettings &curve) { curve.refineSaturation = 10; });
    QVERIFY(graph.readout().isVisible());
    emit preset.activated(2);
    QVERIFY(graph.curve().red == CameraRawCurveSettings::mediumContrast());
    QVERIFY(!graph.readout().isVisible());
    QCOMPARE(graph.dot(0.25, 0.18), QColor(Qt::white));
}

void CameraRawCurveGraphTests::aDoubleClickRemovesAnInnerPointAndTheChoice()
{
    Graph graph(true);
    graph.set([](CameraRawCurveSettings &curve) { curve.rgb = {{0, 0}, {0.5, 0.5}, {1, 1}}; });
    QTest::mouseDClick(graph.graph->window()->windowHandle(), Qt::LeftButton, {}, graph.graph->mapTo(graph.graph->window(), graph.at(0.5, 0.5)));
    QVERIFY(graph.curve().rgb == CameraRawCurveSettings::linear());
    QVERIFY(!graph.readout().isVisible());
    // On empty ground: one click adds, two remove.
    QTest::mouseDClick(graph.graph->window()->windowHandle(), Qt::LeftButton, {}, graph.graph->mapTo(graph.graph->window(), graph.at(0.4, 0.8)));
    QVERIFY(graph.curve().rgb == CameraRawCurveSettings::linear());
    // A move after it drags nothing.
    graph.set([](CameraRawCurveSettings &curve) { curve.rgb = {{0, 0}, {0.3, 0.3}, {0.6, 0.6}, {1, 1}}; });
    QTest::mouseDClick(graph.graph, Qt::LeftButton, {}, graph.at(0.3, 0.3));
    graph.move(graph.at(0.5, 0.9));
    QVERIFY(graph.curve().rgb == (std::vector<CurvePoint>{{0, 0}, {0.6, 0.6}, {1, 1}}));
    // An end point stays, though chosen.
    QTest::mouseDClick(graph.graph, Qt::LeftButton, {}, graph.at(1, 1));
    QCOMPARE(graph.curve().rgb.size(), size_t(3));
    QCOMPARE(graph.readout().text(), QString("In 255   Out 255"));
}

void CameraRawCurveGraphTests::aDoubleClickReachesFourHundredthsByX()
{
    Graph graph(true);
    // Beside the start nothing is added; 0.037 removes, 0.042 not.
    QVERIFY(3.0 / graph.graph->width() < 0.01);
    for (const auto &[inner, removed] : {std::pair(0.045, true), std::pair(0.05, false)}) {
        graph.set([inner](CameraRawCurveSettings &curve) { curve.rgb = {{0, 0}, {inner, 0.9}, {1, 1}}; });
        QTest::mouseDClick(graph.graph->window()->windowHandle(), Qt::LeftButton, {}, graph.graph->mapTo(graph.graph->window(), QPoint(3, 140)));
        QCOMPARE(graph.curve().rgb.size(), removed ? size_t(2) : size_t(3));
    }
}

void CameraRawCurveGraphTests::aDoubleClickInTheStripMovesADivider()
{
    Graph graph;
    const int w = graph.graph->width(), h = graph.graph->height();
    graph.set([](CameraRawCurveSettings &curve) { curve.rgb = {{0, 0}, {0.5, 0.5}, {1, 1}}; });
    // The first press moves the divider; no point goes.
    graph.press(QPoint(int(0.25 * w), h - 5));
    graph.release(QPoint(int(0.25 * w), h - 5));
    QVERIFY(std::abs(graph.curve().shadowSplit - 25) < 0.5);
    // The second press, elsewhere, moves it again.
    QTest::mouseDClick(graph.graph, Qt::LeftButton, {}, QPoint(int(0.3 * w), h - 5));
    QVERIFY(std::abs(graph.curve().shadowSplit - 30) < 0.5);
    QVERIFY(graph.curve().darkSplit == 50 && graph.curve().lightSplit == 75);
    QCOMPARE(graph.curve().rgb.size(), size_t(3));
    QVERIFY(graph.curve().shadows == 0 && graph.curve().darks == 0);
}

void CameraRawCurveGraphTests::onlyTheLeftButtonDragsAndAPressStartsAfresh()
{
    Graph graph(true);
    graph.press(graph.at(0.5, 0.8), Qt::RightButton);
    graph.move(graph.at(0.5, 0.2), Qt::RightButton);
    QVERIFY(graph.curve().rgb == CameraRawCurveSettings::linear());
    graph.set([](CameraRawCurveSettings &curve) { curve.rgb = {{0, 0}, {0.5, 0.5}, {1, 1}}; });
    QTest::mouseDClick(graph.graph, Qt::RightButton, {}, graph.at(0.5, 0.5));
    QVERIFY(graph.curve().rgb == (std::vector<CurvePoint>{{0, 0}, {0.5, 0.5}, {1, 1}}));
    // A move without the left button drags nothing.
    graph.press(graph.at(0.5, 0.5));
    graph.move(graph.at(0.5, 0.9), Qt::RightButton);
    QVERIFY(std::abs(graph.curve().rgb[1].y - 0.5) < 0.01);
    graph.release(graph.at(0.5, 0.5));
    // A lost release: the next press picks its own target.
    graph.set([](CameraRawCurveSettings &curve) { curve.rgb = {{0, 0}, {0.3, 0.3}, {0.7, 0.7}, {1, 1}}; });
    graph.press(graph.at(0.3, 0.3));
    graph.press(graph.at(0.7, 0.7));
    graph.move(graph.at(0.7, 0.9));
    QVERIFY(std::abs(graph.curve().rgb[1].y - 0.3) < 0.01 && std::abs(graph.curve().rgb[2].y - 0.9) < 0.01);
    // A release lets go: moves then change nothing.
    graph.release(graph.at(0.7, 0.9));
    const std::vector<CurvePoint> held = graph.curve().rgb;
    graph.move(graph.at(0.5, 0.1));
    QVERIFY(graph.curve().rgb == held);
    // Parametric too: a release ends the region.
    graph.controls->findChild<QToolButton *>(QStringLiteral("curvePage0"))->click();
    graph.press(QPoint(30, 80));
    graph.release(QPoint(30, 80));
    graph.move(QPoint(30, 10));
    QCOMPARE(graph.curve().shadows, 0.0);
}

void CameraRawCurveGraphTests::theGraphSaysWhatADragDoes()
{
    Graph graph;
    QCOMPARE(graph.graph->toolTip(), QString("Drag up or down to lift or lower those tones. Drag a divider along the bottom to change which tones each region covers."));
    graph.controls->findChild<QToolButton *>(QStringLiteral("curvePage1"))->click();
    QCOMPARE(graph.graph->toolTip(), QString("Drag a point. Click to add one. Double-click a point to remove it."));
}

QTEST_MAIN(CameraRawCurveGraphTests)
#include "CameraRawCurveGraphTests.moc"
