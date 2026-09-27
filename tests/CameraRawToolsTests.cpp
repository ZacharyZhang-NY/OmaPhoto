#include "CameraRawFixtures.h"
#include "Document/EditorSession.h"
#include <QSignalSpy>
#include <QtTest>

// Swift's Camera Raw session tools: eyedroppers, drags, guides.
using namespace CameraRawFixtures;

namespace {
std::unique_ptr<EditorSession> opened(const QImage &image)
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(image.width(), image.height());
    session->insert(ImportedImage(image, image, QStringLiteral("Picture")));
    session->beginFilter(FilterKind::cameraRaw);
    return session;
}

void arm(EditorSession &session, const std::function<void(CameraRawPanel &)> &change)
{
    CameraRawPanel panel = session.filterEdit().value().rawPanel;
    change(panel);
    session.setCameraRawPanel(panel);
}

const CameraRawSettings &raw(const EditorSession &session)
{
    return session.filterEdit().value().settings.cameraRaw;
}
}

class CameraRawToolsTests : public QObject {
    Q_OBJECT
private slots:
    void theDefringeEyedropperCentresTheNearerRange();
    void theEyedroppersNeedArmingAPixelAndAnOpenEdit();
    void theReadoutFollowsTheAdjustedPreview();
    void aTargetedDragMovesTheCurveOrTheMixer();
    void pointColorsArePickedAndReplaced();
    void aDrawnGuideTurnsUprightGuided();
    void theWhiteBalanceToolsKeepTheirMode();
    void samplesReadEveryHueAndStayWithTheirKind();
};

void CameraRawToolsTests::theDefringeEyedropperCentresTheNearerRange()
{
    const auto purple = opened(image(8, 8, 0.8, 0.2, 0.9));
    arm(*purple, [](CameraRawPanel &panel) { panel.samplesDefringe = true; });
    purple->sampleCameraRawDefringe(QPointF(2, 2));
    const double hue = (4 + 0.6 / 0.7) * 60;
    QVERIFY(std::abs(raw(*purple).optics.purpleHueLow - (hue - 25)) < 0.5 && std::abs(raw(*purple).optics.purpleHueHigh - (hue + 25)) < 0.5);
    QCOMPARE(raw(*purple).optics.purpleAmount, 50.0);
    QCOMPARE(raw(*purple).optics.greenAmount, 0.0);
    // A set amount stays; green fringe moves the green range.
    FilterSettings settings = purple->filterEdit().value().settings;
    settings.cameraRaw.optics.purpleAmount = 80;
    purple->updateFilter(settings, true);
    purple->sampleCameraRawDefringe(QPointF(2, 2));
    QCOMPARE(raw(*purple).optics.purpleAmount, 80.0);
    const auto green = opened(image(8, 8, 0.4, 0.8, 0.2));
    arm(*green, [](CameraRawPanel &panel) { panel.samplesDefringe = true; });
    green->sampleCameraRawDefringe(QPointF(2, 2));
    const double greenHue = (2 + (0.2 - 0.4) / 0.6) * 60;
    QVERIFY(std::abs(raw(*green).optics.greenHueLow - (greenHue - 25)) < 0.5 && std::abs(raw(*green).optics.greenHueHigh - (greenHue + 25)) < 0.5);
    QCOMPARE(raw(*green).optics.greenAmount, 50.0);
    QVERIFY(raw(*green).optics.purpleHueLow == 270 && raw(*green).optics.purpleAmount == 0);
    // Past the wheel's end the range is held within 360.
    const auto rose = opened(image(8, 8, 1, 0, 64 / 255.0));
    arm(*rose, [](CameraRawPanel &panel) { panel.samplesDefringe = true; });
    rose->sampleCameraRawDefringe(QPointF(2, 2));
    QCOMPARE(raw(*rose).optics.purpleHueHigh, 360.0);
}

