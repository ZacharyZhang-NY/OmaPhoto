#include "CameraRawFixtures.h"
#include "Document/EditorSession.h"
#include <QtTest>

// Swift's CameraRawTests through the session, and geometry.
using namespace CameraRawFixtures;

namespace {
bool awaited(const std::function<void(std::function<void()>)> &start)
{
    bool done = false;
    start([&done] { done = true; });
    return QTest::qWaitFor([&done] { return done; }, 20000);
}

std::unique_ptr<EditorSession> opened(const QImage &image)
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(image.width(), image.height());
    session->insert(ImportedImage(image, image, QStringLiteral("Picture")));
    session->beginFilter(FilterKind::cameraRaw);
    return session;
}

QImage layerImage(const EditorSession &session)
{
    return session.activeLayer().value().asset.value().image();
}

// Swift's `edit.samplesWhiteBalance = true`, through the panel.
void arm(EditorSession &session, const std::function<void(CameraRawPanel &)> &change)
{
    CameraRawPanel panel = session.filterEdit().value().rawPanel;
    change(panel);
    session.setCameraRawPanel(panel);
}
}

class CameraRawSessionTests : public QObject {
    Q_OBJECT
private slots:
    void eyedropperAndAutoNeutralizeAWarmPixel();
    void autoWhiteBalanceNeutralizesAfterTheScan();
    void hiddenGroupIsLeftOutAndOkIsOneUndoOrNone();
    void hiddenGroupStaysOutOfTheNextOpen();
    void geometryWarpAndCalibrationPrimaries();
    void thePreviewCarriesThePanelsViewsAndItsScope();
    void guidedUprightFollowsADrawnLineAndLeavesAnUnguidedPicture();
};

void CameraRawSessionTests::eyedropperAndAutoNeutralizeAWarmPixel()
{
    const double red = 160 / 255.0, green = 140 / 255.0, blue = 120 / 255.0;
    const QImage warm = image(8, 8, red, green, blue);
    const std::array<int, 4> before = pixels(warm)[0];
    const CameraRawSettings::Balance solved = CameraRawSettings::neutralizeStraight(red, green, blue).value();
    CameraRawSettings settings;
    settings.temperature = solved.temperature;
    settings.tint = solved.tint;
    QVERIFY(chroma(pixels(settings.apply(warm))[0]) < chroma(before) / 2);
    const CameraRawSettings::Balance automatic = CameraRawSettings::autoBalance(warm).value();
    settings.temperature = automatic.temperature;
    settings.tint = automatic.tint;
    QVERIFY(chroma(pixels(settings.apply(warm))[0]) < chroma(before) / 2);
    const auto session = opened(warm);
    arm(*session, [](CameraRawPanel &panel) { panel.samplesWhiteBalance = true; });
    session->sampleCameraRawWhiteBalance(QPointF(1, 1));
    const FilterEdit &edit = session->filterEdit().value();
    QVERIFY(edit.settings.cameraRaw.whiteBalance == CameraRawWhiteBalance::custom);
    QVERIFY(edit.settings.cameraRaw.temperature != 0 || edit.settings.cameraRaw.tint != 0);
    QVERIFY2(chroma(pixels(edit.renderSettings().cameraRaw.apply(warm))[0]) < chroma(before) / 2, "the canvas sample neutralizes");
    session->cancelFilter();
    // A transparent pixel is ignored.
    const auto empty = opened(image(8, 8, 1, 0, 0, 0));
    const FilterSettings untouched = empty->filterEdit().value().settings;
    arm(*empty, [](CameraRawPanel &panel) { panel.samplesWhiteBalance = true; });
    empty->sampleCameraRawWhiteBalance(QPointF(1, 1));
    QVERIFY(empty->filterEdit().value().settings == untouched);
}

void CameraRawSessionTests::autoWhiteBalanceNeutralizesAfterTheScan()
{
    const QImage warm = image(8, 8, 160 / 255.0, 140 / 255.0, 120 / 255.0);
    const std::array<int, 4> before = pixels(warm)[0];
    const auto session = opened(warm);
    QVERIFY(awaited([&](std::function<void()> done) { session->applyCameraRawAutoWhiteBalance(std::move(done)); }));
    const CameraRawSettings raw = session->filterEdit().value().settings.cameraRaw;
    QVERIFY(raw.whiteBalance == CameraRawWhiteBalance::automatic);
    QVERIFY(chroma(pixels(raw.apply(warm))[0]) < chroma(before) / 2);
    session->cancelFilter();
    const auto empty = opened(image(8, 8, 1, 0, 0, 0));
    QVERIFY(awaited([&](std::function<void()> done) { empty->applyCameraRawAutoWhiteBalance(std::move(done)); }));
    const CameraRawSettings untouched = empty->filterEdit().value().settings.cameraRaw;
    QVERIFY(untouched.whiteBalance == CameraRawWhiteBalance::automatic);
    QVERIFY2(untouched.temperature == 0 && untouched.tint == 0, "a layer with no coverage leaves the sliders alone");
}

