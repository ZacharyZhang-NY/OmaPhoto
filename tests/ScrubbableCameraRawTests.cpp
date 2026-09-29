#include "Document/BrushStroke.h"
#include "ScrubFixtures.h"
#include "UI/CameraRawControls.h"
#include "UI/CameraRawRow.h"
#include <QAbstractButton>

// Swift's scrubbable Camera Raw titles, row by row.
namespace {
struct Panel {
    EditorSession session;
    std::unique_ptr<CameraRawControls> controls;
    Panel()
    {
        session.createDocument(8, 8);
        QImage image = BrushRaster::context(8, 8, false);
        image.fill(QColor(200, 120, 60));
        session.insert(ImportedImage(image, image, QStringLiteral("Warm")));
        session.beginFilter(FilterKind::cameraRaw);
        controls = std::make_unique<CameraRawControls>(session);
        controls->resize(392, 700);
        showActive(*controls);
    }
    CameraRawSettings raw() const { return session.filterEdit().value().settings.cameraRaw; }
    void set(const std::function<void(CameraRawSettings &)> &change)
    {
        FilterSettings settings = session.filterEdit().value().settings;
        change(settings.cameraRaw);
        session.updateFilter(settings, true);
    }
    void panel(const std::function<void(CameraRawPanel &)> &change)
    {
        CameraRawPanel copy = session.filterEdit().value().rawPanel;
        change(copy);
        session.setCameraRawPanel(copy);
    }
    // The title of the row whose slider is `name`.
    QLabel &title(const QString &name) const
    {
        auto *slider = controls->findChild<QSlider *>(name + QStringLiteral("Slider"));
        if (!slider)
            throw std::runtime_error(name.toStdString());
        return *slider->parentWidget()->findChildren<QLabel *>(Qt::FindDirectChildrenOnly).value(0);
    }
    bool scrubs(const QString &name) const { return title(name).cursor().shape() == Qt::SizeHorCursor; }
};

// Presses twice, the second a double click, then drags.
void doubleClickDrag(QWidget &label, double dx)
{
    send(label, QEvent::MouseButtonPress, QPointF(2, 2), Qt::LeftButton, Qt::LeftButton);
    send(label, QEvent::MouseButtonRelease, QPointF(2, 2), Qt::LeftButton, Qt::NoButton);
    send(label, QEvent::MouseButtonDblClick, QPointF(2, 2), Qt::LeftButton, Qt::LeftButton);
    send(label, QEvent::MouseMove, QPointF(2 + dx, 2), Qt::NoButton, Qt::LeftButton);
    send(label, QEvent::MouseButtonRelease, QPointF(2 + dx, 2), Qt::LeftButton, Qt::NoButton);
}
}

class ScrubbableCameraRawTests : public QObject {
    Q_OBJECT
private slots:
    void theMainRowsScrubALastDecimalAPoint();
    void aDoubleClickResetsAndStillScrubs();
    void curveAndHslFamilyTitlesScrubColorAndPointTitlesDoNot();
    void detailOpticsGeometryAndCalibrationScrubAUnitAPoint();
    void gradingTitlesStayPassive();
    void altHeldScrubsShowNoPreview();
};

void ScrubbableCameraRawTests::theMainRowsScrubALastDecimalAPoint()
{
    Panel panel;
    // Two decimals: a point is a hundredth, unrounded.
    drag(panel.title("exposure"), 50.5);
    QVERIFY(qAbs(panel.raw().exposure - 0.505) < 1e-9);
    drag(panel.title("exposure"), 9000);
    QCOMPARE(panel.raw().exposure, 5.0);
    drag(panel.title("exposure"), -90000);
    QCOMPARE(panel.raw().exposure, -5.0);
    drag(panel.title("contrast"), -30.5);
    QCOMPARE(panel.raw().contrast, -30.5);
    QCOMPARE(panel.raw().exposure, -5.0);
    // The typed setter: Custom white balance, no clipping view.
    panel.set([](CameraRawSettings &settings) { settings.whiteBalance = CameraRawWhiteBalance::automatic; });
    panel.panel([](CameraRawPanel &raw) { raw.clipping = CameraRawClipping::highlights; });
    drag(panel.title("temperature"), 12);
    QCOMPARE(panel.raw().temperature, 12.0);
    QVERIFY(panel.raw().whiteBalance == CameraRawWhiteBalance::custom);
    QVERIFY(!panel.session.filterEdit().value().rawPanel.clipping);
    panel.set([](CameraRawSettings &settings) { settings.whiteBalance = CameraRawWhiteBalance::automatic; });
    drag(panel.title("tint"), -7);
    QCOMPARE(panel.raw().tint, -7.0);
    QVERIFY(panel.raw().whiteBalance == CameraRawWhiteBalance::custom);
    QCOMPARE(scrubOverTyping(find<QLineEdit>(*panel.controls, "contrastField"), panel.title("contrast"), 3), QString("-28"));
}

void ScrubbableCameraRawTests::aDoubleClickResetsAndStillScrubs()
{
    Panel panel;
    drag(panel.title("contrast"), 40);
    QCOMPARE(panel.raw().contrast, 40.0);
    // The reset runs first; the second press scrubs from zero.
    doubleClickDrag(panel.title("contrast"), 12);
    QCOMPARE(panel.raw().contrast, 12.0);
    drag(panel.title("contrast"), -900);
    QCOMPARE(panel.raw().contrast, -100.0);
}

