#include "UI/HueSaturationSheet.h"
#include "UI/KeyboardShortcuts.h"
#include "Rendering/EyedropperIcon.h"
#include "UI/ColorPickerSheet.h"
#include "UI/NumericScrub.h"
#include "UI/SampleButton.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {
// Swift's slider rows: title, unit and the settings' value.
struct Row {
    const char *title;
    const char *unit;
    double (HueSaturationSettings::*value)() const;
    void (HueSaturationSettings::*set)(double);
};
constexpr std::array<Row, 3> rows{{{"Hue", "°", &HueSaturationSettings::hue, &HueSaturationSettings::setHue},
                                   {"Saturation", "", &HueSaturationSettings::saturation, &HueSaturationSettings::setSaturation},
                                   {"Lightness", "", &HueSaturationSettings::lightness, &HueSaturationSettings::setLightness}}};

// Swift's ranges: Colorize sets hue and saturation outright.
std::pair<double, double> bounds(size_t index, bool colorize)
{
    if (index == 0)
        return colorize ? std::pair(0.0, 360.0) : std::pair(-180.0, 180.0);
    if (index == 1)
        return colorize ? std::pair(0.0, 100.0) : std::pair(-100.0, 100.0);
    return {-100.0, 100.0};
}

// Swift's tracks: hue circle, gray to colour, black to white.
CameraRawSliderTrack track(size_t index, const HueSaturationSettings &settings)
{
    using Kind = CameraRawSliderTrack::Kind;
    // The middle of the chosen range; Master centres on red.
    const auto found = std::find(colorRanges.begin(), colorRanges.end(), settings.range);
    const double rangeHue = found == colorRanges.end() ? 0 : double(found - colorRanges.begin()) * 60;
    if (index == 0)
        return {Kind::spectrum, settings.colorize ? 180 : rangeHue};
    if (index == 1) {
        if (settings.colorize)
            return {Kind::saturation, settings.hue()};
        return settings.range == ColorRange::master ? CameraRawSliderTrack{Kind::chroma} : CameraRawSliderTrack{Kind::saturation, rangeHue};
    }
    return {.kind = Kind::opposing, .from = Qt::black, .to = Qt::white};
}

// Swift's `current`: the open edit's settings, else the defaults.
HueSaturationSettings current(const EditorSession &session)
{
    return session.hueSaturation() ? session.hueSaturation()->settings : HueSaturationSettings();
}

// Swift's hand.point.up.left: a finger up and left, outlined.
void pointingHand(QPainter &painter, const QColor &ink)
{
    QPainterPath finger, palm;
    finger.addRoundedRect(QRectF(-1.5, -7, 3, 9), 1.5, 1.5);
    palm.addRoundedRect(QRectF(-4, 0, 9.5, 7.5), 2.5, 2.5);
    painter.save();
    painter.translate(12, 10);
    painter.rotate(-35);
    painter.translate(-0.75, -0.25);
    painter.setPen(QPen(ink, 1.2));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(finger.united(palm));
    painter.restore();
}

QToolButton *sampleButton(HueSampleMode mode, QWidget *parent)
{
    auto *button = new SampleButton([mode](QPainter &painter, const QColor &ink) { sampleEyedropper(painter, mode, ink); }, parent);
    button->setObjectName(QStringLiteral("hue") + rawValue(mode));
    button->setToolTip(help(mode));
    button->setAccessibleName(rawValue(mode) + QStringLiteral(" color"));
    return button;
}

QFrame *line(QFrame::Shape shape, QWidget *parent)
{
    auto *made = new QFrame(parent);
    made->setFrameShape(shape);
    made->setForegroundRole(QPalette::Mid);
    return made;
}
}

