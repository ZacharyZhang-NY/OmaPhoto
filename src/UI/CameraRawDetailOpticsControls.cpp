#include "UI/CameraRawDetailOpticsControls.h"
#include "UI/ColorPickerSheet.h"
#include "Rendering/EyedropperIcon.h"
#include "UI/CameraRawControls.h"
#include <QCheckBox>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
QLabel *text(const QString &words, int pixels, bool secondary)
{
    auto *label = new QLabel(words);
    QFont font = label->font();
    font.setPixelSize(pixels);
    label->setFont(font);
    label->setWordWrap(true);
    if (secondary)
        label->setForegroundRole(QPalette::PlaceholderText);
    return label;
}

// Swift's `.opacity(0.45).disabled(…)` over a group of rows.
QWidget *group(QVBoxLayout *column)
{
    auto *box = new QWidget;
    auto *rows = new QVBoxLayout(box);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(8);
    box->setGraphicsEffect(new QGraphicsOpacityEffect(box));
    column->addWidget(box);
    return box;
}

void dim(QWidget *box, bool active)
{
    box->setEnabled(active);
    static_cast<QGraphicsOpacityEffect *>(box->graphicsEffect())->setOpacity(active ? 1 : 0.45);
}

void update(EditorSession &session, const std::function<void(CameraRawSettings &)> &change)
{
    FilterSettings settings = session.filterEdit() ? session.filterEdit()->settings : FilterSettings();
    change(settings.cameraRaw);
    session.updateFilter(settings, session.filterEdit() ? session.filterEdit()->preview : true);
}

CameraRawSettings raw(const EditorSession &session)
{
    return session.filterEdit() ? session.filterEdit()->settings.cameraRaw : CameraRawSettings();
}
}

CameraRawDetailControls::CameraRawDetailControls(EditorSession &session, QWidget *parent) : QWidget(parent), m_session(session)
{
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(8);
    column->addWidget(text(QStringLiteral("Sharpening"), 11, false));
    row(column, QStringLiteral("sharpenAmount"), QStringLiteral("Amount"), &CameraRawDetailSettings::sharpenAmount, 150, 0, false,
        QStringLiteral("Controls how strong the sharpening is."));
    row(column, QStringLiteral("sharpenRadius"), QStringLiteral("Radius"), &CameraRawDetailSettings::sharpenRadius, 100, 10, false,
        QStringLiteral("How far from each edge the sharpening reaches, in pixels."));
    row(column, QStringLiteral("sharpenDetail"), QStringLiteral("Detail"), &CameraRawDetailSettings::sharpenDetail, 100, 25, false,
        QStringLiteral("Emphasizes fine texture over broader edges."));
    row(column, QStringLiteral("sharpenMasking"), QStringLiteral("Masking"), &CameraRawDetailSettings::sharpenMasking, 100, 0, true,
        QStringLiteral("Limits sharpening to stronger edges. Hold Alt to see the mask."));
    column->addWidget(text(QStringLiteral("Noise Reduction"), 11, false));
    row(column, QStringLiteral("noiseLuminance"), QStringLiteral("Luminance"), &CameraRawDetailSettings::noiseLuminance, 100, 0, false,
        QStringLiteral("Smooths grain and noise in brightness."));
    m_luminanceDetail = group(column);
    auto *luminance = static_cast<QVBoxLayout *>(m_luminanceDetail->layout());
    row(luminance, QStringLiteral("noiseLuminanceDetail"), QStringLiteral("Luminance Detail"), &CameraRawDetailSettings::noiseLuminanceDetail, 100, 50,
        false, QStringLiteral("Preserves fine texture while luminance noise is reduced."));
    row(luminance, QStringLiteral("noiseLuminanceContrast"), QStringLiteral("Luminance Contrast"), &CameraRawDetailSettings::noiseLuminanceContrast, 100, 0,
        false, QStringLiteral("Keeps local contrast after luminance smoothing."));
    row(column, QStringLiteral("noiseColor"), QStringLiteral("Color"), &CameraRawDetailSettings::noiseColor, 100, 0, false,
        QStringLiteral("Smooths colored speckles."));
    m_colorDetail = group(column);
    auto *color = static_cast<QVBoxLayout *>(m_colorDetail->layout());
    row(color, QStringLiteral("noiseColorDetail"), QStringLiteral("Color Detail"), &CameraRawDetailSettings::noiseColorDetail, 100, 50, false,
        QStringLiteral("Preserves colored edges while color noise is reduced."));
    row(color, QStringLiteral("noiseColorSmoothness"), QStringLiteral("Color Smoothness"), &CameraRawDetailSettings::noiseColorSmoothness, 100, 50, false,
        QStringLiteral("Makes the color smoothing softer or tighter."));
    connect(&m_session, &EditorSession::changed, this, &CameraRawDetailControls::synchronize);
    synchronize();
}

