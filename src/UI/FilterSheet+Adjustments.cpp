#include "UI/FilterSheet.h"
#include <QCheckBox>
#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>

// Black & White's and Color Balance's rows, as Swift's FilterSheet.
void FilterSheet::blackWhite()
{
    // Each slider: how bright that family of colours becomes.
    const std::vector<std::pair<QString, double BlackWhiteSettings::*>> families{
        {QStringLiteral("Reds"), &BlackWhiteSettings::reds},   {QStringLiteral("Yellows"), &BlackWhiteSettings::yellows},
        {QStringLiteral("Greens"), &BlackWhiteSettings::greens}, {QStringLiteral("Cyans"), &BlackWhiteSettings::cyans},
        {QStringLiteral("Blues"), &BlackWhiteSettings::blues},   {QStringLiteral("Magentas"), &BlackWhiteSettings::magentas}};
    for (const auto &[title, field] : families)
        control(title, [field](FilterSettings &settings) -> double & { return settings.blackWhite.*field; }, BlackWhiteSettings::low,
                BlackWhiteSettings::high, QStringLiteral("%"), 0, false);
    flag(QStringLiteral("Tint"), QStringLiteral("Color the result while keeping its tones, for a sepia or a cyanotype"),
         [](FilterSettings &settings) -> bool & { return settings.blackWhite.tint; });
    control(QStringLiteral("Hue"), [](FilterSettings &settings) -> double & { return settings.blackWhite.tintHue; }, 0, 360, QStringLiteral("°"), 0,
            false);
    control(QStringLiteral("Saturation"), [](FilterSettings &settings) -> double & { return settings.blackWhite.tintSaturation; }, 0, 100,
            QStringLiteral("%"), 0, false);
    for (size_t index = m_controls.size() - 2; index < m_controls.size(); ++index)
        m_shownWhen.emplace_back(m_controls[index].slider->parentWidget(), [](const FilterSettings &settings) { return settings.blackWhite.tint; });
}

void FilterSheet::colorBalance()
{
    const std::vector<std::pair<QString, std::array<double ColorBalanceSettings::*, 3>>> ranges{
        {QStringLiteral("Shadows"),
         {&ColorBalanceSettings::shadowCyanRed, &ColorBalanceSettings::shadowMagentaGreen, &ColorBalanceSettings::shadowYellowBlue}},
        {QStringLiteral("Midtones"), {&ColorBalanceSettings::midCyanRed, &ColorBalanceSettings::midMagentaGreen, &ColorBalanceSettings::midYellowBlue}},
        {QStringLiteral("Highlights"),
         {&ColorBalanceSettings::highlightCyanRed, &ColorBalanceSettings::highlightMagentaGreen, &ColorBalanceSettings::highlightYellowBlue}}};
    const std::array<QString, 3> pairs{QStringLiteral("Cyan / Red"), QStringLiteral("Magenta / Green"), QStringLiteral("Yellow / Blue")};
    for (const auto &[range, fields] : ranges) {
        headline(range);
        for (size_t pair = 0; pair < 3; ++pair)
            control(pairs[pair], [field = fields[pair]](FilterSettings &settings) -> double & { return settings.colorBalance.*field; },
                    ColorBalanceSettings::low, ColorBalanceSettings::high, QString(), 0, false);
    }
    flag(QStringLiteral("Preserve Luminosity"), QStringLiteral("Put each pixel's brightness back afterwards, so only the color moves"),
         [](FilterSettings &settings) -> bool & { return settings.colorBalance.preserveLuminosity; });
}

void FilterSheet::flag(const QString &title, const QString &help, std::function<bool &(FilterSettings &)> key)
{
    auto *box = new QCheckBox(title, this);
    box->setToolTip(help);
    connect(box, &QCheckBox::clicked, this, [this, key](bool on) { update([&key, on](FilterSettings &settings) { key(settings) = on; }); });
    m_column->addWidget(box);
    m_flags.emplace_back(box, std::move(key));
}

void FilterSheet::headline(const QString &title)
{
    auto *label = new QLabel(title, this);
    QFont font = label->font();
    font.setPixelSize(13);
    font.setWeight(QFont::DemiBold);
    label->setFont(font);
    m_column->addWidget(label);
}
