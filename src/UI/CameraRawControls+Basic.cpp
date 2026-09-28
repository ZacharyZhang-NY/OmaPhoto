#include "UI/CameraRawControls.h"
#include "Rendering/EyedropperIcon.h"
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
using Key = std::function<double &(CameraRawSettings &)>;

Key field(double CameraRawSettings::*member)
{
    return [member](CameraRawSettings &settings) -> double & { return settings.*member; };
}

constexpr CameraRawClipping highlights = CameraRawClipping::highlights, shadows = CameraRawClipping::shadows;

// Swift's labelled Picker: its title, then the menu.
QComboBox *picker(const QString &title, const QStringList &choices, const QString &help, QVBoxLayout *column)
{
    auto *box = new QWidget;
    auto *label = new QLabel(title, box);
    auto *menu = new QComboBox(box);
    menu->addItems(choices);
    menu->setToolTip(help);
    label->setBuddy(menu);
    auto *row = new QHBoxLayout(box);
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(label);
    row->addWidget(menu);
    row->addStretch(1);
    column->addWidget(box);
    return menu;
}

// Swift's `.padding(.leading, 16)` around a group of rows.
QVBoxLayout *indented(QVBoxLayout *column)
{
    auto *box = new QWidget;
    auto *rows = new QVBoxLayout(box);
    rows->setContentsMargins(16, 0, 0, 0);
    rows->setSpacing(8);
    column->addWidget(box);
    return rows;
}
}

void CameraRawControls::light(QVBoxLayout *column)
{
    slider(column, QStringLiteral("exposure"), QStringLiteral("Exposure"), field(&CameraRawSettings::exposure), CameraRawSettings::exposureLow, CameraRawSettings::exposureHigh, 2,
           highlights, QStringLiteral("Brightens or darkens the whole picture, in stops of light. Hold Alt to see clipped highlights."));
    slider(column, QStringLiteral("contrast"), QStringLiteral("Contrast"), field(&CameraRawSettings::contrast), -100, 100, 0, std::nullopt,
           QStringLiteral("Makes light and dark tones more or less different, mostly around the middle."));
    slider(column, QStringLiteral("highlights"), QStringLiteral("Highlights"), field(&CameraRawSettings::highlights), -100, 100, 0, highlights,
           QStringLiteral("Adjusts the bright parts of the picture. Hold Alt to see clipped highlights."));
    slider(column, QStringLiteral("shadows"), QStringLiteral("Shadows"), field(&CameraRawSettings::shadows), -100, 100, 0, shadows,
           QStringLiteral("Adjusts the dark parts of the picture. Hold Alt to see clipped shadows."));
    slider(column, QStringLiteral("whites"), QStringLiteral("Whites"), field(&CameraRawSettings::whites), -100, 100, 0, highlights,
           QStringLiteral("Sets the brightest point. Hold Alt to see clipped highlights."));
    slider(column, QStringLiteral("blacks"), QStringLiteral("Blacks"), field(&CameraRawSettings::blacks), -100, 100, 0, shadows,
           QStringLiteral("Sets the darkest point. Hold Alt to see clipped shadows."));
}

