#pragma once
#include "Document/EditorSession.h"
#include <QWidget>

class CurvesControls;
class PickerField;
class QButtonGroup;
class QCheckBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSlider;
class QVBoxLayout;
class SwatchButton;

// Swift's GradientMapControls: the gradient, both ends, Reverse.
class GradientMapControls : public QWidget {
    Q_OBJECT
public:
    // `pick` opens the picker: false for Shadows, true for Highlights.
    GradientMapControls(std::function<GradientMapSettings()> value, std::function<void(const GradientMapSettings &)> change,
                        std::function<void(bool)> pick, QWidget *parent = nullptr);
    // Shows the settings as they now stand.
    void synchronize();

private:
    QWidget *swatch(const QString &title, bool highlights, const std::function<void(bool)> &pick);

    const std::function<GradientMapSettings()> m_value;
    const std::function<void(const GradientMapSettings &)> m_change;
    QWidget *const m_gradient;
    QCheckBox *const m_reverse;
    std::vector<SwatchButton *> m_swatches;
};

// Swift's FilterSheet: the kind's settings, Preview, Cancel and OK.
class FilterSheet : public QWidget {
    Q_OBJECT
public:
    explicit FilterSheet(EditorSession &session, QWidget *parent = nullptr);

private:
    // Swift's control: a slider, an exact field and its unit.
    struct Control {
        std::function<double &(FilterSettings &)> key;
        double low;
        double high;
        int decimals;
        bool logarithmic;
        QSlider *slider;
        PickerField *field;
    };
    void control(const QString &title, std::function<double &(FilterSettings &)> key, double low, double high, const QString &unit,
                 int decimals, bool logarithmic);
    void noise();
    // Remove Background's words, Quality, and Advanced's three rows.
    void background();
    // Swift's `settings` and `update`: the defaults once the edit goes.
    FilterSettings settings() const;
    void update(const std::function<void(FilterSettings &)> &change);
    void synchronize();

    EditorSession &m_session;
    QVBoxLayout *const m_column;
    std::vector<Control> m_controls;
    CurvesControls *m_curves = nullptr;
    GradientMapControls *m_gradientMap = nullptr;
    QButtonGroup *m_distribution = nullptr;
    QCheckBox *m_monochromatic = nullptr;
    QButtonGroup *m_quality = nullptr;
    // Shown while the quality is Advanced, as Swift's `if`.
    std::vector<QWidget *> m_advanced;
    QCheckBox *const m_preview;
    QLabel *const m_error;
    QLabel *const m_limited;
    QProgressBar *const m_spinner;
    QLabel *const m_activity;
    QPushButton *const m_cancel;
    QPushButton *const m_ok;
    // The picker's colour as last seen, Swift's onChange.
    std::optional<PaletteColor> m_pickerColour;
};
