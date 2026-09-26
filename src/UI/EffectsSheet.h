#pragma once
#include "Document/EditorSession.h"
#include <QWidget>
#include <functional>
#include <vector>

class PickerField;
class QButtonGroup;
class QSlider;
class QVBoxLayout;

// Swift's EffectsSheet: one effect's controls, bound to its panel's layer.
class EffectsSheet : public QWidget {
    Q_OBJECT
public:
    EffectsSheet(EditorSession &session, LayerEffectKind kind, QWidget *parent = nullptr);

private:
    // Swift's slider: a title, a slider, a whole-number field.
    struct Slider {
        std::function<std::optional<double>(const LayerEffects &)> value;
        std::function<void(LayerEffects &, double)> change;
        double low;
        double high;
        QSlider *slider;
        PickerField *field;
    };
    void header(const QString &title, bool position);
    void colour(bool labelled);
    void slider(const QString &title, std::function<std::optional<double>(const LayerEffects &)> value,
                std::function<void(LayerEffects &, double)> change, double low, double high, const QString &unit);
    // A value clamped to the slider's range, as Swift's binding.
    void apply(const Slider &control, double value);
    void synchronize();

    EditorSession &m_session;
    const LayerEffectKind m_kind;
    QVBoxLayout *const m_column;
    // What shows only while the effect is on its layer.
    std::vector<QWidget *> m_controls;
    std::vector<Slider> m_sliders;
    QButtonGroup *m_position = nullptr;
    // The picker's colour as last seen, Swift's onChange.
    std::optional<PaletteColor> m_pickerColour;
};
