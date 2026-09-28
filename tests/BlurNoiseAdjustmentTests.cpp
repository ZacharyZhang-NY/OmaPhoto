#include "Rendering/EditorCanvas.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectStore+Json.h"
#include "ProjectFixtures.h"
#include "RenderFixtures.h"
#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>
extern "C" {
#include "NoisePixels.h"
}

// Blur and noise adjustment layers: 1.2.3's three kinds.
namespace {
constexpr double notANumber = std::numeric_limits<double>::quiet_NaN();

ImportedImage filled(int width, int height, QRgb premultiplied, const QString &name)
{
    const QImage image = solid(width, height, premultiplied);
    return ImportedImage(image, image, name);
}

bool refused(const QJsonObject &object)
{
    try {
        ManifestJson::adjustment(object);
    } catch (const ProjectError &) {
        return true;
    }
    return false;
}

// Saving and loading judge alike.
std::optional<ProjectError::Kind> stored(const ProjectSnapshot &snapshot)
{
    QTemporaryDir root;
    const std::optional<ProjectError::Kind> saving = projectError([&] { ProjectStore::save(snapshot, root.filePath("Saved.comp")); });
    ProjectStore::save(twoLayers(), root.filePath("Read.comp"));
    for (const auto &[id, pixels] : snapshot.images) {
        if (!pixels.image().save(root.filePath("Read.comp/images/") + uuidString(id) + ".png"))
            throw std::runtime_error("cannot write a fixture image");
    }
    overwrite(root.filePath("Read.comp/manifest.json"), snapshot.manifest.encoded());
    if (saving != projectError([&] { ProjectStore::load(root.filePath("Read.comp")); }))
        throw std::runtime_error("saving and loading judged the project apart");
    return saving;
}

// White, black over the left half's top square, the adjustment.
QUuid blurred(EditorSession &session, const LayerAdjustment &adjustment)
{
    const QSizeF size = session.document().value().size();
    const int side = int(size.width()) / 2;
    session.insert(filled(int(size.width()), int(size.height()), qRgba(255, 255, 255, 255), "White"), QPointF(size.width() / 2, size.height() / 2));
    session.insert(filled(side, side, qRgba(0, 0, 0, 255), "Black"), QPointF(side / 2.0, side / 2.0));
    session.addAdjustment(adjustment.kind);
    session.setAdjustmentEditingID(std::nullopt);
    const QUuid id = session.activeLayerID().value();
    session.updateAdjustment(id, adjustment);
    return id;
}

LayerAdjustment gaussian(double radius)
{
    LayerAdjustment adjustment{AdjustmentKind::gaussianBlur};
    adjustment.setGaussianRadius(radius);
    return adjustment;
}

// A canvas this size at a zoom, no overlay.
void frame(EditorSession &session, CanvasView &canvas, QSize view, double zoom)
{
    session.setShowsTransformControls(false);
    canvas.resize(view);
    session.viewport.resize(QSizeF(view), 1, session.document().value().size());
    session.zoom(zoom);
}

int level(const QImage &image, int x, int y)
{
    return qGray(image.pixel(x, y));
}
}

class BlurNoiseAdjustmentTests : public QObject {
    Q_OBJECT
private slots:
    void settingsFallBackToSwiftsDefaultsAndBound();
    void theHaloCoversEachKindsReach();
    void theManifestWritesTheKeysOnlyWhenSet();
    void theStoreTakesTheKindsFromVersion9();
    void eachNoiseLayerRollsItsOwnSeed();
    void eachEditorWritesItsOwnSettingsAlone();
    void theKindsRunTheirFiltersAtTheScale();
    void noiseStaysOneFieldAcrossRegions();
    void aScaledTargetBlursInItsOwnPixels();
    void theCanvasBlursAtItsZoom();
    void aPartialRepaintReadsTheHalo();
};

