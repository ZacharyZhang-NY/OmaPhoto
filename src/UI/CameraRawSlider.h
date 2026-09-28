#pragma once
#include <QColor>
#include <QSlider>
#include <functional>
#include <optional>
#include <utility>

// Swift's CameraRawSliderTrack: a coloured track, or the system's.
struct CameraRawSliderTrack {
    enum class Kind { plain, temperature, tint, chroma, hue, saturation, luminance };
    Kind kind = Kind::plain;
    // A family's centre, for hue, saturation and luminance.
    double degrees = 0;
    // Left and right colours; none keeps the system track.
    std::optional<std::pair<QColor, QColor>> colors() const;
};

// Swift's CameraRawSlider: a double click on the knob resets.
class CameraRawSlider : public QSlider {
    Q_OBJECT
public:
    CameraRawSlider(double low, double high, CameraRawSliderTrack track, const QString &help, std::function<void(double)> change,
                    std::function<void()> reset, QWidget *parent = nullptr);
    // Shows a value, unless a drag holds the knob.
    void display(double value);
    double shown() const;
    bool isOnKnob(QPoint point) const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    const double m_low;
    const double m_high;
    const CameraRawSliderTrack m_track;
    const std::function<void(double)> m_change;
    const std::function<void()> m_reset;
};
