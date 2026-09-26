#include "AddressSpaceLimit.h"
#include "IO/ImageExporter.h"
#include "RenderFixtures.h"
#include "Rendering/LayerRenderer.h"
#include <QtTest>
#include <cstring>

namespace {
// A mask of four columns: 0, 64, 128 and 255.
QImage tones()
{
    QImage mask(4, 1, QImage::Format_Grayscale8);
    const uchar values[] = {0, 64, 128, 255};
    std::copy(std::begin(values), std::end(values), mask.scanLine(0));
    return mask;
}

// White on its left half, black on its right.
QImage halves(int width, int height)
{
    QImage mask = gray(width, height, 0);
    for (int y = 0; y < height; ++y)
        std::fill_n(mask.scanLine(y), width / 2, uchar(255));
    return mask;
}
}

class DrawCoverageTests : public QObject {
    Q_OBJECT
private slots:
    void coverageOnlyAddsWhite_data();
    void coverageOnlyAddsWhite();
    void thePaintersModeAndOpacityAreInherited();
    void everyModeBlendsAsUnderAClip_data();
    void everyModeBlendsAsUnderAClip();
    void rasterOperationsHaveNoCoverage();
    void aWildAngleTurnsAsItsRemainder();
    void onlyGrayMasksAreCoverage();
    void aMaskTooLargeToWhitenThrows();
    void thePlacementTurnsAndFlipsTheMask();
    void nearestKeepsEdgesHardAndSmoothSoftensThem();
    void smoothSamplingBlendsBetweenMaskPixels();
    void largeReductionsDoNotAlias();
    void oddMasksKeepTheirOverhang();
    void thePainterComesBackAsItWas();
};

void DrawCoverageTests::coverageOnlyAddsWhite_data()
{
    QTest::addColumn<int>("background");
    // Each tone t gives 255 t + background(1 - t).
    QTest::addColumn<QList<int>>("expected");
    QTest::newRow("black") << 0 << QList<int>{0, 64, 128, 255};
    QTest::newRow("gray") << 100 << QList<int>{100, 139, 178, 255};
    QTest::newRow("white") << 255 << QList<int>{255, 255, 255, 255};
}

void DrawCoverageTests::coverageOnlyAddsWhite()
{
    QFETCH(int, background);
    QFETCH(QList<int>, expected);
    for (const QImage::Format format : {QImage::Format_Grayscale8, QImage::Format_RGBA8888_Premultiplied}) {
        QImage device(4, 1, format);
        device.fill(QColor(background, background, background));
        QPainter context(&device);
        LayerRenderer::drawCoverage(tones(), {.origin = {0, 0}, .size = {4, 1}, .sampling = LayerSampling::nearest}, context);
        context.end();
        for (int x = 0; x < 4; ++x) {
            QCOMPARE(device.pixelColor(x, 0).red(), expected[x]);
            QCOMPARE(device.pixelColor(x, 0).alpha(), 255);
        }
    }
}

void DrawCoverageTests::thePaintersModeAndOpacityAreInherited()
{
    const auto drawn = [](QPainter::CompositionMode mode, double opacity) {
        QImage device = gray(4, 1, 100);
        QPainter context(&device);
        context.setCompositionMode(mode);
        context.setOpacity(opacity);
        LayerRenderer::drawCoverage(tones(), {.origin = {0, 0}, .size = {4, 1}, .sampling = LayerSampling::nearest}, context);
        context.end();
        return QList<int>{device.constScanLine(0)[0], device.constScanLine(0)[1], device.constScanLine(0)[2], device.constScanLine(0)[3]};
    };
    // White under Multiply changes nothing, as Swift's fill.
    QCOMPARE(drawn(QPainter::CompositionMode_Multiply, 1), (QList<int>{100, 100, 100, 100}));
    // Half the painter's opacity is half the coverage.
    QCOMPARE(drawn(QPainter::CompositionMode_SourceOver, 0.5), (QList<int>{100, 119, 139, 177}));
    QCOMPARE(drawn(QPainter::CompositionMode_SourceOver, 1), (QList<int>{100, 139, 178, 255}));
}

