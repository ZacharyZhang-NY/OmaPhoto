#include "UI/CameraRawSlider.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionSlider>
#include <cmath>

namespace {
// Thousandths of the range, as the filter sheet's sliders.
constexpr int travel = 1000;

QColor hsb(double degrees, double saturation, double brightness)
{
    double turns = degrees / 360;
    turns -= std::floor(turns);
    return QColor::fromHsvF(float(turns), float(saturation), float(brightness));
}
}

std::optional<std::pair<QColor, QColor>> CameraRawSliderTrack::colors() const
{
    switch (kind) {
    case Kind::plain: return std::nullopt;
    case Kind::temperature: return std::pair(QColor::fromRgbF(0.22f, 0.46f, 0.95f), QColor::fromRgbF(0.98f, 0.82f, 0.18f));
    case Kind::tint: return std::pair(QColor::fromRgbF(0.28f, 0.70f, 0.34f), QColor::fromRgbF(0.70f, 0.40f, 0.64f));
    case Kind::chroma: return std::pair(QColor::fromRgbF(0.62f, 0.62f, 0.64f), QColor::fromRgbF(0.86f, 0.18f, 0.20f));
    case Kind::hue: return std::pair(hsb(degrees - 50, 0.85, 0.9), hsb(degrees + 50, 0.85, 0.9));
    case Kind::saturation: return std::pair(QColor::fromRgbF(0.55f, 0.55f, 0.56f), hsb(degrees, 0.9, 0.9));
    case Kind::luminance: return std::pair(hsb(degrees, 0.55, 0.18), hsb(degrees, 0.35, 0.95));
    }
    throw std::logic_error("unknown slider track");
}

CameraRawSlider::CameraRawSlider(double low, double high, CameraRawSliderTrack track, const QString &help, std::function<void(double)> change,
                                 std::function<void()> reset, QWidget *parent)
    : QSlider(Qt::Horizontal, parent), m_low(low), m_high(high), m_track(track), m_change(std::move(change)), m_reset(std::move(reset))
{
    setRange(0, travel);
    setMinimumWidth(60);
    setFixedHeight(22);
    setToolTip(help);
    setAccessibleName(help);
    connect(this, &QSlider::valueChanged, this, [this] { m_change(shown()); });
}

void CameraRawSlider::display(double value)
{
    // Swift's isTrackingValue: a drag keeps its own knob.
    if (isSliderDown())
        return;
    const QSignalBlocker quiet(this);
    setValue(int(std::lround((value - m_low) / (m_high - m_low) * travel)));
}

double CameraRawSlider::shown() const
{
    return m_low + (m_high - m_low) * value() / travel;
}

bool CameraRawSlider::isOnKnob(QPoint point) const
{
    QStyleOptionSlider option;
    initStyleOption(&option);
    return style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this).adjusted(-2, -2, 2, 2).contains(point);
}

// Swift's GradientSliderCell: the whole track shows the colours.
void CameraRawSlider::paintEvent(QPaintEvent *event)
{
    const std::optional<std::pair<QColor, QColor>> colors = m_track.colors();
    if (!colors) {
        QSlider::paintEvent(event);
        return;
    }
    QStyleOptionSlider option;
    initStyleOption(&option);
    const QRect groove = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, this);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const double height = 4;
    const QRectF bar(groove.left(), groove.center().y() + 0.5 - height / 2, groove.width(), height);
    QLinearGradient gradient(bar.topLeft(), bar.topRight());
    gradient.setColorAt(0, colors->first);
    gradient.setColorAt(1, colors->second);
    QPainterPath shape;
    shape.addRoundedRect(bar, height / 2, height / 2);
    painter.fillPath(shape, gradient);
    option.subControls = QStyle::SC_SliderHandle;
    style()->drawComplexControl(QStyle::CC_Slider, &option, &painter, this);
}

void CameraRawSlider::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && isOnKnob(event->position().toPoint())) {
        m_reset();
        return;
    }
    QSlider::mouseDoubleClickEvent(event);
}