HueSaturationSheet::HueSaturationSheet(EditorSession &session, QWidget *parent)
    : QWidget(parent), m_session(session), m_range(new QComboBox(this)),
      m_samples{sampleButton(HueSampleMode::replace, this), sampleButton(HueSampleMode::add, this), sampleButton(HueSampleMode::remove, this)},
      m_divider(line(QFrame::VLine, this)), m_targeting(new SampleButton(pointingHand, this)), m_rows{row(0), row(1), row(2)},
      m_spectrum(new SpectrumEditor([this] { return current(m_session); },
                                    [this](const HueSaturationSettings &edited) { change([&edited](HueSaturationSettings &settings) { settings = edited; }); }, this)),
      m_invert(new QCheckBox(QStringLiteral("Apply outside this range instead"), this)), m_colorize(new QCheckBox(QStringLiteral("Colorize"), this)),
      m_preview(new QCheckBox(QStringLiteral("Preview"), this)), m_reset(new QPushButton(QStringLiteral("Reset"), this)),
      m_limited(new QLabel(QStringLiteral("Limited to the selection"), this)), m_cancel(new QPushButton(QStringLiteral("Cancel"), this)),
      m_ok(new QPushButton(QStringLiteral("OK"), this))
{
    setFixedWidth(460);
    m_range->setObjectName(QStringLiteral("hueRange"));
    m_range->setFixedWidth(160);
    // Swift hides the label; Linux reads it through the relation.
    auto *rangeLabel = new QLabel(QStringLiteral("Range"), this);
    rangeLabel->setBuddy(m_range);
    rangeLabel->hide();
    for (const ColorRange range : allColorRanges)
        m_range->addItem(rawValue(range));
    connect(m_range, &QComboBox::activated, this,
            [this](int index) { change([index](HueSaturationSettings &settings) { settings.range = allColorRanges[size_t(index)]; }); });
    for (size_t index = 0; index < m_samples.size(); ++index) {
        // A second click puts the eyedropper away.
        connect(m_samples[index], &QToolButton::clicked, this, [this, index] {
            const HueSampleMode mode = allHueSampleModes[index];
            m_session.setHueTargeting(false);
            m_session.setHueSampleMode(m_session.hueSampleMode() == mode ? std::nullopt : std::optional(mode));
        });
    }
    m_divider->setFixedHeight(16);
    m_targeting->setObjectName(QStringLiteral("hueTargeting"));
    m_targeting->setToolTip(QStringLiteral("Targeted adjustment: drag on the image to change that color's saturation, or its hue with Ctrl held"));
    m_targeting->setAccessibleName(QStringLiteral("Targeted adjustment"));
    connect(m_targeting, &QToolButton::clicked, this, [this] {
        m_session.setHueSampleMode(std::nullopt);
        m_session.setHueTargeting(!m_session.hueTargeting());
    });
    m_spectrum->setObjectName(QStringLiteral("hueSpectrum"));
    connect(m_invert, &QCheckBox::clicked, this, [this](bool on) { change([on](HueSaturationSettings &settings) { settings.invertRange = on; }); });
    // Photoshop starts colorizing at hue 0, saturation 25.
    connect(m_colorize, &QCheckBox::clicked, this, [this](bool on) {
        change([on](HueSaturationSettings &settings) { settings = on ? HueSaturationSettings::colorizeStart() : HueSaturationSettings(); });
    });
    connect(m_preview, &QCheckBox::clicked, this, [this](bool on) { m_session.updateHueSaturation(current(m_session), on); });
    m_reset->setAutoDefault(false);
    connect(m_reset, &QPushButton::clicked, this, [this] {
        change([](HueSaturationSettings &settings) { settings = settings.colorize ? HueSaturationSettings::colorizeStart() : HueSaturationSettings(); });
    });
    // Swift's .callout in the secondary ink.
    QFont callout = m_limited->font();
    callout.setPixelSize(12);
    m_limited->setFont(callout);
    m_limited->setForegroundRole(QPalette::PlaceholderText);
    m_limited->setObjectName(QStringLiteral("hueLimited"));
    // Return is OK's from anywhere, as Swift's default action.
    m_cancel->setAutoDefault(false);
    m_ok->setDefault(true);
    // Swift's configuredNativeShortcut: Return and Escape, as remapped.
    NativeShortcut::bind(*this, m_ok, m_cancel);
    connect(m_cancel, &QPushButton::clicked, this, [this] { m_session.cancelHueSaturation(); });
    connect(m_ok, &QPushButton::clicked, this, [this] { m_session.commitHueSaturation(); });
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(24, 24, 24, 24);
    column->setSpacing(16);
    auto *top = new QHBoxLayout;
    top->setSpacing(12);
    top->addWidget(m_range);
    top->addStretch(1);
    auto *sampling = new QHBoxLayout;
    sampling->setSpacing(6);
    for (QToolButton *sample : m_samples)
        sampling->addWidget(sample);
    sampling->addWidget(m_divider);
    sampling->addWidget(m_targeting);
    top->addLayout(sampling);
    column->addLayout(top);
    for (QWidget *each : m_rows)
        column->addWidget(each);
    column->addWidget(m_spectrum);
    column->addWidget(m_invert);
    auto *toggles = new QHBoxLayout;
    toggles->setSpacing(18);
    toggles->addWidget(m_colorize);
    toggles->addWidget(m_preview);
    toggles->addWidget(m_reset);
    toggles->addStretch(1);
    column->addLayout(toggles);
    column->addWidget(m_limited);
    column->addWidget(line(QFrame::HLine, this));
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_cancel);
    buttons->addStretch(1);
    buttons->addWidget(m_ok);
    column->addLayout(buttons);
    connect(&m_session, &EditorSession::changed, this, &HueSaturationSheet::synchronize);
    synchronize();
}

