#include "UI/CameraRawRow.h"
#include "UI/ColorPickerSheet.h"
#include "UI/NumericScrub.h"
#include <QEvent>
#include <QHBoxLayout>
#include <QLocale>
#include <QLabel>
#include <cmath>

namespace {
// Swift's `.number`: at most `decimals` places, none trailing.
QString shown(double value, int decimals, QLocale locale)
{
    locale.setNumberOptions(QLocale::OmitGroupSeparator);
    QString text = locale.toString(value, 'f', decimals);
    if (decimals == 0)
        return text;
    while (text.endsWith(locale.zeroDigit()))
        text.chop(locale.zeroDigit().size());
    if (text.endsWith(locale.decimalPoint()))
        text.chop(locale.decimalPoint().size());
    return text;
}
}

CameraRawRow::CameraRawRow(Spec spec, std::function<double()> value, std::function<void(double)> slide, std::function<void(double)> type,
                           std::function<void()> reset, QWidget *parent)
    : QWidget(parent), m_spec(std::move(spec)), m_value(std::move(value)), m_reset(std::move(reset)), m_title(new QLabel(m_spec.title, this)),
      m_slider(new CameraRawSlider(m_spec.low, m_spec.high, m_spec.track, m_spec.help, std::move(slide), m_reset, this))
{
    if (m_spec.fixedTitle)
        m_title->setFixedWidth(m_spec.titleWidth);
    else
        m_title->setMinimumWidth(m_spec.titleWidth);
    m_title->setToolTip(m_spec.help);
    m_slider->setObjectName(m_spec.name + QStringLiteral("Slider"));
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(10);
    row->addWidget(m_title);
    row->addWidget(m_slider, 1);
    // Before the reset's filter, which runs first and passes on.
    if (m_spec.scrub) {
        new NumericScrub(m_title, {.sensitivity = *m_spec.scrub, .low = m_spec.low, .high = m_spec.high, .step = std::nullopt, .value = m_value,
                                   .set = [this, type](double value) {
                                       type(value);
                                       if (m_field)
                                           m_field->setModified(false);
                                       synchronize();
                                   }});
    }
    if (m_spec.fieldWidth > 0) {
        m_field = new PickerField([this, type = std::move(type)] {
            bool number = false;
            const double typed = m_field->locale().toDouble(m_field->text(), &number);
            if (m_field->isModified() && number && std::isfinite(typed))
                type(typed);
            m_field->setModified(false);
            synchronize();
        }, nullptr, this);
        m_field->setObjectName(m_spec.name + QStringLiteral("Field"));
        m_field->setAccessibleName(m_spec.title);
        m_field->setPlaceholderText(m_spec.title);
        m_field->setToolTip(m_spec.help);
        m_field->setAlignment(Qt::AlignRight);
        m_field->setFixedWidth(m_spec.fieldWidth);
        row->addWidget(m_field);
    }
    if (m_spec.titleResets)
        m_title->installEventFilter(this);
    setAccessibleName(m_spec.title);
}

void CameraRawRow::synchronize()
{
    const double value = m_value();
    m_slider->display(value);
    if (m_field && !(m_field->hasFocus() && m_field->isModified()))
        m_field->setText(shown(value, m_spec.decimals, m_field->locale()));
}

// Swift's double click on the title resets the slider.
bool CameraRawRow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_title && event->type() == QEvent::MouseButtonDblClick) {
        m_reset();
        // A scrub still takes the second press, as Swift's drag.
        return !m_spec.scrub;
    }
    return QWidget::eventFilter(watched, event);
}