void CameraRawDetailControls::row(QVBoxLayout *column, const QString &name, const QString &title, double CameraRawDetailSettings::*key,
                                           double high, double reset, bool maskingPreview, const QString &help)
{
    auto *row = new CameraRawRow({.name = name, .title = title, .help = help, .low = 0, .high = high, .titleWidth = CameraRawControls::labelWidth, .scrub = 1},
                                 [this, key] { return raw(m_session).detail.*key; },
                                 [this, key, maskingPreview](double value) { assign(key, std::round(value), maskingPreview); },
                                 [this, key](double value) { assign(key, value, false); }, [this, key, reset] { assign(key, reset, false); });
    column->addWidget(row);
    m_rows.push_back(row);
}

// Swift's assignDetail: the sharpen mask shows while Alt is held.
void CameraRawDetailControls::assign(double CameraRawDetailSettings::*key, double value, bool maskingPreview)
{
    if (!m_session.filterEdit())
        return;
    CameraRawPanel panel = m_session.filterEdit()->rawPanel;
    panel.sharpenMask = maskingPreview && QGuiApplication::keyboardModifiers().testFlag(Qt::AltModifier);
    m_session.setCameraRawPanel(panel);
    ::update(m_session, [key, value](CameraRawSettings &settings) { settings.detail.*key = value; });
}

void CameraRawDetailControls::synchronize()
{
    const CameraRawDetailSettings detail = raw(m_session).detail;
    dim(m_luminanceDetail, detail.noiseLuminance > 0);
    dim(m_colorDetail, detail.noiseColor > 0);
    for (CameraRawRow *row : m_rows)
        row->synchronize();
}