void BlurNoiseAdjustmentTests::settingsFallBackToSwiftsDefaultsAndBound()
{
    LayerAdjustment adjustment{AdjustmentKind::levels};
    QVERIFY(!adjustment.blurRadius && !adjustment.motionAngle && !adjustment.motionDistance && !adjustment.noiseAmount);
    QVERIFY(!adjustment.noiseGaussian && !adjustment.noiseMonochromatic && !adjustment.noiseSeed);
    QVERIFY(adjustment.gaussianRadius() == 10 && adjustment.resolvedMotionAngle() == 0 && adjustment.resolvedMotionDistance() == 10);
    QVERIFY(adjustment.resolvedNoiseAmount() == 10 && !adjustment.resolvedNoiseGaussian() && !adjustment.resolvedNoiseMonochromatic());
    QCOMPARE(adjustment.resolvedNoiseSeed(), 0u);
    // Each setter fills its own optional.
    adjustment.setGaussianRadius(3);
    adjustment.setResolvedMotionAngle(-20);
    adjustment.setResolvedMotionDistance(40);
    adjustment.setResolvedNoiseAmount(70);
    adjustment.setResolvedNoiseGaussian(true);
    adjustment.setResolvedNoiseMonochromatic(true);
    adjustment.setResolvedNoiseSeed(12);
    QVERIFY(adjustment.blurRadius == 3.0 && adjustment.motionAngle == -20.0 && adjustment.motionDistance == 40.0 && adjustment.noiseAmount == 70.0);
    QVERIFY(adjustment.noiseGaussian == true && adjustment.noiseMonochromatic == true && adjustment.noiseSeed == 12u);
    QVERIFY(adjustment.isValid());
    // Swift's ranges bind every kind, NaN included.
    const std::pair<void (LayerAdjustment::*)(double), std::vector<std::pair<double, bool>>> bounds[] = {
        {&LayerAdjustment::setGaussianRadius, {{0.1, true}, {250, true}, {0.09, false}, {250.5, false}, {notANumber, false}}},
        {&LayerAdjustment::setResolvedMotionAngle, {{-90, true}, {90, true}, {-90.5, false}, {90.5, false}, {notANumber, false}}},
        {&LayerAdjustment::setResolvedMotionDistance, {{1, true}, {2000, true}, {0.5, false}, {2000.5, false}, {notANumber, false}}},
        {&LayerAdjustment::setResolvedNoiseAmount, {{0.1, true}, {400, true}, {0.05, false}, {400.5, false}, {notANumber, false}}}};
    for (const auto &[set, values] : bounds) {
        for (const auto &[value, valid] : values) {
            LayerAdjustment bounded{AdjustmentKind::hsv};
            (bounded.*set)(value);
            QVERIFY2(bounded.isValid() == valid, qPrintable(QString::number(value)));
        }
    }
}

void BlurNoiseAdjustmentTests::theHaloCoversEachKindsReach()
{
    // Swift's samplingMargin: three sigmas plus two, half streak plus two.
    QCOMPARE(gaussian(4).samplingMargin(), 14.0);
    LayerAdjustment motion{AdjustmentKind::motionBlur};
    motion.setResolvedMotionDistance(20);
    QCOMPARE(motion.samplingMargin(), 12.0);
    for (const AdjustmentKind kind : allAdjustmentKinds) {
        if (kind != AdjustmentKind::gaussianBlur && kind != AdjustmentKind::motionBlur)
            QCOMPARE(LayerAdjustment{kind}.samplingMargin(), 0.0);
    }
}

