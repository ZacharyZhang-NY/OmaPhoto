#include "UI/ColorPaletteControls.h"
#include "UI/FilterSheet.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>

// Swift's finishing filters: Vignette, Bloom / Glow, Tonal Contrast.
void FilterSheet::finishing(FilterKind kind)
{
    const auto key = [](double FilterSettings::*field) { return [field](FilterSettings &settings) -> double & { return settings.*field; }; };
    const auto help = [this](const QString &words) { m_controls.back().slider->parentWidget()->setToolTip(words); };
    if (kind == FilterKind::vignette) {
        auto *box = new QWidget(this);
        auto *row = new QHBoxLayout(box);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(8);
        auto *title = new QLabel(QStringLiteral("Color"), box);
        title->setFixedWidth(95);
        m_vignetteSwatch = new SwatchButton([this] {
            const AdjustmentColor colour = settings().vignetteColor;
            return PaletteColor{colour.red, colour.green, colour.blue};
        }, 6, 1.5, 1, box);
        m_vignetteSwatch->setObjectName(QStringLiteral("vignetteSwatch"));
        m_vignetteSwatch->setFixedSize(24, 24);
        m_vignetteSwatch->setToolTip(QStringLiteral("Choose the vignette color"));
        m_vignetteSwatch->setAccessibleName(QStringLiteral("Vignette color"));
        connect(m_vignetteSwatch, &QAbstractButton::clicked, this, [this] { m_session.openVignetteColorPicker(); });
        row->addWidget(title);
        row->addWidget(m_vignetteSwatch);
        row->addStretch(1);
        m_column->addWidget(box);
        control(QStringLiteral("Amount"), key(&FilterSettings::vignetteAmount), 0, 100, QStringLiteral("%"), 0, false);
        help(QStringLiteral("Blend the chosen color into the edges while keeping the center unchanged"));
        control(QStringLiteral("Midpoint"), key(&FilterSettings::vignetteMidpoint), 0, 100, QStringLiteral("%"), 0, false);
        control(QStringLiteral("Roundness"), key(&FilterSettings::vignetteRoundness), -100, 100, QString(), 0, false);
        control(QStringLiteral("Feather"), key(&FilterSettings::vignetteFeather), 0, 100, QStringLiteral("%"), 0, false);
        control(QStringLiteral("Highlights"), key(&FilterSettings::vignetteHighlights), 0, 100, QStringLiteral("%"), 0, false);
        help(QStringLiteral("Protect bright areas near the edge"));
    } else if (kind == FilterKind::bloomGlow) {
        control(QStringLiteral("Amount"), key(&FilterSettings::bloomAmount), 0, 100, QStringLiteral("%"), 0, false);
        control(QStringLiteral("Radius"), key(&FilterSettings::bloomRadius), 1, 150, QStringLiteral("px"), 0, true);
    } else {
        control(QStringLiteral("Amount"), key(&FilterSettings::tonalAmount), 0, 100, QStringLiteral("%"), 0, false);
        control(QStringLiteral("Shadows"), key(&FilterSettings::tonalShadows), -100, 100, QStringLiteral("%"), 0, false);
        control(QStringLiteral("Midtones"), key(&FilterSettings::tonalMidtones), -100, 100, QStringLiteral("%"), 0, false);
        control(QStringLiteral("Highlights"), key(&FilterSettings::tonalHighlights), -100, 100, QStringLiteral("%"), 0, false);
        control(QStringLiteral("Radius"), key(&FilterSettings::tonalRadius), 1, 100, QStringLiteral("px"), 0, true);
    }
}