void ScrubbableCameraRawTests::curveAndHslFamilyTitlesScrubColorAndPointTitlesDoNot()
{
    Panel panel;
    drag(panel.title("curveHighlights"), 20.5);
    QCOMPARE(panel.raw().curve.highlights, 20.5);
    drag(panel.title("curveHighlights"), -900);
    QCOMPARE(panel.raw().curve.highlights, -100.0);
    drag(panel.title("refineSaturation"), 900);
    QCOMPARE(panel.raw().curve.refineSaturation, 100.0);
    // HSL families scrub the shown component.
    drag(panel.title("familyReds"), 15);
    QCOMPARE(panel.raw().mixer.hue[0], 15.0);
    panel.panel([](CameraRawPanel &raw) { raw.mixerTab = CameraRawMixerTab::saturation; });
    drag(panel.title("familyOranges"), -900);
    QCOMPARE(panel.raw().mixer.saturation[1], -100.0);
    QCOMPARE(panel.raw().mixer.hue[1], 0.0);
    // Swift's colorSlider and pointSlider keep plain titles.
    panel.panel([](CameraRawPanel &raw) { raw.mixerPage = CameraRawMixerPage::color; });
    for (const QString &name : {QStringLiteral("colorHue"), QStringLiteral("colorSaturation"), QStringLiteral("colorLuminance")})
        QVERIFY(!panel.scrubs(name));
    panel.set([](CameraRawSettings &settings) { settings.mixer.points = {CameraRawPointColor{.hue = 30, .saturation = 1, .luminance = 0.5}}; });
    panel.panel([](CameraRawPanel &raw) { raw.mixerPage = CameraRawMixerPage::point; });
    QVERIFY(!panel.scrubs("hueShift"));
    QVERIFY(!panel.scrubs("saturationShift"));
    drag(panel.title("hueShift"), 30);
    QCOMPARE(panel.raw().mixer.points[0].hueShift, 0.0);
}

void ScrubbableCameraRawTests::detailOpticsGeometryAndCalibrationScrubAUnitAPoint()
{
    Panel panel;
    // The typed setter: no sharpen-mask view, unrounded.
    panel.panel([](CameraRawPanel &raw) { raw.sharpenMask = true; });
    drag(panel.title("sharpenMasking"), 10.5);
    QCOMPARE(panel.raw().detail.sharpenMasking, 10.5);
    QVERIFY(!panel.session.filterEdit().value().rawPanel.sharpenMask);
    drag(panel.title("sharpenAmount"), 900);
    QCOMPARE(panel.raw().detail.sharpenAmount, 150.0);
    drag(panel.title("sharpenAmount"), -900);
    QCOMPARE(panel.raw().detail.sharpenAmount, 0.0);
    drag(panel.title("distortion"), -12.5);
    QCOMPARE(panel.raw().optics.distortion, -12.5);
    drag(panel.title("lensMidpoint"), 900);
    QCOMPARE(panel.raw().optics.vignetteMidpoint, 100.0);
    drag(panel.title("rotate"), 900);
    QCOMPARE(panel.raw().geometry.rotate, 45.0);
    drag(panel.title("vertical"), 7.5);
    QCOMPARE(panel.raw().geometry.vertical, 7.5);
    drag(panel.title("shadowTint"), -900);
    QCOMPARE(panel.raw().calibration.shadowTint, -100.0);
    drag(panel.title("redHue"), 3.5);
    QCOMPARE(panel.raw().calibration.redHue, 3.5);
}

void ScrubbableCameraRawTests::gradingTitlesStayPassive()
{
    Panel panel;
    QVERIFY(!panel.scrubs("gradeBlending"));
    QVERIFY(!panel.scrubs("gradeBalance"));
    const double blending = panel.raw().grading.blending;
    drag(panel.title("gradeBlending"), 20);
    QCOMPARE(panel.raw().grading.blending, blending);
}

void ScrubbableCameraRawTests::altHeldScrubsShowNoPreview()
{
    Panel panel;
    QWindow *window = panel.controls->windowHandle();
    QTest::keyPress(window, Qt::Key_Alt, Qt::AltModifier);
    QVERIFY(QGuiApplication::keyboardModifiers().testFlag(Qt::AltModifier));
    // The slider shows clipping under Alt; a scrub never does.
    drag(panel.title("exposure"), 10.5);
    QVERIFY(qAbs(panel.raw().exposure - 0.105) < 1e-9);
    QVERIFY(!panel.session.filterEdit().value().rawPanel.clipping);
    drag(panel.title("blacks"), -12.5);
    QCOMPARE(panel.raw().blacks, -12.5);
    QVERIFY(!panel.session.filterEdit().value().rawPanel.clipping);
    drag(panel.title("sharpenMasking"), 20.5);
    QCOMPARE(panel.raw().detail.sharpenMasking, 20.5);
    QVERIFY(!panel.session.filterEdit().value().rawPanel.sharpenMask);
    // The slider itself, for contrast, shows the view under Alt.
    panel.controls->findChild<QSlider *>(QStringLiteral("exposureSlider"))->setValue(100);
    QVERIFY(panel.session.filterEdit().value().rawPanel.clipping.has_value());
    QTest::keyRelease(window, Qt::Key_Alt, Qt::NoModifier);
}

QTEST_MAIN(ScrubbableCameraRawTests)
#include "ScrubbableCameraRawTests.moc"
