#pragma once
#include <QObject>
#include <QPointF>
#include <functional>
#include <optional>

class QWidget;

// Swift's NumericScrub: a label dragged sideways changes its value.
class NumericScrub : public QObject {
public:
    struct Options {
        double sensitivity;
        double low;
        double high;
        // Dragged values snap to multiples of this, when set.
        std::optional<double> step;
        std::function<double()> value;
        std::function<void(double)> set;
        std::function<void()> onStart = [] {};
        std::function<void()> onEnd = [] {};
    };

    // Lives as the label's child and filters its events.
    NumericScrub(QWidget *label, Options options);
    // Ends a drag as a release would.
    void end();
    // New limits, as SwiftUI's next render hands the modifier.
    void reshape(double sensitivity, double low, double high);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *const m_label;
    Options m_options;
    // The press, until the release; Swift's gesture begins there.
    std::optional<QPointF> m_press;
    // Swift's startValue: set once the drag moves a point.
    std::optional<double> m_start;
};