QWidget *HueSaturationSheet::row(size_t index)
{
    const QString title = QString::fromUtf8(rows[index].title);
    auto *box = new QWidget(this);
    auto *layout = new QHBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    auto *label = new QLabel(title, box);
    label->setFixedWidth(76);
    // Swift's scrubbable title; synchronize sets its range.
    m_scrubs[index] = new NumericScrub(label, {.sensitivity = 1, .low = 0, .high = 0, .step = std::nullopt,
                                               .value = [this, index] { return (current(m_session).*rows[index].value)(); },
                                               .set = [this, index](double value) {
                                                   change([&](HueSaturationSettings &settings) { (settings.*rows[index].set)(value); });
                                                   m_fields[index]->setModified(false);
                                                   synchronize();
                                               }});
    const auto [low, high] = bounds(index, current(m_session).colorize);
    auto *slider = new CameraRawSlider(
        low, high, track(index, current(m_session)), title + QStringLiteral(". Double-click to reset."),
        [this, index](double value) {
            // Swift sets whole numbers; unchanged, the knob keeps its travel.
            const double rounded = std::round(value);
            if ((current(m_session).*rows[index].value)() == rounded)
                return;
            change([&](HueSaturationSettings &settings) { (settings.*rows[index].set)(rounded); });
            // Its value replaces the field's typing.
            m_fields[index]->setModified(false);
            synchronize();
        },
        [this, index] { reset(index); }, box);
    slider->setObjectName(title.toLower() + QStringLiteral("Slider"));
    label->setBuddy(slider);
    // After the scrub's filter, so this one runs first.
    label->installEventFilter(this);
    m_titles[index] = label;
    // A readable number applies as typed, as Swift's value binding.
    auto *field = new PickerField([this, index] {
        PickerField &edited = *m_fields[index];
        bool number = false;
        const double typed = edited.locale().toDouble(edited.text(), &number);
        // Swift's Int() traps on what is no finite number.
        if (edited.isModified() && number && std::isfinite(typed))
            change([&](HueSaturationSettings &settings) { (settings.*rows[index].set)(typed); });
        edited.setModified(false);
        synchronize();
    }, nullptr, box);
    field->setObjectName(title.toLower() + QStringLiteral("Field"));
    field->setAccessibleName(title);
    field->setPlaceholderText(title);
    field->setAlignment(Qt::AlignRight);
    field->setFixedWidth(48);
    // Swift's onSubmit clamps; the default button then commits.
    connect(field, &QLineEdit::returnPressed, this, [this, index] {
        const std::pair<double, double> limits = bounds(index, current(m_session).colorize);
        change([&](HueSaturationSettings &settings) {
            (settings.*rows[index].set)(std::clamp((settings.*rows[index].value)(), limits.first, limits.second));
        });
    });
    auto *suffixed = new QHBoxLayout;
    suffixed->setSpacing(2);
    suffixed->addWidget(field);
    suffixed->addWidget(new QLabel(QString::fromUtf8(rows[index].unit), box));
    layout->addWidget(label);
    layout->addWidget(slider, 1);
    layout->addLayout(suffixed);
    m_sliders[index] = slider;
    m_fields[index] = field;
    return box;
}

