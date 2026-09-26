#include "UI/SliderSnap.h"
#include <QApplication>
#include <QMouseEvent>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionSlider>
#include <algorithm>

namespace {
// Swift's snapValue: the value under a press on the track.
class Snap : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        auto *slider = qobject_cast<QSlider *>(watched);
        if (!slider || event->type() != QEvent::MouseButtonPress)
            return false;
        const auto *press = static_cast<QMouseEvent *>(event);
        // Qt refuses a press made with another button held.
        if (press->buttons() != Qt::LeftButton || slider->orientation() != Qt::Horizontal || !slider->isEnabled()
            || slider->maximum() <= slider->minimum())
            return false;
        // QSlider's own style option, which it keeps protected.
        QStyleOptionSlider option;
        option.initFrom(slider);
        option.orientation = Qt::Horizontal;
        option.state |= QStyle::State_Horizontal;
        option.minimum = slider->minimum();
        option.maximum = slider->maximum();
        option.tickPosition = slider->tickPosition();
        option.tickInterval = slider->tickInterval();
        option.upsideDown = slider->invertedAppearance() != (option.direction == Qt::RightToLeft);
        option.direction = Qt::LeftToRight;
        option.sliderPosition = slider->sliderPosition();
        option.sliderValue = slider->value();
        option.singleStep = slider->singleStep();
        option.pageStep = slider->pageStep();
        const QRect groove = slider->style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, slider);
        const QRect knob = slider->style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, slider);
        const QPoint point = press->position().toPoint();
        // The knob itself drags from where it is.
        if (knob.contains(point) || groove.width() <= knob.width())
            return false;
        // Down first, as Swift's editing begins before the value.
        slider->setSliderDown(true);
        slider->setValue(QStyle::sliderValueFromPosition(slider->minimum(), slider->maximum(), point.x() - groove.x() - knob.width() / 2,
                                                         groove.width() - knob.width(), option.upsideDown));
        // A rounding owner may move the knob: press there.
        option.sliderPosition = slider->sliderPosition();
        option.sliderValue = slider->value();
        const QRect moved = slider->style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, slider);
        const QPoint inside(std::clamp(point.x(), moved.left(), moved.right()), std::clamp(point.y(), moved.top(), moved.bottom()));
        const QPointF shift = inside - point;
        QMouseEvent forwarded(QEvent::MouseButtonPress, inside, press->scenePosition() + shift, press->globalPosition() + shift, Qt::LeftButton,
                              Qt::LeftButton, press->modifiers());
        QCoreApplication::sendEvent(slider, &forwarded);
        return true;
    }
};
}

void SliderSnap::install()
{
    // Once, as Swift's lazy static swaps the method once.
    static const bool installed = [] {
        qApp->installEventFilter(new Snap(qApp));
        return true;
    }();
    (void)installed;
}
