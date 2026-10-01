#include "BrushFixtures.h"
#include "Document/EditorSession.h"

// Swift's BlurBrushTests: Radius sets the reach, whatever the size.
namespace {
// A hard edge blurred along: row 50's gray columns.
int spread(double radius, double diameter = 60)
{
    EditorSession session;
    session.createDocument(200, 100);
    QImage image = BrushRaster::context(200, 100, false);
    image.fill(Qt::black);
    QPainter(&image).fillRect(QRect(100, 0, 100, 100), Qt::white);
    session.insert(ImportedImage(image, image, QStringLiteral("Edge")));
    session.selectTool(NavigationTool::blur);
    session.setBlurMode(BlurToolMode::blur);
    BrushSettings settings = session.brushSettings();
    settings.diameter = diameter;
    settings.hardness = 1;
    settings.blurRadius = radius;
    session.setBrushSettings(settings);
    session.beginBrush(QPointF(100, 20));
    session.continueBrush(QPointF(100, 80));
    session.finishBrushImmediately();
    const QImage result = session.activeLayer().value().asset.value().image().convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    int gray = 0;
    for (int x = 0; x < 200; ++x) {
        const int value = result.constScanLine(50)[x * 4];
        gray += value > 10 && value < 245;
    }
    return gray;
}
}

class BlurBrushTests : public QObject {
    Q_OBJECT
private slots:
    void radiusSetsHowFarItSoftens();
};

void BlurBrushTests::radiusSetsHowFarItSoftens()
{
    const int narrow = spread(2), wide = spread(12);
    QVERIFY2(wide > narrow * 2, qPrintable(QString("%1 against %2").arg(narrow).arg(wide)));
    QCOMPARE(narrow, 6);
    QCOMPARE(wide, 42);
    // The brush size leaves the reach alone.
    QCOMPARE(spread(12, 80), wide);
    // Past its ends, Radius holds to 0.5 and 50.
    QCOMPARE(spread(0.1), spread(0.5));
    QCOMPARE(spread(80), spread(50));
}

QTEST_GUILESS_MAIN(BlurBrushTests)
#include "BlurBrushTests.moc"
