#include "UI/FilterSheet.h"
#include "UI/ColorPaletteControls.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>
#include <QVBoxLayout>

// Swift's ditherControls, beside the filter sheet's other kinds.
QComboBox *FilterSheet::menu(const QString &title, const QString &name, const QString &help)
{
    auto *box = new QWidget(this);
    auto *row = new QHBoxLayout(box);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(10);
    auto *label = new QLabel(title, box);
    auto *choices = new QComboBox(box);
    choices->setObjectName(name);
    choices->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    label->setBuddy(choices);
    box->setToolTip(help);
    row->addWidget(label);
    row->addWidget(choices);
    row->addStretch(1);
    m_column->addWidget(box);
    return choices;
}

void FilterSheet::dither()
{
    const auto key = [](double DitherSettings::*field) { return [field](FilterSettings &settings) -> double & { return settings.dither.*field; }; };
    const auto box = [this] { return m_controls.back().slider->parentWidget(); };
    // Style: grouped as Swift's menu, a rule between groups.
    QComboBox *style = menu(QStringLiteral("Style"), QStringLiteral("ditherStyle"), QString());
    for (const DitherStyle each : allDitherStyles) {
        if (style->count() > 0 && ditherGroup(each) != ditherGroup(DitherStyle(style->itemData(style->count() - 1).toInt())))
            style->insertSeparator(style->count());
        style->addItem(rawValue(each), int(each));
    }
    connect(style, &QComboBox::activated, this,
            [this, style](int index) { update([&](FilterSettings &settings) { settings.dither.style = DitherStyle(style->itemData(index).toInt()); }); });
    m_follows.emplace_back([style](const FilterSettings &settings) {
        const QSignalBlocker quiet(style);
        style->setCurrentIndex(style->findData(int(settings.dither.style)));
    });
    control(QStringLiteral("Pixel Size"), key(&DitherSettings::pixelSize), DitherSettings::pixelSizeLow, DitherSettings::pixelSizeHigh, QStringLiteral("px"), 0, false);
    box()->setToolTip(QStringLiteral("Make each dithered pixel this many pixels across, for a chunky old-screen look"));
    m_shownWhen.emplace_back(box(), [](const FilterSettings &settings) { return usesPixelSize(settings.dither.style); });
    control(QStringLiteral("Text Size"), key(&DitherSettings::textSize), DitherSettings::textSizeLow, DitherSettings::textSizeHigh, QStringLiteral("px"), 0, false);
    box()->setToolTip(QStringLiteral("The height of each line of characters"));
    m_shownWhen.emplace_back(box(), [](const FilterSettings &settings) { return settings.dither.style == DitherStyle::ascii; });
    // Scanlines' four rows.
    const auto scanlines = [](const FilterSettings &settings) { return settings.dither.style == DitherStyle::scanlines; };
    control(QStringLiteral("Line Spacing"), key(&DitherSettings::lineSpacing), DitherSettings::lineSpacingLow, DitherSettings::lineSpacingHigh, QStringLiteral("px"), 0,
            false);
    box()->setToolTip(QStringLiteral("How far apart the screen's lines are"));
    m_shownWhen.emplace_back(box(), scanlines);
    control(QStringLiteral("Glow"), key(&DitherSettings::glow), 0, 100, QStringLiteral("%"), 0, false);
    box()->setToolTip(QStringLiteral("Light blooming around the lines, like a CRT's phosphors"));
    m_shownWhen.emplace_back(box(), scanlines);
    control(QStringLiteral("Dots"), key(&DitherSettings::dots), 0, 100, QStringLiteral("%"), 0, false);
    box()->setToolTip(QStringLiteral("Break the lines into glowing beads"));
    m_shownWhen.emplace_back(box(), scanlines);
    control(QStringLiteral("Wobble"), key(&DitherSettings::wobble), DitherSettings::wobbleLow, DitherSettings::wobbleHigh, QStringLiteral("px"), 0, false);
    box()->setToolTip(QStringLiteral("Make the lines waver sideways down the screen, like a CRT losing sync"));
    m_shownWhen.emplace_back(box(), scanlines);
    control(QStringLiteral("Cell Size"), key(&DitherSettings::cellSize), DitherSettings::cellSizeLow, DitherSettings::cellSizeHigh, QStringLiteral("px"), 0, false);
    m_shownWhen.emplace_back(box(), [](const FilterSettings &settings) { return isHalftone(settings.dither.style); });
    control(QStringLiteral("Angle"), key(&DitherSettings::angle), -90, 90, QStringLiteral("°"), 0, false);
    m_shownWhen.emplace_back(box(), [](const FilterSettings &settings) { return isHalftone(settings.dither.style); });
    // ASCII's characters, typed as they will draw.
    auto *characters = new QWidget(this);
    auto *row = new QHBoxLayout(characters);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(10);
    auto *label = new QLabel(QStringLiteral("Characters"), characters);
    auto *field = new QLineEdit(characters);
    field->setObjectName(QStringLiteral("ditherCharacters"));
    field->setPlaceholderText(QStringLiteral("Characters"));
    field->setAccessibleName(QStringLiteral("Characters"));
    field->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    label->setBuddy(field);
    characters->setToolTip(QStringLiteral("The characters to draw with, in any order: each spot gets the one whose ink best matches its tone"));
    row->addWidget(label);
    row->addWidget(field, 1);
    m_column->addWidget(characters);
    connect(field, &QLineEdit::textEdited, this, [this](const QString &text) { update([&text](FilterSettings &settings) { settings.dither.characters = text; }); });
    // Left, it shows what the settings kept of the typing.
    connect(field, &QLineEdit::editingFinished, this, [this, field] { field->setText(settings().dither.characters); });
    m_follows.emplace_back([field](const FilterSettings &settings) {
        // Typing keeps its own text; the settings hold it normalized.
        if (!field->hasFocus() && field->text() != settings.dither.characters)
            field->setText(settings.dither.characters);
    });
    m_shownWhen.emplace_back(characters, [](const FilterSettings &settings) { return settings.dither.style == DitherStyle::ascii; });
    control(QStringLiteral("Tones"), key(&DitherSettings::levels), DitherSettings::levelsLow, DitherSettings::levelsHigh, QString(), 0, false);
    box()->setToolTip(QStringLiteral("Tones per channel: 2 is pure black and white"));
    m_shownWhen.emplace_back(box(), [](const FilterSettings &settings) { return hasTones(settings.dither.style); });
    control(QStringLiteral("Diffusion"), key(&DitherSettings::diffusion), 0, 100, QStringLiteral("%"), 0, false);
    box()->setToolTip(QStringLiteral("How much of each pixel's error spreads to its neighbors. Less gives flatter areas"));
    m_shownWhen.emplace_back(box(), [](const FilterSettings &settings) { return diffuses(settings.dither.style); });
    control(QStringLiteral("Density"), key(&DitherSettings::density), -100, 100, QString(), 0, false);
    box()->setToolTip(QStringLiteral("More ink (darker) or less before dithering"));
    control(QStringLiteral("Contrast"), key(&DitherSettings::contrast), -100, 100, QString(), 0, false);
    // Colors: a menu, as Swift's: the panel keeps its size.
    QComboBox *colors = menu(QStringLiteral("Colors"), QStringLiteral("ditherColors"), QString());
    for (const DitherColors each : allDitherColors)
        colors->addItem(rawValue(each), int(each));
    connect(colors, &QComboBox::activated, this, [this](int index) { update([index](FilterSettings &settings) { settings.dither.colors = DitherColors(index); }); });
    m_follows.emplace_back([colors](const FilterSettings &settings) {
        const QSignalBlocker quiet(colors);
        colors->setCurrentIndex(int(settings.dither.colors));
    });
    // Two Colors: the dark and the light swatch.
    auto *pair = new QWidget(this);
    auto *swatches = new QHBoxLayout(pair);
    swatches->setContentsMargins(0, 0, 0, 0);
    swatches->setSpacing(8);
    for (const bool light : {false, true}) {
        auto *title = new QLabel(light ? QStringLiteral("Light") : QStringLiteral("Dark"), pair);
        auto *swatch = new SwatchButton([this, light] {
            const AdjustmentColor colour = light ? settings().dither.light : settings().dither.dark;
            return PaletteColor{colour.red, colour.green, colour.blue};
        }, 6, 1.5, 1, pair);
        swatch->setObjectName(light ? QStringLiteral("ditherLight") : QStringLiteral("ditherDark"));
        swatch->setFixedSize(24, 24);
        swatch->setToolTip(light ? QStringLiteral("Choose the light color") : QStringLiteral("Choose the dark color"));
        swatch->setAccessibleName(light ? QStringLiteral("Light color") : QStringLiteral("Dark color"));
        connect(swatch, &QAbstractButton::clicked, this, [this, light] { m_session.openDitherColorPicker(light); });
        m_follows.emplace_back([swatch](const FilterSettings &) { swatch->update(); });
        if (light)
            swatches->addSpacing(10);
        swatches->addWidget(title);
        swatches->addWidget(swatch);
    }
    swatches->addStretch(1);
    m_column->addWidget(pair);
    m_shownWhen.emplace_back(pair, [](const FilterSettings &settings) { return settings.dither.colors == DitherColors::twoColors; });
    QComboBox *shape = menu(QStringLiteral("Pixel Shape"), QStringLiteral("ditherPixelShape"),
                            QStringLiteral("Draw each chunky pixel as a solid square, or as a round dot like a dot-matrix screen"));
    for (const DitherPixelShape each : allDitherPixelShapes)
        shape->addItem(rawValue(each), int(each));
    connect(shape, &QComboBox::activated, this, [this](int index) { update([index](FilterSettings &settings) { settings.dither.pixelShape = DitherPixelShape(index); }); });
    m_follows.emplace_back([shape](const FilterSettings &settings) {
        const QSignalBlocker quiet(shape);
        shape->setCurrentIndex(int(settings.dither.pixelShape));
    });
    m_shownWhen.emplace_back(shape->parentWidget(), [](const FilterSettings &settings) { return settings.dither.pixelSize > 1 && usesPixelSize(settings.dither.style); });
    flag(QStringLiteral("Light on Dark"), QStringLiteral("Draw the marks for the light tones on the dark color, like a glowing screen"),
         [](FilterSettings &settings) -> bool & { return settings.dither.lightOnDark; });
    m_shownWhen.emplace_back(m_flags.back().first, [](const FilterSettings &settings) { return drawsMarks(settings.dither.style); });
}
