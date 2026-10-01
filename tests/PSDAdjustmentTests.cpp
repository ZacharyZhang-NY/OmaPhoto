#include "Document/BrushStroke.h"
#include "IO/PSD/PSDDocumentBuilder.h"
#include "IO/PSD/PSDReader.h"
#include <QtTest>

// Swift's PSDAdjustmentTests: adjustments and masks as Photoshop writes them.
namespace {
QByteArray shorts(const std::vector<int> &values)
{
    QByteArray data;
    for (const int value : values) {
        const quint16 bits = quint16(qint16(value));
        data.append(char(bits >> 8)).append(char(bits & 0xFF));
    }
    return data;
}

// A white patch on a gray grid, as text rows.
QStringList rows(const QImage &mask)
{
    QStringList result;
    for (int y = 0; y < mask.height(); ++y) {
        QString row;
        for (int x = 0; x < mask.width(); ++x)
            row += mask.constScanLine(y)[x] > 127 ? u'#' : u'.';
        result << row;
    }
    return result;
}

QImage whitePatch()
{
    QImage patch = BrushRaster::context(2, 2, true);
    patch.fill(255);
    return patch;
}
}

class PSDAdjustmentTests : public QObject {
    Q_OBJECT
private slots:
    void levelsGammaIsInHundredths();
    void hueSaturationReadsMasterAndEachRange();
    void maskPatchSitsWhereItIsOnTheCanvas();
    void maskPatchFollowsAPixelLayersGrid();
};

void PSDAdjustmentTests::levelsGammaIsInHundredths()
{
    // RGB 2–254 at gamma 2.50, the rest untouched; 29 records.
    std::vector<int> values{2, 2, 254, 0, 255, 250};
    for (int channel = 0; channel < 3; ++channel)
        values.insert(values.end(), {0, 255, 0, 255, 100});
    QByteArray data = shorts(values);
    data.append(QByteArray(292 - data.size(), 0));
    const LevelsSettings levels = PSDAdjustments::levels(data).value().levels;
    QVERIFY(levels.ranges[0] == (LevelRange{.black = 2, .gamma = 2.5, .white = 254, .outputBlack = 0, .outputWhite = 255}));
    for (size_t channel = 1; channel < 4; ++channel)
        QCOMPARE(levels.ranges[channel].gamma, 1.0);
    QVERIFY(!PSDAdjustments::levels(data.left(291)));
}

void PSDAdjustmentTests::hueSaturationReadsMasterAndEachRange()
{
    // Version 2, Colorize off; its values, the Master's, the Reds'.
    QByteArray data = shorts({2}) + QByteArray(2, 0) + shorts({23, 25, 0, 5, 4, 0, 315, 345, 15, 45, 0, -30, 10});
    // Yellows' band wraps: −30 reads as 330.
    data += shorts({-30, 15, 45, 75, 7, 8, 9});
    data += shorts(std::vector<int>(4 * 7, 0));
    const HueSaturationSettings settings = PSDAdjustments::hue(data).value().hsvSettings.value();
    QVERIFY(!settings.colorize);
    QVERIFY(settings.adjustments.at(ColorRange::master) == (RangeAdjustment{5, 4, 0}));
    QVERIFY(settings.adjustments.at(ColorRange::reds) == (RangeAdjustment{0, -30, 10}));
    QVERIFY(settings.bands.at(ColorRange::reds) == (HueBand{315, 345, 15, 45}));
    QVERIFY(settings.bands.at(ColorRange::yellows) == (HueBand{330, 15, 45, 75}));
    QVERIFY(settings.adjustments.at(ColorRange::yellows) == (RangeAdjustment{7, 8, 9}));
    QVERIFY(settings.adjustments.at(ColorRange::magentas) == RangeAdjustment());

    // Colorize on: its own values apply, and nothing else.
    data[2] = 1;
    const HueSaturationSettings colorized = PSDAdjustments::hue(data).value().hsvSettings.value();
    QVERIFY(colorized.colorize && colorized.adjustments.at(ColorRange::master) == (RangeAdjustment{23, 25, 0}));
    QVERIFY(!colorized.adjustments.contains(ColorRange::reds));
    QVERIFY(colorized.bands.at(ColorRange::reds) == defaultBand(ColorRange::reds));

    // Cut short: the ranges that fit; under 16 bytes, none.
    data[2] = 0;
    const HueSaturationSettings cut = PSDAdjustments::hue(data.left(16 + 14 + 13)).value().hsvSettings.value();
    QVERIFY(cut.adjustments.at(ColorRange::reds) == (RangeAdjustment{0, -30, 10}));
    QVERIFY(!cut.adjustments.contains(ColorRange::yellows));
    QVERIFY(!PSDAdjustments::hue(data.left(15)));
}

