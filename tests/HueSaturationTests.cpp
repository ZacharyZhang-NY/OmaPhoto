#include "Document/EditorSession.h"
#include "IO/ImageExporter.h"
#include <QtTest>

// Swift's HueSaturationTests.
namespace {
// 40 by 20: red, gray, a half-clear blue strip.
std::unique_ptr<EditorSession> makeSession()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(40, 20);
    QImage image(40, 20, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::transparent);
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 40; ++x)
            image.setPixelColor(x, y, y >= 16 ? QColor(0, 0, 255, 128) : x < 20 ? QColor(255, 0, 0) : QColor(128, 128, 128));
    }
    session->insert(ImportedImage(image, image, QStringLiteral("Colors")));
    return session;
}

// 40 by 20: red left, blue right, both opaque.
std::unique_ptr<EditorSession> redAndBlue()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(40, 20);
    QImage image(40, 20, QImage::Format_RGBA8888_Premultiplied);
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 40; ++x)
            image.setPixelColor(x, y, x < 20 ? QColor(255, 0, 0) : QColor(0, 0, 255));
    }
    session->insert(ImportedImage(image, image, QStringLiteral("RedBlue")));
    return session;
}

// Premultiplied RGBA bytes of an image's pixel.
std::array<int, 4> bytesAt(const QImage &image, int x, int y)
{
    const uchar *pixel = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied).constScanLine(y) + x * 4;
    return {pixel[0], pixel[1], pixel[2], pixel[3]};
}

std::array<int, 4> pixel(const EditorSession &session, int x, int y)
{
    return bytesAt(ImageExporter::render(session.projectSnapshot().value()).image, x, y);
}

// The cube is interpolated: a few levels either way.
bool near(const std::array<int, 4> &value, const std::array<int, 4> &target, int tolerance = 8)
{
    for (size_t index = 0; index < 4; ++index) {
        if (std::abs(value[index] - target[index]) > tolerance)
            return false;
    }
    return true;
}

// Swift's `await hueSaturationTask?.value`: a preview has landed since.
bool previewSettles(const EditorSession &session, int revision)
{
    return QTest::qWaitFor([&] { return session.brushRevision() > revision && !session.hueSaturationPending(); });
}

// Previews what the settings make and waits for it.
bool preview(EditorSession &session, const HueSaturationSettings &settings)
{
    const int revision = session.brushRevision();
    session.updateHueSaturation(settings, true);
    return previewSettles(session, revision);
}

void commit(EditorSession &session)
{
    bool done = false;
    session.commitHueSaturation([&done] { done = true; });
    if (!QTest::qWaitFor([&] { return done; }))
        throw std::runtime_error("the commit never finished");
}

void apply(EditorSession &session, const HueSaturationSettings &settings)
{
    session.beginHueSaturation();
    session.updateHueSaturation(settings, true);
    commit(session);
}

std::array<int, 4> previewPixel(const EditorSession &session, int x, int y)
{
    const HueSaturationEdit &edit = session.hueSaturation().value();
    return bytesAt(edit.previewImage(edit.layerID).value(), x, y);
}

HueSaturationSettings withRange(ColorRange range, RangeAdjustment adjustment)
{
    return HueSaturationSettings(adjustment.hue, adjustment.saturation, adjustment.lightness, false, range);
}
}

class HueSaturationTests : public QObject {
    Q_OBJECT
private slots:
    void defaultsAreAnExactNoOp();
    void hueRotatesSaturationAndLightnessFollowPhotoshopRanges();
    void colorizeGivesEverythingOneHueAndKeepsAlpha();
    void adjustmentStaysInsideTheSelectionAndIsOneUndoStep();
    void previewIsLiveDoesNotTouchTheDocumentAndNeverAccumulates();
    void bandWeightsRampThroughFalloffAndWrapAround();
    void colorRangesAdjustIndependently();
    void rangesLeaveOtherHuesAloneAndInvertFlipsTheBand();
    void slidersEditTheSelectedRangeAndTheAfterBarFollowsHueShifts();
    void eyedroppersRecenterWidenAndNarrowTheBand();
    void targetedAdjustmentPicksTheRangeUnderTheCursor();
    void samplingNeedsAColorRangeAndAColorfulPixel();
};

