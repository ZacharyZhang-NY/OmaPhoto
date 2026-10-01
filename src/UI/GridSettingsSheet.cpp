#include "UI/GridSettingsSheet.h"
#include "UI/ColorPickerSheet+Dialog.h"
#include "UI/ColorPickerSheet.h"
#include "UI/KeyboardShortcuts.h"
#include "UI/NumericScrub.h"
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {
// A row's title, 110 wide, as Swift's frame.
QLabel *title(const QString &words, QWidget *parent)
{
    auto *label = new QLabel(words, parent);
    label->setFixedWidth(110);
    return label;
}

QHBoxLayout *row(QLabel *label)
{
    auto *layout = new QHBoxLayout;
    layout->setSpacing(8);
    layout->addWidget(label);
    return layout;
}

// Swift's fractionLength(0...2), grouped as Swift's formatted().
QString pixels(double value, const QLocale &locale)
{
    QString number = locale.toString(value, 'f', 2);
    while (number.endsWith(locale.zeroDigit()))
        number.chop(locale.zeroDigit().size());
    if (number.endsWith(locale.decimalPoint()))
        number.chop(locale.decimalPoint().size());
    return number;
}
}

GridSettingsSheet::GridSettingsSheet(EditorSession &session, LayoutGrid layout, GridAppearance appearance, std::function<void(const Settings &)> preview,
                                     std::function<void(std::optional<Settings>)> finish, QWidget *parent)
    : QWidget(parent), m_session(session), m_preview(std::move(preview)), m_finish(std::move(finish)), m_spacing(layout.spacing),
      m_subdivisions(layout.subdivisions), m_appearance(appearance), m_preset(new QComboBox(this)),
      m_swatch(new DialogColorSwatch(
          QStringLiteral("Grid Color"), [this] { return m_appearance.color(); }, [this](const PaletteColor &picked) { pick(picked); }, session, this)),
      m_style(new QComboBox(this)), m_opacitySlider(new QSlider(Qt::Horizontal, this)),
      m_opacity(field(QStringLiteral("gridOpacity"), [this](int value) { setOpacity(value); },
                      [this](int delta) { setOpacity(m_appearance.opacity + delta); })),
      m_spacingField(field(QStringLiteral("gridSpacing"), [this](int value) { setSpacing(value, m_subdivisions); }, nullptr)),
      m_subdivisionField(field(QStringLiteral("gridSubdivisions"), [this](int value) { setSpacing(m_spacing, value); }, nullptr)),
      m_note(new QLabel(this)), m_ok(new QPushButton(QStringLiteral("OK"), this))
{
    setFixedWidth(360);
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(24, 24, 24, 24);
    column->setSpacing(18);
    auto *heading = new QLabel(QStringLiteral("Grid"), this);
    QFont bold = heading->font();
    bold.setPixelSize(17);
    bold.setBold(true);
    heading->setFont(bold);
    column->addWidget(heading);

    QLabel *colorTitle = title(QStringLiteral("Color"), this);
    colorTitle->setBuddy(m_preset);
    m_preset->setObjectName(QStringLiteral("gridPreset"));
    for (const GridAppearance::Preset preset : allGridPresets)
        m_preset->addItem(rawValue(preset));
    connect(m_preset, &QComboBox::activated, this, [this](int index) {
        GridAppearance next = m_appearance;
        next.preset = allGridPresets[size_t(index)];
        setAppearance(next);
    });
    m_swatch->setObjectName(QStringLiteral("gridColor"));
    m_swatch->setToolTip(QStringLiteral("Choose a custom grid color"));
    QHBoxLayout *colorRow = row(colorTitle);
    colorRow->addWidget(m_preset);
    colorRow->addWidget(m_swatch);
    colorRow->addStretch(1);
    column->addLayout(colorRow);

    QLabel *styleTitle = title(QStringLiteral("Style"), this);
    styleTitle->setBuddy(m_style);
    m_style->setObjectName(QStringLiteral("gridStyle"));
    for (const GridAppearance::Style style : allGridStyles)
        m_style->addItem(rawValue(style));
    connect(m_style, &QComboBox::activated, this, [this](int index) {
        GridAppearance next = m_appearance;
        next.style = allGridStyles[size_t(index)];
        setAppearance(next);
    });
    QHBoxLayout *styleRow = row(styleTitle);
    styleRow->addWidget(m_style);
    styleRow->addStretch(1);
    column->addLayout(styleRow);

    QLabel *opacityTitle = title(QStringLiteral("Opacity"), this);
    opacityTitle->setBuddy(m_opacitySlider);
    new NumericScrub(opacityTitle, {.sensitivity = 0.5, .low = GridAppearance::opacityLow, .high = GridAppearance::opacityHigh, .step = std::nullopt,
                                    .value = [this] { return double(m_appearance.opacity); },
                                    .set = [this](double value) {
                                        m_opacity->setModified(false);
                                        setOpacity(int(std::lround(value)));
                                    }});
    m_opacitySlider->setObjectName(QStringLiteral("gridOpacitySlider"));
    m_opacitySlider->setRange(GridAppearance::opacityLow, GridAppearance::opacityHigh);
    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int value) {
        m_opacity->setModified(false);
        setOpacity(value);
    });
    m_opacity->setFixedWidth(48);
    // Swift's TextField titles: their names and placeholders.
    for (const auto &[entry, words] : {std::pair(m_opacity, QStringLiteral("Opacity")), std::pair(m_spacingField, QStringLiteral("Gridline every")),
                                       std::pair(m_subdivisionField, QStringLiteral("Subdivisions"))}) {
        entry->setAccessibleName(words);
        entry->setPlaceholderText(words);
    }
    QHBoxLayout *opacityRow = row(opacityTitle);
    opacityRow->addWidget(m_opacitySlider, 1);
    // Swift's unitSuffix: the unit two points after.
    auto *suffixed = new QHBoxLayout;
    suffixed->setSpacing(2);
    suffixed->addWidget(m_opacity);
    suffixed->addWidget(new QLabel(QStringLiteral("%"), this));
    opacityRow->addLayout(suffixed);
    column->addLayout(opacityRow);

    auto *divider = new QFrame(this);
    divider->setFrameShape(QFrame::HLine);
    divider->setForegroundRole(QPalette::Mid);
    column->addWidget(divider);

    QLabel *spacingTitle = title(QStringLiteral("Gridline every"), this);
    spacingTitle->setBuddy(m_spacingField);
    new NumericScrub(spacingTitle, {.sensitivity = 1, .low = LayoutGrid::spacingLow, .high = LayoutGrid::spacingHigh, .step = std::nullopt,
                                    .value = [this] { return double(m_spacing); },
                                    .set = [this](double value) {
                                        m_spacingField->setModified(false);
                                        setSpacing(int(std::lround(value)), m_subdivisions);
                                    }});
    QHBoxLayout *spacingRow = row(spacingTitle);
    spacingRow->addWidget(m_spacingField, 1);
    auto *unit = new QLabel(QStringLiteral("pixels"), this);
    unit->setForegroundRole(QPalette::PlaceholderText);
    spacingRow->addWidget(unit);
    column->addLayout(spacingRow);

    QLabel *subdivisionTitle = title(QStringLiteral("Subdivisions"), this);
    subdivisionTitle->setBuddy(m_subdivisionField);
    new NumericScrub(subdivisionTitle, {.sensitivity = 0.2, .low = LayoutGrid::subdivisionLow, .high = LayoutGrid::subdivisionHigh, .step = std::nullopt,
                                        .value = [this] { return double(m_subdivisions); },
                                        .set = [this](double value) {
                                            m_subdivisionField->setModified(false);
                                            setSpacing(m_spacing, int(std::lround(value)));
                                        }});
    QHBoxLayout *subdivisionRow = row(subdivisionTitle);
    subdivisionRow->addWidget(m_subdivisionField, 1);
    column->addLayout(subdivisionRow);

    m_note->setObjectName(QStringLiteral("gridNote"));
    m_note->setWordWrap(true);
    QFont callout = m_note->font();
    callout.setPixelSize(12);
    m_note->setFont(callout);
    column->addWidget(m_note);

    auto *cancel = new QPushButton(QStringLiteral("Cancel"), this);
    cancel->setObjectName(QStringLiteral("gridCancel"));
    cancel->setAutoDefault(false);
    connect(cancel, &QPushButton::clicked, this, [this] {
        DialogColorSwatch::closePicker(m_session);
        m_finish(std::nullopt);
    });
    auto *defaults = new QPushButton(QStringLiteral("Restore Defaults"), this);
    defaults->setObjectName(QStringLiteral("gridDefaults"));
    defaults->setAutoDefault(false);
    // Custom's colour stays for when Custom returns.
    connect(defaults, &QPushButton::clicked, this, [this] {
        m_spacing = LayoutGrid().spacing;
        m_subdivisions = LayoutGrid().subdivisions;
        m_pickedFrom.reset();
        GridAppearance next = m_appearance;
        next.preset = GridAppearance().preset;
        next.style = GridAppearance().style;
        next.opacity = GridAppearance().opacity;
        m_appearance = next;
        m_preview({grid(), m_appearance});
        synchronize();
    });
    m_ok->setObjectName(QStringLiteral("gridOK"));
    m_ok->setDefault(true);
    // OK rests while the grid is invalid: no guard needed.
    connect(m_ok, &QPushButton::clicked, this, [this] {
        DialogColorSwatch::closePicker(m_session);
        m_finish(Settings{grid(), m_appearance});
    });
    NativeShortcut::bind(*this, m_ok, cancel);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(cancel);
    buttons->addWidget(defaults);
    buttons->addStretch(1);
    buttons->addWidget(m_ok);
    column->addLayout(buttons);
    synchronize();
}