void CameraRawSessionTests::hiddenGroupIsLeftOutAndOkIsOneUndoOrNone()
{
    const QImage input = gray(8, 8);
    CameraRawSettings settings;
    settings.exposure = 1;
    settings.temperature = 40;
    const CameraRawSettings hiddenLight = settings.applying({.light = false});
    QVERIFY(pixels(hiddenLight.apply(input)) != pixels(input));
    QVERIFY(hiddenLight.exposure == 0 && hiddenLight.temperature == 40);
    const CameraRawSettings neither = settings.applying({.light = false, .color = false});
    QVERIFY(neither.isIdentity() && pixels(neither.apply(input)) == pixels(input));
    const auto session = opened(input);
    const auto original = pixels(input);
    int count = session->history.undoCount();
    QVERIFY(awaited([&](std::function<void()> done) { session->commitFilter(std::move(done)); }));
    QVERIFY2(!session->filterEdit() && session->history.undoCount() == count, "an unchanged grade is not an edit");
    QVERIFY(pixels(layerImage(*session)) == original);
    // An eye turned off drops that group.
    const auto exposed = [&] {
        session->beginFilter(FilterKind::cameraRaw);
        FilterSettings edited = session->filterEdit().value().settings;
        edited.cameraRaw.exposure = 1;
        session->updateFilter(edited, true);
    };
    exposed();
    arm(*session, [](CameraRawPanel &panel) { panel.shows.light = false; });
    count = session->history.undoCount();
    QVERIFY(awaited([&](std::function<void()> done) { session->commitFilter(std::move(done)); }));
    QCOMPARE(session->history.undoCount(), count);
    QVERIFY(pixels(layerImage(*session)) == original);
    exposed();
    session->cancelFilter();
    QVERIFY(!session->filterEdit() && pixels(layerImage(*session)) == original);
    exposed();
    count = session->history.undoCount();
    QVERIFY(awaited([&](std::function<void()> done) { session->commitFilter(std::move(done)); }));
    QVERIFY(session->history.undoCount() == count + 1 && session->history.undoName() == "Camera Raw Filter");
    QVERIFY2(std::abs(pixels(layerImage(*session))[0][0] - 176) <= 2, "OK bakes the grade");
}

void CameraRawSessionTests::hiddenGroupStaysOutOfTheNextOpen()
{
    const QImage input = gray(8, 8);
    const auto session = opened(input);
    FilterSettings edited = session->filterEdit().value().settings;
    edited.cameraRaw.exposure = 1;
    edited.cameraRaw.temperature = 40;
    session->updateFilter(edited, true);
    arm(*session, [](CameraRawPanel &panel) { panel.shows.light = false; });
    QVERIFY(awaited([&](std::function<void()> done) { session->commitFilter(std::move(done)); }));
    // OK bakes the grade, the hidden Light left out.
    CameraRawSettings colour;
    colour.temperature = 40;
    QVERIFY(pixels(layerImage(*session)) == pixels(colour.apply(input)));
    session->beginFilter(FilterKind::cameraRaw);
    const CameraRawSettings restored = session->filterEdit().value().settings.cameraRaw;
    QVERIFY(restored.exposure == 0 && restored.temperature == 40);
    session->cancelFilter();
}