CameraRawOpticsControls::CameraRawOpticsControls(EditorSession &session, QWidget *parent) : QWidget(parent), m_session(session)
{
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(8);
    const auto toggle = [this, column](const QString &name, const QString &title, const QString &help, bool CameraRawOpticsSettings::*key) {
        auto *box = new QCheckBox(title, this);
        box->setObjectName(name);
        box->setToolTip(help);
        connect(box, &QCheckBox::clicked, this, [this, key](bool on) { update([key, on](CameraRawOpticsSettings &optics) { optics.*key = on; }); });
        column->addWidget(box);
        return box;
    };
    m_chromatic = toggle(QStringLiteral("removeChromaticAberration"), QStringLiteral("Remove Chromatic Aberration"),
                         QStringLiteral("Pulls red and blue fringes apart toward the center to reduce color edging."),
                         &CameraRawOpticsSettings::removeChromaticAberration);
    m_profile = toggle(QStringLiteral("enableLensProfile"), QStringLiteral("Enable Lens Profile Corrections"),
                       QStringLiteral("Applies generic profile strength when camera metadata is not available."), &CameraRawOpticsSettings::enableLensProfile);
    m_profileRows = new QWidget(this);
    auto *profile = new QVBoxLayout(m_profileRows);
    profile->setContentsMargins(0, 0, 0, 0);
    profile->setSpacing(8);
    profile->addWidget(text(QStringLiteral("No lens metadata on this layer. Profile sliders set generic correction strength."), 10, true));
    row(profile, QStringLiteral("profileDistortion"), QStringLiteral("Distortion"), &CameraRawOpticsSettings::profileDistortion, 0, 100, 100,
        QStringLiteral("How much of the profile distortion correction is applied."));
    row(profile, QStringLiteral("profileVignetting"), QStringLiteral("Vignetting"), &CameraRawOpticsSettings::profileVignetting, 0, 100, 100,
        QStringLiteral("How much of the profile vignetting correction is applied."));
    column->addWidget(m_profileRows);
    column->addWidget(text(QStringLiteral("Manual"), 11, false));
    row(column, QStringLiteral("distortion"), QStringLiteral("Distortion"), &CameraRawOpticsSettings::distortion, -100, 100, 0,
        QStringLiteral("Straightens barrel or pincushion bending."));
    auto *defringe = new QHBoxLayout;
    defringe->setSpacing(10);
    auto *title = new QLabel(QStringLiteral("Defringe"), this);
    title->setMinimumWidth(CameraRawControls::labelWidth);
    title->setToolTip(QStringLiteral("Click a purple or green fringe to set its hue range."));
    m_defringe = new QToolButton(this);
    m_defringe->setObjectName(QStringLiteral("defringeSampler"));
    m_defringe->setAutoRaise(true);
    m_defringe->setToolTip(QStringLiteral("Click a purple or green fringe to set its hue range."));
    connect(m_defringe, &QToolButton::clicked, this, [this] {
        if (!m_session.filterEdit())
            return;
        CameraRawPanel panel = m_session.filterEdit()->rawPanel;
        panel.samplesDefringe = !panel.samplesDefringe;
        m_session.setCameraRawPanel(panel);
    });
    defringe->addWidget(title);
    defringe->addWidget(m_defringe);
    defringe->addStretch(1);
    column->addLayout(defringe);
    m_defringeHint = text(QStringLiteral("Click the fringe on the layer. Click the eyedropper again to stop."), 10, true);
    m_defringeHint->setObjectName(QStringLiteral("defringeHint"));
    column->addWidget(m_defringeHint);
    row(column, QStringLiteral("purpleAmount"), QStringLiteral("Purple Amount"), &CameraRawOpticsSettings::purpleAmount, 0, 100, 0,
        QStringLiteral("Weakens purple fringes inside the purple hue range."));
    column->addWidget(hueRange(QStringLiteral("purpleHue"), QStringLiteral("Purple Hue"), &CameraRawOpticsSettings::purpleHueLow,
                               &CameraRawOpticsSettings::purpleHueHigh, 270, 310, QStringLiteral("Hue range where purple defringe runs.")));
    row(column, QStringLiteral("greenAmount"), QStringLiteral("Green Amount"), &CameraRawOpticsSettings::greenAmount, 0, 100, 0,
        QStringLiteral("Weakens green fringes inside the green hue range."));
    column->addWidget(hueRange(QStringLiteral("greenHue"), QStringLiteral("Green Hue"), &CameraRawOpticsSettings::greenHueLow,
                               &CameraRawOpticsSettings::greenHueHigh, 60, 120, QStringLiteral("Hue range where green defringe runs.")));
    row(column, QStringLiteral("lensVignetting"), QStringLiteral("Vignetting"), &CameraRawOpticsSettings::vignetteAmount, -100, 100, 0,
        QStringLiteral("Brightens or darkens the corners to counter lens falloff."));
    row(column, QStringLiteral("lensMidpoint"), QStringLiteral("Midpoint"), &CameraRawOpticsSettings::vignetteMidpoint, 0, 100, 50,
        QStringLiteral("Moves the vignette correction inward or outward."));
    connect(&m_session, &EditorSession::changed, this, &CameraRawOpticsControls::synchronize);
    synchronize();
}

