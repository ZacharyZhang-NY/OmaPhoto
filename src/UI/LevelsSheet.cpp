#include "UI/LevelsSheet.h"
#include "UI/KeyboardShortcuts.h"
#include "Rendering/EyedropperIcon.h"
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <cmath>

namespace {
// The five entries: Swift's names, fields and decimals.
struct Entry {
    const char *name;
    double LevelRange::*value;
    int decimals;
};
constexpr std::array<Entry, 5> entries{{{"Input black", &LevelRange::black, 0},
                                        {"Gamma", &LevelRange::gamma, 2},
                                        {"Input white", &LevelRange::white, 0},
                                        {"Output black", &LevelRange::outputBlack, 0},
                                        {"Output white", &LevelRange::outputWhite, 0}}};

// SwiftUI's gray, and its dark red, green and blue.
const QColor gray(142, 142, 147);

QColor colour(LevelsChannel channel)
{
    switch (channel) {
    case LevelsChannel::rgb: return gray;
    case LevelsChannel::red: return QColor(255, 66, 69);
    case LevelsChannel::green: return QColor(48, 209, 88);
    case LevelsChannel::blue: return QColor(0, 145, 255);
    }
    throw std::logic_error("no such channel");
}

// Swift's .caption: ten points, in the secondary ink unless primary.
QLabel *caption(const QString &text, QWidget *parent, QPalette::ColorRole ink = QPalette::PlaceholderText)
{
    auto *label = new QLabel(text, parent);
    QFont small = label->font();
    small.setPixelSize(10);
    label->setFont(small);
    label->setForegroundRole(ink);
    return label;
}

// The channel's bins, scaled so spikes cannot flatten the rest.
class Histogram : public QWidget {
public:
    Histogram(const EditorSession &session, QWidget *parent)
        : QWidget(parent), m_session(session), m_loading(caption(QStringLiteral("Loading histogram…"), this, QPalette::WindowText))
    {
        setFixedHeight(150);
        setToolTip(QStringLiteral("Linear histogram with automatic vertical scaling. Tall spikes may extend beyond the graph; all tones from 0 "
                                  "to 255 remain included."));
        m_loading->move(8, 8);
        connect(&session, &EditorSession::changed, this, [this] { synchronize(); });
        synchronize();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(0, 0, 0, 64));
        const std::optional<LevelsEdit> &edit = m_session.levels();
        if (!edit)
            return;
        const std::array<double, 256> &bins = edit->histogram[size_t(edit->settings.channel)];
        const double peak = LevelsHistogramDisplay::scale(bins);
        if (peak <= 0)
            return;
        QPainterPath bars;
        bars.setFillRule(Qt::WindingFill);
        const double step = width() / 256.0;
        for (size_t index = 0; index < 256; ++index) {
            const double tall = height() * std::min(1.0, std::max(0.0, bins[index] / peak));
            bars.addRect(QRectF(double(index) * step, height() - tall, step + 0.1, tall));
        }
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillPath(bars, colour(edit->settings.channel));
    }

private:
    void synchronize()
    {
        const std::optional<LevelsEdit> &edit = m_session.levels();
        if (!edit)
            return;
        setAccessibleName(QStringLiteral("Original %1 histogram").arg(rawValue(edit->settings.channel)));
        m_loading->setVisible(!edit->histogramReady);
        update();
    }

    const EditorSession &m_session;
    QLabel *const m_loading;
};

// Black to white across, over the output handles.
class Gradient : public QWidget {
public:
    explicit Gradient(QWidget *parent) : QWidget(parent) { setFixedHeight(14); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        QLinearGradient across(QPointF(0, 0), QPointF(width(), 0));
        across.setColorAt(0, Qt::black);
        across.setColorAt(1, Qt::white);
        painter.fillRect(rect(), across);
    }
};

