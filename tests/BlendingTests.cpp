#include "Rendering/EditorCanvas.h"
#include "IO/ImageExporter.h"
#include "RenderFixtures.h"
#include <QPainter>
#include <QRadialGradient>
#include <QtTest>

// Compositor 1.4's blending: stacks, adjustments in a mode, Soft Light.
namespace {
ImportedImage filled(int width, int height, QColor colour, const QString &name)
{
    const QImage image = solid(width, height, colour.rgba());
    return ImportedImage(image, image, name);
}

// A canvas at 1:1 over the document.
struct Scene {
    EditorSession session;
    CanvasView canvas{session};
    Scene(int width, int height)
    {
        session.createDocument(width, height);
        session.setShowsTransformControls(false);
        canvas.resize(width + 40, height + 40);
        session.viewport.resize(QSizeF(width + 40, height + 40), 1, QSizeF(width, height));
        session.zoom(1);
    }
    QImage exported() { return ImageExporter::render(session.projectSnapshot().value()).image.convertToFormat(QImage::Format_ARGB32); }
    // Copy Merged of the whole canvas.
    QImage merged()
    {
        session.selectAll();
        const QImage image = session.renderMergedPixels().value().image.convertToFormat(QImage::Format_ARGB32);
        session.deselect();
        return image;
    }
    // The worst channel gap to the export, inside the border.
    int canvasGap()
    {
        const QImage expected = exported(), shot = canvas.grab().toImage().convertToFormat(QImage::Format_ARGB32);
        const QPointF origin = session.viewport.viewPoint(QPointF(0, 0), session.document().value().size());
        int worst = 0;
        for (int y = 1; y < expected.height() - 1; ++y) {
            for (int x = 1; x < expected.width() - 1; ++x) {
                const QRgb wanted = expected.pixel(x, y), shown = shot.pixel(int(origin.x() + x + 0.5), int(origin.y() + y + 0.5));
                worst = std::max({worst, std::abs(qRed(wanted) - qRed(shown)), std::abs(qGreen(wanted) - qGreen(shown)),
                                  std::abs(qBlue(wanted) - qBlue(shown))});
            }
        }
        return worst;
    }
    void mode(LayerBlendMode mode)
    {
        session.setLayerBlendMode(mode);
    }
};

std::array<int, 3> rgb(QRgb pixel)
{
    return {qRed(pixel), qGreen(pixel), qBlue(pixel)};
}

bool near(QRgb pixel, std::array<int, 3> expected, int tolerance)
{
    const std::array<int, 3> actual = rgb(pixel);
    for (int channel = 0; channel < 3; ++channel) {
        if (std::abs(actual[size_t(channel)] - expected[size_t(channel)]) > tolerance)
            return false;
    }
    return true;
}

QString text(QRgb pixel)
{
    return QStringLiteral("(%1, %2, %3)").arg(qRed(pixel)).arg(qGreen(pixel)).arg(qBlue(pixel));
}
}

class BlendingTests : public QObject {
    Q_OBJECT
private slots:
    void clippingStacksBlendInTheirBasesMode_data();
    void clippingStacksBlendInTheirBasesMode();
    void adjustmentsBlendInAnyMode_data();
    void adjustmentsBlendInAnyMode();
    void softLightMatchesPhotoshop();
    void softEdgedLayersBlendTheSame_data();
    void softEdgedLayersBlendTheSame();
};

void BlendingTests::clippingStacksBlendInTheirBasesMode_data()
{
    QTest::addColumn<LayerBlendMode>("mode");
    QTest::newRow("linearDodge") << LayerBlendMode::linearDodge;
    QTest::newRow("colorDodge") << LayerBlendMode::colorDodge;
    QTest::newRow("multiply") << LayerBlendMode::multiply;
}

// Swift's: the stack takes its base's mode wherever drawn.
void BlendingTests::clippingStacksBlendInTheirBasesMode()
{
    QFETCH(LayerBlendMode, mode);
    Scene scene(100, 100);
    EditorSession &session = scene.session;
    session.insert(filled(100, 100, QColor::fromRgbF(0.4f, 0.2f, 0.1f), QStringLiteral("Base")));
    session.insert(filled(100, 100, QColor::fromRgbF(0.3f, 0.3f, 0.3f), QStringLiteral("Blended")));
    const QUuid blended = session.activeLayerID().value();
    scene.mode(mode);
    session.insert(filled(50, 50, QColor::fromRgbF(0.2f, 0.05f, 0), QStringLiteral("Clipped")), QPointF(25, 25));
    QVERIFY(session.linkMask(blended, session.activeLayerID().value()));
    const QImage exported = scene.exported(), merged = scene.merged();
    // Under the clipped layer, and the stack's base alone.
    for (const QPoint point : {QPoint(20, 20), QPoint(80, 80)})
        QCOMPARE(text(merged.pixel(point)), text(exported.pixel(point)));
    if (mode == LayerBlendMode::linearDodge) {
        QVERIFY2(near(exported.pixel(20, 20), {153, 64, 26}, 1), qPrintable(text(exported.pixel(20, 20))));
        QVERIFY2(near(exported.pixel(80, 80), {179, 128, 102}, 1), qPrintable(text(exported.pixel(80, 80))));
    }
    QVERIFY(text(exported.pixel(20, 20)) != text(exported.pixel(80, 80)));
    QVERIFY(scene.canvasGap() <= 1);
}

