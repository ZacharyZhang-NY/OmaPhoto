#include "Document/EditorSession.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectStore+Json.h"
#include "SessionFixtures.h"
#include <QTemporaryDir>
#include <QtTest>

// Swift's AdjustmentLayerTests and AdjustmentEditorTests, as far as they reach.
namespace {
// Swift's fixture: two by two, one colour, these alphas.
ImportedImage image(const PaletteColor &colour, const std::array<int, 4> &alpha = {255, 255, 255, 255})
{
    QImage pixels(2, 2, QImage::Format_RGBA8888_Premultiplied);
    for (int index = 0; index < 4; ++index) {
        uchar *pixel = pixels.scanLine(index / 2) + (index % 2) * 4;
        const int a = alpha[size_t(index)];
        pixel[0] = uchar(std::lround(colour.red * a));
        pixel[1] = uchar(std::lround(colour.green * a));
        pixel[2] = uchar(std::lround(colour.blue * a));
        pixel[3] = uchar(a);
    }
    return ImportedImage(pixels, pixels, QStringLiteral("Fixture"));
}

// Premultiplied bytes, row after row.
std::vector<int> pixels(const QImage &image)
{
    const QImage drawn = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    std::vector<int> result;
    for (int y = 0; y < drawn.height(); ++y)
        result.insert(result.end(), drawn.constScanLine(y), drawn.constScanLine(y) + drawn.width() * 4);
    return result;
}

std::vector<int> rendered(const EditorSession &session)
{
    return pixels(ImageExporter::render(session.projectSnapshot().value()).image);
}

std::vector<int> part(const std::vector<int> &bytes, size_t from, size_t to)
{
    return std::vector<int>(bytes.begin() + qsizetype(from), bytes.begin() + qsizetype(to));
}

// Swift's `add`: no editor opens here.
QUuid add(AdjustmentKind kind, EditorSession &session)
{
    session.addAdjustment(kind);
    session.setAdjustmentEditingID(std::nullopt);
    return session.activeLayerID().value();
}
}

class AdjustmentLayerTests : public QObject {
    Q_OBJECT
private slots:
    void globalAdjustmentAffectsBelowButNotAboveAndRemainsLive();
    void clippedCurveChangesOnlyItsBaseAndCopyMergedMatchesExport();
    void hueOpacityAndMaskPreserveOriginalPixels();
    void adjustmentPersistsDuplicatesAndUndoRestoresSettings();
    void curvesIdentityAndImageCommandPreserveAlpha();
    void adjustmentBlendAndSoftMaskPreserveCoverage();
    void legacyHSVStillDecodesAndRenders();
    void theLiveCompositeDrawsAsTheExport();
    void opacityMixesAndGrainSitsInTheDocument();
    void everyKindAdjustsThroughItsSettings();
    void invertAppliesWithoutAnEditor();
    void aFoldersMaskClipsTheAdjustmentsInside();
    void anyImageTargetTakesTheAdjustment();
};

void AdjustmentLayerTests::globalAdjustmentAffectsBelowButNotAboveAndRemainsLive()
{
    EditorSession s;
    s.createDocument(2, 2);
    s.insert(image(PaletteColor::white()));
    const QUuid base = s.activeLayerID().value();
    const QUuid adjustment = add(AdjustmentKind::levels, s);
    LayerAdjustment settings{AdjustmentKind::levels};
    settings.levels.ranges[0].outputWhite = 0;
    s.updateAdjustment(adjustment, settings);
    QVERIFY((rendered(s) == std::vector<int>{0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255}));
    s.insert(image(PaletteColor{1, 0, 0}, {255, 0, 0, 0}));
    QVERIFY((part(rendered(s), 0, 4) == std::vector<int>{255, 0, 0, 255}));
    rewrite(s, [&](ProjectSnapshot &snapshot) { snapshot.images.insert_or_assign(base, image(PaletteColor{0, 0, 1})); });
    QCOMPARE(s.document().value().layers[0].id, base);
    QVERIFY((part(rendered(s), 4, 8) == std::vector<int>{0, 0, 0, 255}));
    s.selectLayer(adjustment);
    s.toggleLayerVisibility(adjustment);
    QVERIFY((part(rendered(s), 4, 8) == std::vector<int>{0, 0, 255, 255}));
}