// Swift's handle: a triangle in a 22 by 20 frame.
class Handle : public QWidget {
public:
    Handle(const QString &name, const QColor &fill, const QWidget &track, std::function<void(double)> drag)
        : QWidget(track.parentWidget()), m_fill(fill), m_track(track), m_drag(std::move(drag))
    {
        setAccessibleName(name);
        resize(22, 20);
    }
    // Centred on the tone, nine points down the track.
    void place(double tone) { move(int(std::round(m_track.x() + tone / 255 * m_track.width() - 11)), m_track.y() - 1); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath triangle(QPointF(11, 4.75));
        triangle.lineTo(17, 15.25);
        triangle.lineTo(5, 15.25);
        triangle.closeSubpath();
        // Swift's half-point gray shadow: Qt has no blur.
        painter.strokePath(triangle, QPen(gray, 1));
        painter.fillPath(triangle, m_fill);
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            drag(event->position());
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (event->buttons().testFlag(Qt::LeftButton))
            drag(event->position());
    }

private:
    // The pointer's tone along the track, as Swift's named space.
    void drag(QPointF point) { m_drag((mapToParent(point).x() - m_track.x()) / m_track.width() * 255); }

    const QColor m_fill;
    const QWidget &m_track;
    const std::function<void(double)> m_drag;
};

// Swift's handles view: tones across, each triangle on top.
class Track : public QWidget {
public:
    Track(const EditorSession &session, bool output, const std::function<void(size_t, double)> &drag, QWidget *parent)
        : QWidget(parent), m_session(session), m_output(output)
    {
        setFixedHeight(20);
        const QStringList names = output ? QStringList{QStringLiteral("Output black"), QStringLiteral("Output white")}
                                         : QStringList{QStringLiteral("Input black"), QStringLiteral("Gamma"), QStringLiteral("Input white")};
        for (qsizetype index = 0; index < names.size(); ++index) {
            const QColor fill = index == 0 ? QColor(Qt::black) : index + 1 == names.size() ? QColor(Qt::white) : gray;
            m_handles.push_back(new Handle(names[index], fill, *this, [drag, index](double tone) { drag(size_t(index), tone); }));
        }
        connect(&session, &EditorSession::changed, this, [this] { place(); });
    }

protected:
    void moveEvent(QMoveEvent *) override { place(); }
    void resizeEvent(QResizeEvent *) override { place(); }

private:
    void place()
    {
        const std::optional<LevelsEdit> &edit = m_session.levels();
        if (!edit)
            return;
        const LevelRange range = edit->settings.current();
        const std::vector<double> tones = m_output ? std::vector{range.outputBlack, range.outputWhite}
                                                   : std::vector{range.black, range.black + (range.white - range.black) * std::pow(0.5, range.gamma), range.white};
        for (size_t index = 0; index < tones.size(); ++index)
            m_handles[index]->place(tones[index]);
    }

    const EditorSession &m_session;
    const bool m_output;
    std::vector<Handle *> m_handles;
};

// Swift's drag: whole tones for ends, a fraction for gamma.
void dragHandle(LevelRange &range, bool output, size_t index, double tone)
{
    const double x = std::min(255.0, std::max(0.0, tone));
    if (output)
        (index == 0 ? range.outputBlack : range.outputWhite) = std::round(x);
    else if (index == 0)
        range.black = std::min(range.white - 1, std::round(x));
    else if (index == 2)
        range.white = std::max(range.black + 1, std::round(x));
    else
        range.gamma = std::log(std::min(0.999, std::max(0.001, (x - range.black) / (range.white - range.black)))) / std::log(0.5);
}

// Swift's HStack: spread by Spacers, or packed to the left.
QHBoxLayout *row(std::initializer_list<QWidget *> widgets, bool spread)
{
    auto *layout = new QHBoxLayout;
    for (QWidget *widget : widgets) {
        if (spread && layout->count() > 0)
            layout->addStretch(1);
        layout->addWidget(widget);
    }
    if (!spread)
        layout->addStretch(1);
    return layout;
}

QVBoxLayout *stack(std::initializer_list<QWidget *> widgets, int spacing)
{
    auto *layout = new QVBoxLayout;
    layout->setSpacing(spacing);
    for (QWidget *widget : widgets)
        layout->addWidget(widget);
    return layout;
}
}