void BlendingTests::adjustmentsBlendInAnyMode_data()
{
    QTest::addColumn<LayerBlendMode>("mode");
    QTest::addColumn<bool>("clipped");
    QTest::addColumn<QList<int>>("expected");
    // (0.2, 0.4, 0.6) inverted is (0.8, 0.6, 0.4).
    QTest::newRow("difference") << LayerBlendMode::difference << false << QList<int>{153, 51, 51};
    QTest::newRow("linearLight") << LayerBlendMode::linearLight << false << QList<int>{204, 153, 102};
    QTest::newRow("vividLight in a stack") << LayerBlendMode::vividLight << true << QList<int>{128, 128, 128};
    QTest::newRow("subtract in a stack") << LayerBlendMode::subtract << true << QList<int>{0, 0, 51};
}

// Blended at full coverage, alpha restored, then mixed by opacity.
void BlendingTests::adjustmentsBlendInAnyMode()
{
    QFETCH(LayerBlendMode, mode);
    QFETCH(bool, clipped);
    QFETCH(QList<int>, expected);
    Scene scene(40, 30);
    EditorSession &session = scene.session;
    session.insert(filled(40, 30, QColor::fromRgbF(0.2f, 0.4f, 0.6f), QStringLiteral("Photo")));
    const QUuid photo = session.activeLayerID().value();
    session.addAdjustment(AdjustmentKind::invert);
    session.setAdjustmentEditingID(std::nullopt);
    const QUuid adjustment = session.activeLayerID().value();
    scene.mode(mode);
    if (clipped)
        QVERIFY(session.linkMask(photo, adjustment));
    const QImage exported = scene.exported();
    const QRgb shown = exported.pixel(10, 10);
    QVERIFY2(near(shown, {expected[0], expected[1], expected[2]}, 1), qPrintable(text(shown)));
    QCOMPARE(qAlpha(shown), 255);
    QCOMPARE(text(scene.merged().pixel(10, 10)), text(shown));
    QVERIFY(scene.canvasGap() <= 1);
    // At half opacity the result mixes halfway back.
    session.selectLayer(adjustment);
    session.setLayerOpacity(0.5);
    const QRgb half = scene.exported().pixel(10, 10);
    QVERIFY2(near(half, {(expected[0] + 51) / 2, (expected[1] + 102) / 2, (expected[2] + 153) / 2}, 1), qPrintable(text(half)));
}

// Photoshop's: 2·b·(1 − s) + √b·(2s − 1) above half.
void BlendingTests::softLightMatchesPhotoshop()
{
    const auto blended = [](double backdrop, double source) {
        Scene scene(10, 10);
        scene.session.insert(filled(10, 10, QColor::fromRgbF(float(backdrop), float(backdrop), float(backdrop)), QStringLiteral("Base")));
        scene.session.insert(filled(10, 10, QColor::fromRgbF(float(source), float(source), float(source)), QStringLiteral("Soft")));
        scene.mode(LayerBlendMode::softLight);
        const int exported = qRed(scene.exported().pixel(5, 5));
        if (qRed(scene.merged().pixel(5, 5)) != exported || scene.canvasGap() > 1)
            return -1;
        return exported;
    };
    // 2·0.5·0.1 + √0.5·0.8 = 0.666: 170.
    QCOMPARE(blended(0.5, 0.9), 170);
    // Dark: √(5/255) is 0.140, so 36; the PDF gives 19.
    QCOMPARE(blended(5 / 255.0, 1), 36);
    // Below half, 2bs + b²(1 − 2s): 0.1375 here.
    QCOMPARE(blended(0.25, 0.2), 35);
}

void BlendingTests::softEdgedLayersBlendTheSame_data()
{
    QTest::addColumn<LayerBlendMode>("mode");
    QTest::newRow("softLight") << LayerBlendMode::softLight;
    QTest::newRow("overlay") << LayerBlendMode::overlay;
    QTest::newRow("multiply") << LayerBlendMode::multiply;
    QTest::newRow("colorDodge") << LayerBlendMode::colorDodge;
}

// A soft brush's layer, mostly partly clear, alike everywhere.
void BlendingTests::softEdgedLayersBlendTheSame()
{
    QFETCH(LayerBlendMode, mode);
    Scene scene(60, 50);
    const QImage photo = noise(60, 50, 4);
    scene.session.insert(ImportedImage(photo, photo, QStringLiteral("Photo")));
    QImage soft(28, 18, QImage::Format_RGBA8888_Premultiplied);
    soft.fill(Qt::transparent);
    {
        QPainter painter(&soft);
        QRadialGradient gradient(QPointF(14, 9), 11);
        gradient.setColorAt(0.3, QColor::fromRgbF(1, 0.05f, 0, 1));
        gradient.setColorAt(1, QColor::fromRgbF(1, 0.05f, 0, 0));
        painter.fillRect(soft.rect(), gradient);
    }
    scene.session.insert(ImportedImage(soft, soft, QStringLiteral("Paint")), QPointF(30, 23));
    scene.mode(mode);
    const QImage exported = scene.exported();
    // Partly clear pixels blend, as do the opaque ones.
    QVERIFY(exported.pixel(30, 23) != photo.pixel(30, 23) && exported.pixel(23, 23) != photo.pixel(23, 23));
    QCOMPARE(scene.merged(), exported);
    QVERIFY(scene.canvasGap() <= 1);
}

QTEST_MAIN(BlendingTests)
#include "BlendingTests.moc"
