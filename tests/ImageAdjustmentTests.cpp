#include "Document/Curves.h"
#include "Document/EditorSession.h"
#include "Document/ImageAdjustments.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectStore+Json.h"
#include <QJsonDocument>
#include <QtTest>
#include <set>

// Swift's ImageAdjustmentTests.
namespace {
// A width by height image of one straight sRGB colour.
QImage image(int width, int height, double red, double green, double blue, double alpha = 1)
{
    QImage result(width, height, QImage::Format_RGBA8888_Premultiplied);
    result.fill(QColor::fromRgbF(float(red), float(green), float(blue), float(alpha)));
    return result;
}

QImage gray(int width = 4, int height = 4, double alpha = 1)
{
    return image(width, height, 128.0 / 255, 128.0 / 255, 128.0 / 255, alpha);
}

// Straight RGBA bytes of every pixel, top row first.
std::vector<std::array<int, 4>> pixels(const QImage &image)
{
    const QImage drawn = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    std::vector<std::array<int, 4>> result;
    for (int y = 0; y < drawn.height(); ++y) {
        const uchar *row = drawn.constScanLine(y);
        for (int x = 0; x < drawn.width(); ++x) {
            const int a = row[x * 4 + 3];
            std::array<int, 4> pixel{0, 0, 0, a};
            for (int c = 0; c < 3; ++c)
                pixel[size_t(c)] = a == 0 ? 0 : std::min(255, (row[x * 4 + c] * 255 + a / 2) / a);
            result.push_back(pixel);
        }
    }
    return result;
}

bool committed(EditorSession &session)
{
    bool done = false;
    session.commitFilter([&done] { done = true; });
    return QTest::qWaitFor([&done] { return done; }, 20000);
}
}

class ImageAdjustmentTests : public QObject {
    Q_OBJECT
private slots:
    void exposureWorksInLinearLightWithOffsetAndGamma();
    void gradientMapColorsByBrightnessAndReverses();
    void grainIsFixedInDocumentSpaceAndLeavesTransparencyAlone();
    void imageMenuExposureChangesTheLayerInOneStep();
    void gradientMapColorsUseTheAppColorPicker();
    void settingsSaveAndOlderAdjustmentsStillOpen();
    void newAdjustmentLayersStartFromThePaletteRenderAndEditInThePanel();
};

void ImageAdjustmentTests::exposureWorksInLinearLightWithOffsetAndGamma()
{
    const QImage input = gray();
    QVERIFY(pixels(ExposureSettings().apply(input)) == pixels(input));
    const std::array<int, 4> brighter = pixels(ExposureSettings{1, 0, 1}.apply(input))[0];
    QVERIFY2(std::abs(brighter[0] - 176) <= 2, "+1 stop doubles linear light");
    QCOMPARE(brighter[0], brighter[2]);
    const std::array<int, 4> lifted = pixels(ExposureSettings{0, 0, 2}.apply(input))[0];
    QVERIFY2(std::abs(lifted[0] - 181) <= 2, "gamma 2 takes the square root of linear light");
    const std::array<int, 4> offset = pixels(ExposureSettings{0, 0.1, 1}.apply(image(4, 4, 0, 0, 0)))[0];
    QVERIFY2(std::abs(offset[0] - 89) <= 2, "offset adds linear light");
    const QImage translucent = gray(4, 4, 0.5);
    QCOMPARE(pixels(ExposureSettings{1, 0, 1}.apply(translucent))[0][3], pixels(translucent)[0][3]);
}

void ImageAdjustmentTests::gradientMapColorsByBrightnessAndReverses()
{
    GradientMapSettings settings{{1, 0, 0}, {0, 0, 1}, false};
    QVERIFY((pixels(settings.apply(image(4, 4, 0, 0, 0)))[0] == std::array<int, 4>{255, 0, 0, 255}));
    QVERIFY((pixels(settings.apply(image(4, 4, 1, 1, 1)))[0] == std::array<int, 4>{0, 0, 255, 255}));
    const std::array<int, 4> middle = pixels(settings.apply(gray()))[0];
    QVERIFY(std::abs(middle[0] - 127) <= 2);
    QVERIFY(std::abs(middle[2] - 128) <= 2);
    QCOMPARE(middle[1], 0);
    const std::array<int, 4> translucent = pixels(settings.apply(image(4, 4, 1, 1, 1, 0.5)))[0];
    QVERIFY(translucent[2] >= 250);
    QVERIFY(translucent[0] <= 5);
    QVERIFY2(std::abs(translucent[3] - 128) <= 1, "alpha kept");
    settings.reversed = true;
    QVERIFY((pixels(settings.apply(image(4, 4, 0, 0, 0)))[0] == std::array<int, 4>{0, 0, 255, 255}));
}

