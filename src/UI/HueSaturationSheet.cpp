#include "UI/HueSaturationSheet.h"
#include "UI/KeyboardShortcuts.h"
#include "Rendering/EyedropperIcon.h"
#include "UI/ColorPickerSheet.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
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

// Sliders move in hundredths, as Swift's run freely.
constexpr double sliderScale = 100;

// Swift's ranges: Colorize sets hue and saturation outright.
std::pair<double, double> bounds(size_t index, bool colorize)
{
    if (index == 0)
        return colorize ? std::pair(0.0, 360.0) : std::pair(-180.0, 180.0);
    if (index == 1)
        return colorize ? std::pair(0.0, 100.0) : std::pair(-100.0, 100.0);
    return {-100.0, 100.0};
}

// Swift's `current`: the open edit's settings, else the defaults.
HueSaturationSettings current(const EditorSession &session)
{
    return session.hueSaturation() ? session.hueSaturation()->settings : HueSaturationSettings();
}

// Swift's 72 hue slices, as they are or adjusted.
class Spectrum : public QWidget {
public:
    Spectrum(std::function<HueSaturationSettings()> value, bool after, QWidget *parent) : QWidget(parent), m_value(std::move(value)), m_after(after)
    {
        setFixedHeight(16);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        constexpr int slices = 72;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath rounded;
        rounded.addRoundedRect(QRectF(rect()), 3, 3);
        painter.setClipPath(rounded);
        const HueSaturationSettings settings = m_value();
        const double width = double(this->width()) / slices;
        for (int slice = 0; slice < slices; ++slice) {
            const double hue = double(slice) / slices * 360;
            const double shown = m_after ? HueSaturationFilter::shiftedHue(hue, settings) : hue;
            painter.fillRect(QRectF(slice * width, 0, width + 0.5, height()), QColor::fromHsvF(float(shown / 360), 1, 1));
        }
    }

private:
    const std::function<HueSaturationSettings()> m_value;
    const bool m_after;
};

// Swift's handles: shoulders as blocks, the core's ends as bars.
class SpectrumHandles : public QWidget {
public:
    SpectrumHandles(std::function<HueSaturationSettings()> value, std::function<void(double, double, bool)> drag, QWidget *parent)
        : QWidget(parent), m_value(std::move(value)), m_drag(std::move(drag))
    {
        setFixedHeight(12);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const std::array<double, 4> handles = m_value().band().handles();
        for (size_t index = 0; index < handles.size(); ++index) {
            const double x = handles[index] / 360 * width();
            const bool inner = index == 1 || index == 2;
            painter.fillRect(inner ? QRectF(x - 1, 0, 2, height()) : QRectF(x - 3.5, height() / 2.0 - 2.5, 7, 5), palette().color(QPalette::WindowText));
        }
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            m_drag(event->position().x(), width(), true);
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (event->buttons().testFlag(Qt::LeftButton))
            m_drag(event->position().x(), width(), false);
    }

private:
    const std::function<HueSaturationSettings()> m_value;
    const std::function<void(double, double, bool)> m_drag;
};

// The nearest handle round the circle; ties take the first.
int nearestHandle(const HueBand &band, double degrees)
{
    const std::array<double, 4> handles = band.handles();
    std::array<double, 4> distances{};
    for (size_t index = 0; index < handles.size(); ++index) {
        const double gap = std::fmod(std::abs(handles[index] - degrees), 360);
        distances[index] = std::min(gap, 360 - gap);
    }
    return int(std::min_element(distances.begin(), distances.end()) - distances.begin());
}

// Swift's plain sampling buttons: a glyph, tinted when chosen.
class SampleButton : public QToolButton {
public:
    SampleButton(std::function<void(QPainter &, const QColor &)> glyph, QWidget *parent) : QToolButton(parent), m_glyph(std::move(glyph))
    {
        setCheckable(true);
        setFixedSize(24, 20);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (isChecked()) {
            QColor tint = palette().color(QPalette::Highlight);
            tint.setAlphaF(0.25f);
            painter.setPen(Qt::NoPen);
            painter.setBrush(tint);
            painter.drawRoundedRect(QRectF(rect()), 4, 4);
        }
        m_glyph(painter, palette().color(QPalette::WindowText));
    }

private:
    const std::function<void(QPainter &, const QColor &)> m_glyph;
};