void HueSaturationTests::defaultsAreAnExactNoOp()
{
    const std::unique_ptr<EditorSession> session = makeSession();
    const std::optional<CanvasDocument> before = session->document();
    const int count = session->history.undoCount();
    session->beginHueSaturation();
    // Other edits wait.
    QVERIFY(session->hueSaturation() && !session->canEditLayers());
    session->updateHueSaturation(HueSaturationSettings(), true);
    commit(*session);
    QVERIFY(session->document() == before && session->history.undoCount() == count);
    QVERIFY(!session->hueSaturation() && session->canEditLayers());
}

void HueSaturationTests::hueRotatesSaturationAndLightnessFollowPhotoshopRanges()
{
    const std::unique_ptr<EditorSession> session = makeSession();
    apply(*session, HueSaturationSettings(120));
    QVERIFY(near(pixel(*session, 5, 5), {0, 255, 0, 255}));
    QVERIFY(near(pixel(*session, 30, 5), {128, 128, 128, 255}));
    session->undo();
    apply(*session, HueSaturationSettings(0, -100));
    const std::array<int, 4> gray = pixel(*session, 5, 5);
    QVERIFY(gray[0] == gray[1] && gray[1] == gray[2] && gray[3] == 255);
    session->undo();
    apply(*session, HueSaturationSettings(0, 0, 100));
    QVERIFY(near(pixel(*session, 5, 5), {255, 255, 255, 255}));
    session->undo();
    apply(*session, HueSaturationSettings(0, 0, -100));
    QVERIFY(near(pixel(*session, 5, 5), {0, 0, 0, 255}));
}

void HueSaturationTests::colorizeGivesEverythingOneHueAndKeepsAlpha()
{
    const std::unique_ptr<EditorSession> session = makeSession();
    apply(*session, HueSaturationSettings(240, 100, 0, true));
    const std::array<int, 4> left = pixel(*session, 5, 5), right = pixel(*session, 30, 5);
    QVERIFY(left[2] > left[0] && right[2] > right[0]);
    QCOMPARE(left[3], 255);
    // The half-clear strip keeps its alpha.
    QVERIFY(std::abs(pixel(*session, 5, 18)[3] - 128) <= 2);
}

void HueSaturationTests::adjustmentStaysInsideTheSelectionAndIsOneUndoStep()
{
    const std::unique_ptr<EditorSession> session = makeSession();
    QPainterPath left;
    left.addRect(0, 0, 10, 20);
    session->applySelection(left, SelectionMode::replace, QStringLiteral("Select"));
    const int count = session->history.undoCount();
    apply(*session, HueSaturationSettings(120));
    QVERIFY(session->history.undoCount() == count + 1 && session->history.undoName() == QString("Hue/Saturation"));
    QVERIFY(near(pixel(*session, 5, 5), {0, 255, 0, 255}));
    QVERIFY(near(pixel(*session, 15, 5), {255, 0, 0, 255}));
    session->undo();
    QVERIFY(near(pixel(*session, 5, 5), {255, 0, 0, 255}));
}

