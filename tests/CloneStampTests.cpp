#include "BrushFixtures.h"
#include "Document/EditorSession.h"
#include "IO/ImageExporter.h"
#include <QSignalSpy>
#include <cmath>

// Swift's CloneStampTests: a source copied under the brush.
namespace {
// Left half red with a green square, right half blue.
QImage colors()
{
    QImage image = BrushRaster::context(80, 40, false);
    QPainter painter(&image);
    painter.fillRect(QRect(0, 0, 40, 40), QColor(255, 0, 0));
    painter.fillRect(QRect(40, 0, 40, 40), QColor(0, 0, 255));
    painter.fillRect(QRect(10, 15, 10, 10), QColor(0, 255, 0));
    return image;
}

// Red left of `edge`, blue from it: a document-sized sample.
QImage edged(int width, int height, int edge)
{
    QImage image = BrushRaster::context(width, height, false);
    QPainter painter(&image);
    painter.fillRect(QRect(0, 0, edge, height), QColor(255, 0, 0));
    painter.fillRect(QRect(edge, 0, width - edge, height), QColor(0, 0, 255));
    return image;
}

std::vector<int> exported(const EditorSession &session, int x, int y)
{
    return pixel(ImageExporter::render(session.projectSnapshot().value()).image, x, y);
}

void stroke(EditorSession &session, QPointF point)
{
    session.beginBrush(point);
    session.continueBrush(point + QPointF(0.5, 0));
    session.finishBrush();
}

void colored(EditorSession &session)
{
    session.createDocument(80, 40);
    const QImage image = colors();
    session.insert(ImportedImage(image, image, QStringLiteral("Colors")));
    session.selectTool(NavigationTool::cloneStamp);
    session.setBrushSettings(BrushSettings{.diameter = 6, .hardness = 1});
}

// Half red, half blue, within a level or two.
bool halfway(const std::vector<int> &pixel)
{
    return std::abs(pixel[0] - 128) <= 2 && pixel[1] == 0 && std::abs(pixel[2] - 128) <= 2 && pixel[3] == 255;
}

const std::vector<int> redPixel = {255, 0, 0, 255}, bluePixel = {0, 0, 255, 255}, greenPixel = {0, 255, 0, 255};
}

class CloneStampTests : public QObject {
    Q_OBJECT
private slots:
    void copiesTheSourceUnderTheBrushKeepingAlignmentUntilItIsTurnedOff();
    void cloneStampKeepsItsOwnSoftBrushTip();
    void eachFamilyKeepsItsOpacityAndTheSmearItsTip();
    void theSamplePointFollowsTheOffsetOnceAStrokeFixesIt();
    void aSourceThatIsNoNumberIsRefused();
    void theActiveLayerAloneOrEveryVisibleLayerIsSampled();
    void aMaskIsNeverCloned();
    void aScaledLayerSamplesThroughItsGrid();
    void aLayerBetweenPixelsSamplesTheFraction();
    void aQuarterPixelWeighsItsNeighbours();
    void opacityAndTheSelectionLimitTheClone();
};

void CloneStampTests::copiesTheSourceUnderTheBrushKeepingAlignmentUntilItIsTurnedOff()
{
    EditorSession session;
    colored(session);
    // Nothing to copy until a source is set.
    session.beginBrush(QPointF(60, 20));
    QVERIFY(!session.brushStroke());
    QCOMPARE(session.brushError().value(), QString("Alt-click where Clone Stamp should copy from first."));
    session.setBrushError(std::nullopt);
    session.setCloneSource(QPointF(15, 20));
    const int count = session.history.undoCount();
    stroke(session, QPointF(60, 20));
    QVERIFY(!session.brushError());
    QCOMPARE(session.history.undoCount(), count + 1);
    QCOMPARE(session.history.undoName(), QString("Clone Stamp"));
    QCOMPARE(exported(session, 60, 20), greenPixel);
    QCOMPARE(exported(session, 70, 5), bluePixel);
    // Aligned: (66, 20) copies red from (21, 20).
    stroke(session, QPointF(66, 20));
    QCOMPARE(exported(session, 66, 20), redPixel);
    // Not aligned: every stroke starts at the source again.
    session.setCloneSettings(CloneSettings{false, false});
    stroke(session, QPointF(50, 10));
    QCOMPARE(exported(session, 50, 10), greenPixel);
}

