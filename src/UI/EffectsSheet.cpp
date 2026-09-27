#include "UI/EffectsSheet.h"
#include "UI/KeyboardShortcuts.h"
#include "UI/ColorPaletteControls.h"
#include "UI/ColorPickerSheet.h"
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

namespace {
// A thousand steps across each slider's range.
constexpr int travel = 1000;

// Swift's .number with no decimals, ties to even, never grouped.
QString shown(double value, QLocale locale)
{
    locale.setNumberOptions(QLocale::OmitGroupSeparator);
    return locale.toString(std::nearbyint(value), 'f', 0);
}

QLabel *words(const QString &text, bool headline, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    if (headline) {
        QFont font = label->font();
        font.setPixelSize(13);
        font.setWeight(QFont::DemiBold);
        label->setFont(font);
    }
    return label;
}

std::optional<double> percent(const auto &effect)
{
    return effect ? std::optional(effect->opacity * 100) : std::nullopt;
}
}

EffectsSheet::EffectsSheet(EditorSession &session, LayerEffectKind kind, QWidget *parent)
    : QWidget(parent), m_session(session), m_kind(kind), m_column(new QVBoxLayout(this))
{
    setObjectName(QStringLiteral("effectsSheet"));
    setFixedWidth(340);
    m_column->setContentsMargins(20, 20, 20, 20);
    m_column->setSpacing(16);
    switch (kind) {
    case LayerEffectKind::stroke:
        header(QStringLiteral("Stroke"), true);
        colour(true);
        slider(QStringLiteral("Size"), [](const LayerEffects &e) { return e.stroke ? std::optional(e.stroke->size) : std::nullopt; },
               [](LayerEffects &e, double value) { e.stroke->size = value; }, 0, 20, QStringLiteral("px"), StrokeEffect::maxSize);
        slider(QStringLiteral("Opacity"), [](const LayerEffects &e) { return percent(e.stroke); },
               [](LayerEffects &e, double value) { e.stroke->opacity = value / 100; }, 0, 100, QStringLiteral("%"));
        break;
    case LayerEffectKind::shadow:
        header(QStringLiteral("Drop Shadow"), false);
        slider(QStringLiteral("Opacity"), [](const LayerEffects &e) { return percent(e.shadow); },
               [](LayerEffects &e, double value) { e.shadow->opacity = value / 100; }, 0, 100, QStringLiteral("%"));
        slider(QStringLiteral("Angle"), [](const LayerEffects &e) { return e.shadow ? std::optional(e.shadow->angle) : std::nullopt; },
               [](LayerEffects &e, double value) { e.shadow->angle = value; }, -180, 180, QStringLiteral("°"));
        slider(QStringLiteral("Distance"), [](const LayerEffects &e) { return e.shadow ? std::optional(e.shadow->distance) : std::nullopt; },
               [](LayerEffects &e, double value) { e.shadow->distance = value; }, 0, 100, QStringLiteral("px"), 5000);
        slider(QStringLiteral("Blur"), [](const LayerEffects &e) { return e.shadow ? std::optional(e.shadow->blur) : std::nullopt; },
               [](LayerEffects &e, double value) { e.shadow->blur = value; }, 0, 100, QStringLiteral("px"), 500);
        break;
    case LayerEffectKind::colorOverlay:
        header(QStringLiteral("Color Overlay"), false);
        slider(QStringLiteral("Opacity"), [](const LayerEffects &e) { return percent(e.colorOverlay); },
               [](LayerEffects &e, double value) { e.colorOverlay->opacity = value / 100; }, 0, 100, QStringLiteral("%"));
        break;
    case LayerEffectKind::innerShadow:
        header(QStringLiteral("Inner Shadow"), false);
        slider(QStringLiteral("Opacity"), [](const LayerEffects &e) { return percent(e.innerShadow); },
               [](LayerEffects &e, double value) { e.innerShadow->opacity = value / 100; }, 0, 100, QStringLiteral("%"));
        slider(QStringLiteral("Angle"), [](const LayerEffects &e) { return e.innerShadow ? std::optional(e.innerShadow->angle) : std::nullopt; },
               [](LayerEffects &e, double value) { e.innerShadow->angle = value; }, -180, 180, QStringLiteral("°"));
        slider(QStringLiteral("Distance"), [](const LayerEffects &e) { return e.innerShadow ? std::optional(e.innerShadow->distance) : std::nullopt; },
               [](LayerEffects &e, double value) { e.innerShadow->distance = value; }, 0, 50, QStringLiteral("px"), 5000);
        slider(QStringLiteral("Blur"), [](const LayerEffects &e) { return e.innerShadow ? std::optional(e.innerShadow->blur) : std::nullopt; },
               [](LayerEffects &e, double value) { e.innerShadow->blur = value; }, 0, 100, QStringLiteral("px"), 500);
        break;
    case LayerEffectKind::outerGlow:
        header(QStringLiteral("Outer Glow"), false);
        slider(QStringLiteral("Size"), [](const LayerEffects &e) { return e.outerGlow ? std::optional(e.outerGlow->size) : std::nullopt; },
               [](LayerEffects &e, double value) { e.outerGlow->size = value; }, 0, 100, QStringLiteral("px"), 500);
        slider(QStringLiteral("Opacity"), [](const LayerEffects &e) { return percent(e.outerGlow); },
               [](LayerEffects &e, double value) { e.outerGlow->opacity = value / 100; }, 0, 100, QStringLiteral("%"));
        break;
    }
    auto *cancel = new QPushButton(QStringLiteral("Cancel"), this);
    cancel->setObjectName(QStringLiteral("effectsCancel"));
    cancel->setAutoDefault(false);
    auto *ok = new QPushButton(QStringLiteral("OK"), this);
    ok->setObjectName(QStringLiteral("effectsOK"));
    ok->setDefault(true);
    // Swift's configuredNativeShortcut: Return and Escape, as remapped.
    NativeShortcut::bind(*this, ok, cancel);
    connect(cancel, &QPushButton::clicked, this, [this] { m_session.finishEffectsEditing(false); });
    connect(ok, &QPushButton::clicked, this, [this] { m_session.finishEffectsEditing(true); });
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(ok);
    m_column->addLayout(buttons);
    connect(&m_session, &EditorSession::changed, this, &EffectsSheet::synchronize);
    synchronize();
}