GridSettingsSheet::~GridSettingsSheet()
{
    releaseFocus(*this);
    DialogColorSwatch::closePicker(m_session);
}

bool GridSettingsSheet::valid() const
{
    return m_spacing >= LayoutGrid::spacingLow && m_spacing <= LayoutGrid::spacingHigh && m_subdivisions >= LayoutGrid::subdivisionLow
        && m_subdivisions <= LayoutGrid::subdivisionHigh && m_subdivisions <= m_spacing;
}

void GridSettingsSheet::setAppearance(const GridAppearance &appearance)
{
    if (appearance == m_appearance)
        return;
    if (appearance.preset != GridAppearance::Preset::custom)
        m_pickedFrom.reset();
    m_appearance = appearance;
    m_preview({grid(), m_appearance});
    synchronize();
}

// Swift's TextField binding: any whole number; OK rests until valid.
void GridSettingsSheet::setSpacing(int spacing, int subdivisions)
{
    m_spacing = spacing;
    m_subdivisions = subdivisions;
    if (valid())
        m_preview({grid(), m_appearance});
    synchronize();
}

void GridSettingsSheet::setOpacity(int opacity)
{
    GridAppearance next = m_appearance;
    next.opacity = std::clamp(opacity, GridAppearance::opacityLow, GridAppearance::opacityHigh);
    setAppearance(next);
}