void CameraRawControls::color(QVBoxLayout *column)
{
    const QString help = QStringLiteral("Auto balances the average color. Custom follows Temperature and Tint.");
    auto *box = new QWidget;
    auto *label = new QLabel(QStringLiteral("White Balance"), box);
    label->setMinimumWidth(labelWidth);
    label->setToolTip(help);
    m_whiteBalance = new QComboBox(box);
    m_whiteBalance->setObjectName(QStringLiteral("whiteBalance"));
    m_whiteBalance->addItems({QStringLiteral("Custom"), QStringLiteral("Auto")});
    m_whiteBalance->setToolTip(help);
    label->setBuddy(m_whiteBalance);
    connect(m_whiteBalance, &QComboBox::activated, this, [this](int index) {
        if (CameraRawWhiteBalance(index) == CameraRawWhiteBalance::automatic) {
            m_session.applyCameraRawAutoWhiteBalance();
            return;
        }
        update([](CameraRawSettings &settings) { settings.whiteBalance = CameraRawWhiteBalance::custom; });
    });
    m_whiteBalanceSampler = new QToolButton(box);
    m_whiteBalanceSampler->setObjectName(QStringLiteral("whiteBalanceSelector"));
    m_whiteBalanceSampler->setAutoRaise(true);
    m_whiteBalanceSampler->setToolTip(QStringLiteral("Click a pixel that should be neutral."));
    m_whiteBalanceSampler->setAccessibleName(QStringLiteral("White Balance Selector"));
    connect(m_whiteBalanceSampler, &QToolButton::clicked, this,
            [this] { panel([](CameraRawPanel &raw) { raw.samplesWhiteBalance = !raw.samplesWhiteBalance; }); });
    auto *row = new QHBoxLayout(box);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(10);
    row->addWidget(label);
    row->addWidget(m_whiteBalance, 1);
    row->addWidget(m_whiteBalanceSampler);
    column->addWidget(box);
    m_whiteBalanceHint = new QLabel(QStringLiteral("Click the original layer. Click the eyedropper again to stop."));
    m_whiteBalanceHint->setObjectName(QStringLiteral("whiteBalanceHint"));
    m_whiteBalanceHint->setWordWrap(true);
    m_whiteBalanceHint->setForegroundRole(QPalette::PlaceholderText);
    QFont caption = m_whiteBalanceHint->font();
    caption.setPixelSize(10);
    m_whiteBalanceHint->setFont(caption);
    column->addWidget(m_whiteBalanceHint);
    using Kind = CameraRawSliderTrack::Kind;
    slider(column, QStringLiteral("temperature"), QStringLiteral("Temperature"), field(&CameraRawSettings::temperature), -100, 100, 0, std::nullopt,
           QStringLiteral("Shifts the picture from blue to yellow."), {Kind::temperature});
    slider(column, QStringLiteral("tint"), QStringLiteral("Tint"), field(&CameraRawSettings::tint), -100, 100, 0, std::nullopt,
           QStringLiteral("Shifts the picture from green to mauve."), {Kind::tint});
    slider(column, QStringLiteral("vibrance"), QStringLiteral("Vibrance"), field(&CameraRawSettings::vibrance), -100, 100, 0, std::nullopt,
           QStringLiteral("Strengthens quiet colors more than colors that are already strong, and protects skin tones."), {Kind::chroma});
    slider(column, QStringLiteral("saturation"), QStringLiteral("Saturation"), field(&CameraRawSettings::saturation), -100, 100, 0, std::nullopt,
           QStringLiteral("Strengthens or weakens every color by the same amount."), {Kind::chroma});
}