void HueSaturationSheet::reset(size_t index)
{
    change([index](HueSaturationSettings &settings) {
        const HueSaturationSettings start = settings.colorize ? HueSaturationSettings::colorizeStart() : HueSaturationSettings();
        (settings.*rows[index].set)((start.*rows[index].value)());
    });
    m_fields[index]->setModified(false);
    synchronize();
}

// Swift's double click on a title resets its slider.
bool HueSaturationSheet::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonDblClick && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
        for (size_t index = 0; index < m_titles.size(); ++index) {
            if (m_titles[index] == watched && m_titles[index]->isEnabled())
                reset(index);
        }
    }
    // The scrub still takes the second press, as Swift's drag.
    return QWidget::eventFilter(watched, event);
}

void HueSaturationSheet::change(const std::function<void(HueSaturationSettings &)> &edit)
{
    HueSaturationSettings settings = current(m_session);
    edit(settings);
    m_session.updateHueSaturation(settings, !m_session.hueSaturation() || m_session.hueSaturation()->preview);
}

void HueSaturationSheet::synchronize()
{
    // Closed, the panel hides the sheet until the next edit.
    const std::optional<HueSaturationEdit> &edit = m_session.hueSaturation();
    if (!edit)
        return;
    const HueSaturationSettings &settings = edit->settings;
    const bool spectrum = settings.range != ColorRange::master && !settings.colorize;
    m_range->setCurrentIndex(int(settings.range));
    m_range->setEnabled(!settings.colorize);
    for (size_t index = 0; index < m_samples.size(); ++index) {
        m_samples[index]->setVisible(spectrum);
        m_samples[index]->setChecked(m_session.hueSampleMode() == allHueSampleModes[index]);
    }
    m_divider->setVisible(spectrum);
    m_targeting->setVisible(!settings.colorize);
    m_targeting->setChecked(m_session.hueTargeting());
    for (size_t index = 0; index < rows.size(); ++index) {
        const auto [low, high] = bounds(index, settings.colorize);
        m_scrubs[index]->reshape(1, low, high);
        const double value = (settings.*rows[index].value)();
        // Swift's updateNSView: range and track follow the settings.
        m_sliders[index]->reshape(low, high, track(index, settings), std::clamp(value, low, high));
        // Ungrouped digits; a field being typed in keeps its typing.
        QLocale locale = m_fields[index]->locale();
        locale.setNumberOptions(QLocale::OmitGroupSeparator);
        const QString shown = locale.toString(value, 'f', 0);
        if (!m_fields[index]->isModified() && m_fields[index]->text() != shown)
            m_fields[index]->setText(shown);
    }
    m_spectrum->setVisible(spectrum);
    m_spectrum->synchronize();
    m_invert->setVisible(spectrum);
    m_invert->setChecked(settings.invertRange);
    m_colorize->setChecked(settings.colorize);
    m_preview->setChecked(edit->preview);
    // An adjustment layer's editor ignores the selection.
    m_limited->setVisible(!m_session.adjustmentOriginal() && m_session.selection());
}

HueSaturationSheet::~HueSaturationSheet()
{
    releaseFocus(*this);
}