// Swift's opticsSlider: ranges from zero round the slider's value.
void CameraRawOpticsControls::row(QVBoxLayout *column, const QString &name, const QString &title, double CameraRawOpticsSettings::*key, double low,
                                  double high, double reset, const QString &help)
{
    const auto set = [this, key](double value) { update([key, value](CameraRawOpticsSettings &optics) { optics.*key = value; }); };
    auto *row = new CameraRawRow({.name = name, .title = title, .help = help, .low = low, .high = high, .titleWidth = CameraRawControls::labelWidth, .scrub = 1},
                                 [this, key] { return raw(m_session).optics.*key; },
                                 [set, low](double value) { set(low < 0 ? value : std::round(value)); }, set, [set, reset] { set(reset); });
    column->addWidget(row);
    m_rows.push_back(row);
}

// Swift's hueRange: Low and High, inset past the titles.
QWidget *CameraRawOpticsControls::hueRange(const QString &name, const QString &title, double CameraRawOpticsSettings::*low,
                                           double CameraRawOpticsSettings::*high, double lowReset, double highReset, const QString &help)
{
    auto *box = new QWidget;
    auto *column = new QVBoxLayout(box);
    column->setContentsMargins(CameraRawControls::labelWidth + 10, 0, 0, 0);
    column->setSpacing(4);
    QLabel *heading = text(title, 10, true);
    heading->setToolTip(help);
    column->addWidget(heading);
    auto *row = new QHBoxLayout;
    row->setSpacing(8);
    for (const bool start : {true, false}) {
        double CameraRawOpticsSettings::*key = start ? low : high;
        const QString words = start ? QStringLiteral("Start of the hue range, in degrees.") : QStringLiteral("End of the hue range, in degrees.");
        QLabel *end = text(start ? QStringLiteral("Low") : QStringLiteral("High"), 10, false);
        end->setToolTip(words);
        const double reset = start ? lowReset : highReset;
        auto *slider = new CameraRawSlider(
            0, 360, {}, words, [this, key](double value) { update([key, value](CameraRawOpticsSettings &optics) { optics.*key = std::round(value); }); },
            [this, key, reset] { update([key, reset](CameraRawOpticsSettings &optics) { optics.*key = reset; }); }, box);
        slider->setObjectName(name + (start ? QStringLiteral("Low") : QStringLiteral("High")));
        row->addWidget(end);
        row->addWidget(slider, 1);
        m_hueSliders.push_back({slider, key});
    }
    column->addLayout(row);
    return box;
}

void CameraRawOpticsControls::update(const std::function<void(CameraRawOpticsSettings &)> &change)
{
    ::update(m_session, [&change](CameraRawSettings &settings) { change(settings.optics); });
}

void CameraRawOpticsControls::synchronize()
{
    const CameraRawOpticsSettings optics = raw(m_session).optics;
    const QSignalBlocker chromatic(m_chromatic), profile(m_profile);
    m_chromatic->setChecked(optics.removeChromaticAberration);
    m_profile->setChecked(optics.enableLensProfile);
    m_profileRows->setVisible(optics.enableLensProfile);
    const bool sampling = m_session.filterEdit() && m_session.filterEdit()->rawPanel.samplesDefringe;
    m_defringe->setIcon(EyedropperIcon::icon(palette().color(sampling ? QPalette::Highlight : QPalette::PlaceholderText), devicePixelRatioF()));
    m_defringeHint->setVisible(sampling);
    for (CameraRawRow *row : m_rows)
        row->synchronize();
    for (const auto &[slider, key] : m_hueSliders)
        slider->display(optics.*key);
}

CameraRawDetailControls::~CameraRawDetailControls()
{
    releaseFocus(*this);
}

CameraRawOpticsControls::~CameraRawOpticsControls()
{
    releaseFocus(*this);
}