void CameraRawToolsTests::theEyedroppersNeedArmingAPixelAndAnOpenEdit()
{
    const auto session = opened(image(8, 8, 0.8, 0.2, 0.9));
    const FilterSettings untouched = session->filterEdit().value().settings;
    // Unarmed, outside the document or on a clear pixel: nothing.
    session->sampleCameraRawDefringe(QPointF(2, 2));
    session->sampleCameraRawWhiteBalance(QPointF(2, 2));
    arm(*session, [](CameraRawPanel &panel) { panel.samplesDefringe = panel.samplesWhiteBalance = true; });
    for (const QPointF outside : {QPointF(-0.5, 2), QPointF(8, 2), QPointF(2, 8)}) {
        session->sampleCameraRawDefringe(outside);
        session->sampleCameraRawWhiteBalance(outside);
    }
    QVERIFY(session->filterEdit().value().settings == untouched);
    const auto clear = opened(image(8, 8, 0.8, 0.2, 0.9, 0));
    arm(*clear, [](CameraRawPanel &panel) { panel.samplesDefringe = true; });
    clear->sampleCameraRawDefringe(QPointF(2, 2));
    QVERIFY(clear->filterEdit().value().settings.cameraRaw == CameraRawSettings());
    // The panel's setter announces; with no edit it is silent.
    QSignalSpy changes(session.get(), &EditorSession::changed);
    arm(*session, [](CameraRawPanel &panel) { panel.mixerSwatch = 3; });
    QCOMPARE(changes.count(), 1);
    QCOMPARE(session->filterEdit().value().rawPanel.mixerSwatch, 3);
    session->cancelFilter();
    changes.clear();
    session->setCameraRawPanel(CameraRawPanel());
    QCOMPARE(changes.count(), 0);
    // A clear pixel reads nothing.
    clear->updateCameraRawReadout(QPointF(2, 2));
    QVERIFY(!clear->filterEdit().value().rawPanel.readout);
}

void CameraRawToolsTests::theReadoutFollowsTheAdjustedPreview()
{
    const auto session = opened(image(8, 8, 160 / 255.0, 140 / 255.0, 120 / 255.0));
    QSignalSpy changes(session.get(), &EditorSession::changed);
    session->updateCameraRawReadout(QPointF(3, 3));
    QCOMPARE(session->filterEdit().value().rawPanel.readout, std::optional(std::array{160, 140, 120}));
    QCOMPARE(changes.count(), 1);
    // The same pixel again says nothing; past the layer, none.
    session->updateCameraRawReadout(QPointF(4, 4));
    QCOMPARE(changes.count(), 1);
    session->updateCameraRawReadout(QPointF(9, 3));
    QVERIFY(!session->filterEdit().value().rawPanel.readout);
    // Once a preview lands it reads the adjusted pixels.
    FilterSettings settings = session->filterEdit().value().settings;
    settings.cameraRaw.exposure = 1;
    session->updateFilter(settings, true);
    QTRY_VERIFY(!session->filterEdit().value().preparing);
    session->updateCameraRawReadout(QPointF(3, 3));
    const std::array<int, 3> read = session->filterEdit().value().rawPanel.readout.value();
    const std::array<int, 4> expected = pixels(session->filterEdit().value().preparedPreview.value())[0];
    QVERIFY(read[0] == expected[0] && read[1] == expected[1] && read[2] == expected[2] && read[0] > 160);
}

void CameraRawToolsTests::aTargetedDragMovesTheCurveOrTheMixer()
{
    const auto session = opened(gray(8, 8));
    arm(*session, [](CameraRawPanel &panel) { panel.targetsCurve = true; });
    // Mid gray is a light; a hundred points up, 35.
    session->beginCameraRawDrag(QPointF(2, 5));
    session->dragCameraRaw(QPointF(2, -95));
    QCOMPARE(raw(*session).curve.lights, 35.0);
    QCOMPARE(raw(*session).curve.darks, 0.0);
    // Each step measures from the press, not the last step.
    session->dragCameraRaw(QPointF(2, -1000));
    QCOMPARE(raw(*session).curve.lights, 100.0);
    session->dragCameraRaw(QPointF(2, 105));
    QCOMPARE(raw(*session).curve.lights, -35.0);
    // A drag starts over from the settings at its press.
    FilterSettings moved = session->filterEdit().value().settings;
    moved.cameraRaw.exposure = 1;
    session->updateFilter(moved, true);
    session->dragCameraRaw(QPointF(2, 5));
    QVERIFY(raw(*session).exposure == 0 && raw(*session).curve.lights == 0);
    // The point page nudges the nearest point, a hundredth.
    arm(*session, [](CameraRawPanel &panel) { panel.curvePage = CameraRawCurvePage::point; panel.pointChannel = CameraRawPointChannel::red; });
    session->beginCameraRawDrag(QPointF(2, 5));
    session->dragCameraRaw(QPointF(2, 105));
    QCOMPARE(raw(*session).curve.red, (std::vector<CurvePoint>{{0, 0}, {1, 0.65}}));
    QVERIFY(raw(*session).curve.rgb == CameraRawCurveSettings::linear());
    // The mixer's tab shares the drag by each family's weight.
    const auto red = opened(image(8, 8, 1, 0, 0));
    arm(*red, [](CameraRawPanel &panel) { panel.targetsMixer = true; panel.mixerTab = CameraRawMixerTab::saturation; });
    red->beginCameraRawDrag(QPointF(2, 5));
    red->dragCameraRaw(QPointF(2, -35));
    QCOMPARE(raw(*red).mixer.saturation[0], 14.0);
    QCOMPARE(raw(*red).mixer.saturation[1], 3.5);
    QVERIFY(raw(*red).mixer.hue[0] == 0 && raw(*red).mixer.saturation[2] == 0);
    // Without a drag begun, a drag does nothing.
    const auto idle = opened(gray(8, 8));
    arm(*idle, [](CameraRawPanel &panel) { panel.targetsCurve = true; });
    idle->dragCameraRaw(QPointF(2, -95));
    QVERIFY(raw(*idle) == CameraRawSettings());
}

