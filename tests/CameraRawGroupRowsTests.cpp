#include "CameraRawRowTable.h"
#include "Document/BrushStroke.h"
#include "UI/CameraRawColorControls.h"
#include "UI/CameraRawDetailOpticsControls.h"
#include "UI/CameraRawGeometryCalibrationControls.h"
#include <QCheckBox>
#include <QToolButton>

// Every row of the seven groups against Swift's own values.
namespace {
template <typename Controls> struct Group {
    EditorSession session;
    std::unique_ptr<Controls> controls;
    Group()
    {
        session.createDocument(8, 8);
        QImage image = BrushRaster::context(8, 8, false);
        image.fill(QColor(200, 120, 60));
        session.insert(ImportedImage(image, image, QStringLiteral("Warm")));
        session.beginFilter(FilterKind::cameraRaw);
    }
    void show()
    {
        controls = std::make_unique<Controls>(session);
        controls->resize(374, 900);
        controls->show();
    }
    void set(const std::function<void(CameraRawSettings &)> &change)
    {
        FilterSettings settings = session.filterEdit().value().settings;
        change(settings.cameraRaw);
        session.updateFilter(settings, true);
    }
    void arm(const std::function<void(CameraRawPanel &)> &change)
    {
        CameraRawPanel panel = session.filterEdit().value().rawPanel;
        change(panel);
        session.setCameraRawPanel(panel);
    }
};

template <typename Settings> std::function<double(const CameraRawSettings &)> of(Settings CameraRawSettings::*group, double Settings::*member)
{
    return [group, member](const CameraRawSettings &settings) { return settings.*group.*member; };
}

std::function<double(const CameraRawSettings &)> family(int tab, int index)
{
    return [tab, index](const CameraRawSettings &settings) {
        const std::array<double, 8> &values = tab == 1 ? settings.mixer.saturation : tab == 2 ? settings.mixer.luminance : settings.mixer.hue;
        return values.at(size_t(index));
    };
}

std::function<double(const CameraRawSettings &)> point(double CameraRawPointColor::*member)
{
    return [member](const CameraRawSettings &settings) { return settings.mixer.points.at(0).*member; };
}
}

class CameraRawGroupRowsTests : public QObject {
    Q_OBJECT
private slots:
    void curveRows();
    void mixerRows();
    void gradingRows();
    void detailRows();
    void opticsRows();
    void geometryRows();
    void calibrationRows();
};

void CameraRawGroupRowsTests::curveRows()
{
    Group<CameraRawCurveControls> group;
    group.show();
    const auto curve = [](double CameraRawCurveSettings::*member) { return of(&CameraRawSettings::curve, member); };
    for (const RowCase &row : std::vector<RowCase>{
             {"curveHighlights", "Highlights", "Lifts or lowers the brightest tones.", -100, 100, 0, false, 48, 88, true, false, curve(&CameraRawCurveSettings::highlights)},
             {"curveLights", "Lights", "Lifts or lowers the light tones.", -100, 100, 0, false, 48, 88, true, false, curve(&CameraRawCurveSettings::lights)},
             {"curveDarks", "Darks", "Lifts or lowers the dark tones.", -100, 100, 0, false, 48, 88, true, false, curve(&CameraRawCurveSettings::darks)},
             {"curveShadows", "Shadows", "Lifts or lowers the darkest tones.", -100, 100, 0, false, 48, 88, true, false, curve(&CameraRawCurveSettings::shadows)}})
        checkRow(*group.controls, group.session, row);
    group.arm([](CameraRawPanel &panel) { panel.curvePage = CameraRawCurvePage::point; });
    checkRow(*group.controls, group.session,
             {"refineSaturation", "Refine Saturation", "How much the curve also changes color strength. Zero matches Photoshop; lower keeps it to brightness, higher adds more color.", -100, 100, 0, false,
              48, 88, true, false, curve(&CameraRawCurveSettings::refineSaturation)});
}