void PSDAdjustmentTests::maskPatchSitsWhereItIsOnTheCanvas()
{
    // A white patch at (3, 1), black round it.
    PSDRecord record;
    record.id = QUuid::createUuid();
    record.maskBounds = QRectF(3, 1, 2, 2);
    record.maskDefault = 0;
    const ImageLayer layer(QStringLiteral("Levels"), QSizeF(6, 4));
    const QImage mask = PSDDocumentBuilder::maskOnLayerGrid(whitePatch(), record, layer, QSizeF(6, 4)).value();
    QCOMPARE(mask.size(), QSize(6, 4));
    QCOMPARE(rows(mask), QStringList({"......", "...##.", "...##.", "......"}));

    // Through the builder, the patch at the bottom left.
    record.mask = whitePatch();
    record.maskBounds = QRectF(0, 2, 2, 2);
    record.adjustment = LayerAdjustment{.kind = AdjustmentKind::levels};
    record.kind = PSDLayerKind::adjustment;
    const PSDImport imported = PSDDocumentBuilder::makeImport(PSDDocument{6, 4, 72, {record}});
    QCOMPARE(rows(imported.layers.at(0).mask.value().asset.image()), QStringList({"......", "......", "##....", "##...."}));
    QCOMPARE(imported.conversions.size(), size_t(1));
    QCOMPARE(imported.conversions[0].message, QStringLiteral("Adjustment parameters may not match Photoshop exactly."));
}

void PSDAdjustmentTests::maskPatchFollowsAPixelLayersGrid()
{
    QImage pixels = BrushRaster::context(4, 2, false);
    pixels.fill(Qt::red);
    ImageLayer layer(ImportedImage(pixels, pixels, QStringLiteral("Red")), QPointF(2, 1));
    PSDRecord record;
    record.maskBounds = QRectF(3, 1, 2, 2);
    record.maskDefault = 0;
    QCOMPARE(rows(PSDDocumentBuilder::maskOnLayerGrid(whitePatch(), record, layer, QSizeF(6, 4)).value()), QStringList({".##.", ".##."}));
    // A layer stretched twice as wide: the patch is too.
    layer.transform.size = QSizeF(2, 2);
    QCOMPARE(rows(PSDDocumentBuilder::maskOnLayerGrid(whitePatch(), record, layer, QSizeF(6, 4)).value()), QStringList({"..##", "..##"}));
    // Already on the grid: the patch itself.
    QImage grid = BrushRaster::context(4, 2, true);
    record.maskBounds = QRectF(2, 1, 2, 2);
    QCOMPARE(PSDDocumentBuilder::maskOnLayerGrid(grid, record, layer, QSizeF(6, 4)).value().cacheKey(), grid.cacheKey());
    // No bounds, or a sizeless layer: the patch stays.
    record.maskBounds = QRectF(2, 1, 0, 2);
    QCOMPARE(PSDDocumentBuilder::maskOnLayerGrid(whitePatch(), record, layer, QSizeF(6, 4)).value().size(), QSize(2, 2));
    record.maskBounds = QRectF(2, 1, 2, 0);
    QCOMPARE(PSDDocumentBuilder::maskOnLayerGrid(whitePatch(), record, layer, QSizeF(6, 4)).value().size(), QSize(2, 2));
    record.maskBounds = QRectF(2, 1, 2, 2);
    for (const QSizeF size : {QSizeF(0, 2), QSizeF(2, 0)}) {
        layer.transform.size = size;
        QCOMPARE(PSDDocumentBuilder::maskOnLayerGrid(whitePatch(), record, layer, QSizeF(6, 4)).value().size(), QSize(2, 2));
    }
    // The grid's bounds, a smaller patch: drawn up to it.
    layer.transform.size = QSizeF(4, 2);
    record.maskBounds = QRectF(2, 1, 4, 2);
    QCOMPARE(rows(PSDDocumentBuilder::maskOnLayerGrid(whitePatch(), record, layer, QSizeF(6, 4)).value()), QStringList({"####", "####"}));
    // The grid's size, a pixel along: placed there.
    QImage wide = BrushRaster::context(4, 2, true);
    wide.fill(255);
    record.maskBounds = QRectF(3, 1, 4, 2);
    QCOMPARE(rows(PSDDocumentBuilder::maskOnLayerGrid(wide, record, layer, QSizeF(6, 4)).value()), QStringList({".###", ".###"}));
}

QTEST_GUILESS_MAIN(PSDAdjustmentTests)
#include "PSDAdjustmentTests.moc"