void AdjustmentLayerTests::clippedCurveChangesOnlyItsBaseAndCopyMergedMatchesExport()
{
    EditorSession s;
    s.createDocument(2, 2);
    s.insert(image(PaletteColor{0, 0, 1}));
    s.insert(image(PaletteColor{0, 1, 0}, {255, 0, 128, 0}));
    const QUuid adjustment = add(AdjustmentKind::curves, s);
    LayerAdjustment settings{AdjustmentKind::curves};
    settings.curves.channels[0] = {{0, 255}, {255, 0}};
    s.updateAdjustment(adjustment, settings);
    s.toggleClippingMask(adjustment);
    const std::vector<int> result = rendered(s);
    QVERIFY((part(result, 0, 4) == std::vector<int>{255, 0, 255, 255}));
    QVERIFY((part(result, 4, 8) == std::vector<int>{0, 0, 255, 255}));
    QVERIFY(pixels(s.renderMergedPixels().value().image) == result);
    s.toggleClippingMask(adjustment);
    QVERIFY((part(rendered(s), 4, 8) == std::vector<int>{255, 255, 0, 255}));
}

void AdjustmentLayerTests::hueOpacityAndMaskPreserveOriginalPixels()
{
    EditorSession s;
    s.createDocument(2, 2);
    s.insert(image(PaletteColor{1, 0, 0}));
    const ImageIdentity original = s.document().value().layers[0].asset.value().identity();
    const QUuid id = add(AdjustmentKind::hsv, s);
    LayerAdjustment value{AdjustmentKind::hsv};
    value.hue = 120;
    s.updateAdjustment(id, value);
    const std::vector<int> result = rendered(s);
    QVERIFY2(result[1] > 250 && result[0] < 5 && result[2] < 5, qPrintable(QString::number(result[0])));
    s.setLayerOpacity(0);
    QVERIFY((part(rendered(s), 0, 4) == std::vector<int>{255, 0, 0, 255}));
    s.setLayerOpacity(1);
    // Swift's LayerMask.solid(revealing: false).
    s.addMask(false);
    QVERIFY((part(rendered(s), 0, 4) == std::vector<int>{255, 0, 0, 255}));
    QVERIFY(s.document().value().layers[0].asset.value().identity() == original);
}

