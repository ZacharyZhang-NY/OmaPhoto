#include "UI/NumericScrub.h"
#include <QApplication>
#include <QMouseEvent>
#include <QWidget>
#include <algorithm>
#include <cmath>

NumericScrub::NumericScrub(QWidget *label, Options options) : QObject(label), m_label(label), m_options(std::move(options))
{
    // A disabled label shows its parent's cursor, as Qt does.
    label->setCursor(Qt::SizeHorCursor);
    // Buttonless moves must arrive to end a lost release.
    label->setMouseTracking(true);
    label->installEventFilter(this);
}

bool NumericScrub::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type type = event->type();
    // A double click's second press is a press too.
    const bool press = type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick;
    if (!press && type != QEvent::MouseMove && type != QEvent::MouseButtonRelease)
        return false;
    const auto *mouse = static_cast<QMouseEvent *>(event);
    if (watched != m_label) {
        // A popup opened mid-drag takes the release.
        if (type == QEvent::MouseButtonRelease && mouse->button() == Qt::LeftButton)
            end();
        return false;
    }
    if (press) {
        // Filters see a disabled label's presses before Qt drops them.
        if (mouse->button() != Qt::LeftButton || !m_label->isEnabled())
            return false;
        // The label holds the grab: its own points stay exact.
        m_press = mouse->position();
        qApp->installEventFilter(this);
        return true;
    }
    if (!m_press)
        return false;
    if (type == QEvent::MouseButtonRelease) {
        if (mouse->button() == Qt::LeftButton)
            end();
        return true;
    }
    // Qt can lose a release: a buttonless move ends.
    if (!mouse->buttons().testFlag(Qt::LeftButton)) {
        end();
        return true;
    }
    const QPointF translation = mouse->position() - *m_press;
    if (!m_start) {
        if (std::hypot(translation.x(), translation.y()) < 1)
            return true;
        m_start = m_options.value();
        m_options.onStart();
    }
    double proposed = *m_start + translation.x() * m_options.sensitivity;
    if (m_options.step && *m_options.step > 0)
        proposed = std::round(proposed / *m_options.step) * *m_options.step;
    m_options.set(std::min(m_options.high, std::max(m_options.low, proposed)));
    return true;
}

void NumericScrub::reshape(double sensitivity, double low, double high)
{
    m_options.sensitivity = sensitivity;
    m_options.low = low;
    m_options.high = high;
}

void NumericScrub::end()
{
    if (!m_press)
        return;
    qApp->removeEventFilter(this);
    m_press.reset();
    if (m_start) {
        m_start.reset();
        m_options.onEnd();
    }
}