void DrawCoverageTests::everyModeBlendsAsUnderAClip_data()
{
    QTest::addColumn<int>("mode");
    QTest::addColumn<double>("opacity");
    for (int mode = QPainter::CompositionMode_SourceOver; mode <= QPainter::CompositionMode_Exclusion; ++mode) {
        QTest::addRow("mode %d", mode) << mode << 1.0;
        QTest::addRow("mode %d at half opacity", mode) << mode << 0.5;
    }
}

void DrawCoverageTests::everyModeBlendsAsUnderAClip()
{
    QFETCH(int, mode);
    QFETCH(double, opacity);
    // Opaque, half clear and clear rows under four mask tones.
    QImage device(4, 3, QImage::Format_RGBA8888_Premultiplied);
    const QRgb rows[] = {qRgba(100, 150, 200, 255), qRgba(50, 75, 100, 128), qRgba(0, 0, 0, 0)};
    QImage mask = gray(4, 3, 0);
    for (int y = 0; y < 3; ++y) {
        std::copy_n(tones().constScanLine(0), 4, mask.scanLine(y));
        for (int x = 0; x < 4; ++x)
            std::memcpy(device.scanLine(y) + x * 4, &rows[y], 4);
    }
    // The oracle: opaque white under the mode, mixed by coverage.
    QImage applied = device;
    {
        QPainter whole(&applied);
        whole.setCompositionMode(QPainter::CompositionMode(mode));
        whole.setOpacity(opacity);
        whole.fillRect(applied.rect(), Qt::white);
    }
    QImage drawn = device;
    const LayerTransform whole{.origin = {0, 0}, .size = {4, 3}, .sampling = LayerSampling::nearest};
    // Seven modes are not linear in the source: no opacity.
    const QList<int> erasing{QPainter::CompositionMode_Clear, QPainter::CompositionMode_Source, QPainter::CompositionMode_SourceIn,
                             QPainter::CompositionMode_DestinationIn, QPainter::CompositionMode_SourceOut,
                             QPainter::CompositionMode_DestinationAtop, QPainter::CompositionMode_Plus};
    {
        QPainter context(&drawn);
        context.setCompositionMode(QPainter::CompositionMode(mode));
        context.setOpacity(opacity);
        if (opacity < 1 && erasing.contains(mode)) {
            QVERIFY_EXCEPTION_THROWN(LayerRenderer::drawCoverage(mask, whole, context), std::logic_error);
            return;
        }
        LayerRenderer::drawCoverage(mask, whole, context);
    }
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 4; ++x) {
            const int coverage = mask.constScanLine(y)[x];
            for (int channel = 0; channel < 4; ++channel) {
                const int before = device.constScanLine(y)[x * 4 + channel], after = applied.constScanLine(y)[x * 4 + channel];
                const int expected = qRound((before * (255 - coverage) + after * coverage) / 255.0);
                const int actual = drawn.constScanLine(y)[x * 4 + channel];
                QVERIFY2(qAbs(actual - expected) <= 2,
                         qPrintable(QStringLiteral("row %1 tone %2 channel %3: %4, not %5").arg(y).arg(coverage).arg(channel).arg(actual).arg(expected)));
            }
        }
    }
}

void DrawCoverageTests::rasterOperationsHaveNoCoverage()
{
    // A refusal must leave no saved state behind.
    QTest::failOnWarning(QRegularExpression("saved states"));
    QImage device = solid(4, 1, qRgba(0, 0, 0, 255));
    QPainter context(&device);
    context.setCompositionMode(QPainter::RasterOp_SourceXorDestination);
    QVERIFY_EXCEPTION_THROWN(LayerRenderer::drawCoverage(tones(), {.origin = {0, 0}, .size = {4, 1}}, context), std::logic_error);
    context.setCompositionMode(QPainter::RasterOp_SourceOrDestination);
    QVERIFY_EXCEPTION_THROWN(LayerRenderer::drawCoverage(tones(), {.origin = {0, 0}, .size = {4, 1}}, context), std::logic_error);
    context.end();
}

