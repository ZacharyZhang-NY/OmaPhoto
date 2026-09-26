#include "CanvasFixtures.h"
#include "IO/ImageExporter.h"
#include <QElapsedTimer>
#include <algorithm>

// Swift's 4K brush benchmark: run alone, with BRUSH_BENCHMARK=1.
class BrushPerformanceTests : public QObject {
    Q_OBJECT
private slots:
    void fourKInteractiveStroke();
};

void BrushPerformanceTests::fourKInteractiveStroke()
{
    if (qEnvironmentVariable("BRUSH_BENCHMARK") != QStringLiteral("1"))
        QSKIP("BRUSH_BENCHMARK=1 runs the benchmark");
    for (const double diameter : {40.0, 800.0}) {
        for (const bool opaque : {false, true}) {
            Shown shown(QSize(4000, 4000), QSize(1000, 1000));
            EditorSession &session = shown.session;
            if (opaque) {
                QImage black = BrushRaster::context(4000, 4000, false);
                black.fill(Qt::black);
                session.insert(ImportedImage(black, black, "Opaque 4K"));
            } else {
                session.addBlankLayer();
            }
            session.selectTool(NavigationTool::brush);
            session.setBrushSettings(BrushSettings{.diameter = diameter, .hardness = 0, .red = 1, .green = 1, .blue = 1});
            shown.settle();
            session.fit();
            shown.canvas->synchronizeDisplay();
            shown.canvas->repaint();
            // Each update: the model, then the dirty region painted.
            const auto display = [&] {
                shown.canvas->synchronizeDisplay();
                QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);
            };
            std::vector<double> frames, models;
            for (int pass = 0; pass < 2; ++pass) {
                QElapsedTimer stroke;
                stroke.start();
                session.beginBrush(QPointF(700, 3200));
                for (int i = 1; i <= 120; ++i) {
                    QElapsedTimer frame;
                    frame.start();
                    session.continueBrush(i <= 60 ? QPointF(700, 3200 - i * 40) : QPointF(700 + (i - 60) * 40, 800));
                    models.push_back(double(frame.nsecsElapsed()) / 1e6);
                    display();
                    frames.push_back(double(frame.nsecsElapsed()) / 1e6);
                }
                const double draw = double(stroke.nsecsElapsed()) / 1e6;
                QElapsedTimer up;
                up.start();
                session.finishBrush();
                display();
                qInfo().noquote() << QStringLiteral("BRUSH BENCH diameter=%1 opaque=%2 pass=%3 draw=%4 mouseUp=%5ms")
                                         .arg(diameter).arg(opaque).arg(pass).arg(draw, 0, 'f', 1).arg(double(up.nsecsElapsed()) / 1e6, 0, 'f', 2);
                QVERIFY(!session.brushError().has_value());
            }
            std::sort(frames.begin(), frames.end());
            std::sort(models.begin(), models.end());
            qInfo().noquote() << QStringLiteral("BRUSH BENCH diameter=%1 opaque=%2 frame median=%3 p95=%4 max=%5ms")
                                     .arg(diameter).arg(opaque).arg(frames[frames.size() / 2], 0, 'f', 2)
                                     .arg(frames[size_t(double(frames.size()) * 0.95)], 0, 'f', 2).arg(frames.back(), 0, 'f', 2);
            // The model's share: the rest is the repaint.
            qInfo().noquote() << QStringLiteral("BRUSH BENCH diameter=%1 opaque=%2 model median=%3ms").arg(diameter).arg(opaque).arg(models[models.size() / 2], 0, 'f', 2);
            // The gate of linux/TASKS.md 8.3.
            QVERIFY2(frames[frames.size() / 2] <= 8.0, qPrintable(QString::number(frames[frames.size() / 2])));
            if (diameter == 800 && opaque) {
                const QString path = QDir::temp().filePath(QStringLiteral("compositor-brush-benchmark.png"));
                ImageExporter::exportPNG(session.projectSnapshot().value(), path);
            }
        }
    }
}

int main(int argc, char **argv)
{
    // Swift's benchmark draws at a backing scale of two.
    qputenv("QT_SCALE_FACTOR", "2");
    QApplication app(argc, argv);
    BrushPerformanceTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "BrushPerformanceTests.moc"