void CameraRawControls::effects(QVBoxLayout *column)
{
    slider(column, QStringLiteral("texture"), QStringLiteral("Texture"), field(&CameraRawSettings::texture), -100, 100, 0, std::nullopt, QStringLiteral("Adds or softens small detail."));
    slider(column, QStringLiteral("clarity"), QStringLiteral("Clarity"), field(&CameraRawSettings::clarity), -100, 100, 0, std::nullopt,
           QStringLiteral("Adds or softens contrast along broader shapes."));
    slider(column, QStringLiteral("dehaze"), QStringLiteral("Dehaze"), field(&CameraRawSettings::dehaze), -100, 100, 0, std::nullopt,
           QStringLiteral("Clears haze when raised, and adds haze when lowered."));
    subheadline(QStringLiteral("Glow"), column);
    slider(column, QStringLiteral("glow"), QStringLiteral("Glow"), field(&CameraRawSettings::glow), 0, 100, 0, std::nullopt, QStringLiteral("Spreads a glow from the bright areas."));
    m_glowStyle = picker(QStringLiteral("Style"), {QStringLiteral("Diffusion"), QStringLiteral("Bloom"), QStringLiteral("Halation")},
                         QStringLiteral("Diffusion is soft and wide, Bloom is tighter, and Halation is a red fringe."), column);
    m_glowStyle->setObjectName(QStringLiteral("glowStyle"));
    connect(m_glowStyle, &QComboBox::activated, this,
            [this](int index) { update([index](CameraRawSettings &settings) { settings.glowStyle = CameraRawGlowStyle(index); }); });
    QVBoxLayout *glow = indented(column);
    slider(glow, QStringLiteral("glowRange"), QStringLiteral("Range"), field(&CameraRawSettings::glowRange), -100, 100, 0, std::nullopt,
           QStringLiteral("Chooses how bright an area must be to glow. Has no effect until Glow is raised."));
    slider(glow, QStringLiteral("glowSpread"), QStringLiteral("Spread"), field(&CameraRawSettings::glowSpread), -100, 100, 0, std::nullopt,
           QStringLiteral("Sets how far the glow reaches. Has no effect until Glow is raised."));
    slider(glow, QStringLiteral("glowWarmth"), QStringLiteral("Warmth"), field(&CameraRawSettings::glowWarmth), -100, 100, 0, std::nullopt,
           QStringLiteral("Shifts the glow from cool to warm. Halation stays red. Has no effect until Glow is raised."));
    subheadline(QStringLiteral("Vignette"), column);
    slider(column, QStringLiteral("vignetteAmount"), QStringLiteral("Amount"), field(&CameraRawSettings::vignetteAmount), -100, 100, 0, std::nullopt,
           QStringLiteral("Darkens or lightens the edges. The center does not change."));
    m_vignetteStyle = picker(QStringLiteral("Style"), {QStringLiteral("Highlight Priority"), QStringLiteral("Color Priority"), QStringLiteral("Paint Overlay")},
                             QStringLiteral("Highlight Priority protects bright edges. Color Priority also reduces color. Paint Overlay covers the edges evenly."),
                             column);
    m_vignetteStyle->setObjectName(QStringLiteral("vignetteStyle"));
    connect(m_vignetteStyle, &QComboBox::activated, this,
            [this](int index) { update([index](CameraRawSettings &settings) { settings.vignetteStyle = CameraRawVignetteStyle(index); }); });
    QVBoxLayout *vignette = indented(column);
    slider(vignette, QStringLiteral("vignetteMidpoint"), QStringLiteral("Midpoint"), field(&CameraRawSettings::vignetteMidpoint), 0, 100, 0, std::nullopt,
           QStringLiteral("Sets where the vignette begins, from the center outward."), {}, 50);
    slider(vignette, QStringLiteral("vignetteRoundness"), QStringLiteral("Roundness"), field(&CameraRawSettings::vignetteRoundness), -100, 100, 0, std::nullopt,
           QStringLiteral("Makes the vignette rounder or more square."));
    slider(vignette, QStringLiteral("vignetteFeather"), QStringLiteral("Feather"), field(&CameraRawSettings::vignetteFeather), 0, 100, 0, std::nullopt,
           QStringLiteral("Softens the edge of the vignette."), {}, 50);
    slider(vignette, QStringLiteral("vignetteHighlights"), QStringLiteral("Highlights"), field(&CameraRawSettings::vignetteHighlights), 0, 100, 0, std::nullopt,
           QStringLiteral("Protects bright pixels while a dark vignette is applied. Used by Highlight Priority."));
    subheadline(QStringLiteral("Grain"), column);
    slider(column, QStringLiteral("grainAmount"), QStringLiteral("Amount"), field(&CameraRawSettings::grainAmount), 0, 100, 0, std::nullopt,
           QStringLiteral("Adds film grain, strongest in the middle tones."));
    slider(column, QStringLiteral("grainSize"), QStringLiteral("Size"), field(&CameraRawSettings::grainSize), 0, 100, 0, std::nullopt, QStringLiteral("Makes the grain coarser or finer."),
           {}, 25);
    slider(column, QStringLiteral("grainRoughness"), QStringLiteral("Roughness"), field(&CameraRawSettings::grainRoughness), 0, 100, 0, std::nullopt,
           QStringLiteral("Makes the grain smoother or more uneven."), {}, 50);
}