void CloneStampTests::cloneStampKeepsItsOwnSoftBrushTip()
{
    EditorSession session;
    session.createDocument(40, 20);
    session.selectTool(NavigationTool::brush);
    BrushSettings settings = session.brushSettings();
    settings.diameter = 30;
    session.setBrushSettings(settings);
    QCOMPARE(session.brushSettings().hardness, 1.0);
    session.selectTool(NavigationTool::cloneStamp);
    QCOMPARE(session.brushSettings().hardness, 0.0);
    QCOMPARE(session.brushSettings().diameter, 40.0);
    settings = session.brushSettings();
    settings.hardness = 0.5;
    session.setBrushSettings(settings);
    // The Brush's tip returns for Spot Healing; Clone Stamp's stays.
    session.selectTool(NavigationTool::spotHealing);
    QCOMPARE(session.brushSettings().hardness, 1.0);
    QCOMPARE(session.brushSettings().diameter, 30.0);
    session.selectTool(NavigationTool::cloneStamp);
    QCOMPARE(session.brushSettings().hardness, 0.5);
    QCOMPARE(session.brushSettings().diameter, 40.0);
}

void CloneStampTests::eachFamilyKeepsItsOpacityAndTheSmearItsTip()
{
    EditorSession session;
    session.createDocument(40, 20);
    session.selectTool(NavigationTool::cloneStamp);
    BrushSettings settings = session.brushSettings();
    settings.opacity = 0.3;
    settings.diameter = 12;
    session.setBrushSettings(settings);
    // The Smear starts soft at 40, as Swift's second family.
    session.selectTool(NavigationTool::blur);
    QCOMPARE(session.brushSettings().diameter, 40.0);
    QCOMPARE(session.brushSettings().hardness, 0.0);
    QCOMPARE(session.brushSettings().opacity, 1.0);
    settings = session.brushSettings();
    settings.diameter = 70;
    session.setBrushSettings(settings);
    // Tools outside the brushes share the first family.
    session.selectTool(NavigationTool::move);
    QCOMPARE(session.brushSettings().diameter, 40.0);
    QCOMPARE(session.brushSettings().hardness, 1.0);
    session.selectTool(NavigationTool::brush);
    QCOMPARE(session.brushSettings().diameter, 40.0);
    session.selectTool(NavigationTool::cloneStamp);
    QCOMPARE(session.brushSettings().opacity, 0.3);
    QCOMPARE(session.brushSettings().diameter, 12.0);
    session.selectTool(NavigationTool::blur);
    QCOMPARE(session.brushSettings().diameter, 70.0);
    // The colour is no tip: it stays across families.
    settings = session.brushSettings();
    settings.green = 1;
    session.setBrushSettings(settings);
    session.selectTool(NavigationTool::brush);
    QCOMPARE(session.brushSettings().green, 1.0);
}

void CloneStampTests::theSamplePointFollowsTheOffsetOnceAStrokeFixesIt()
{
    EditorSession session;
    colored(session);
    QVERIFY(!session.cloneSamplePoint(QPointF(60, 20)));
    QVERIFY(!session.cloneStrokeOffset(QPointF(60, 20)));
    session.setCloneSource(QPointF(15.4, 20));
    // Until a stroke, the source itself; the offset rounds.
    QCOMPARE(session.cloneSamplePoint(QPointF(60, 20)).value(), QPointF(15.4, 20));
    QCOMPARE(session.cloneStrokeOffset(QPointF(60.2, 21.4)).value(), QSizeF(-45, -1));
    QCOMPARE(session.cloneStrokeOffset(QPointF(59.8, 18.4)).value(), QSizeF(-44, 2));
    stroke(session, QPointF(60, 20));
    QCOMPARE(session.cloneSamplePoint(QPointF(30, 5)).value(), QPointF(-15, 5));
    QCOMPARE(session.cloneStrokeOffset(QPointF(30, 5)).value(), QSizeF(-45, 0));
    // Not aligned: the source, or a live stroke's offset.
    session.setCloneSettings(CloneSettings{false, false});
    QCOMPARE(session.cloneSamplePoint(QPointF(30, 5)).value(), QPointF(15.4, 20));
    QCOMPARE(session.cloneStrokeOffset(QPointF(30, 5)).value(), QSizeF(-15, 15));
    session.beginBrush(QPointF(50, 10));
    QCOMPARE(session.cloneSamplePoint(QPointF(52, 10)).value(), QPointF(17, 20));
    session.cancelBrush();
    // A new source starts a new alignment.
    session.setCloneSettings(CloneSettings{true, false});
    session.setCloneSource(QPointF(5, 5));
    QCOMPARE(session.cloneSamplePoint(QPointF(30, 5)).value(), QPointF(5, 5));
    QCOMPARE(session.cloneStrokeOffset(QPointF(30, 5)).value(), QSizeF(-25, 0));
}