void ImageAdjustmentTests::grainIsFixedInDocumentSpaceAndLeavesTransparencyAlone()
{
    const GrainSettings settings{60, 2, 40, 7};
    const std::vector<std::array<int, 4>> whole = pixels(settings.apply(gray(40, 40)));
    std::set<int> reds;
    for (const std::array<int, 4> &pixel : whole)
        reds.insert(pixel[0]);
    QVERIFY2(reds.size() > 5, "grain varies the brightness");
    QVERIFY2(std::all_of(whole.begin(), whole.end(), [](const std::array<int, 4> &pixel) { return pixel[0] == pixel[1] && pixel[1] == pixel[2]; }),
             "the same change on every channel");
    // A piece at its place gets the whole's grain there.
    const std::vector<std::array<int, 4>> part = pixels(settings.apply(gray(20, 20), QPointF(10, 10)));
    std::vector<std::array<int, 4>> crop;
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 20; ++x)
            crop.push_back(whole[size_t((y + 10) * 40 + x + 10)]);
    }
    QVERIFY2(part == crop, "grain must not shift when only part of the canvas redraws");
    GrainSettings reseeded = settings;
    reseeded.seed = 8;
    QVERIFY2(pixels(reseeded.apply(gray(40, 40))) != whole, "another seed, another pattern");
    QVERIFY2(pixels(GrainSettings{0, 1.5, 50, 0}.apply(gray())) == pixels(gray()), "no amount, no change");
    const std::vector<std::array<int, 4>> cleared = pixels(settings.apply(gray(4, 4, 0)));
    QVERIFY2(std::all_of(cleared.begin(), cleared.end(), [](const std::array<int, 4> &pixel) { return pixel[3] == 0; }), "clear pixels stay clear");
}

void ImageAdjustmentTests::imageMenuExposureChangesTheLayerInOneStep()
{
    EditorSession session;
    session.createDocument(8, 8);
    const QImage base = gray(8, 8);
    session.insert(ImportedImage(base, base, QStringLiteral("Gray")));
    session.beginFilter(FilterKind::exposure);
    FilterSettings settings = session.filterEdit().value().settings;
    settings.exposure.exposure = 1;
    session.updateFilter(settings, true);
    const int count = session.history.undoCount();
    QVERIFY(committed(session));
    QVERIFY(session.history.undoCount() == count + 1 && session.history.undoName() == QStringLiteral("Exposure"));
    const std::array<int, 4> result = pixels(session.activeLayer().value().asset.value().image())[0];
    QVERIFY2(std::abs(result[0] - 176) <= 2, QByteArray::number(result[0]));
    QVERIFY(isImageAdjustment(FilterKind::exposure) && !isImageAdjustment(FilterKind::gaussianBlur));
}

void ImageAdjustmentTests::gradientMapColorsUseTheAppColorPicker()
{
    EditorSession session;
    session.createDocument(8, 8);
    const QImage base = gray(8, 8);
    session.insert(ImportedImage(base, base, QStringLiteral("Gray")));
    const PaletteColor foreground = session.foregroundColor(), background = session.backgroundColor();
    session.beginFilter(FilterKind::gradientMap);
    const auto map = [&session] { return session.filterEdit().value().settings.gradientMap; };
    const AdjustmentColor start = map().highlights;

    session.openGradientMapColorPicker(true);
    const ColorPickerState picker = session.colorPicker().value();
    QVERIFY((picker.target == ColorPickerTarget{ColorPickerTarget::Kind::gradientMap, false, std::nullopt, true}));
    QVERIFY(AdjustmentColor(picker.original) == start);
    PickerHSB hsb = picker.hsb;
    hsb.setRGB(PaletteColor{1, 0, 0});
    session.setColorPickerHSB(hsb);
    session.previewGradientMapColor();
    QVERIFY2(map().highlights == AdjustmentColor(1, 0, 0), "the gradient follows the working color");
    session.closeColorPicker(false);
    QVERIFY2(map().highlights == start, "Cancel restores it");

    session.openGradientMapColorPicker(false);
    hsb = session.colorPicker().value().hsb;
    hsb.setRGB(PaletteColor{0, 0, 1});
    session.setColorPickerHSB(hsb);
    session.closeColorPicker(true);
    QVERIFY(map().shadows == AdjustmentColor(0, 0, 1));
    QVERIFY(session.foregroundColor() == foreground);
    QVERIFY2(session.backgroundColor() == background, "the palette is untouched");

    session.openGradientMapColorPicker(true);
    session.cancelFilter();
    QVERIFY2(!session.colorPicker() && !session.filterEdit(), "closing the panel closes its picker");
}