void CameraRawGroupRowsTests::mixerRows()
{
    Group<CameraRawMixerControls> group;
    group.show();
    const std::array<const char *, 3> tabs{"Hue", "Saturation", "Luminance"};
    for (int tab = 0; tab < 3; ++tab) {
        group.arm([tab](CameraRawPanel &panel) { panel.mixerTab = CameraRawMixerTab(tab); });
        for (int index = 0; index < 8; ++index) {
            const QByteArray name = "family" + CameraRawMixerSettings::names.at(size_t(index)).toUtf8();
            const QByteArray title = CameraRawMixerSettings::names.at(size_t(index)).toUtf8();
            const QByteArray help = QStringLiteral("%1 of %2.").arg(QString::fromLatin1(tabs.at(size_t(tab))), QString::fromUtf8(title)).toUtf8();
            checkRow(*group.controls, group.session, {name.constData(), title.constData(), help.constData(), -100, 100, 0, false, 48, 78, true, true, family(tab, index)});
        }
    }
    group.arm([](CameraRawPanel &panel) {
        panel.mixerPage = CameraRawMixerPage::color;
        panel.mixerSwatch = 3;
    });
    checkRow(*group.controls, group.session,
             {"colorHue", "Hue", "Shifts the selected color family around the wheel.", -100, 100, 0, false, 0, 88, true, true, family(0, 3)});
    checkRow(*group.controls, group.session,
             {"colorSaturation", "Saturation", "Makes the selected color family stronger or quieter.", -100, 100, 0, false, 0, 88, true, true, family(1, 3)});
    checkRow(*group.controls, group.session,
             {"colorLuminance", "Luminance", "Makes the selected color family lighter or darker.", -100, 100, 0, false, 0, 88, true, true, family(2, 3)});
    group.arm([](CameraRawPanel &panel) {
        panel.mixerPage = CameraRawMixerPage::point;
        panel.pointIndex = 0;
    });
    group.set([](CameraRawSettings &settings) { settings.mixer.points = {CameraRawPointColor{.hue = 200, .saturation = 0.6, .luminance = 0.5}}; });
    for (const RowCase &row : std::vector<RowCase>{
             {"hueShift", "Hue Shift", "Shifts the picked color around the color wheel.", -100, 100, 0, false, 0, 110, true, true, point(&CameraRawPointColor::hueShift)},
             {"saturationShift", "Saturation Shift", "Makes the picked color stronger or quieter.", -100, 100, 0, false, 0, 110, true, true,
              point(&CameraRawPointColor::saturationShift)},
             {"luminanceShift", "Luminance Shift", "Makes the picked color lighter or darker.", -100, 100, 0, false, 0, 110, true, true,
              point(&CameraRawPointColor::luminanceShift)},
             {"hueRange", "Hue Range", "How far in hue the adjustment reaches.", 5, 180, 30, false, 0, 110, true, false, point(&CameraRawPointColor::hueRange)},
             {"saturationRange", "Saturation Range", "How far in saturation the adjustment reaches.", 0.05, 1, 0.4, false, 0, 110, true, false,
              point(&CameraRawPointColor::saturationRange)},
             {"luminanceRange", "Luminance Range", "How far in brightness the adjustment reaches.", 0.05, 1, 0.4, false, 0, 110, true, false,
              point(&CameraRawPointColor::luminanceRange)}})
        checkRow(*group.controls, group.session, row);
}

void CameraRawGroupRowsTests::gradingRows()
{
    Group<CameraRawGradingControls> group;
    group.show();
    checkRow(*group.controls, group.session,
             {"gradeBlending", "Blending", "Controls how much the three tonal wheels overlap.", 0, 100, 50, false, 0, 78, true, false,
              of(&CameraRawSettings::grading, &CameraRawGradingSettings::blending)});
    checkRow(*group.controls, group.session,
             {"gradeBalance", "Balance", "Shifts the wheels toward shadows or highlights.", -100, 100, 0, false, 0, 78, true, false,
              of(&CameraRawSettings::grading, &CameraRawGradingSettings::balance)});
}

