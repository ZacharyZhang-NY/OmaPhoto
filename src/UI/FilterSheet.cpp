#include "UI/FilterSheet.h"
#include "UI/KeyboardShortcuts.h"
#include "UI/ColorPaletteControls.h"
#include "UI/ColorPickerSheet.h"
#include "UI/CurvesControls.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

namespace {
// A slider's travel: thousandths of its range.
constexpr int travel = 1000;

QLabel *words(const QString &text, bool callout, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    label->setObjectName(QStringLiteral("filterWords"));
    if (callout) {
        QFont font = label->font();
        font.setPixelSize(12);
        label->setFont(font);
        label->setForegroundRole(QPalette::PlaceholderText);
    }
    return label;
}

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

QColor colour(const AdjustmentColor &value)
{
    return QColor::fromRgbF(float(value.red), float(value.green), float(value.blue));
}

// Swift's LinearGradient from the dark end to the light.
class GradientBar : public QWidget {
public:
    GradientBar(std::function<GradientMapSettings()> value, QWidget *parent) : QWidget(parent), m_value(std::move(value))
    {
        setFixedHeight(20);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF bounds(rect());
        const GradientMapSettings::Ends ends = m_value().ends();
        QLinearGradient gradient(bounds.topLeft(), bounds.topRight());
        gradient.setColorAt(0, colour(ends.dark));
        gradient.setColorAt(1, colour(ends.light));
        QPainterPath shape;
        shape.addRoundedRect(bounds, 4, 4);
        painter.fillPath(shape, gradient);
        painter.setPen(QPen(QColor::fromRgbF(0, 0, 0, 0.35f), 1));
        painter.drawRoundedRect(bounds.adjusted(0.5, 0.5, -0.5, -0.5), 3.5, 3.5);
    }

private:
    const std::function<GradientMapSettings()> m_value;
};
}

GradientMapControls::GradientMapControls(std::function<GradientMapSettings()> value, std::function<void(const GradientMapSettings &)> change,
                                         std::function<void(bool)> pick, QWidget *parent)
    : QWidget(parent), m_value(std::move(value)), m_change(std::move(change)), m_gradient(new GradientBar(m_value, this)),
      m_reverse(new QCheckBox(QStringLiteral("Reverse"), this))
{
    m_gradient->setObjectName(QStringLiteral("gradientMapBar"));
    m_reverse->setObjectName(QStringLiteral("gradientMapReverse"));
    connect(m_reverse, &QCheckBox::clicked, this, [this](bool on) {
        GradientMapSettings settings = m_value();
        settings.reversed = on;
        m_change(settings);
    });
    auto *ends = new QHBoxLayout;
    ends->setSpacing(20);
    ends->addWidget(swatch(QStringLiteral("Shadows"), false, pick));
    ends->addWidget(swatch(QStringLiteral("Highlights"), true, pick));
    ends->addStretch(1);
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(12);
    column->addWidget(m_gradient);
    column->addLayout(ends);
    column->addWidget(m_reverse);
}

QWidget *GradientMapControls::swatch(const QString &title, bool highlights, const std::function<void(bool)> &pick)
{
    auto *box = new QWidget(this);
    auto *button = new SwatchButton([this, highlights] {
        const GradientMapSettings settings = m_value();
        const AdjustmentColor &end = highlights ? settings.highlights : settings.shadows;
        return PaletteColor{end.red, end.green, end.blue};
    }, 6, 1.5, 1, box);
    button->setObjectName(title.toLower() + QStringLiteral("Swatch"));
    button->setFixedSize(24, 24);
    button->setToolTip(QStringLiteral("Choose the %1 color").arg(title.toLower()));
    button->setAccessibleName(QStringLiteral("%1 color").arg(title));
    connect(button, &QAbstractButton::clicked, this, [pick, highlights] { pick(highlights); });
    auto *row = new QHBoxLayout(box);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    row->addWidget(button);
    row->addWidget(new QLabel(title, box));
    m_swatches.push_back(button);
    return box;
}

void GradientMapControls::synchronize()
{
    const QSignalBlocker quiet(m_reverse);
    m_reverse->setChecked(m_value().reversed);
    m_gradient->update();
    for (SwatchButton *each : m_swatches)
        each->update();
}