void ImageAdjustmentTests::settingsSaveAndOlderAdjustmentsStillOpen()
{
    const LayerAdjustment levels{AdjustmentKind::levels};
    const QJsonObject data = ManifestJson::encoded(levels);
    const QString json = QString::fromUtf8(QJsonDocument(data).toJson());
    QVERIFY2(!json.contains("exposureSettings"), "an existing kind saves exactly as before");
    QVERIFY(!json.contains("gradientMapSettings"));
    QVERIFY(!json.contains("grainSettings"));
    QVERIFY(ManifestJson::adjustment(data) == levels);
    LayerAdjustment grain{AdjustmentKind::grain};
    grain.setGrain(GrainSettings{.amount = 40, .size = 3, .roughness = 10, .seed = 9});
    const LayerAdjustment decoded = ManifestJson::adjustment(ManifestJson::encoded(grain));
    QVERIFY(decoded == grain);
    QVERIFY(decoded.isValid());
    LayerAdjustment broken{AdjustmentKind::exposure};
    ExposureSettings exposure = broken.exposure();
    exposure.gamma = 0;
    broken.setExposure(exposure);
    QVERIFY(!broken.isValid());
}

void ImageAdjustmentTests::newAdjustmentLayersStartFromThePaletteRenderAndEditInThePanel()
{
    EditorSession session;
    session.createDocument(20, 20);
    const QImage base = gray(20, 20);
    session.insert(ImportedImage(base, base, QStringLiteral("Gray")));
    session.setPaletteColor(PaletteColor{1, 0, 0}, false);
    session.setPaletteColor(PaletteColor{0, 0, 1}, true);
    session.addAdjustment(AdjustmentKind::gradientMap);
    const QUuid id = session.activeLayerID().value();
    const LayerAdjustment adjustment = session.activeLayer().value().adjustment.value();
    QVERIFY(adjustment.kind == AdjustmentKind::gradientMap);
    QVERIFY(adjustment.gradientMap().shadows == AdjustmentColor(1, 0, 0));
    QVERIFY(adjustment.gradientMap().highlights == AdjustmentColor(0, 0, 1));
    QCOMPARE(session.adjustmentEditingID(), std::optional(id));
    bool done = false;
    session.beginAdjustmentEditing(id, [&done] { done = true; });
    QTRY_VERIFY(done);
    QVERIFY2(session.filterEdit().value().kind == FilterKind::gradientMap, "edited in the floating filter panel, like Curves");
    session.finishAdjustmentEditing(false);

    const std::array<int, 4> middle = pixels(ImageExporter::render(session.projectSnapshot().value()).image)[210];
    const QString shown = QStringLiteral("%1 %2 %3").arg(middle[0]).arg(middle[1]).arg(middle[2]);
    QVERIFY2(middle[0] > 100, qPrintable("gray maps between red and blue: " + shown));
    QVERIFY2(middle[2] > 100, qPrintable(shown));
    QVERIFY2(middle[1] < 20, qPrintable(shown));

    session.addAdjustment(AdjustmentKind::grain);
    const quint32 first = session.activeLayer().value().adjustment.value().grain().seed;
    // This test never opened its panel: no edit to finish.
    session.setAdjustmentEditingID(std::nullopt);
    session.addAdjustment(AdjustmentKind::grain);
    const std::vector<ImageLayer> &layers = session.document().value().layers;
    QCOMPARE(std::count_if(layers.begin(), layers.end(), [](const ImageLayer &layer) { return layer.adjustment && layer.adjustment->kind == AdjustmentKind::grain; }), 2);
    QVERIFY2(session.activeLayer().value().adjustment.value().grain().seed != first, "each Grain layer gets its own pattern");
    QVERIFY(std::find(allAdjustmentKinds.begin(), allAdjustmentKinds.end(), AdjustmentKind::exposure) != allAdjustmentKinds.end()
            && filterKind(AdjustmentKind::exposure) == FilterKind::exposure);
}

QTEST_GUILESS_MAIN(ImageAdjustmentTests)
#include "ImageAdjustmentTests.moc"