void CameraRawGroupRowsTests::detailRows()
{
    Group<CameraRawDetailControls> group;
    // Idle noise rows rest; raised, they take writes.
    group.set([](CameraRawSettings &settings) {
        settings.detail.noiseLuminance = 30;
        settings.detail.noiseColor = 30;
    });
    group.show();
    const auto detail = [](double CameraRawDetailSettings::*member) { return of(&CameraRawSettings::detail, member); };
    for (const RowCase &row : std::vector<RowCase>{
             {"sharpenAmount", "Amount", "Controls how strong the sharpening is.", 0, 150, 0, true, 56, 96, false, false, detail(&CameraRawDetailSettings::sharpenAmount)},
             {"sharpenRadius", "Radius", "How far from each edge the sharpening reaches, in pixels.", 0, 100, 10, true, 56, 96, false, false,
              detail(&CameraRawDetailSettings::sharpenRadius)},
             {"sharpenDetail", "Detail", "Emphasizes fine texture over broader edges.", 0, 100, 25, true, 56, 96, false, false,
              detail(&CameraRawDetailSettings::sharpenDetail)},
             {"sharpenMasking", "Masking", "Limits sharpening to stronger edges. Hold Alt to see the mask.", 0, 100, 0, true, 56, 96, false, false,
              detail(&CameraRawDetailSettings::sharpenMasking)},
             {"noiseLuminanceDetail", "Luminance Detail", "Preserves fine texture while luminance noise is reduced.", 0, 100, 50, true, 56, 96, false, false,
              detail(&CameraRawDetailSettings::noiseLuminanceDetail)},
             {"noiseLuminanceContrast", "Luminance Contrast", "Keeps local contrast after luminance smoothing.", 0, 100, 0, true, 56, 96, false, false,
              detail(&CameraRawDetailSettings::noiseLuminanceContrast)},
             {"noiseColorDetail", "Color Detail", "Preserves colored edges while color noise is reduced.", 0, 100, 50, true, 56, 96, false, false,
              detail(&CameraRawDetailSettings::noiseColorDetail)},
             {"noiseColorSmoothness", "Color Smoothness", "Makes the color smoothing softer or tighter.", 0, 100, 50, true, 56, 96, false, false,
              detail(&CameraRawDetailSettings::noiseColorSmoothness)},
             {"noiseLuminance", "Luminance", "Smooths grain and noise in brightness.", 0, 100, 0, true, 56, 96, false, false,
              detail(&CameraRawDetailSettings::noiseLuminance)},
             {"noiseColor", "Color", "Smooths colored speckles.", 0, 100, 0, true, 56, 96, false, false, detail(&CameraRawDetailSettings::noiseColor)}})
        checkRow(*group.controls, group.session, row);
}

void CameraRawGroupRowsTests::opticsRows()
{
    Group<CameraRawOpticsControls> group;
    group.set([](CameraRawSettings &settings) { settings.optics.enableLensProfile = true; });
    group.show();
    const auto optics = [](double CameraRawOpticsSettings::*member) { return of(&CameraRawSettings::optics, member); };
    for (const RowCase &row : std::vector<RowCase>{
             {"profileDistortion", "Distortion", "How much of the profile distortion correction is applied.", 0, 100, 100, true, 56, 96, false, false,
              optics(&CameraRawOpticsSettings::profileDistortion)},
             {"profileVignetting", "Vignetting", "How much of the profile vignetting correction is applied.", 0, 100, 100, true, 56, 96, false, false,
              optics(&CameraRawOpticsSettings::profileVignetting)},
             {"distortion", "Distortion", "Straightens barrel or pincushion bending.", -100, 100, 0, false, 56, 96, false, false,
              optics(&CameraRawOpticsSettings::distortion)},
             {"purpleAmount", "Purple Amount", "Weakens purple fringes inside the purple hue range.", 0, 100, 0, true, 56, 96, false, false,
              optics(&CameraRawOpticsSettings::purpleAmount)},
             {"greenAmount", "Green Amount", "Weakens green fringes inside the green hue range.", 0, 100, 0, true, 56, 96, false, false,
              optics(&CameraRawOpticsSettings::greenAmount)},
             {"lensVignetting", "Vignetting", "Brightens or darkens the corners to counter lens falloff.", -100, 100, 0, false, 56, 96, false, false,
              optics(&CameraRawOpticsSettings::vignetteAmount)},
             {"lensMidpoint", "Midpoint", "Moves the vignette correction inward or outward.", 0, 100, 50, true, 56, 96, false, false,
              optics(&CameraRawOpticsSettings::vignetteMidpoint)}})
        checkRow(*group.controls, group.session, row);
}