FilterSheet::FilterSheet(EditorSession &session, QWidget *parent)
    : QWidget(parent), m_session(session), m_column(new QVBoxLayout(this)), m_preview(new QCheckBox(QStringLiteral("Preview"), this)),
      m_error(words(QString(), false, this)), m_limited(words(QStringLiteral("Limited to the selection"), true, this)),
      m_spinner(new QProgressBar(this)), m_activity(words(QString(), true, this)), m_cancel(new QPushButton(QStringLiteral("Cancel"), this)),
      m_ok(new QPushButton(QStringLiteral("OK"), this))
{
    setFixedWidth(380);
    m_column->setContentsMargins(24, 24, 24, 24);
    m_column->setSpacing(16);
    const auto key = [](double FilterSettings::*field) { return [field](FilterSettings &settings) -> double & { return settings.*field; }; };
    // Swift's `edit?.kind ?? .gaussianBlur`.
    switch (m_session.filterEdit() ? m_session.filterEdit()->kind : FilterKind::gaussianBlur) {
    case FilterKind::curves:
        m_curves = new CurvesControls([this] { return settings().curves; },
                                      [this](const CurvesSettings &curves) { update([&curves](FilterSettings &settings) { settings.curves = curves; }); }, this);
        m_column->addWidget(m_curves);
        break;
    case FilterKind::exposure:
        control(QStringLiteral("Exposure"), [](FilterSettings &settings) -> double & { return settings.exposure.exposure; },
                ExposureSettings::exposureLow, ExposureSettings::exposureHigh, QString(), 2, false);
        control(QStringLiteral("Offset"), [](FilterSettings &settings) -> double & { return settings.exposure.offset; }, ExposureSettings::offsetLow,
                ExposureSettings::offsetHigh, QString(), 4, false);
        control(QStringLiteral("Gamma"), [](FilterSettings &settings) -> double & { return settings.exposure.gamma; }, ExposureSettings::gammaLow,
                ExposureSettings::gammaHigh, QString(), 2, true);
        break;
    case FilterKind::gradientMap:
        m_gradientMap = new GradientMapControls([this] { return settings().gradientMap; },
                                                [this](const GradientMapSettings &map) { update([&map](FilterSettings &settings) { settings.gradientMap = map; }); },
                                                [this](bool highlights) { m_session.openGradientMapColorPicker(highlights); }, this);
        m_column->addWidget(m_gradientMap);
        break;
    case FilterKind::grain:
        control(QStringLiteral("Amount"), [](FilterSettings &settings) -> double & { return settings.grain.amount; }, GrainSettings::amountLow,
                GrainSettings::amountHigh, QString(), 0, false);
        control(QStringLiteral("Size"), [](FilterSettings &settings) -> double & { return settings.grain.size; }, GrainSettings::sizeLow,
                GrainSettings::sizeHigh, QStringLiteral("px"), 1, true);
        control(QStringLiteral("Roughness"), [](FilterSettings &settings) -> double & { return settings.grain.roughness; },
                GrainSettings::roughnessLow, GrainSettings::roughnessHigh, QString(), 0, false);
        break;
    case FilterKind::removeBackground: background(); break;
    case FilterKind::blackWhite: blackWhite(); break;
    case FilterKind::colorBalance: colorBalance(); break;
    case FilterKind::contentAwareFill:
        m_column->addWidget(words(QStringLiteral("Fill the selection using surrounding pixels from this layer."), false, this));
        break;
    case FilterKind::gaussianBlur:
        control(QStringLiteral("Radius"), key(&FilterSettings::radius), 0.1, 250, QStringLiteral("px"), 1, true);
        break;
    case FilterKind::motionBlur:
        control(QStringLiteral("Angle"), key(&FilterSettings::angle), -90, 90, QStringLiteral("°"), 0, false);
        control(QStringLiteral("Distance"), key(&FilterSettings::distance), 1, 2000, QStringLiteral("px"), 0, true);
        break;
    case FilterKind::addNoise:
        control(QStringLiteral("Amount"), key(&FilterSettings::amount), 0.1, 400, QStringLiteral("%"), 1, true);
        noise();
        break;
    case FilterKind::lensCorrection:
        control(QStringLiteral("Remove Distortion"), key(&FilterSettings::distortion), -100, 100, QString(), 0, false);
        m_column->addWidget(words(QStringLiteral("Positive straightens lines that bow outward (barrel); negative, lines that bow inward (pincushion)."),
                                  true, this));
        break;
    }
    m_preview->setObjectName(QStringLiteral("filterPreview"));
    m_error->setObjectName(QStringLiteral("filterError"));
    m_error->setForegroundRole(QPalette::BrightText);
    m_limited->setObjectName(QStringLiteral("filterLimited"));
    m_spinner->setObjectName(QStringLiteral("filterSpinner"));
    m_spinner->setRange(0, 0);
    m_spinner->setTextVisible(false);
    m_spinner->setFixedSize(16, 16);
    m_activity->setObjectName(QStringLiteral("filterActivity"));
    m_cancel->setObjectName(QStringLiteral("filterCancel"));
    m_cancel->setAutoDefault(false);
    m_ok->setObjectName(QStringLiteral("filterOK"));
    m_ok->setDefault(true);
    // Swift's configuredNativeShortcut: Return and Escape, as remapped.
    NativeShortcut::bind(*this, m_ok, m_cancel);
    connect(m_preview, &QCheckBox::clicked, this, [this](bool on) { m_session.updateFilter(settings(), on); });
    connect(m_cancel, &QPushButton::clicked, this, [this] { m_session.cancelFilter(); });
    connect(m_ok, &QPushButton::clicked, this, [this] { m_session.commitFilter(); });
    auto *divider = new QFrame(this);
    divider->setFrameShape(QFrame::HLine);
    divider->setForegroundRole(QPalette::Mid);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_cancel);
    buttons->addStretch(1);
    buttons->addWidget(m_spinner);
    buttons->addWidget(m_activity);
    buttons->addWidget(m_ok);
    m_column->addWidget(m_preview);
    m_column->addWidget(m_error);
    m_column->addWidget(m_limited);
    m_column->addWidget(divider);
    m_column->addLayout(buttons);
    connect(&m_session, &EditorSession::changed, this, &FilterSheet::synchronize);
    synchronize();
}