LevelsSheet::LevelsSheet(EditorSession &session, QWidget *parent)
    : QWidget(parent), m_session(session), m_channel(new QComboBox(this)), m_histogram(new Histogram(session, this)),
      m_input(new Track(session, false, [this](size_t index, double tone) { dragTo(false, index, tone); }, this)),
      m_gradient(new Gradient(this)), m_output(new Track(session, true, [this](size_t index, double tone) { dragTo(true, index, tone); }, this)),
      m_fields{field(0), field(1), field(2), field(3), field(4)}, m_samples{new QPushButton(this), new QPushButton(this), new QPushButton(this)},
      m_sampleHint(caption(QString(), this)), m_autos{new QPushButton(this), new QPushButton(this), new QPushButton(this)},
      m_preview(new QCheckBox(QStringLiteral("Preview"), this)), m_reset(new QPushButton(QStringLiteral("Reset"), this)),
      m_caption(caption(QString(), this)), m_cancel(new QPushButton(QStringLiteral("Cancel"), this)), m_spinner(new QProgressBar(this)),
      m_ok(new QPushButton(QStringLiteral("OK"), this))
{
    setFixedWidth(440);
    for (const LevelsChannel channel : allLevelsChannels)
        m_channel->addItem(rawValue(channel));
    connect(m_channel, &QComboBox::activated, this,
            [this](int index) { change([index](LevelsSettings &settings) { settings.channel = allLevelsChannels[size_t(index)]; }); });
    auto *picker = new QWidget(this);
    picker->setFixedWidth(180);
    auto *pickerRow = new QHBoxLayout(picker);
    pickerRow->setContentsMargins(0, 0, 0, 0);
    // The label names the picker, as Swift's Picker title.
    auto *title = new QLabel(QStringLiteral("Channel"), picker);
    title->setBuddy(m_channel);
    pickerRow->addWidget(title);
    pickerRow->addWidget(m_channel, 1);
    m_histogram->setObjectName(QStringLiteral("levelsHistogram"));
    m_input->setObjectName(QStringLiteral("levelsInputHandles"));
    m_output->setObjectName(QStringLiteral("levelsOutputHandles"));
    for (size_t index = 0; index < 3; ++index) {
        m_samples[index]->setText(rawValue(allLevelsSamples[index]));
        m_samples[index]->setCheckable(true);
        m_samples[index]->setAutoDefault(false);
        // A second click puts the eyedropper away.
        connect(m_samples[index], &QPushButton::clicked, this, [this, index] {
            const std::optional<LevelsSample> mode = allLevelsSamples[index];
            m_session.setLevelsSampleMode(m_session.levels().value().sampleMode == mode ? std::nullopt : mode);
        });
        m_autos[index]->setText(rawValue(allLevelsAutos[index]));
        m_autos[index]->setAutoDefault(false);
        connect(m_autos[index], &QPushButton::clicked, this, [this, index] { m_session.autoLevels(allLevelsAutos[index]); });
    }
    m_sampleHint->setWordWrap(true);
    new NativeShortcut(ShortcutChord(QStringLiteral("p"), 2), *this, *m_preview);
    connect(m_preview, &QCheckBox::clicked, this, [this](bool on) { m_session.updateLevels(m_session.levels().value().settings, on); });
    m_reset->setAutoDefault(false);
    connect(m_reset, &QPushButton::clicked, this, [this] {
        m_session.setLevelsSampleMode(std::nullopt);
        change([](LevelsSettings &settings) { settings = LevelsSettings(); });
    });
    m_cancel->setAutoDefault(false);
    m_ok->setDefault(true);
    // Swift's configuredNativeShortcut: Return and Escape, as remapped.
    NativeShortcut::bind(*this, m_ok, m_cancel);
    connect(m_cancel, &QPushButton::clicked, this, [this] { m_session.cancelLevels(); });
    connect(m_ok, &QPushButton::clicked, this, [this] { m_session.commitLevels(); });
    m_spinner->setRange(0, 0);
    m_spinner->setFixedWidth(40);
    m_spinner->setTextVisible(false);
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(24, 24, 24, 24);
    column->setSpacing(16);
    column->addWidget(picker);
    column->addLayout(stack({m_histogram, m_input}, 0));
    column->addLayout(row({m_fields[0]->parentWidget(), m_fields[1]->parentWidget(), m_fields[2]->parentWidget()}, true));
    column->addLayout(stack({m_gradient, m_output}, 0));
    column->addLayout(row({m_fields[3]->parentWidget(), m_fields[4]->parentWidget()}, true));
    column->addLayout(row({caption(QStringLiteral("Sample"), this), m_samples[0], m_samples[1], m_samples[2]}, false));
    column->addWidget(m_sampleHint);
    QVBoxLayout *automatic = stack({caption(QStringLiteral("Auto"), this)}, 6);
    automatic->addLayout(row({m_autos[0], m_autos[1], m_autos[2]}, false));
    column->addLayout(automatic);
    column->addLayout(row({m_preview, m_reset}, true));
    column->addWidget(m_caption);
    auto *divider = new QFrame(this);
    divider->setFrameShape(QFrame::HLine);
    divider->setForegroundRole(QPalette::Mid);
    column->addWidget(divider);
    QHBoxLayout *buttons = row({m_cancel, m_spinner}, true);
    buttons->addWidget(m_ok);
    column->addLayout(buttons);
    connect(&m_session, &EditorSession::changed, this, &LevelsSheet::synchronize);
    synchronize();
}