void BlurNoiseAdjustmentTests::theManifestWritesTheKeysOnlyWhenSet()
{
    QCOMPARE(ManifestJson::encoded(LayerAdjustment{AdjustmentKind::addNoise}).keys(),
             (QStringList{"colorize", "curves", "hue", "kind", "levels", "lightness", "saturation"}));
    LayerAdjustment set{AdjustmentKind::motionBlur};
    set.setGaussianRadius(2.5);
    set.setResolvedMotionAngle(-45);
    set.setResolvedMotionDistance(30);
    set.setResolvedNoiseAmount(25);
    set.setResolvedNoiseGaussian(true);
    set.setResolvedNoiseMonochromatic(false);
    set.setResolvedNoiseSeed(4'294'967'295u);
    const QJsonObject object = ManifestJson::encoded(set);
    QCOMPARE(object.value("kind").toString(), QString("Motion Blur"));
    QVERIFY(object.value("blurRadius") == 2.5 && object.value("motionAngle") == -45 && object.value("motionDistance") == 30);
    QVERIFY(object.value("noiseAmount") == 25 && object.value("noiseGaussian") == true && object.value("noiseMonochromatic") == false);
    QCOMPARE(object.value("noiseSeed").toInteger(), qint64(4'294'967'295));
    QVERIFY(ManifestJson::adjustment(object) == set);
    // Absent or null is nil; wrong types and seeds refuse.
    for (const char *key : {"blurRadius", "motionAngle", "motionDistance", "noiseAmount", "noiseGaussian", "noiseMonochromatic", "noiseSeed"}) {
        QJsonObject changed = object;
        changed.insert(QLatin1String(key), QJsonValue::Null);
        QVERIFY2(!refused(changed) && ManifestJson::adjustment(changed) != set, key);
        changed.remove(QLatin1String(key));
        QVERIFY2(!refused(changed), key);
        changed.insert(QLatin1String(key), QJsonArray());
        QVERIFY2(refused(changed), key);
    }
    for (const QJsonValue &seed : {QJsonValue(-1), QJsonValue(qint64(4'294'967'296)), QJsonValue(1.5)}) {
        QJsonObject changed = object;
        changed.insert("noiseSeed", seed);
        QVERIFY(refused(changed));
    }
}

void BlurNoiseAdjustmentTests::theStoreTakesTheKindsFromVersion9()
{
    for (const AdjustmentKind kind : {AdjustmentKind::gaussianBlur, AdjustmentKind::motionBlur, AdjustmentKind::addNoise}) {
        ProjectSnapshot snapshot = twoLayers();
        snapshot.manifest.layers[1].adjustment = LayerAdjustment{kind};
        QCOMPARE(stored(snapshot), std::nullopt);
        snapshot.manifest.version = 8;
        QCOMPARE(stored(snapshot), std::optional(ProjectError::Kind::invalid));
        // Invalid settings refuse it at any version.
        snapshot.manifest.version = 9;
        snapshot.manifest.layers[1].adjustment->setGaussianRadius(300);
        QCOMPARE(stored(snapshot), std::optional(ProjectError::Kind::invalid));
    }
    // The rule reads the kind: older kinds may carry keys.
    ProjectSnapshot older = twoLayers();
    older.manifest.version = 7;
    older.manifest.layers[1].adjustment = LayerAdjustment{AdjustmentKind::levels};
    older.manifest.layers[1].adjustment->setGaussianRadius(5);
    QCOMPARE(stored(older), std::nullopt);
}

void BlurNoiseAdjustmentTests::eachNoiseLayerRollsItsOwnSeed()
{
    EditorSession session;
    session.createDocument(4, 4);
    session.addAdjustment(AdjustmentKind::addNoise);
    const LayerAdjustment first = session.activeLayer().value().adjustment.value();
    session.setAdjustmentEditingID(std::nullopt);
    session.addAdjustment(AdjustmentKind::addNoise);
    const LayerAdjustment second = session.activeLayer().value().adjustment.value();
    QVERIFY(first.noiseSeed && second.noiseSeed && *first.noiseSeed != *second.noiseSeed);
    // Only noise rolls one; the rest keep their defaults.
    session.setAdjustmentEditingID(std::nullopt);
    session.addAdjustment(AdjustmentKind::gaussianBlur);
    QVERIFY(session.activeLayer().value().adjustment.value() == LayerAdjustment{AdjustmentKind::gaussianBlur});
}

void BlurNoiseAdjustmentTests::eachEditorWritesItsOwnSettingsAlone()
{
    for (const AdjustmentKind kind : {AdjustmentKind::gaussianBlur, AdjustmentKind::motionBlur, AdjustmentKind::addNoise}) {
        EditorSession session;
        session.createDocument(4, 4);
        session.insert(filled(4, 4, qRgba(255, 255, 255, 255), "White"));
        session.addAdjustment(kind);
        const QUuid id = session.activeLayerID().value();
        // Every field set: the panel reads them all.
        LayerAdjustment preset = session.activeLayer().value().adjustment.value();
        preset.setGaussianRadius(20);
        preset.setResolvedMotionAngle(45);
        preset.setResolvedMotionDistance(30);
        preset.setResolvedNoiseAmount(50);
        preset.setResolvedNoiseGaussian(true);
        preset.setResolvedNoiseMonochromatic(true);
        session.updateAdjustment(id, preset);
        bool open = false;
        session.beginAdjustmentEditing(id, [&open] { open = true; });
        QTRY_VERIFY(open);
        const FilterEdit &edit = session.filterEdit().value();
        QVERIFY(edit.kind == filterKind(kind));
        QVERIFY(edit.settings.radius == 20 && edit.settings.angle == 45 && edit.settings.distance == 30 && edit.settings.amount == 50);
        QVERIFY(edit.settings.gaussian && edit.settings.monochromatic);
        session.updateFilter(FilterSettings{.radius = 5, .angle = -30, .distance = 12, .amount = 70}, true);
        // The kind writes its own; the others keep the preset.
        const LayerAdjustment live = session.activeLayer().value().adjustment.value();
        const bool blur = kind == AdjustmentKind::gaussianBlur, motion = kind == AdjustmentKind::motionBlur, noise = !blur && !motion;
        QCOMPARE(live.blurRadius, std::optional(blur ? 5.0 : 20.0));
        QCOMPARE(live.motionAngle, std::optional(motion ? -30.0 : 45.0));
        QCOMPARE(live.motionDistance, std::optional(motion ? 12.0 : 30.0));
        QCOMPARE(live.noiseAmount, std::optional(noise ? 70.0 : 50.0));
        QCOMPARE(live.noiseGaussian, std::optional(!noise));
        QCOMPARE(live.noiseMonochromatic, std::optional(!noise));
        // The seed stays the layer's own.
        QCOMPARE(live.noiseSeed.has_value(), noise);
        session.cancelFilter();
    }
}

void BlurNoiseAdjustmentTests::theKindsRunTheirFiltersAtTheScale()
{
    QImage image = BrushRaster::context(12, 12, false);
    image.fill(0);
    QPainter(&image).fillRect(QRect(3, 3, 6, 6), QColor(200, 100, 50));
    const auto filtered = [&image](FilterKind kind, const FilterSettings &settings, quint32 seed = 0) {
        return PixelFilter::run(FilterJob{kind, image, settings, 1, std::nullopt, QTransform(), seed});
    };
    // At scale two a blur reads twice the radius.
    QCOMPARE(gaussian(1.5).apply(image, std::nullopt, 2), filtered(FilterKind::gaussianBlur, FilterSettings{.radius = 3}));
    LayerAdjustment motion{AdjustmentKind::motionBlur};
    motion.setResolvedMotionAngle(30);
    motion.setResolvedMotionDistance(4);
    QCOMPARE(motion.apply(image, std::nullopt, 2), filtered(FilterKind::motionBlur, FilterSettings{.angle = 30, .distance = 8}));
    LayerAdjustment noise{AdjustmentKind::addNoise};
    noise.setResolvedNoiseAmount(60);
    noise.setResolvedNoiseGaussian(true);
    noise.setResolvedNoiseMonochromatic(true);
    noise.setResolvedNoiseSeed(77);
    QCOMPARE(noise.apply(image), filtered(FilterKind::addNoise, FilterSettings{.amount = 60, .gaussian = true, .monochromatic = true}, 77));
    QVERIFY(noise.apply(image) != image);
}

void BlurNoiseAdjustmentTests::noiseStaysOneFieldAcrossRegions()
{
    QImage gray = BrushRaster::context(8, 8, false);
    gray.fill(QColor(128, 128, 128));
    LayerAdjustment noise{AdjustmentKind::addNoise};
    noise.setResolvedNoiseAmount(50);
    noise.setResolvedNoiseSeed(3);
    // A region's pixels are the whole field's there.
    const QImage whole = noise.apply(gray, QRectF(10, 20, 8, 8));
    QCOMPARE(noise.apply(gray.copy(2, 3, 4, 4), QRectF(12, 23, 4, 4)), whole.copy(2, 3, 4, 4));
    // The kernel's origin, in the image's own pixels.
    QImage expected = gray.copy();
    noise_add_at(expected.bits(), 8, 8, size_t(expected.bytesPerLine()), 50, 0, 0, 3, 20, 40);
    QCOMPARE(noise.apply(gray, QRectF(10, 20, 4, 4)), expected);
    // A negative origin floors, as Swift rounds down.
    expected = gray.copy();
    noise_add_at(expected.bits(), 8, 8, size_t(expected.bytesPerLine()), 50, 0, 0, 3, -11, -21);
    QCOMPARE(noise.apply(gray, QRectF(-10.5, -20.5, 8, 8)), expected);
    // Without a region, the origin is zero.
    expected = gray.copy();
    noise_add_at(expected.bits(), 8, 8, size_t(expected.bytesPerLine()), 50, 0, 0, 3, 0, 0);
    QCOMPARE(noise.apply(gray), expected);
}

void BlurNoiseAdjustmentTests::aScaledTargetBlursInItsOwnPixels()
{
    // Drawn twice as large, the blur doubles in pixels.
    EditorSession session;
    session.createDocument(16, 16);
    blurred(session, gaussian(1.5));
    QImage twice = BrushRaster::context(32, 32, false);
    twice.fill(0);
    {
        QPainter painter(&twice);
        painter.scale(2, 2);
        session.drawLiveComposite(session.document().value(), painter);
    }
    session.toggleLayerVisibility(session.activeLayerID().value());
    QImage plain = BrushRaster::context(32, 32, false);
    plain.fill(0);
    {
        QPainter painter(&plain);
        painter.scale(2, 2);
        session.drawLiveComposite(session.document().value(), painter);
    }
    QCOMPARE(twice, gaussian(1.5).apply(plain, QRectF(0, 0, 16, 16), 2));
}

void BlurNoiseAdjustmentTests::theCanvasBlursAtItsZoom()
{
    // At half size radius 8 spreads 4 view points.
    EditorSession session;
    session.createDocument(200, 100);
    CanvasView canvas(session);
    blurred(session, gaussian(8));
    frame(session, canvas, QSize(200, 100), 0.5);
    QCOMPARE(session.viewport.viewPoint(QPointF(100, 50), QSizeF(200, 100)), QPointF(100, 50));
    const QImage shot = canvas.grab().toImage();
    // 0.875 sigma out 19% of the black spreads; 1.875, 3%.
    const int near = level(shot, 103, 50), far = level(shot, 107, 50);
    QVERIFY2(near > 195 && near < 220, qPrintable(QString::number(near)));
    QVERIFY2(far > 240, qPrintable(QString::number(far)));
}

void BlurNoiseAdjustmentTests::aPartialRepaintReadsTheHalo()
{
    // A dirty rect by the edge sees pixels past it.
    for (const double zoom : {1.0, 1.5, 4.0}) {
        EditorSession session;
        session.createDocument(60, 60);
        CanvasView canvas(session);
        blurred(session, gaussian(3));
        // A hair-thin blur above: the widest margin must win.
        session.addAdjustment(AdjustmentKind::gaussianBlur);
        session.setAdjustmentEditingID(std::nullopt);
        session.updateAdjustment(session.activeLayerID().value(), gaussian(0.2));
        frame(session, canvas, QSize(240, 240), zoom);
        const QImage full = canvas.grab().toImage();
        const QPointF edge = session.viewport.viewPoint(QPointF(30, 15), QSizeF(60, 60));
        const QRect dirty(int(edge.x()) - 3, int(edge.y()) - 3, 6, 6);
        QImage part(canvas.size(), QImage::Format_ARGB32_Premultiplied);
        part.fill(0);
        canvas.render(&part, dirty.topLeft(), QRegion(dirty));
        for (int y = dirty.top(); y <= dirty.bottom(); ++y) {
            for (int x = dirty.left(); x <= dirty.right(); ++x)
                QVERIFY2(part.pixel(x, y) == full.pixel(x, y), qPrintable(QStringLiteral("%1 at %2, %3").arg(zoom).arg(x).arg(y)));
        }
    }
}

QTEST_MAIN(BlurNoiseAdjustmentTests)
#include "BlurNoiseAdjustmentTests.moc"
