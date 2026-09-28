#include "Rendering/AdjustmentSurface.h"
#include "AddressSpaceLimit.h"
#include "Rendering/LayerRenderer.h"
#include "RenderFixtures.h"
#include <QPixmap>
#include <QtTest>

class AdjustmentSurfaceTests : public QObject {
    Q_OBJECT
private slots:
    void init();
    void theBodyDrawsInThePaintersOwnCoordinates();
    void onlyWhatThePainterShowsIsAllocated();
    void handBlendedModesWorkOnAnyDevice();
    void pastItsPixelBudgetNothingIsDrawn();
    void theSurfaceStartsClear();
    void theSurfaceComesBackUnderThePaintersOwnState();
    void aSurfaceThatCannotBeAllocatedIsLoggedAndNothingIsDrawn();
};

void AdjustmentSurfaceTests::init()
{
    QTest::failOnWarning(QRegularExpression("saved states|Unbalanced save/restore"));
}

void AdjustmentSurfaceTests::theBodyDrawsInThePaintersOwnCoordinates()
{
    const QImage image = noise(20, 12, 1, 200);
    const LayerTransform transform{.origin = {3, 2}, .size = {20, 12}, .rotation = 20, .sampling = LayerSampling::smooth};
    const auto scene = [&](bool throughSurface) {
        QImage surface = noise(64, 48, 2);
        QPainter painter(&surface);
        painter.translate(5.5, 4);
        painter.scale(1.5, 1.5);
        const auto body = [&](QPainter &target) { LayerRenderer::draw(image, transform, transform.center(), target, {.opacity = 0.8}); };
        if (throughSurface)
            AdjustmentSurface::draw(painter, body);
        else
            body(painter);
        painter.end();
        return surface;
    };
    QCOMPARE(largestAnywhere(scene(false), scene(true)), 0);
    QVERIFY(scene(true) != noise(64, 48, 2));
}

void AdjustmentSurfaceTests::onlyWhatThePainterShowsIsAllocated()
{
    QImage surface = BrushRaster::context(64, 48, false);
    QPainter painter(&surface);
    painter.scale(2, 2);
    painter.setClipRect(QRectF(4, 6, 10, 5));
    QSize allocated;
    QTransform placement;
    AdjustmentSurface::draw(painter, [&](QPainter &target) {
        allocated = QSize(target.device()->width(), target.device()->height());
        placement = target.deviceTransform();
        target.fillRect(QRectF(0, 0, 32, 24), QColor(0, 0, 255));
    });
    painter.end();
    QCOMPARE(allocated, QSize(20, 10));
    QCOMPARE(placement, QTransform::fromScale(2, 2) * QTransform::fromTranslate(-8, -12));
    QCOMPARE(surface.pixel(8, 12), qRgba(0, 0, 255, 255));
    QCOMPARE(surface.pixel(27, 21), qRgba(0, 0, 255, 255));
    QCOMPARE(surface.pixel(7, 12), qRgba(0, 0, 0, 0));
    QCOMPARE(surface.pixel(28, 21), qRgba(0, 0, 0, 0));
    QCOMPARE(surface.pixel(8, 22), qRgba(0, 0, 0, 0));

    // A turned painter's box passes the device; the surface stays.
    QImage turned = BrushRaster::context(40, 30, false);
    QPainter rotated(&turned);
    rotated.translate(20, 15);
    rotated.rotate(30);
    AdjustmentSurface::draw(rotated, [&](QPainter &target) { allocated = QSize(target.device()->width(), target.device()->height()); });
    QCOMPARE(allocated, QSize(40, 30));
}

void AdjustmentSurfaceTests::handBlendedModesWorkOnAnyDevice()
{
    const LayerTransform transform = placedAt({0, 0}, {8, 8});
    const auto body = [&](QPainter &target) {
        LayerRenderer::draw(solid(8, 8, qRgba(200, 40, 40, 255)), transform, transform.center(), target);
        LayerRenderer::draw(solid(8, 8, qRgba(20, 20, 220, 255)), transform, transform.center(), target, {.blendMode = LayerBlendMode::luminosity});
    };
    QImage expected = BrushRaster::context(8, 8, false);
    {
        QPainter painter(&expected);
        body(painter);
    }
    // No pixmap reads back; a surface supplies the backdrop.
    QPixmap pixmap(8, 8);
    pixmap.fill(Qt::transparent);
    {
        QPainter painter(&pixmap);
        QVERIFY_EXCEPTION_THROWN(body(painter), std::logic_error);
    }
    pixmap.fill(Qt::transparent);
    {
        QPainter painter(&pixmap);
        AdjustmentSurface::draw(painter, body);
    }
    QCOMPARE(pixmap.toImage().convertToFormat(QImage::Format_RGBA8888_Premultiplied), expected);
    QVERIFY(expected.pixel(4, 4) != qRgba(200, 40, 40, 255));
}