void GridSettingsSheet::pick(const PaletteColor &picked)
{
    // Both sides in 8 bits: the picker's opening and Cancel.
    if (picked.quantized() == m_appearance.color().quantized())
        return;
    GridAppearance next = m_appearance;
    if (m_pickedFrom && presetColor(*m_pickedFrom).value().quantized() == picked.quantized()) {
        // A preset again: setAppearance ends the pick.
        next.preset = *m_pickedFrom;
        setAppearance(next);
        return;
    }
    if (next.preset != GridAppearance::Preset::custom)
        m_pickedFrom = next.preset;
    next.customColor = picked;
    next.preset = GridAppearance::Preset::custom;
    setAppearance(next);
}

PickerField *GridSettingsSheet::field(const QString &name, std::function<void(int)> apply, std::function<void(int)> step)
{
    // A readable whole number applies; other text goes back.
    auto made = std::make_shared<PickerField *>(nullptr);
    auto *entry = new PickerField(
        [this, made, apply = std::move(apply)] {
            PickerField *const typed = *made;
            bool number = false;
            const int value = typed->locale().toInt(typed->text(), &number);
            if (typed->isModified() && number)
                apply(value);
            typed->setModified(false);
            synchronize();
        },
        std::move(step), this);
    *made = entry;
    entry->setObjectName(name);
    entry->setAlignment(Qt::AlignRight);
    return entry;
}

void GridSettingsSheet::synchronize()
{
    const auto show = [](PickerField *entry, int value) {
        // A field being typed in keeps its typing.
        if (!entry->hasFocus() || !entry->isModified())
            entry->setText(QString::number(value));
    };
    {
        const QSignalBlocker quiet(m_opacitySlider);
        m_opacitySlider->setValue(m_appearance.opacity);
    }
    show(m_opacity, m_appearance.opacity);
    show(m_spacingField, m_spacing);
    show(m_subdivisionField, m_subdivisions);
    m_preset->setCurrentIndex(int(std::find(allGridPresets.begin(), allGridPresets.end(), m_appearance.preset) - allGridPresets.begin()));
    m_style->setCurrentIndex(int(std::find(allGridStyles.begin(), allGridStyles.end(), m_appearance.style) - allGridStyles.begin()));
    m_swatch->update();
    const bool ok = valid();
    m_note->setText(ok ? QStringLiteral("A subdivision every %1 pixels.").arg(pixels(grid().step(), locale()))
                       : QStringLiteral("Use gridlines every %1–%2 pixels and %3–%4 subdivisions, no more than the pixels between gridlines.")
                             .arg(LayoutGrid::spacingLow)
                             .arg(locale().toString(LayoutGrid::spacingHigh))
                             .arg(LayoutGrid::subdivisionLow)
                             .arg(LayoutGrid::subdivisionHigh));
    m_note->setForegroundRole(ok ? QPalette::PlaceholderText : QPalette::BrightText);
    m_ok->setEnabled(ok);
}