void CameraRawSessionTests::thePreviewCarriesThePanelsViewsAndItsScope()
{
    QImage picture = step();
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 24; ++x)
            picture.setPixelColor(x, y, x < 12 ? QColor(5, 5, 5) : QColor(250, 200, 240));
    const auto session = opened(picture);
    FilterSettings settings = session->filterEdit().value().settings;
    settings.cameraRaw.whites = 100;
    settings.cameraRaw.blacks = -100;
    session->updateFilter(settings, true);
    QTRY_VERIFY(!session->filterEdit().value().preparing);
    const QImage graded = session->filterEdit().value().preparedPreview.value();
    QVERIFY(pixels(graded) == pixels(settings.cameraRaw.apply(picture)));
    QVERIFY(session->filterEdit().value().rawPanel.scope == CameraRawScope::make(graded));
    // The panel's views reach the preview, never the scope.
    const std::optional<CameraRawScope> scope = CameraRawScope::make(graded);
    const auto shown = [&](const std::function<void(CameraRawPanel &)> &change) {
        arm(*session, change);
        session->updateFilter(session->filterEdit().value().settings, true);
        [[maybe_unused]] const bool landed = QTest::qWaitFor([&] { return !session->filterEdit().value().preparing; }, 5000);
        return std::pair(pixels(session->filterEdit().value().preparedPreview.value()), session->filterEdit().value().rawPanel.scope == scope);
    };
    const std::vector<std::pair<std::function<void(CameraRawPanel &)>, std::vector<std::array<int, 4>>>> views{
        {[](CameraRawPanel &p) { p.clipping = CameraRawClipping::highlights; }, pixels(settings.cameraRaw.apply(picture, CameraRawClipping::highlights))},
        {[](CameraRawPanel &p) { p.clipping.reset(); p.showsHighlightClipping = true; }, pixels(CameraRawScope::overlay(graded, false, true))},
        {[](CameraRawPanel &p) { p.showsHighlightClipping = false; p.showsShadowClipping = true; }, pixels(CameraRawScope::overlay(graded, true, false))},
        {[](CameraRawPanel &p) { p.showsShadowClipping = false; p.sharpenMask = true; },
         pixels(settings.cameraRaw.apply(picture, std::nullopt, 1, 0, -1, true))},
    };
    for (size_t index = 0; index < views.size(); ++index) {
        const auto [preview, scoped] = shown(views[index].first);
        QVERIFY(preview == views[index].second && scoped);
        QVERIFY2(preview != pixels(graded), qPrintable(QString::number(index)));
    }
    // A chosen point colour shows alone while it asks to.
    FilterSettings pointed = settings;
    pointed.cameraRaw.mixer.points = {CameraRawPointColor{.hue = 10, .saturation = 1, .luminance = 0.5, .visualize = true}};
    session->updateFilter(pointed, true);
    const auto alone = shown([](CameraRawPanel &p) { p.sharpenMask = false; }).first;
    QVERIFY(alone == pixels(pointed.cameraRaw.apply(picture, std::nullopt, 1, 0, 0)) && alone != pixels(graded));
    QVERIFY(session->filterEdit().value().rawPanel.scope == CameraRawScope::make(pointed.cameraRaw.apply(picture)));
    // Settings arrive normalized: exposure stops at five.
    pointed.cameraRaw.exposure = 9;
    session->updateFilter(pointed, true);
    QCOMPARE(session->filterEdit().value().settings.cameraRaw.exposure, 5.0);
    session->updateFilter(settings, true);
    // An eye turned off keeps its group out of view.
    QVERIFY(shown([](CameraRawPanel &p) { p.sharpenMask = false; p.shows.light = false; }).first == pixels(picture));
}

void CameraRawSessionTests::geometryWarpAndCalibrationPrimaries()
{
    const QImage grid = checker(12, 12);
    CameraRawSettings settings;
    settings.geometry.vertical = 40;
    QVERIFY(pixels(settings.geometry.apply(grid)) != pixels(grid));
    settings = CameraRawSettings();
    settings.calibration.redHue = 80;
    const QImage red = image(4, 4, 1, 0, 0);
    const std::array<int, 4> before = pixels(red)[0];
    QVERIFY2(pixels(settings.apply(red))[0] != before, "calibration shifts a pure red");
    QVERIFY(pixels(settings.applying({.calibration = false}).apply(red))[0] == before);
}

void CameraRawSessionTests::guidedUprightFollowsADrawnLineAndLeavesAnUnguidedPicture()
{
    const QImage cool = image(16, 16, 0.2, 0.45, 0.8), warm = image(16, 16, 0.85, 0.25, 0.15);
    CameraRawSettings settings;
    settings.geometry.upright = CameraRawUprightMode::guided;
    QVERIFY(pixels(settings.apply(cool)) == pixels(cool));
    QVERIFY(pixels(settings.apply(warm)) == pixels(warm));
    settings.geometry.guides = {CameraRawGeometryGuide{0.1, 0.15, 0.9, 0.8}};
    const auto coolGuided = pixels(settings.apply(cool)), warmGuided = pixels(settings.apply(warm));
    QVERIFY(coolGuided != pixels(cool) && warmGuided != pixels(warm) && coolGuided != warmGuided);
}

QTEST_GUILESS_MAIN(CameraRawSessionTests)
#include "CameraRawSessionTests.moc"
