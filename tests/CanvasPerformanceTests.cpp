#include "CanvasFixtures.h"
#include <QElapsedTimer>
#include <algorithm>

// Pan and zoom at 4K, ten layers: CANVAS_BENCHMARK=1, alone.
class CanvasPerformanceTests : public QObject {
    Q_OBJECT
private slots:
    void fourKPanAndZoom();
};

namespace {
struct Timings {
    std::vector<double> frames;
    double median() { return at(0.5); }
    double at(double share)
    {
        std::sort(frames.begin(), frames.end());
        return frames[std::min(frames.size() - 1, size_t(double(frames.size()) * share))];
    }
};
}

void CanvasPerformanceTests::fourKPanAndZoom()
{
    if (qEnvironmentVariable("CANVAS_BENCHMARK") != QStringLiteral("1"))
        QSKIP("CANVAS_BENCHMARK=1 runs the benchmark");
    Shown shown(QSize(3840, 2160), QSize(1000, 1000));
    EditorSession &session = shown.session;
    // Ten layers: photographs, halves, blend modes and opacities.
    const LayerBlendMode modes[] = {LayerBlendMode::normal, LayerBlendMode::multiply, LayerBlendMode::screen, LayerBlendMode::overlay, LayerBlendMode::hue};
    const int count = qEnvironmentVariableIntValue("CANVAS_LAYERS") > 0 ? qEnvironmentVariableIntValue("CANVAS_LAYERS") : 10;
    for (int index = 0; index < count; ++index) {
        const bool whole = index % 2 == 0;
        const QImage pixels = noise(whole ? 3840 : 1920, whole ? 2160 : 1080, quint32(index + 1), whole ? 255 : 200);
        session.insert(ImportedImage(pixels, pixels, QStringLiteral("Layer %1").arg(index)), QPointF(whole ? 1920 : 800 + index * 150, whole ? 1080 : 700));
        session.setLayerBlendMode(qEnvironmentVariableIsSet("CANVAS_NORMAL") ? LayerBlendMode::normal : modes[index % 5]);
        session.setLayerOpacity(index == 0 ? 1.0 : 0.7);
    }
    shown.settle();
    session.fit();
    shown.canvas->synchronizeDisplay();
    shown.canvas->repaint();
    const auto display = [&] {
        shown.canvas->synchronizeDisplay();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);
    };
    const QPointF middle(500, 500);
    std::vector<double> medians;
    for (const bool actual : {false, true}) {
        // Actual Pixels, as the menu sets it.
        if (actual) {
            session.zoom(1, middle);
            QCOMPARE(session.viewport.zoom(), 1.0);
            display();
        }
        // A trackpad's pan: pixel deltas through the canvas's own path.
        Timings pan;
        for (int step = 0; step < 120; ++step) {
            QElapsedTimer frame;
            frame.start();
            wheel(*shown.canvas, middle, QPoint(step < 60 ? 9 : -9, step % 2 ? 6 : -6), QPoint(0, 0), Qt::NoModifier);
            display();
            pan.frames.push_back(double(frame.nsecsElapsed()) / 1e6);
        }
        qInfo().noquote() << QStringLiteral("CANVAS BENCH pan zoom=%1 median=%2 p95=%3 max=%4ms")
                                 .arg(actual ? QStringLiteral("100%") : QStringLiteral("fit")).arg(pan.median(), 0, 'f', 2).arg(pan.at(0.95), 0, 'f', 2)
                                 .arg(pan.at(1), 0, 'f', 2);
        medians.push_back(pan.median());
    }
    // A pinch: zoom steps in and out around the middle.
    session.fit();
    display();
    Timings zooming;
    for (int step = 0; step < 80; ++step) {
        QElapsedTimer frame;
        frame.start();
        session.zoom(session.viewport.zoom() * (step < 40 ? 1.05 : 1 / 1.05), middle);
        display();
        zooming.frames.push_back(double(frame.nsecsElapsed()) / 1e6);
    }
    qInfo().noquote() << QStringLiteral("CANVAS BENCH zoom median=%1 p95=%2 max=%3ms")
                             .arg(zooming.median(), 0, 'f', 2).arg(zooming.at(0.95), 0, 'f', 2).arg(zooming.at(1), 0, 'f', 2);
    medians.push_back(zooming.median());
    // Guards at half again the medians measured in 11.1.
    const double limits[] = {45, 100, 115};
    for (size_t index = 0; index < medians.size(); ++index)
        QVERIFY2(medians[index] <= limits[index], qPrintable(QString::number(medians[index])));
}

int main(int argc, char **argv)
{
    // Swift's benchmark draws at a backing scale of two.
    qputenv("QT_SCALE_FACTOR", "2");
    QApplication app(argc, argv);
    CanvasPerformanceTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "CanvasPerformanceTests.moc"