void AdjustmentLayerTests::adjustmentPersistsDuplicatesAndUndoRestoresSettings()
{
    EditorSession s;
    s.createDocument(2, 2);
    s.insert(image(PaletteColor::white()));
    const QUuid id = add(AdjustmentKind::curves, s);
    LayerAdjustment value{AdjustmentKind::curves};
    value.curves.channels[0].insert(value.curves.channels[0].begin() + 1, CurvePoint{128, 190});
    s.beginEdit(QStringLiteral("Edit Curves"));
    s.updateAdjustment(id, value);
    s.endEdit();
    s.undo();
    QVERIFY(s.activeLayer().value().adjustment.value().curves == CurvesSettings());
    s.redo();
    QVERIFY(s.activeLayer().value().adjustment == value);
    s.duplicateActiveLayer();
    QVERIFY(s.activeLayer().value().adjustment == value);
    QTemporaryDir root;
    const QString path = root.filePath(QStringLiteral("Adjustment-%1.comp").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    ProjectStore::save(s.projectSnapshot().value(), path);
    const ProjectSnapshot loaded = ProjectStore::load(path);
    QVERIFY(loaded.manifest.layers.back().adjustment == value);
    EditorSession restored;
    restored.installProject(loaded, path);
    QVERIFY(rendered(restored) == rendered(s));
}

void AdjustmentLayerTests::curvesIdentityAndImageCommandPreserveAlpha()
{
    const ImportedImage asset = image(PaletteColor{0.4, 0.7, 0.1}, {255, 128, 32, 0});
    QVERIFY(pixels(CurvesSettings().apply(asset.image())) == pixels(asset.image()));
    EditorSession s;
    s.createDocument(2, 2);
    s.insert(asset);
    s.beginFilter(FilterKind::curves);
    FilterSettings settings;
    settings.curves.channels[0] = {{0, 255}, {255, 255}};
    s.updateFilter(settings, true);
    bool done = false;
    s.commitFilter([&done] { done = true; });
    QTRY_VERIFY(done);
    QVERIFY((pixels(s.activeLayer().value().asset.value().image()) == std::vector<int>{255, 255, 255, 255, 128, 128, 128, 128, 32, 32, 32, 32, 0, 0, 0, 0}));
    s.undo();
    QVERIFY(s.activeLayer().value().asset.value().identity() == asset.identity());
}

void AdjustmentLayerTests::adjustmentBlendAndSoftMaskPreserveCoverage()
{
    EditorSession s;
    s.createDocument(2, 2);
    s.insert(image(PaletteColor{1, 0, 0}, {255, 128, 32, 0}));
    const QUuid id = add(AdjustmentKind::hsv, s);
    LayerAdjustment value{AdjustmentKind::hsv};
    value.hue = 120;
    s.updateAdjustment(id, value);
    s.setLayerBlendMode(LayerBlendMode::multiply);
    QVERIFY((rendered(s) == std::vector<int>{0, 0, 0, 255, 0, 0, 0, 128, 0, 0, 0, 32, 0, 0, 0, 0}));
    // Green over red adds to yellow; Swift adjusts as Normal.
    s.setLayerBlendMode(LayerBlendMode::linearDodge);
    QVERIFY((part(rendered(s), 0, 4) == std::vector<int>{255, 255, 0, 255}));
    QCOMPARE(rendered(s)[7], 128);
    s.setLayerBlendMode(LayerBlendMode::normal);
    QImage gray(1, 1, QImage::Format_Grayscale8);
    gray.fill(128);
    rewrite(s, [&](ProjectSnapshot &snapshot) { setMask(snapshot, id, LayerMask::assetFrom(gray)); });
    const std::vector<int> p = rendered(s);
    QVERIFY(p[3] == 255 && p[7] == 128 && p[11] == 32 && p[15] == 0);
    QVERIFY2(std::abs(p[0] - 127) <= 2 && std::abs(p[1] - 128) <= 2, qPrintable(QStringLiteral("%1 %2").arg(p[0]).arg(p[1])));
}

void AdjustmentLayerTests::legacyHSVStillDecodesAndRenders()
{
    const QJsonObject data = ManifestJson::encoded(LayerAdjustment{.kind = AdjustmentKind::hsv, .hue = 120});
    const LayerAdjustment decoded = ManifestJson::adjustment(data);
    QVERIFY(!decoded.hsvSettings);
    QCOMPARE(decoded.resolvedHSV().hue(), 120.0);
}

void AdjustmentLayerTests::theLiveCompositeDrawsAsTheExport()
{
    // A global Levels, a clipped Curve and a masked Hue.
    EditorSession s;
    s.createDocument(2, 2);
    s.insert(image(PaletteColor{0.2, 0.6, 1}, {255, 200, 128, 64}));
    const QUuid levels = add(AdjustmentKind::levels, s);
    LayerAdjustment darker{AdjustmentKind::levels};
    darker.levels.ranges[0].outputWhite = 180;
    s.updateAdjustment(levels, darker);
    s.insert(image(PaletteColor{1, 0.5, 0}, {255, 0, 255, 0}));
    const QUuid curve = add(AdjustmentKind::curves, s);
    LayerAdjustment inverted{AdjustmentKind::curves};
    inverted.curves.channels[0] = {{0, 255}, {255, 0}};
    s.updateAdjustment(curve, inverted);
    s.toggleClippingMask(curve);
    const QUuid hue = add(AdjustmentKind::hsv, s);
    s.updateAdjustment(hue, LayerAdjustment{.kind = AdjustmentKind::hsv, .hue = 90});
    s.addMask(true);
    s.setLayerOpacity(0.5);
    QImage live(2, 2, QImage::Format_RGBA8888_Premultiplied);
    live.fill(0);
    {
        QPainter painter(&live);
        s.drawLiveComposite(s.document().value(), painter);
    }
    QVERIFY(pixels(live) == rendered(s));
    // Under a translucent painter the composite fades once, whole.
    const QImage exported = ImageExporter::render(s.projectSnapshot().value()).image;
    QImage faded(2, 2, QImage::Format_RGBA8888_Premultiplied), expected(2, 2, QImage::Format_RGBA8888_Premultiplied);
    faded.fill(Qt::white);
    expected.fill(Qt::white);
    {
        QPainter painter(&faded);
        painter.setOpacity(0.5);
        s.drawLiveComposite(s.document().value(), painter);
        QPainter over(&expected);
        over.setOpacity(0.5);
        over.drawImage(QRectF(0, 0, 2, 2), exported);
    }
    const std::vector<int> got = pixels(faded), wanted = pixels(expected);
    for (size_t index = 0; index < got.size(); ++index)
        QVERIFY2(std::abs(got[index] - wanted[index]) <= 1, qPrintable(QString::number(index)));
}

void AdjustmentLayerTests::opacityMixesAndGrainSitsInTheDocument()
{
    // Half opacity lies halfway to the change.
    EditorSession s;
    s.createDocument(2, 2);
    s.insert(image(PaletteColor{1, 0, 0}));
    const QUuid hue = add(AdjustmentKind::hsv, s);
    s.updateAdjustment(hue, LayerAdjustment{.kind = AdjustmentKind::hsv, .hue = 120});
    s.setLayerOpacity(0.5);
    QVERIFY((part(rendered(s), 0, 4) == std::vector<int>{128, 128, 0, 255}));
    // Grain drawn for part of a document matches the whole.
    EditorSession grained;
    grained.createDocument(12, 12);
    QImage gray(12, 12, QImage::Format_RGBA8888_Premultiplied);
    gray.fill(QColor(128, 128, 128));
    grained.insert(ImportedImage(gray, gray, QStringLiteral("Gray")));
    const QUuid grain = add(AdjustmentKind::grain, grained);
    LayerAdjustment strong = grained.activeLayer().value().adjustment.value();
    GrainSettings settings = strong.grain();
    settings.amount = 100;
    strong.setGrain(settings);
    grained.updateAdjustment(grain, strong);
    const QImage whole = ImageExporter::render(grained.projectSnapshot().value()).image;
    QImage corner(5, 4, QImage::Format_RGBA8888_Premultiplied);
    corner.fill(0);
    {
        QPainter painter(&corner);
        painter.translate(-7, -8);
        grained.drawLiveComposite(grained.document().value(), painter);
    }
    QVERIFY(corner == whole.copy(7, 8, 5, 4));
    QVERIFY(corner != gray.copy(7, 8, 5, 4));
    // Twice as fine, each pixel is half a grain unit.
    QImage fine(24, 24, QImage::Format_RGBA8888_Premultiplied);
    fine.fill(0);
    {
        QPainter painter(&fine);
        painter.scale(2, 2);
        grained.drawLiveComposite(grained.document().value(), painter);
    }
    QImage flat(24, 24, QImage::Format_RGBA8888_Premultiplied);
    flat.fill(QColor(128, 128, 128));
    QVERIFY(fine == settings.apply(flat, QPointF(), 0.5));
}

void AdjustmentLayerTests::everyKindAdjustsThroughItsSettings()
{
    QImage gray(2, 2, QImage::Format_RGBA8888_Premultiplied);
    gray.fill(QColor(100, 150, 200));
    std::vector<LayerAdjustment> kinds;
    for (const AdjustmentKind kind : allAdjustmentKinds)
        kinds.push_back(LayerAdjustment{kind});
    kinds[0].hue = 60;
    kinds[1].levels.ranges[0].outputWhite = 100;
    kinds[2].curves.channels[0] = {{0, 50}, {255, 255}};
    kinds[3].setExposure(ExposureSettings{.exposure = 1});
    kinds[4].setGradientMap(GradientMapSettings{{1, 0, 0}, {0, 0, 1}, false});
    kinds[5].setGrain(GrainSettings{.amount = 80, .seed = 4});
    kinds[7].setBlackWhite(BlackWhiteSettings{.reds = 100});
    kinds[8].setColorBalance(ColorBalanceSettings{.midCyanRed = 60});
    for (const LayerAdjustment &adjustment : kinds) {
        EditorSession s;
        s.createDocument(2, 2);
        s.insert(ImportedImage(gray, gray, QStringLiteral("Colour")));
        s.updateAdjustment(add(adjustment.kind, s), adjustment);
        const std::vector<int> made = pixels(adjustment.apply(gray));
        QVERIFY2(made != pixels(gray) && rendered(s) == made, qPrintable(rawValue(adjustment.kind)));
    }
}

// Swift's: nothing to set, no editor; the pixels invert.
void AdjustmentLayerTests::invertAppliesWithoutAnEditor()
{
    EditorSession session;
    session.createDocument(2, 2);
    session.insert(image(PaletteColor::white()));
    session.addAdjustment(AdjustmentKind::invert);
    QVERIFY(!session.adjustmentEditingID());
    QCOMPARE(session.history.undoName(), QString("New Invert Adjustment"));
    const LayerAdjustment adjustment = session.activeLayer().value().adjustment.value();
    QVERIFY(adjustment.kind == AdjustmentKind::invert && !isEditable(adjustment.kind));
    QImage source(2, 2, QImage::Format_RGBA8888_Premultiplied);
    source.fill(QColor::fromRgbF(0.2f, 0.4f, 0.6f));
    source.setPixelColor(1, 1, QColor(102, 51, 153, 128));
    const QImage inverted = adjustment.apply(source);
    for (const QPoint at : {QPoint(0, 0), QPoint(1, 1)}) {
        const QColor before = source.pixelColor(at), after = inverted.pixelColor(at);
        QVERIFY(std::abs(before.red() + after.red() - 255) <= 1 && std::abs(before.green() + after.green() - 255) <= 1);
        QVERIFY(std::abs(before.blue() + after.blue() - 255) <= 1);
        // Transparency is left alone.
        QCOMPARE(after.alpha(), before.alpha());
    }
    // The export inverts the white beneath to black.
    QCOMPARE(ImageExporter::render(session.projectSnapshot().value()).image.pixelColor(0, 0), QColor(0, 0, 0));
}

void AdjustmentLayerTests::aFoldersMaskClipsTheAdjustmentsInside()
{
    // White beneath; a folder whose mask hides its left column.
    EditorSession s;
    s.createDocument(2, 2);
    s.insert(image(PaletteColor::white()));
    const QUuid levels = add(AdjustmentKind::levels, s);
    LayerAdjustment black{AdjustmentKind::levels};
    black.levels.ranges[0].outputWhite = 0;
    s.updateAdjustment(levels, black);
    s.groupSelectedLayers();
    const QUuid folder = s.activeLayerID().value();
    QImage right(2, 2, QImage::Format_Grayscale8);
    right.fill(255);
    right.setPixel(0, 0, 0);
    right.setPixel(0, 1, 0);
    rewrite(s, [&](ProjectSnapshot &snapshot) { setMask(snapshot, folder, LayerMask::assetFrom(right)); });
    QVERIFY((rendered(s) == std::vector<int>{255, 255, 255, 255, 0, 0, 0, 255, 255, 255, 255, 255, 0, 0, 0, 255}));
}

void AdjustmentLayerTests::anyImageTargetTakesTheAdjustment()
{
    EditorSession s;
    s.createDocument(2, 2);
    s.insert(image(PaletteColor{0.2, 0.6, 1}, {255, 200, 128, 64}));
    const QUuid curve = add(AdjustmentKind::curves, s);
    LayerAdjustment inverted{AdjustmentKind::curves};
    inverted.curves.channels[0] = {{0, 255}, {255, 0}};
    s.updateAdjustment(curve, inverted);
    // ARGB32's byte order: the adjustment reads colours, not bytes.
    QImage argb(2, 2, QImage::Format_ARGB32_Premultiplied);
    argb.fill(0);
    {
        QPainter painter(&argb);
        s.drawLiveComposite(s.document().value(), painter);
    }
    QVERIFY(pixels(argb) == rendered(s));
    // ARGB32 byte order: the adjustment reads colours, not bytes.
    for (const LayerBlendMode mode : {LayerBlendMode::normal, LayerBlendMode::multiply}) {
        s.setLayerBlendMode(mode);
        QImage sharp(4, 4, QImage::Format_RGBA8888_Premultiplied), scaled(4, 4, QImage::Format_RGBA8888_Premultiplied);
        sharp.setDevicePixelRatio(2);
        sharp.fill(0);
        scaled.fill(0);
        {
            QPainter ratio(&sharp);
            s.drawLiveComposite(s.document().value(), ratio);
            QPainter scale(&scaled);
            scale.scale(2, 2);
            s.drawLiveComposite(s.document().value(), scale);
        }
        QVERIFY2(pixels(sharp) == pixels(scaled), qPrintable(rawValue(mode)));
    }
}

QTEST_MAIN(AdjustmentLayerTests)
#include "AdjustmentLayerTests.moc"