void DrawCoverageTests::aWildAngleTurnsAsItsRemainder()
{
    const auto drawn = [](double rotation) {
        QImage device = gray(40, 40, 0);
        QPainter context(&device);
        LayerRenderer::drawCoverage(halves(20, 10), {.origin = {10, 15}, .size = {20, 10}, .rotation = rotation}, context);
        context.end();
        return device;
    };
    // LayerTransform::radians takes the remainder first, as draw does.
    QCOMPARE(drawn(1e20), drawn(std::fmod(1e20, 360)));
    QCOMPARE(drawn(450), drawn(90));
    QVERIFY(drawn(1e20) != drawn(0));
}

void DrawCoverageTests::onlyGrayMasksAreCoverage()
{
    QImage device = gray(4, 1, 0);
    QPainter context(&device);
    QVERIFY_EXCEPTION_THROWN(LayerRenderer::drawCoverage(solid(4, 1, qRgba(255, 255, 255, 255)), {.origin = {0, 0}, .size = {4, 1}}, context),
                             std::logic_error);
}

void DrawCoverageTests::aMaskTooLargeToWhitenThrows()
{
    QImage device = gray(4, 4, 0);
    QPainter context(&device);
    // 9000 x 9000: 81 MB of gray, 324 of white.
    const QImage mask = gray(9000, 9000, 255);
    std::optional<ExportError::Kind> kind;
    {
        const AddressSpaceLimit limit(200ll * 1024 * 1024);
        try {
            LayerRenderer::drawCoverage(mask, {.origin = {0, 0}, .size = {9000, 9000}, .sampling = LayerSampling::nearest}, context);
        } catch (const ExportError &error) {
            kind = error.kind;
        }
    }
    QCOMPARE(kind, std::optional(ExportError::Kind::render));
}

void DrawCoverageTests::thePlacementTurnsAndFlipsTheMask()
{
    const auto drawn = [](const LayerTransform &transform) {
        QImage device = gray(40, 40, 0);
        QPainter context(&device);
        LayerRenderer::drawCoverage(halves(20, 10), transform, context);
        return device;
    };
    const LayerTransform upright{.origin = {10, 15}, .size = {20, 10}, .sampling = LayerSampling::nearest};
    const QImage plain = drawn(upright);
    QCOMPARE(int(plain.constScanLine(20)[12]), 255);
    QCOMPARE(int(plain.constScanLine(20)[28]), 0);
    QCOMPARE(int(plain.constScanLine(14)[12]), 0);
    QCOMPARE(int(plain.constScanLine(24)[19]), 255);
    LayerTransform flipped = upright;
    flipped.flipX = true;
    QCOMPARE(int(drawn(flipped).constScanLine(20)[12]), 0);
    QCOMPARE(int(drawn(flipped).constScanLine(20)[28]), 255);
    // A quarter turn clockwise stands the white half on top.
    LayerTransform turned = upright;
    turned.rotation = 90;
    QCOMPARE(int(drawn(turned).constScanLine(12)[20]), 255);
    QCOMPARE(int(drawn(turned).constScanLine(28)[20]), 0);
    QCOMPARE(int(drawn(turned).constScanLine(20)[12]), 0);
    LayerTransform upsideDown = upright;
    upsideDown.flipY = true;
    upsideDown.size = {20, 10};
    QImage device = gray(40, 40, 0);
    QPainter context(&device);
    QImage topOnly = gray(20, 10, 0);
    std::fill_n(topOnly.scanLine(0), 20, uchar(255));
    LayerRenderer::drawCoverage(topOnly, upsideDown, context);
    context.end();
    QCOMPARE(int(device.constScanLine(24)[20]), 255);
    QCOMPARE(int(device.constScanLine(15)[20]), 0);
}

void DrawCoverageTests::nearestKeepsEdgesHardAndSmoothSoftensThem()
{
    // Twice the size: the mask's edge falls at device 10.5.
    const auto edge = [](LayerSampling sampling) {
        QImage device = gray(60, 40, 0);
        QPainter context(&device);
        context.scale(2, 2);
        LayerRenderer::drawCoverage(gray(20, 10, 255), {.origin = {5.25, 5}, .size = {20, 10}, .sampling = sampling}, context);
        context.end();
        return int(device.constScanLine(20)[10]);
    };
    QVERIFY(edge(LayerSampling::nearest) == 0 || edge(LayerSampling::nearest) == 255);
    QVERIFY2(qAbs(edge(LayerSampling::smooth) - 128) <= 2, qPrintable(QString::number(edge(LayerSampling::smooth))));
    QVERIFY2(qAbs(edge(LayerSampling::high) - 128) <= 2, qPrintable(QString::number(edge(LayerSampling::high))));
}