void CameraRawToolsTests::pointColorsArePickedAndReplaced()
{
    const auto session = opened(image(8, 8, 1, 0.5, 0));
    arm(*session, [](CameraRawPanel &panel) { panel.samplesPointColor = true; panel.pointIndex = -1; });
    session->sampleCameraRawPointColor(QPointF(1, 1));
    QCOMPARE(raw(*session).mixer.points.size(), size_t(1));
    const CameraRawPointColor picked = raw(*session).mixer.points[0];
    QVERIFY(std::abs(picked.hue - 30) < 0.5 && std::abs(picked.saturation - 1) < 0.01 && std::abs(picked.luminance - 0.5) < 0.01);
    QCOMPARE(session->filterEdit().value().rawPanel.pointIndex, 0);
    // Picked again, the chosen point takes it, keeping shifts.
    FilterSettings settings = session->filterEdit().value().settings;
    settings.cameraRaw.mixer.points[0].hueShift = 20;
    settings.cameraRaw.mixer.points[0].saturationShift = -30;
    settings.cameraRaw.mixer.points[0].luminanceShift = 40;
    session->updateFilter(settings, true);
    session->sampleCameraRawPointColor(QPointF(1, 1));
    QCOMPARE(raw(*session).mixer.points.size(), size_t(1));
    const CameraRawPointColor kept = raw(*session).mixer.points[0];
    QVERIFY(kept.hueShift == 20 && kept.saturationShift == -30 && kept.luminanceShift == 40);
    // A pick chooses its addition; with none chosen, eight.
    for (int pick = 0; pick < 9; ++pick) {
        arm(*session, [](CameraRawPanel &panel) { panel.pointIndex = -1; });
        session->sampleCameraRawPointColor(QPointF(1, 1));
    }
    QCOMPARE(raw(*session).mixer.points.size(), size_t(8));
    QCOMPARE(session->filterEdit().value().rawPanel.pointIndex, -1);
    // A hue past red's end reads round the wheel.
    const auto rose = opened(image(8, 8, 1, 0, 64 / 255.0));
    arm(*rose, [](CameraRawPanel &panel) { panel.samplesPointColor = true; });
    rose->sampleCameraRawPointColor(QPointF(1, 1));
    QVERIFY(std::abs(raw(*rose).mixer.points.at(0).hue - (360 - 60 * 64 / 255.0)) < 0.01);
}

void CameraRawToolsTests::aDrawnGuideTurnsUprightGuided()
{
    const auto session = opened(gray(8, 8));
    session->beginCameraRawGeometryGuide(QPointF(2, 2));
    QVERIFY(!session->filterEdit().value().rawPanel.guideDraft);
    arm(*session, [](CameraRawPanel &panel) { panel.drawingGeometryGuide = true; });
    session->beginCameraRawGeometryGuide(QPointF(-1, 2));
    QVERIFY(!session->filterEdit().value().rawPanel.guideDraft);
    session->beginCameraRawGeometryGuide(QPointF(2, 2));
    session->continueCameraRawGeometryGuide(QPointF(6, 4));
    // A point past the preview leaves the draft alone.
    session->continueCameraRawGeometryGuide(QPointF(9, 4));
    QCOMPARE(session->filterEdit().value().rawPanel.guideDraft, std::optional(std::pair(QPointF(0.25, 0.25), QPointF(0.75, 0.5))));
    session->commitCameraRawGeometryGuide();
    QVERIFY(!session->filterEdit().value().rawPanel.guideDraft);
    QVERIFY(raw(*session).geometry.upright == CameraRawUprightMode::guided);
    QCOMPARE(raw(*session).geometry.guides, (std::vector<CameraRawGeometryGuide>{{0.25, 0.25, 0.75, 0.5}}));
}