void FilterSheet::control(const QString &title, std::function<double &(FilterSettings &)> key, double low, double high, const QString &unit,
                          int decimals, bool logarithmic)
{
    QString name = title;
    name.remove(QLatin1Char(' '));
    name[0] = name[0].toLower();
    const size_t index = m_controls.size();
    auto *box = new QWidget(this);
    auto *label = new QLabel(title, box);
    label->setMinimumWidth(60);
    label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    auto *slider = new QSlider(Qt::Horizontal, box);
    slider->setObjectName(name + QStringLiteral("Slider"));
    slider->setRange(0, travel);
    label->setBuddy(slider);
    // Swift rounds what the slider sets to the field's decimals.
    connect(slider, &QSlider::valueChanged, this, [this, index](int position) {
        const Control &at = m_controls[index];
        const double from = at.logarithmic ? std::log(at.low) : at.low, to = at.logarithmic ? std::log(at.high) : at.high;
        const double value = from + (to - from) * position / travel, step = std::pow(10.0, at.decimals);
        const double rounded = std::round((at.logarithmic ? std::exp(value) : value) * step) / step;
        // Unchanged, the thumb stays, so arrow steps add up.
        FilterSettings current = settings();
        if (at.key(current) == rounded)
            return;
        update([&at, rounded](FilterSettings &settings) { at.key(settings) = rounded; });
        // Its value replaces the field's typing.
        at.field->setModified(false);
        synchronize();
    });
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
    m_controls.push_back(Control{std::move(key), low, high, decimals, logarithmic, slider, field});
}

void FilterSheet::noise()
{
    auto *box = new QWidget(this);
    auto *row = new QHBoxLayout(box);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(10);
    row->addWidget(new QLabel(QStringLiteral("Distribution"), box));
    m_distribution = new QButtonGroup(this);
    for (const bool gaussian : {false, true}) {
        auto *choice = new QToolButton(box);
        choice->setText(gaussian ? QStringLiteral("Gaussian") : QStringLiteral("Uniform"));
        choice->setObjectName(gaussian ? QStringLiteral("gaussianChoice") : QStringLiteral("uniformChoice"));
        choice->setCheckable(true);
        choice->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_distribution->addButton(choice, gaussian ? 1 : 0);
        row->addWidget(choice);
    }
    connect(m_distribution, &QButtonGroup::idClicked, this, [this](int id) { update([id](FilterSettings &settings) { settings.gaussian = id == 1; }); });
    m_column->addWidget(box);
    m_monochromatic = new QCheckBox(QStringLiteral("Monochromatic"), this);
    m_monochromatic->setObjectName(QStringLiteral("monochromatic"));
    connect(m_monochromatic, &QCheckBox::clicked, this, [this](bool on) { update([on](FilterSettings &settings) { settings.monochromatic = on; }); });
    m_column->addWidget(m_monochromatic);
}