void DrawCoverageTests::smoothSamplingBlendsBetweenMaskPixels()
{
    // Two mask pixels, black and white, drawn sixteen wide.
    QImage mask = gray(2, 1, 0);
    mask.scanLine(0)[1] = 255;
    const auto middle = [&](LayerSampling sampling) {
        QImage device = gray(16, 4, 0);
        QPainter context(&device);
        LayerRenderer::drawCoverage(mask, {.origin = {0, 0}, .size = {16, 4}, .sampling = sampling}, context);
        context.end();
        return int(device.constScanLine(2)[7]);
    };
    QCOMPARE(middle(LayerSampling::nearest), 0);
    QVERIFY2(middle(LayerSampling::smooth) > 80 && middle(LayerSampling::smooth) < 140, qPrintable(QString::number(middle(LayerSampling::smooth))));
    QVERIFY2(middle(LayerSampling::high) > 80 && middle(LayerSampling::high) < 140, qPrintable(QString::number(middle(LayerSampling::high))));
}

void DrawCoverageTests::largeReductionsDoNotAlias()
{
    // One white column in three, drawn eight times smaller.
    QImage stripes = gray(510, 8, 0);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 510; x += 3)
            stripes.scanLine(y)[x] = 255;
    }
    QImage device = gray(64, 8, 0);
    QPainter context(&device);
    LayerRenderer::drawCoverage(stripes, {.origin = {0, 0}, .size = {63.75, 8}, .sampling = LayerSampling::high}, context);
    context.end();
    // A third of white is 85; bilinear gives 0, 128.
    for (int x = 4; x < 58; ++x)
        QVERIFY2(qAbs(int(device.constScanLine(4)[x]) - 85) <= 25, qPrintable(QStringLiteral("%1: %2").arg(x).arg(device.constScanLine(4)[x])));
}

void DrawCoverageTests::oddMasksKeepTheirOverhang()
{
    // A ramp of 101 columns halves to 51, then 26.
    const auto ramp = [](int width) {
        QImage mask = gray(width, 8, 0);
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < width; ++x)
                mask.scanLine(y)[x] = uchar(std::min(x, 100) * 2.5);
        }
        return mask;
    };
    const auto drawn = [](const QImage &mask, double width) {
        QImage device = gray(26, 8, 0);
        QPainter context(&device);
        LayerRenderer::drawCoverage(mask, {.origin = {0, 0}, .size = {width, 8}, .sampling = LayerSampling::high}, context);
        context.end();
        return device;
    };
    // 104 columns halve evenly; both land on the same pixels.
    const QImage odd = drawn(ramp(101), 25.25), even = drawn(ramp(104), 26);
    for (int x = 0; x < 26; ++x)
        QVERIFY2(qAbs(int(odd.constScanLine(4)[x]) - int(even.constScanLine(4)[x])) <= 1, qPrintable(QString::number(x)));
    QVERIFY(int(even.constScanLine(4)[20]) > 180);
}

void DrawCoverageTests::thePainterComesBackAsItWas()
{
    QImage device = gray(40, 40, 0);
    QPainter context(&device);
    context.translate(3, 4);
    context.setCompositionMode(QPainter::CompositionMode_Plus);
    const QTransform before = context.transform();
    LayerRenderer::drawCoverage(tones(), {.origin = {0, 0}, .size = {4, 1}}, context);
    QCOMPARE(context.transform(), before);
    QCOMPARE(context.compositionMode(), QPainter::CompositionMode_Plus);
    QVERIFY(!context.testRenderHint(QPainter::Antialiasing));
    QVERIFY(!context.testRenderHint(QPainter::SmoothPixmapTransform));
    // The painter's own transform placed the mask.
    context.end();
    QCOMPARE(int(device.constScanLine(4)[6]), 255);
    QCOMPARE(int(device.constScanLine(4)[3]), 0);
    QCOMPARE(int(device.constScanLine(0)[3]), 0);
}

QTEST_MAIN(DrawCoverageTests)
#include "DrawCoverageTests.moc"