// The kind's headline, then the stroke's position or the swatch.
void EffectsSheet::header(const QString &title, bool position)
{
    auto *row = new QHBoxLayout;
    row->addWidget(words(title, true, this));
    row->addStretch(1);
    if (!position) {
        colour(false);
        row->addWidget(m_controls.back());
    } else {
        auto *choice = new QWidget(this);
        choice->setObjectName(QStringLiteral("strokePosition"));
        choice->setAccessibleName(QStringLiteral("Position"));
        auto *buttons = new QHBoxLayout(choice);
        buttons->setContentsMargins(0, 0, 0, 0);
        buttons->setSpacing(0);
        m_position = new QButtonGroup(choice);
        for (const int inside : {0, 1}) {
            auto *button = new QToolButton(choice);
            button->setText(inside ? QStringLiteral("Inside") : QStringLiteral("Outside"));
            button->setCheckable(true);
            m_position->addButton(button, inside);
            buttons->addWidget(button);
        }
        connect(m_position, &QButtonGroup::idClicked, this, [this](int inside) {
            m_session.changeEffects([inside](LayerEffects &effects) { effects.stroke->inside = inside == 1; });
        });
        row->addWidget(choice);
        m_controls.push_back(choice);
    }
    m_column->addLayout(row);
}

// The effect's colour, opened in the app's own picker.
void EffectsSheet::colour(bool labelled)
{
    auto *swatch = new SwatchButton([this] { return m_session.editingEffects().color(m_kind).value_or(PaletteColor::black()); }, 3, 1, 1, this);
    swatch->setObjectName(QStringLiteral("effectColor"));
    swatch->setFixedSize(36, 18);
    swatch->setToolTip(rawValue(m_kind) + QStringLiteral(" color"));
    swatch->setAccessibleName(rawValue(m_kind) + QStringLiteral(" color"));
    connect(swatch, &QAbstractButton::clicked, this, [this] { m_session.openEffectColorPicker(m_kind); });
    if (!labelled) {
        m_controls.push_back(swatch);
        return;
    }
    auto *row = new QWidget(this);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *label = words(QStringLiteral("Color"), false, row);
    label->setFixedWidth(64);
    layout->addWidget(label);
    layout->addWidget(swatch);
    layout->addStretch(1);
    m_column->addWidget(row);
    m_controls.push_back(row);
}