void CloneStampTests::aSourceThatIsNoNumberIsRefused()
{
    EditorSession session;
    colored(session);
    session.setCloneSource(QPointF(15, 20));
    QSignalSpy changed(&session, &EditorSession::changed);
    session.setCloneSource(QPointF(std::nan(""), 3));
    session.setCloneSource(QPointF(3, std::numeric_limits<double>::infinity()));
    QCOMPARE(changed.size(), 0);
    QCOMPARE(session.cloneSource().value(), QPointF(15, 20));
    session.setCloneSource(QPointF(16, 20));
    QCOMPARE(changed.size(), 1);
}

void CloneStampTests::theActiveLayerAloneOrEveryVisibleLayerIsSampled()
{
    EditorSession session;
    colored(session);
    session.addBlankLayer();
    session.selectTool(NavigationTool::cloneStamp);
    session.setBrushSettings(BrushSettings{.diameter = 6, .hardness = 1});
    session.setCloneSource(QPointF(15, 20));
    // The blank layer copies nothing over the blue.
    stroke(session, QPointF(60, 20));
    QCOMPARE(exported(session, 60, 20), bluePixel);
    // Tab samples every visible layer, as the canvas shows them.
    session.cycleToolMode();
    QVERIFY(session.cloneSettings().sampleAllLayers);
    QVERIFY(session.cloneSettings().aligned);
    session.setCloneSource(QPointF(15, 20));
    stroke(session, QPointF(60, 20));
    QCOMPARE(exported(session, 60, 20), greenPixel);
    // Hidden layers are not shown, so not sampled.
    session.toggleLayerVisibility(session.document().value().layers.front().id);
    session.setCloneSource(QPointF(15, 5));
    stroke(session, QPointF(70, 30));
    QCOMPARE(exported(session, 70, 30), (std::vector<int>{0, 0, 0, 0}));
    // Tab leaves Aligned as it was.
    session.setCloneSettings(CloneSettings{false, true});
    session.cycleToolMode();
    QVERIFY(!session.cloneSettings().sampleAllLayers && !session.cloneSettings().aligned);
}

void CloneStampTests::aMaskIsNeverCloned()
{
    EditorSession session;
    colored(session);
    session.setCloneSource(QPointF(15, 20));
    session.addLayerMask(true);
    QVERIFY(session.isMaskSelected());
    session.beginBrush(QPointF(60, 20));
    QVERIFY(!session.brushStroke());
    QVERIFY(!session.brushError());
}

void CloneStampTests::aScaledLayerSamplesThroughItsGrid()
{
    // Twenty pixels shown forty wide: each grid pixel spans two.
    const QImage clear = BrushRaster::context(20, 10, false);
    ImageLayer layer(ImportedImage(clear, clear, QStringLiteral("Scaled")), QPointF(0, 0));
    layer.transform.size = QSizeF(40, 20);
    BrushStroke paint(layer, false, brush(30, 1, 0, 0, 0), QSizeF(80, 20));
    paint.clone = BrushStroke::Clone{edged(80, 20, 31), QSizeF(10, 0)};
    paint.append(QPointF(21, 10));
    const QImage tile = paint.patches().at(0).image;
    // Grid pixel 10 sits at document 21, sampling 31.
    QCOMPARE(pixel(tile, 9, 5), redPixel);
    QVERIFY2(halfway(pixel(tile, 10, 5)), qPrintable(QString::number(pixel(tile, 10, 5)[0])));
    QCOMPARE(pixel(tile, 11, 5), bluePixel);
}

void CloneStampTests::aLayerBetweenPixelsSamplesTheFraction()
{
    // Half a pixel right: each grid centre falls between two.
    ImageLayer layer(QStringLiteral("Blank"), QSizeF(40, 20));
    layer.transform.origin = QPointF(0.5, 0);
    BrushStroke paint(layer, false, brush(20, 1, 0, 0, 0), QSizeF(80, 20));
    paint.clone = BrushStroke::Clone{edged(80, 20, 31), QSizeF(3, 0)};
    paint.append(QPointF(28, 10));
    const QPointF centre = paint.pixelToDocument.map(QPointF(28.5, 10.5));
    QCOMPARE(centre, QPointF(28, 10.5));
    const QImage tile = paint.patches().at(0).image;
    QCOMPARE(pixel(tile, 27, 10), redPixel);
    QVERIFY2(halfway(pixel(tile, 28, 10)), qPrintable(QString::number(pixel(tile, 28, 10)[0])));
    QCOMPARE(pixel(tile, 29, 10), bluePixel);
    // Up and down too: a row between two.
    ImageLayer lowered(QStringLiteral("Blank"), QSizeF(40, 20));
    lowered.transform.origin = QPointF(0, 0.5);
    BrushStroke down(lowered, false, brush(20, 1, 0, 0, 0), QSizeF(40, 40));
    QImage rows = BrushRaster::context(40, 40, false);
    rows.fill(QColor(255, 0, 0));
    QPainter(&rows).fillRect(QRect(0, 12, 40, 28), QColor(0, 0, 255));
    down.clone = BrushStroke::Clone{rows, QSizeF(0, 0)};
    down.append(QPointF(20, 12));
    const QImage lowTile = down.patches().at(0).image;
    const int row = int(std::floor(down.pixelToDocument.inverted().map(QPointF(20, 12)).y()));
    QVERIFY2(halfway(pixel(lowTile, 20, row)), qPrintable(QString::number(row)));
    QCOMPARE(pixel(lowTile, 20, row - 1), redPixel);
    QCOMPARE(pixel(lowTile, 20, row + 1), bluePixel);
}