PickerField *LevelsSheet::field(size_t index)
{
    const QString name = QString::fromLatin1(entries[index].name);
    auto *box = new QWidget(this);
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5);
    layout->addWidget(caption(name, box));
    // A number in the user's locale applies; other text reverts.
    auto *entry = new PickerField([this, index] {
        PickerField &edited = *m_fields[index];
        bool number = false;
        const double typed = edited.locale().toDouble(edited.text(), &number);
        if (edited.isModified() && number) {
            change([&](LevelsSettings &settings) {
                LevelRange range = settings.current();
                range.*entries[index].value = typed;
                settings.setCurrent(range);
            });
        }
        edited.setModified(false);
        synchronize();
    }, nullptr, box);
    entry->setObjectName(QStringLiteral("levels") + QString(name).remove(QLatin1Char(' ')));
    entry->setPlaceholderText(name);
    entry->setAccessibleName(name);
    entry->setAlignment(Qt::AlignRight);
    entry->setFixedWidth(80);
    layout->addWidget(entry);
    return entry;
}

void LevelsSheet::dragTo(bool output, size_t index, double tone)
{
    change([&](LevelsSettings &settings) {
        LevelRange range = settings.current();
        dragHandle(range, output, index, tone);
        settings.setCurrent(range);
    });
    m_fields[output ? 3 + index : index]->setModified(false);
    synchronize();
}

void LevelsSheet::change(const std::function<void(LevelsSettings &)> &edit)
{
    const std::optional<LevelsEdit> &levels = m_session.levels();
    if (!levels)
        return;
    LevelsSettings settings = levels->settings;
    edit(settings);
    m_session.updateLevels(settings, levels->preview);
}

void LevelsSheet::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        synchronize();
}

void LevelsSheet::synchronize()
{
    // Closed, the panel hides the sheet until the next edit.
    const std::optional<LevelsEdit> &edit = m_session.levels();
    if (!edit)
        return;
    const LevelRange range = edit->settings.current();
    m_channel->setCurrentIndex(int(edit->settings.channel));
    // An entry being typed in keeps its typing.
    for (size_t index = 0; index < entries.size(); ++index) {
        const QString shown = m_fields[index]->locale().toString(range.*entries[index].value, 'f', entries[index].decimals);
        if (!m_fields[index]->isModified() && m_fields[index]->text() != shown)
            m_fields[index]->setText(shown);
    }
    for (size_t index = 0; index < 3; ++index) {
        const bool chosen = edit->sampleMode == allLevelsSamples[index];
        m_samples[index]->setChecked(chosen);
        m_samples[index]->setIcon(EyedropperIcon::icon(palette().color(chosen ? QPalette::Highlight : QPalette::PlaceholderText), devicePixelRatioF()));
        m_autos[index]->setEnabled(edit->histogramReady);
    }
    m_sampleHint->setVisible(edit->sampleMode.has_value());
    if (edit->sampleMode)
        m_sampleHint->setText(QStringLiteral("Click the original layer to set %1. Click the eyedropper again to stop.")
                                  .arg(rawValue(*edit->sampleMode).toLower()));
    m_preview->setChecked(edit->preview);
    m_caption->setText(m_session.adjustmentOriginal() ? QStringLiteral("Underlying pixels · alpha-weighted histogram")
                       : m_session.selection()        ? QStringLiteral("Original pixels · selection and alpha-weighted histogram")
                                                      : QStringLiteral("Original pixels · alpha-weighted histogram"));
    m_spinner->setVisible(edit->committing);
    setEnabled(!edit->committing);
}