void CameraRawToolsTests::theWhiteBalanceToolsKeepTheirMode()
{
    const QImage warm = image(8, 8, 160 / 255.0, 140 / 255.0, 120 / 255.0);
    // The eyedropper sets Custom over Auto.
    const auto sampled = opened(warm);
    FilterSettings automatic = sampled->filterEdit().value().settings;
    automatic.cameraRaw.whiteBalance = CameraRawWhiteBalance::automatic;
    sampled->updateFilter(automatic, true);
    arm(*sampled, [](CameraRawPanel &panel) { panel.samplesWhiteBalance = true; });
    sampled->sampleCameraRawWhiteBalance(QPointF(1, 1));
    QVERIFY(raw(*sampled).whiteBalance == CameraRawWhiteBalance::custom);
    // Custom chosen before the scan lands keeps the sliders.
    const auto changed = opened(warm);
    bool done = false;
    changed->applyCameraRawAutoWhiteBalance([&done] { done = true; });
    FilterSettings custom = changed->filterEdit().value().settings;
    custom.cameraRaw.whiteBalance = CameraRawWhiteBalance::custom;
    changed->updateFilter(custom, true);
    QTRY_VERIFY(done);
    QVERIFY(raw(*changed).temperature == 0 && raw(*changed).tint == 0);
}

void CameraRawToolsTests::samplesReadEveryHueAndStayWithTheirKind()
{
    // Green and blue on top take their own hue branches.
    const auto hueOf = [](double red, double green, double blue) {
        const auto session = opened(image(8, 8, red, green, blue));
        arm(*session, [](CameraRawPanel &panel) { panel.samplesPointColor = true; });
        session->sampleCameraRawPointColor(QPointF(1, 1));
        return raw(*session).mixer.points.at(0).hue;
    };
    QVERIFY(std::abs(hueOf(0.2, 0.8, 0.4) - (2 + 0.2 / 0.6) * 60) < 0.5);
    QVERIFY(std::abs(hueOf(0.3, 0.2, 0.9) - (4 + 0.1 / 0.7) * 60) < 0.5);
    // The Luminance tab moves luminance alone.
    const auto red = opened(image(8, 8, 1, 0, 0));
    arm(*red, [](CameraRawPanel &panel) { panel.targetsMixer = true; panel.mixerTab = CameraRawMixerTab::luminance; });
    red->beginCameraRawDrag(QPointF(2, 5));
    red->dragCameraRaw(QPointF(2, -35));
    QVERIFY(raw(*red).mixer.luminance[0] > 0 && raw(*red).mixer.hue[0] == 0 && raw(*red).mixer.saturation[0] == 0);
    // Rec. 709 tone: pure green is a light.
    const auto green = opened(image(8, 8, 0, 1, 0));
    arm(*green, [](CameraRawPanel &panel) { panel.targetsCurve = true; });
    green->beginCameraRawDrag(QPointF(2, 5));
    green->dragCameraRaw(QPointF(2, -95));
    QVERIFY(raw(*green).curve.lights == 35 && raw(*green).curve.darks == 0);
    // A soft pixel's readout rounds to nearest.
    QImage soft = BrushRaster::context(4, 4, false);
    soft.fill(Qt::transparent);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            std::fill_n(soft.scanLine(y) + x * 4, 3, uchar(51)), soft.scanLine(y)[x * 4 + 3] = 128;
    const auto readout = opened(soft);
    readout->updateCameraRawReadout(QPointF(1, 1));
    QCOMPARE(readout->filterEdit().value().rawPanel.readout, std::optional(std::array{102, 102, 102}));
    // Another filter's edit takes no white balance.
    const auto blur = opened(image(8, 8, 0.8, 0.5, 0.3));
    blur->cancelFilter();
    blur->beginFilter(FilterKind::gaussianBlur);
    arm(*blur, [](CameraRawPanel &panel) { panel.samplesWhiteBalance = true; });
    blur->sampleCameraRawWhiteBalance(QPointF(1, 1));
    QVERIFY(raw(*blur).temperature == 0 && raw(*blur).tint == 0);
}

QTEST_GUILESS_MAIN(CameraRawToolsTests)
#include "CameraRawToolsTests.moc"
