#include "UI/FilterSheet.h"
#include "UI/ColorPickerSheet.h"
#include "UI/NumericScrub.h"
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QSlider>
#include <QVBoxLayout>
#include <cmath>

// Swift's `control`: title, slider, exact field; coloured tracks reset.
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

CameraRawSliderTrack opposing(QColor from, QColor to)
{
    return {.kind = CameraRawSliderTrack::Kind::opposing, .from = from, .to = to};
}
}

FilterSettings FilterSheet::resetting(const std::function<double &(FilterSettings &)> &key, FilterSettings settings)
{
    FilterSettings defaults;
    key(settings) = key(defaults);
    return settings;
}

CameraRawSliderTrack FilterSheet::cyanRedTrack()
{
    return opposing(QColor::fromRgbF(0.10f, 0.72f, 0.80f), QColor::fromRgbF(0.86f, 0.18f, 0.20f));
}

CameraRawSliderTrack FilterSheet::magentaGreenTrack()
{
    return opposing(QColor::fromRgbF(0.80f, 0.22f, 0.70f), QColor::fromRgbF(0.24f, 0.70f, 0.30f));
}

CameraRawSliderTrack FilterSheet::yellowBlueTrack()
{
    return opposing(QColor::fromRgbF(0.95f, 0.82f, 0.18f), QColor::fromRgbF(0.22f, 0.40f, 0.92f));
}

void FilterSheet::control(const QString &title, std::function<double &(FilterSettings &)> key, double low, double high, const QString &unit,
                          int decimals, bool logarithmic, std::function<CameraRawSliderTrack(const FilterSettings &)> track)
{
    if (track && logarithmic)
        throw std::logic_error("a coloured track is linear");
    QString name = title;
    name.remove(QLatin1Char(' '));
    name[0] = name[0].toLower();
    const size_t index = m_controls.size();
    auto *box = new QWidget(this);
    auto *label = new QLabel(title, box);
    // Swift's scrubbable title: a point is the field's last decimal.
    new NumericScrub(label, {.sensitivity = 1 / std::pow(10.0, decimals), .low = low, .high = high, .step = std::nullopt,
                             .value = [this, index] {
                                 FilterSettings current = settings();
                                 return m_controls[index].key(current);
                             },
                             .set = [this, index](double value) {
                                 update([this, index, value](FilterSettings &settings) { m_controls[index].key(settings) = value; });
                                 m_controls[index].field->setModified(false);
                                 synchronize();
                             }});
    // Swift rounds what the slider sets to the field's decimals.
    const auto slide = [this, index](double value) {
        const Control &at = m_controls[index];
        const double step = std::pow(10.0, at.decimals), rounded = std::round(value * step) / step;
        // Unchanged, the thumb stays, so arrow steps add up.
        FilterSettings current = settings();
        if (at.key(current) == rounded)
            return;
        update([&at, rounded](FilterSettings &settings) { at.key(settings) = rounded; });
        // Its value replaces the field's typing.
        at.field->setModified(false);
        synchronize();
    };
    QSlider *slider = nullptr;
    if (track) {
        slider = new CameraRawSlider(low, high, track(settings()), title + QStringLiteral(". Double-click to reset."), slide, [this, index] { reset(index); },
                                     box);
        // After the scrub's filter, so this one runs first.
        label->installEventFilter(this);
    } else {
        slider = new QSlider(Qt::Horizontal, box);
        slider->setRange(0, travel);
        connect(slider, &QSlider::valueChanged, this, [this, index, slide](int position) {
            const Control &at = m_controls[index];
            const double from = at.logarithmic ? std::log(at.low) : at.low, to = at.logarithmic ? std::log(at.high) : at.high;
            const double value = from + (to - from) * position / travel;
            slide(at.logarithmic ? std::exp(value) : value);
        });
    }
    slider->setObjectName(name + QStringLiteral("Slider"));
    label->setBuddy(slider);
    // A readable number applies as typed; normalizing clamps it.
    auto *field = new PickerField([this, index] {
        PickerField &edited = *m_controls[index].field;
        bool number = false;
        const double typed = edited.locale().toDouble(edited.text(), &number);
        if (edited.isModified() && number && std::isfinite(typed))
            update([this, index, typed](FilterSettings &settings) { m_controls[index].key(settings) = typed; });
        edited.setModified(false);
        synchronize();
    }, nullptr, box);
    field->setObjectName(name + QStringLiteral("Field"));
    field->setAccessibleName(title);
    field->setPlaceholderText(title);
    field->setAlignment(Qt::AlignRight);
    field->setFixedWidth(56);
    auto *suffixed = new QHBoxLayout;
    suffixed->setSpacing(2);
    suffixed->addWidget(field);
    suffixed->addWidget(new QLabel(unit, box));
    auto *row = new QHBoxLayout(box);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(10);
    row->addWidget(label);
    row->addWidget(slider, 1);
    row->addLayout(suffixed);
    m_column->addWidget(box);
    m_controls.push_back(Control{std::move(key), low, high, decimals, logarithmic, slider, field, label, std::move(track)});
}

void FilterSheet::showControl(Control &control, const FilterSettings &settings)
{
    FilterSettings copy = settings;
    const double value = control.key(copy);
    if (control.track) {
        // Swift's updateNSView: the track follows the settings.
        static_cast<CameraRawSlider *>(control.slider)->reshape(control.low, control.high, control.track(settings), value);
    } else {
        const double from = control.logarithmic ? std::log(control.low) : control.low, to = control.logarithmic ? std::log(control.high) : control.high;
        const double at = control.logarithmic ? std::log(value) : value;
        // The slider shows the session's number without writing back.
        const QSignalBlocker quiet(control.slider);
        control.slider->setValue(int(std::lround((at - from) / (to - from) * travel)));
    }
    // A field being typed in keeps its typing.
    const QString text = shown(value, control.decimals, control.field->locale());
    if (!control.field->isModified() && control.field->text() != text)
        control.field->setText(text);
}

void FilterSheet::alignTitles()
{
    int widest = 60;
    for (const Control &control : m_controls) {
        if (!control.slider->parentWidget()->isHidden())
            widest = std::max(widest, control.title->sizeHint().width());
    }
    for (const Control &control : m_controls)
        control.title->setFixedWidth(widest);
}

void FilterSheet::reset(size_t index)
{
    update([this, index](FilterSettings &settings) { settings = resetting(m_controls[index].key, settings); });
    m_controls[index].field->setModified(false);
    synchronize();
}

// Swift's double click on a coloured slider's title resets it.
bool FilterSheet::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonDblClick && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
        for (size_t index = 0; index < m_controls.size(); ++index) {
            if (m_controls[index].title == watched && m_controls[index].track && m_controls[index].title->isEnabled())
                reset(index);
        }
    }
    // The scrub still takes the second press, as Swift's drag.
    return QWidget::eventFilter(watched, event);
}