void FilterSheet::background()
{
    m_column->addWidget(words(QStringLiteral("Hide the background behind a layer mask, keeping the foreground subjects. The pixels stay, "
                                             "so the background can be painted back at any time."),
                              false, this));
    // Swift's segmented picker, its label hidden.
    auto *box = new QWidget(this);
    box->setObjectName(QStringLiteral("backgroundQuality"));
    box->setAccessibleName(QStringLiteral("Quality"));
    box->setToolTip(QStringLiteral("Basic is quick; Advanced refines the mask against the layer's own detail, for hair and fur"));
    auto *row = new QHBoxLayout(box);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    m_quality = new QButtonGroup(this);
    for (const BackgroundQuality quality : allBackgroundQualities) {
        auto *choice = new QToolButton(box);
        choice->setText(rawValue(quality));
        choice->setCheckable(true);
        choice->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_quality->addButton(choice, int(quality));
        row->addWidget(choice);
    }
    connect(m_quality, &QButtonGroup::idClicked, this,
            [this](int id) { update([id](FilterSettings &settings) { settings.backgroundQuality = BackgroundQuality(id); }); });
    m_column->addWidget(box);
    const auto key = [](double FilterSettings::*field) { return [field](FilterSettings &settings) -> double & { return settings.*field; }; };
    control(QStringLiteral("Refine"), key(&FilterSettings::refineEdges), 0, 40, QStringLiteral("px"), 0, false);
    control(QStringLiteral("Contrast"), key(&FilterSettings::matteContrast), 0, 100, QStringLiteral("%"), 0, false);
    control(QStringLiteral("Shift Edge"), key(&FilterSettings::shiftEdge), -10, 10, QStringLiteral("px"), 0, false);
    const QStringList helps{QStringLiteral("Pull the mask onto the image's own edges, which recovers hair and fur"),
                            QStringLiteral("Clear the haze that leaves background showing through thin areas"),
                            QStringLiteral("Shrink the mask to drop the rim of background color around the subject, or grow it")};
    for (qsizetype index = 0; index < helps.size(); ++index) {
        QWidget *advanced = m_controls[size_t(index)].slider->parentWidget();
        advanced->setToolTip(helps[index]);
        m_shownWhen.emplace_back(advanced, [](const FilterSettings &settings) { return settings.backgroundQuality == BackgroundQuality::advanced; });
    }
}

FilterSettings FilterSheet::settings() const
{
    return m_session.filterEdit() ? m_session.filterEdit()->settings : FilterSettings();
}

void FilterSheet::update(const std::function<void(FilterSettings &)> &change)
{
    FilterSettings value = settings();
    change(value);
    m_session.updateFilter(value, !m_session.filterEdit() || m_session.filterEdit()->preview);
}

void FilterSheet::synchronize()
{
    // Closed, the panel hides the sheet until the next edit.
    if (!m_session.filterEdit())
        return;
    // Swift's onChange: the open map follows the picker's colour.
    const std::optional<PaletteColor> picked = m_session.colorPicker() ? std::optional(m_session.colorPicker()->color()) : std::nullopt;
    if (picked != m_pickerColour) {
        m_pickerColour = picked;
        m_session.previewGradientMapColor();
    }
    const FilterEdit &edit = m_session.filterEdit().value();
    for (Control &control : m_controls) {
        FilterSettings copy = edit.settings;
        const double value = control.key(copy);
        const double from = control.logarithmic ? std::log(control.low) : control.low, to = control.logarithmic ? std::log(control.high) : control.high;
        const double at = control.logarithmic ? std::log(value) : value;
        {
            // The slider shows the session's number without writing back.
            const QSignalBlocker quiet(control.slider);
            control.slider->setValue(int(std::lround((at - from) / (to - from) * travel)));
        }
        // A field being typed in keeps its typing.
        const QString text = shown(value, control.decimals, control.field->locale());
        if (!control.field->isModified() && control.field->text() != text)
            control.field->setText(text);
    }
    if (m_curves)
        m_curves->synchronize();
    if (m_gradientMap)
        m_gradientMap->synchronize();
    if (m_distribution)
        m_distribution->button(edit.settings.gaussian ? 1 : 0)->setChecked(true);
    if (m_monochromatic)
        m_monochromatic->setChecked(edit.settings.monochromatic);
    if (m_quality)
        m_quality->button(int(edit.settings.backgroundQuality))->setChecked(true);
    for (auto &[box, key] : m_flags) {
        FilterSettings copy = edit.settings;
        box->setChecked(key(copy));
    }
    for (const auto &[row, shows] : m_shownWhen)
        row->setVisible(shows(edit.settings));
    m_preview->setChecked(edit.preview);
    m_error->setText(edit.previewError.value_or(QString()));
    m_error->setVisible(edit.previewError.has_value());
    // An adjustment layer's editor ignores the selection.
    m_limited->setVisible(!m_session.adjustmentOriginal() && m_session.selection());
    // While the result is made, OK waits and says so.
    const bool working = edit.committing || edit.preparing;
    m_spinner->setVisible(working);
    m_activity->setVisible(working);
    m_activity->setText(edit.committing ? QStringLiteral("Applying…") : QStringLiteral("Working…"));
    m_ok->setEnabled(!(isAutomatic(edit.kind) && (edit.preparing || edit.previewError)));
    setEnabled(!edit.committing);
}