void HueSaturationTests::previewIsLiveDoesNotTouchTheDocumentAndNeverAccumulates()
{
    const std::unique_ptr<EditorSession> session = makeSession();
    const std::optional<CanvasDocument> before = session->document();
    const int count = session->history.undoCount();
    session->beginHueSaturation();
    QVERIFY(preview(*session, HueSaturationSettings(120)));
    QVERIFY(near(previewPixel(*session, 5, 5), {0, 255, 0, 255}));
    QVERIFY(session->document() == before && session->history.undoCount() == count);
    // Each preview starts from the original, never stacking.
    QVERIFY(preview(*session, HueSaturationSettings(240)));
    QVERIFY(near(previewPixel(*session, 5, 5), {0, 0, 255, 255}));
    // Preview off drops back to the layer's own pixels.
    session->updateHueSaturation(HueSaturationSettings(240), false);
    const HueSaturationEdit &edit = session->hueSaturation().value();
    QVERIFY(!edit.previewImage(edit.layerID));
    session->cancelHueSaturation();
    QVERIFY(session->document() == before && !session->hueSaturation());
    // OK renders full size, only then changing the layer.
    session->beginHueSaturation();
    QVERIFY(preview(*session, HueSaturationSettings(120)));
    commit(*session);
    QCOMPARE(session->document().value().layers.front().asset.value().size().width(), before.value().layers.front().asset.value().size().width());
    QVERIFY(near(pixel(*session, 5, 5), {0, 255, 0, 255}));
}

void HueSaturationTests::bandWeightsRampThroughFalloffAndWrapAround()
{
    // 315, 345, 15, 45: wrapping past zero.
    const HueBand reds = defaultBand(ColorRange::reds);
    QVERIFY(reds.weight(0) == 1 && reds.weight(345) == 1 && reds.weight(15) == 1);
    QVERIFY(std::abs(reds.weight(330) - 0.5) < 0.001);
    QVERIFY(std::abs(reds.weight(30) - 0.5) < 0.001);
    QVERIFY(reds.weight(315) == 0 && reds.weight(45) == 0 && reds.weight(180) == 0);
    QCOMPARE(defaultBand(ColorRange::master).weight(123), 1.0);
    // Handles keep their order: crossings are refused.
    HueBand band = defaultBand(ColorRange::greens);
    band.setHandle(1, 200);
    QVERIFY(band == defaultBand(ColorRange::greens));
    band.setHandle(1, 110);
    QCOMPARE(band.rangeStart, 110.0);
}

void HueSaturationTests::colorRangesAdjustIndependently()
{
    const std::unique_ptr<EditorSession> session = redAndBlue();
    HueSaturationSettings settings(60, 0, 0, false, ColorRange::reds);
    settings.adjustments[ColorRange::blues] = RangeAdjustment{0, -100, 0};
    session->beginHueSaturation();
    QVERIFY(preview(*session, settings));
    commit(*session);
    const std::array<int, 4> red = pixel(*session, 5, 5), blue = pixel(*session, 30, 5);
    QVERIFY(near(red, {255, 255, 0, 255}));
    QVERIFY(blue[0] == blue[1] && blue[1] == blue[2]);
}

void HueSaturationTests::rangesLeaveOtherHuesAloneAndInvertFlipsTheBand()
{
    const std::unique_ptr<EditorSession> session = redAndBlue();
    const std::array<int, 4> before = pixel(*session, 30, 5);
    apply(*session, withRange(ColorRange::reds, {0, 0, -100}));
    QVERIFY(near(pixel(*session, 5, 5), {0, 0, 0, 255}));
    QVERIFY(near(pixel(*session, 30, 5), before));
    session->undo();
    HueSaturationSettings inverted = withRange(ColorRange::reds, {0, 0, -100});
    inverted.invertRange = true;
    apply(*session, inverted);
    QVERIFY(near(pixel(*session, 5, 5), {255, 0, 0, 255}));
    QVERIFY(near(pixel(*session, 30, 5), {0, 0, 0, 255}));
}

