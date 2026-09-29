#pragma once
#include "Document/EditorSession.h"
#include <QWidget>
#include <array>
#include <functional>
#include <optional>

class NumericScrub;
class PickerField;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QToolButton;

// Swift's SpectrumEditor: the hues, the band's handles, the hues after.
class SpectrumEditor : public QWidget {
    Q_OBJECT
public:
    // Swift's binding: the settings read, and written back.
    SpectrumEditor(std::function<HueSaturationSettings()> value, std::function<void(const HueSaturationSettings &)> change, QWidget *parent = nullptr);
    // Shows the settings as they now stand.
    void synchronize();

private:
    // Swift's drag: the held handle, else the nearest.
    void drag(double x, double width, bool pressed);

    const std::function<HueSaturationSettings()> m_value;
    const std::function<void(const HueSaturationSettings &)> m_change;
    std::optional<int> m_dragging;
    QWidget *const m_handles;
    QWidget *const m_after;
    QLabel *const m_readout;
};

// Swift's HueSaturationSheet: range, samples, sliders, spectrum, toggles.
class HueSaturationSheet : public QWidget {
    Q_OBJECT
public:
    explicit HueSaturationSheet(EditorSession &session, QWidget *parent = nullptr);

private:
    // Swift's settings binding: changed, then handed to the session.
    void change(const std::function<void(HueSaturationSettings &)> &edit);
    // Swift's slider row: title, slider, field and unit.
    QWidget *row(size_t index);
    void synchronize();

    EditorSession &m_session;
    QComboBox *const m_range;
    const std::array<QToolButton *, 3> m_samples;
    QWidget *const m_divider;
    QToolButton *const m_targeting;
    // Hue, Saturation, Lightness; the rows make them.
    std::array<QSlider *, 3> m_sliders{};
    std::array<PickerField *, 3> m_fields{};
    // Each title's scrub; Colorize changes its range.
    std::array<NumericScrub *, 3> m_scrubs{};
    const std::array<QWidget *, 3> m_rows;
    SpectrumEditor *const m_spectrum;
    QCheckBox *const m_invert;
    QCheckBox *const m_colorize;
    QCheckBox *const m_preview;
    QPushButton *const m_reset;
    QLabel *const m_limited;
    QPushButton *const m_cancel;
    QPushButton *const m_ok;
};