// Swift's eyedropper, badged plus or minus for Add and Remove.
void eyedropper(QPainter &painter, HueSampleMode mode, const QColor &ink)
{
    // A 14-point glyph, centred in the 24 by 20 frame.
    painter.save();
    painter.translate(5, 3);
    painter.save();
    painter.scale(14.0 / 18, 14.0 / 18);
    painter.setPen(QPen(ink, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    EyedropperIcon::paint(painter);
    painter.restore();
    if (mode != HueSampleMode::replace) {
        // An 8-point disc, bottom right, nudged 3 and 1.
        QPainterPath badge;
        badge.addEllipse(QRectF(9, 7, 8, 8));
        QPainterPath sign;
        sign.setFillRule(Qt::WindingFill);
        sign.addRect(QRectF(10.75, 10.4, 4.5, 1.2));
        if (mode == HueSampleMode::add)
            sign.addRect(QRectF(12.4, 8.75, 1.2, 4.5));
        painter.fillPath(badge.subtracted(sign), ink);
    }
    painter.restore();
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
    auto *button = new SampleButton([mode](QPainter &painter, const QColor &ink) { eyedropper(painter, mode, ink); }, parent);
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

SpectrumEditor::SpectrumEditor(std::function<HueSaturationSettings()> value, std::function<void(const HueSaturationSettings &)> change, QWidget *parent)
    : QWidget(parent), m_value(std::move(value)), m_change(std::move(change)),
      m_handles(new SpectrumHandles(m_value, [this](double x, double width, bool pressed) { drag(x, width, pressed); }, this)),
      m_after(new Spectrum(m_value, true, this)), m_readout(new QLabel(this))
{
    auto *before = new Spectrum(m_value, false, this);
    before->setObjectName(QStringLiteral("spectrumBefore"));
    m_handles->setObjectName(QStringLiteral("spectrumHandles"));
    m_after->setObjectName(QStringLiteral("spectrumAfter"));
    m_readout->setObjectName(QStringLiteral("spectrumReadout"));
    // Swift's .caption in the secondary ink.
    QFont small = m_readout->font();
    small.setPixelSize(10);
    m_readout->setFont(small);
    m_readout->setForegroundRole(QPalette::PlaceholderText);
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(5);
    column->addWidget(before);
    column->addWidget(m_handles);
    column->addWidget(m_after);
    column->addWidget(m_readout, 0, Qt::AlignHCenter);
    synchronize();
}

void SpectrumEditor::synchronize()
{
    QStringList degrees;
    for (const double handle : m_value().band().handles())
        degrees << QStringLiteral("%1°").arg(std::lround(handle));
    m_readout->setText(degrees.join(QStringLiteral("   ")));
    m_handles->update();
    m_after->update();
}

void SpectrumEditor::drag(double x, double width, bool pressed)
{
    // A press starts afresh: Qt can lose a release.
    if (pressed)
        m_dragging.reset();
    const double degrees = std::clamp(x, 0.0, width) / width * 360;
    HueSaturationSettings settings = m_value();
    HueBand band = settings.band();
    const int index = m_dragging.value_or(nearestHandle(band, degrees));
    m_dragging = index;
    band.setHandle(index, degrees);
    settings.setBand(band);
    m_change(settings);
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
    auto *slider = new QSlider(Qt::Horizontal, box);
    slider->setObjectName(title.toLower() + QStringLiteral("Slider"));
    label->setBuddy(slider);
    connect(slider, &QSlider::valueChanged, this, [this, index](int value) {
        change([&](HueSaturationSettings &settings) { (settings.*rows[index].set)(value / sliderScale); });
        // Its value replaces the field's typing.
        m_fields[index]->setModified(false);
        synchronize();
    });
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
        const double value = (settings.*rows[index].value)();
        {
            // The sliders show the session's numbers without writing back.
            const QSignalBlocker quiet(m_sliders[index]);
            m_sliders[index]->setRange(int(low * sliderScale), int(high * sliderScale));
            m_sliders[index]->setValue(int(std::lround(std::clamp(value, low, high) * sliderScale)));
        }
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