void HueSaturationTests::slidersEditTheSelectedRangeAndTheAfterBarFollowsHueShifts()
{
    HueSaturationSettings settings;
    settings.setHue(30);
    settings.range = ColorRange::greens;
    QCOMPARE(settings.hue(), 0.0);
    settings.setHue(-40);
    QVERIFY(settings.adjustments.at(ColorRange::master).hue == 30 && settings.adjustments.at(ColorRange::greens).hue == -40);
    settings.range = ColorRange::master;
    QVERIFY(settings.hue() == 30 && !settings.isIdentity());
    // The after spectrum moves hues in the band alone.
    HueSaturationSettings greensOnly(60, 0, 0, false, ColorRange::greens);
    greensOnly.adjustments[ColorRange::master] = RangeAdjustment();
    QVERIFY(std::abs(HueSaturationFilter::shiftedHue(120, greensOnly) - 180) < 0.001);
    QVERIFY(std::abs(HueSaturationFilter::shiftedHue(0, greensOnly)) < 0.001);
}

void HueSaturationTests::eyedroppersRecenterWidenAndNarrowTheBand()
{
    // Red at hue 0 left, blue at 240 right.
    const std::unique_ptr<EditorSession> session = redAndBlue();
    session->beginHueSaturation();
    session->updateHueSaturation(HueSaturationSettings(10, 0, 0, false, ColorRange::greens), false);
    session->setHueSampleMode(HueSampleMode::replace);
    session->sampleHueRange(QPointF(5, 5));
    HueBand band = session->hueSaturation().value().settings.band();
    QVERIFY(band.weight(0) == 1 && band.weight(120) == 0);
    session->setHueSampleMode(HueSampleMode::add);
    session->sampleHueRange(QPointF(30, 5));
    band = session->hueSaturation().value().settings.band();
    QVERIFY(band.weight(240) == 1 && band.weight(0) == 1);
    session->setHueSampleMode(HueSampleMode::remove);
    session->sampleHueRange(QPointF(30, 5));
    band = session->hueSaturation().value().settings.band();
    QCOMPARE(band.weight(240), 0.0);
    session->cancelHueSaturation();
    QVERIFY(!session->hueSampleMode());
}

void HueSaturationTests::targetedAdjustmentPicksTheRangeUnderTheCursor()
{
    const std::unique_ptr<EditorSession> session = redAndBlue();
    session->beginHueSaturation();
    session->setHueTargeting(true);
    QVERIFY(session->beginHueTargeting(QPointF(30, 5)));
    QCOMPARE(session->hueSaturation().value().settings.range, ColorRange::blues);
    session->dragHueTargeting(60, false);
    QCOMPARE(session->hueSaturation().value().settings.adjustments.at(ColorRange::blues).saturation, 30.0);
    // Each drag counts from its start, never stacking.
    session->dragHueTargeting(20, false);
    QCOMPARE(session->hueSaturation().value().settings.adjustments.at(ColorRange::blues).saturation, 10.0);
    session->dragHueTargeting(-40, true);
    QCOMPARE(session->hueSaturation().value().settings.adjustments.at(ColorRange::blues).hue, -20.0);
    session->endHueTargeting();
    session->cancelHueSaturation();
    QVERIFY(!session->hueTargeting());
}

void HueSaturationTests::samplingNeedsAColorRangeAndAColorfulPixel()
{
    const std::unique_ptr<EditorSession> session = redAndBlue();
    session->beginHueSaturation();
    const HueBand untouched = session->hueSaturation().value().settings.band();
    session->setHueSampleMode(HueSampleMode::replace);
    // Master is selected: nothing to retarget.
    session->sampleHueRange(QPointF(5, 5));
    QVERIFY(session->hueSaturation().value().settings.band() == untouched);
    // A gray pixel has no hue to sample.
    const std::unique_ptr<EditorSession> gray = makeSession();
    gray->beginHueSaturation();
    gray->updateHueSaturation(HueSaturationSettings(0, 0, 0, false, ColorRange::reds), false);
    const HueBand before = gray->hueSaturation().value().settings.band();
    gray->setHueSampleMode(HueSampleMode::replace);
    gray->sampleHueRange(QPointF(30, 5));
    QVERIFY(gray->hueSaturation().value().settings.band() == before);
}

QTEST_GUILESS_MAIN(HueSaturationTests)
#include "HueSaturationTests.moc"
