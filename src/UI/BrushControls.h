#pragma once
#include "UI/ToolHeaderStyle.h"
#include <functional>

class EditorSession;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;
class QToolButton;
class PickerField;
class SelectionAmountField;
class SwatchButton;

// Swift's BrushControls: mode, type or source; tip, opacity, mask.
class BrushControls : public ToolHeaderBar {
    Q_OBJECT
public:
    explicit BrushControls(EditorSession &session, QWidget *parent = nullptr);

private:
    // A segment of Swift's segmented picker: one checkable button.
    QToolButton *choice(QButtonGroup *group, const QString &name, const QString &text, const QString &tip, const std::function<void()> &pick);
    QSlider *slider(const QString &name, int low, int high, const std::function<void(int)> &change);
    void synchronize();

    EditorSession &m_session;
    QButtonGroup *const m_modes;
    QToolButton *const m_paint;
    QToolButton *const m_erase;
    // The Smear's mode, Swift's second segmented picker.
    QButtonGroup *const m_blurModes;
    // Spot Healing's type, Swift's `spotHealingType` picker.
    QButtonGroup *const m_healingTypes;
    // Clone Stamp's options: Aligned, and what it samples.
    QCheckBox *const m_aligned;
    QButtonGroup *const m_samples;
    QToolButton *const m_thisLayer;
    QToolButton *const m_allLayers;
    SelectionAmountField *const m_size;
    // Sliders step in thousandths, fields show whole percents.
    QSlider *const m_hardnessSlider;
    SelectionAmountField *const m_hardness;
    QLabel *const m_opacityLabel;
    QSlider *const m_opacitySlider;
    SelectionAmountField *const m_opacity;
    // Blur's own Radius, apart from Strength.
    QLabel *const m_radiusLabel;
    QSlider *const m_radiusSlider;
    PickerField *const m_radius;
    QLabel *const m_radiusUnit;
    // Paint and Erase only; other tools have their own feel.
    QLabel *const m_smoothingLabel;
    QSlider *const m_smoothingSlider;
    SelectionAmountField *const m_smoothing;
    QLabel *const m_paintLabel;
    QComboBox *const m_maskPaint;
    // The foreground colour, the rail swatch's own picker.
    QLabel *const m_colourLabel;
    SwatchButton *const m_colour;
    QLabel *const m_cloneNote;
    QLabel *const m_mask;
};