void CloneStampTests::aQuarterPixelWeighsItsNeighbours()
{
    // The layer sits a quarter right, three quarters down.
    ImageLayer layer(QStringLiteral("Blank"), QSizeF(40, 20));
    layer.transform.origin = QPointF(0.25, 0.75);
    const auto near = [](const std::vector<int> &pixel, int red, int blue) {
        return std::abs(pixel[0] - red) <= 2 && pixel[1] == 0 && std::abs(pixel[2] - blue) <= 2 && pixel[3] == 255;
    };
    BrushStroke across(layer, false, brush(20, 1, 0, 0, 0), QSizeF(80, 40));
    across.clone = BrushStroke::Clone{edged(80, 40, 31), QSizeF(0, 0)};
    across.append(QPointF(31, 12));
    QCOMPARE(across.pixelToDocument.map(QPointF(31.5, 12.5)), QPointF(30.75, 12.25));
    const std::vector<int> mixed = pixel(across.patches().at(0).image, 31, 12);
    QVERIFY2(near(mixed, 191, 64), qPrintable(QString("%1 %2").arg(mixed[0]).arg(mixed[2])));
    // Rows: red above twelve, blue from it.
    QImage rows = BrushRaster::context(80, 40, false);
    rows.fill(QColor(255, 0, 0));
    QPainter(&rows).fillRect(QRect(0, 12, 80, 28), QColor(0, 0, 255));
    BrushStroke down(layer, false, brush(20, 1, 0, 0, 0), QSizeF(80, 40));
    down.clone = BrushStroke::Clone{rows, QSizeF(0, 0)};
    down.append(QPointF(20, 12));
    const std::vector<int> stacked = pixel(down.patches().at(0).image, 20, 12);
    QVERIFY2(near(stacked, 64, 191), qPrintable(QString("%1 %2").arg(stacked[0]).arg(stacked[2])));
}

void CloneStampTests::opacityAndTheSelectionLimitTheClone()
{
    QImage under = BrushRaster::context(40, 20, false);
    under.fill(QColor(0, 0, 255));
    QImage over = BrushRaster::context(40, 20, false);
    over.fill(QColor(255, 0, 0));
    BrushStroke half(ImageLayer(ImportedImage(under, under, QStringLiteral("Blue")), QPointF(0, 0)), false, brush(10, 1, 0, 0, 0, 0.5),
                     QSizeF(40, 20));
    half.clone = BrushStroke::Clone{over, QSizeF(0, 0)};
    half.append(QPointF(10, 10));
    // Each byte is rounded once: 127.5 and 127.5 make 128.
    QCOMPARE(pixel(half.patches().at(0).image, 10, 10), (std::vector<int>{128, 0, 128, 255}));
    // The selection's edge bounds it; a clear sample clears nothing.
    BrushStroke bounded(ImageLayer(ImportedImage(under, under, QStringLiteral("Blue")), QPointF(0, 0)), false, brush(10, 1, 0, 0, 0),
                        QSizeF(40, 20));
    DocumentSelection selection;
    selection.path.addRect(QRectF(0, 0, 10, 20));
    bounded.selectionClip = selection.clip(QSizeF(40, 20));
    QImage sample = over.copy();
    {
        QPainter painter(&sample);
        painter.setCompositionMode(QPainter::CompositionMode_Clear);
        painter.fillRect(QRect(0, 0, 40, 5), Qt::transparent);
    }
    bounded.clone = BrushStroke::Clone{sample, QSizeF(0, 0)};
    bounded.append(QPointF(10, 8));
    const QImage tile = bounded.patches().at(0).image;
    QCOMPARE(pixel(tile, 8, 8), redPixel);
    QCOMPARE(pixel(tile, 11, 8), bluePixel);
    QCOMPARE(pixel(tile, 8, 4), bluePixel);
}

QTEST_GUILESS_MAIN(CloneStampTests)
#include "CloneStampTests.moc"