void CameraRawGroupRowsTests::geometryRows()
{
    Group<CameraRawGeometryControls> group;
    group.show();
    const auto geometry = [](double CameraRawGeometrySettings::*member) { return of(&CameraRawSettings::geometry, member); };
    for (const RowCase &row : std::vector<RowCase>{
             {"vertical", "Vertical", "Straightens vertical lines toward the center.", -100, 100, 0, true, 56, 96, false, false, geometry(&CameraRawGeometrySettings::vertical)},
             {"horizontal", "Horizontal", "Straightens horizontal lines toward the center.", -100, 100, 0, true, 56, 96, false, false,
              geometry(&CameraRawGeometrySettings::horizontal)},
             {"rotate", "Rotate", "Rotates the picture around its center.", -45, 45, 0, true, 56, 96, false, false, geometry(&CameraRawGeometrySettings::rotate)},
             {"aspect", "Aspect", "Stretches width relative to height.", -100, 100, 0, true, 56, 96, false, false, geometry(&CameraRawGeometrySettings::aspect)},
             {"scale", "Scale", "Zooms the transformed picture within the frame.", -100, 100, 0, true, 56, 96, false, false, geometry(&CameraRawGeometrySettings::scale)},
             {"offsetX", "Offset X", "Moves the picture left or right.", -100, 100, 0, true, 56, 96, false, false, geometry(&CameraRawGeometrySettings::offsetX)},
             {"offsetY", "Offset Y", "Moves the picture up or down.", -100, 100, 0, true, 56, 96, false, false, geometry(&CameraRawGeometrySettings::offsetY)}})
        checkRow(*group.controls, group.session, row);
}

void CameraRawGroupRowsTests::calibrationRows()
{
    Group<CameraRawCalibrationControls> group;
    group.show();
    const auto calibration = [](double CameraRawCalibrationSettings::*member) { return of(&CameraRawSettings::calibration, member); };
    for (const RowCase &row : std::vector<RowCase>{
             {"shadowTint", "Tint", "Adds green or magenta to the darkest tones.", -100, 100, 0, true, 56, 96, false, false,
              calibration(&CameraRawCalibrationSettings::shadowTint)},
             {"redHue", "Hue", "Shifts how red is interpreted.", -100, 100, 0, true, 56, 96, false, false, calibration(&CameraRawCalibrationSettings::redHue)},
             {"redSaturation", "Saturation", "Strengthens or weakens the red primary.", -100, 100, 0, true, 56, 96, false, false,
              calibration(&CameraRawCalibrationSettings::redSaturation)},
             {"greenHue", "Hue", "Shifts how green is interpreted.", -100, 100, 0, true, 56, 96, false, false, calibration(&CameraRawCalibrationSettings::greenHue)},
             {"greenSaturation", "Saturation", "Strengthens or weakens the green primary.", -100, 100, 0, true, 56, 96, false, false,
              calibration(&CameraRawCalibrationSettings::greenSaturation)},
             {"blueHue", "Hue", "Shifts how blue is interpreted.", -100, 100, 0, true, 56, 96, false, false, calibration(&CameraRawCalibrationSettings::blueHue)},
             {"blueSaturation", "Saturation", "Strengthens or weakens the blue primary.", -100, 100, 0, true, 56, 96, false, false,
              calibration(&CameraRawCalibrationSettings::blueSaturation)}})
        checkRow(*group.controls, group.session, row);
}

QTEST_MAIN(CameraRawGroupRowsTests)
#include "CameraRawGroupRowsTests.moc"