void EffectsSheet::slider(const QString &title, std::function<std::optional<double>(const LayerEffects &)> value,
                          std::function<void(LayerEffects &, double)> change, double low, double high, const QString &unit,
                          std::optional<double> typedHigh)
{
    const size_t index = m_sliders.size();
    const QString name = title.toLower();
    auto *row = new QWidget(this);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    auto *label = words(title, false, row);
    label->setFixedWidth(64);
    auto *slider = new QSlider(Qt::Horizontal, row);
    slider->setObjectName(name + QStringLiteral("Slider"));
    slider->setRange(0, travel);
    slider->setFixedWidth(130);
    label->setBuddy(slider);
    connect(slider, &QSlider::valueChanged, this, [this, index](int position) {
        const Slider &at = m_sliders[index];
        apply(at, at.low + (at.high - at.low) * position / travel);
        // Its value replaces the field's typing, as a step does.
        at.field->setModified(false);
        synchronize();
    });
    // A readable number applies on Return or leaving; else reverts.
    auto *field = new PickerField([this, index] {
        PickerField &edited = *m_sliders[index].field;
        bool number = false;
        const double typed = edited.locale().toDouble(edited.text(), &number);
        if (edited.isModified() && number && std::isfinite(typed))
            apply(m_sliders[index], typed);
        edited.setModified(false);
        synchronize();
    }, [this, index](int step) {
        const Slider &at = m_sliders[index];
        if (const std::optional<double> current = at.value(m_session.editingEffects()))
            apply(at, *current + step);
        // The step replaces any typing, clamped or not.
        at.field->setModified(false);
        synchronize();
    }, row);
    field->setObjectName(name + QStringLiteral("Field"));
    field->setAccessibleName(title);
    field->setPlaceholderText(title);
    field->setAlignment(Qt::AlignRight);
    field->setFixedWidth(48);
    auto *suffixed = new QHBoxLayout;
    suffixed->setSpacing(2);
    suffixed->addWidget(field);
    suffixed->addWidget(new QLabel(unit, row));
    layout->addWidget(label);
    layout->addWidget(slider);
    layout->addLayout(suffixed);
    m_column->addWidget(row);
    m_controls.push_back(row);
    m_sliders.push_back(Slider{std::move(value), std::move(change), low, high, typedHigh.value_or(high), slider, field});
}

void EffectsSheet::apply(const Slider &control, double value)
{
    const double clamped = std::clamp(value, control.low, control.typedHigh);
    m_session.changeEffects([&control, clamped](LayerEffects &effects) { control.change(effects, clamped); });
}

void EffectsSheet::synchronize()
{
    // Swift's onChange: the open picker previews on the layer.
    const std::optional<PaletteColor> picked = m_session.colorPicker() ? std::optional(m_session.colorPicker()->color()) : std::nullopt;
    if (picked != m_pickerColour) {
        m_pickerColour = picked;
        m_session.previewEffectColor();
    }
    const LayerEffects effects = m_session.editingEffects();
    for (QWidget *control : m_controls) {
        control->setVisible(effects.contains(m_kind));
        control->update();
    }
    if (m_position && effects.stroke)
        m_position->button(effects.stroke->inside ? 1 : 0)->setChecked(true);
    for (const Slider &control : m_sliders) {
        const std::optional<double> value = control.value(effects);
        if (!value)
            continue;
        {
            // QSlider pins a larger typed value at its end.
            const QSignalBlocker quiet(control.slider);
            control.slider->setValue(int(std::lround((*value - control.low) / (control.high - control.low) * travel)));
        }
        // A field being typed in keeps its typing.
        const QString text = shown(*value, control.field->locale());
        if (!control.field->isModified() && control.field->text() != text)
            control.field->setText(text);
    }
}