void AdjustmentSurfaceTests::pastItsPixelBudgetNothingIsDrawn()
{
    QImage surface = BrushRaster::context(8, 8, false);
    QPainter painter(&surface);
    // The budget counts device pixels: 64 here, not 16 units.
    painter.scale(2, 2);
    bool called = false;
    QTest::ignoreMessage(QtWarningMsg, "an adjustment surface passes its pixel budget: QSize(8, 8)");
    AdjustmentSurface::draw(painter, [&](QPainter &) { called = true; }, 0, 63);
    QVERIFY(!called);
    AdjustmentSurface::draw(painter, [&](QPainter &target) {
        called = true;
        target.fillRect(QRectF(0, 0, 4, 4), Qt::red);
    }, 64);
    painter.end();
    QVERIFY(called);
    QCOMPARE(surface.pixel(7, 7), qRgba(255, 0, 0, 255));
}

void AdjustmentSurfaceTests::theSurfaceStartsClear()
{
    const QImage background = noise(64, 48, 3);
    for (int round = 0; round < 8; ++round) {
        // The next surface may get freed memory full of ones.
        for (int dirty = 0; dirty < 4; ++dirty) {
            QImage scratch(64, 48, QImage::Format_RGBA8888_Premultiplied);
            scratch.fill(0xffffffff);
        }
        QImage surface = background;
        QPainter painter(&surface);
        AdjustmentSurface::draw(painter, [](QPainter &target) { target.fillRect(QRectF(0, 0, 8, 8), Qt::red); });
        painter.end();
        QCOMPARE(surface.pixel(4, 4), qRgba(255, 0, 0, 255));
        QCOMPARE(surface.copy(8, 8, 56, 40), background.copy(8, 8, 56, 40));
    }
}

void AdjustmentSurfaceTests::theSurfaceComesBackUnderThePaintersOwnState()
{
    const auto body = [](QPainter &target) { target.fillRect(QRectF(0, 0, 8, 6), QColor(128, 128, 128)); };
    // Opacity.
    QImage faded = solid(8, 6, qRgba(200, 100, 50, 255));
    {
        QPainter painter(&faded);
        painter.setOpacity(0);
        AdjustmentSurface::draw(painter, body);
        QCOMPARE(painter.opacity(), 0.0);
    }
    QCOMPARE(faded, solid(8, 6, qRgba(200, 100, 50, 255)));
    // Composition mode.
    QImage multiplied = solid(8, 6, qRgba(200, 100, 50, 255));
    {
        QPainter painter(&multiplied);
        painter.setCompositionMode(QPainter::CompositionMode_Multiply);
        AdjustmentSurface::draw(painter, body);
        QCOMPARE(painter.compositionMode(), QPainter::CompositionMode_Multiply);
    }
    QCOMPARE(multiplied.pixel(7, 5), qRgba(100, 50, 25, 255));
    // A clip of two rectangles; the painter's transform stays.
    QImage clipped = solid(8, 6, qRgba(200, 100, 50, 255));
    {
        QPainter painter(&clipped);
        painter.translate(1, 1);
        painter.setClipRegion(QRegion(0, 0, 2, 2) + QRegion(4, 2, 2, 2));
        AdjustmentSurface::draw(painter, body);
        QCOMPARE(painter.worldTransform(), QTransform::fromTranslate(1, 1));
    }
    QCOMPARE(clipped.pixel(1, 1), qRgba(128, 128, 128, 255));
    QCOMPARE(clipped.pixel(5, 3), qRgba(128, 128, 128, 255));
    QCOMPARE(clipped.pixel(4, 2), qRgba(200, 100, 50, 255));
    QCOMPARE(clipped.pixel(0, 0), qRgba(200, 100, 50, 255));
}

void AdjustmentSurfaceTests::aSurfaceThatCannotBeAllocatedIsLoggedAndNothingIsDrawn()
{
    // The surface would need 256 MB.
    QImage device(8192, 8192, QImage::Format_Alpha8);
    device.fill(0);
    QPainter painter(&device);
    bool called = false;
    std::optional<AddressSpaceLimit> limit(std::in_place, 96 * 1024 * 1024);
    QTest::ignoreMessage(QtWarningMsg, "an adjustment surface could not be allocated: QSize(8192, 8192)");
    AdjustmentSurface::draw(painter, [&](QPainter &) { called = true; });
    limit.reset();
    painter.end();
    QVERIFY(!called);
    QCOMPARE(int(device.constScanLine(100)[100]), 0);
}

QTEST_MAIN(AdjustmentSurfaceTests)
#include "AdjustmentSurfaceTests.moc"
