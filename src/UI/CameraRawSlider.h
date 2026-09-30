#pragma once
#include <QColor>
#include <QSlider>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

// Swift's CameraRawSliderTrack: a coloured track, or the system's.
struct CameraRawSliderTrack {
    enum class Kind { plain, temperature, tint, chroma, hue, saturation, luminance, opposing, spectrum };
    Kind kind = Kind::plain;
    // A family's centre, for hue, saturation, luminance and spectrum.
    double degrees = 0;
    // Opposing's two ends, as Color Balance's Cyan / Red.
    QColor from = {};
    QColor to = {};
    // Left-to-right colours, evenly spaced; none keeps the system track.
    std::optional<std::vector<QColor>> colors() const;
};

// Swift's CameraRawSlider: a double click on the knob resets.
class CameraRawSlider : public QSlider {
    Q_OBJECT
public:
    CameraRawSlider(double low, double high, CameraRawSliderTrack track, const QString &help, std::function<void(double)> change,
                    std::function<void()> reset, QWidget *parent = nullptr);
    // Shows a value, unless a drag holds the knob.
    void display(double value);
    // Swift's updateNSView: new bounds and track, the value shown again.
    void reshape(double low, double high, CameraRawSliderTrack track, double value);
    double shown() const;
    const CameraRawSliderTrack &track() const { return m_track; }
    bool isOnKnob(QPoint point) const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    double m_low;
    double m_high;
    CameraRawSliderTrack m_track;
    const std::function<void(double)> m_change;
    const std::function<void()> m_reset;
};
