#pragma once
#include "UI/CameraRawSlider.h"
#include <QWidget>
#include <functional>
#include <optional>

class PickerField;
class QLabel;

// Swift's Camera Raw rows: title, slider, exact field.
class CameraRawRow : public QWidget {
    Q_OBJECT
public:
    struct Spec {
        QString name;
        QString title;
        QString help;
        double low = -100;
        double high = 100;
        int decimals = 0;
        CameraRawSliderTrack track = {};
        // The title's width: at least this, or exactly when fixed.
        int titleWidth = 96;
        bool fixedTitle = false;
        // No field at zero.
        int fieldWidth = 56;
        // A double click on the title resets it.
        bool titleResets = false;
        // Swift's scrubbable title: a point's worth, through `type`.
        std::optional<double> scrub = std::nullopt;
    };
    // `slide` takes the slider's own value, `type` a typed number.
    CameraRawRow(Spec spec, std::function<double()> value, std::function<void(double)> slide, std::function<void(double)> type,
                 std::function<void()> reset, QWidget *parent = nullptr);
    // Shows the value; a focused field keeps its typing.
    void synchronize();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    const Spec m_spec;
    const std::function<double()> m_value;
    const std::function<void()> m_reset;
    QLabel *const m_title;
    CameraRawSlider *const m_slider;
    PickerField *m_field = nullptr;
};
