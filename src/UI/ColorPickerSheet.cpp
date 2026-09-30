#include "UI/ColorPickerSheet.h"
#include "UI/NumericScrub.h"
#include "UI/KeyboardShortcuts.h"
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {
constexpr double fieldSize = 256;

// A pointer drag reports each point it passes.
class Picking : public QWidget {
public:
    Picking(const EditorSession &session, std::function<void(QPointF)> pick, QWidget *parent)
        : QWidget(parent), m_session(session), m_pick(std::move(pick))
    {
    }

protected:
    // Swift's drag answers the primary button alone.
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            m_pick(event->position());
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (event->buttons().testFlag(Qt::LeftButton))
            m_pick(event->position());
    }
    const EditorSession &m_session;

private:
    const std::function<void(QPointF)> m_pick;
};

// Swift's saturation and brightness field, its marker ringed.
class SaturationBrightness : public Picking {
public:
    using Picking::Picking;

protected:
    void paintEvent(QPaintEvent *) override
    {
        const std::optional<ColorPickerState> &picker = m_session.colorPicker();
        if (!picker)
            return;
        QPainter painter(this);
        const QRectF rect(0, 0, fieldSize, fieldSize);
        QLinearGradient across(rect.topLeft(), rect.topRight());
        across.setColorAt(0, Qt::white);
        across.setColorAt(1, PickerHSB(picker->hsb.hue, 1, 1).rgb().color());
        painter.fillRect(rect, across);
        QLinearGradient down(rect.topLeft(), rect.bottomLeft());
        down.setColorAt(0, Qt::transparent);
        down.setColorAt(1, Qt::black);
        painter.fillRect(rect, down);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(Qt::NoBrush);
        const QPointF centre(picker->hsb.saturation * fieldSize, (1 - picker->hsb.brightness) * fieldSize);
        // Black behind, then white: Swift's ring and its background.
        painter.setPen(QPen(Qt::black, 0.75));
        painter.drawEllipse(centre, 6.375, 6.375);
        painter.setPen(QPen(Qt::white, 1.5));
        painter.drawEllipse(centre, 5.25, 5.25);
        painter.setPen(QPen(QColor(0, 0, 0, 153), 1));
        painter.drawRect(rect.adjusted(0.5, 0.5, -0.5, -0.5));
    }
};

// Swift's hue strip, 360 on top; the sheet draws arrows.
class HueStrip : public Picking {
public:
    using Picking::Picking;

protected:
    void paintEvent(QPaintEvent *) override
    {
        const std::optional<ColorPickerState> &picker = m_session.colorPicker();
        if (!picker)
            return;
        QPainter painter(this);
        const QRectF strip(7, 0, 20, fieldSize);
        QLinearGradient down(strip.topLeft(), strip.bottomLeft());
        for (int step = 0; step <= 6; ++step)
            down.setColorAt(step / 6.0, PickerHSB(360 - step * 60, 1, 1).rgb().color());
        painter.fillRect(strip, down);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(0, 0, 0, 153), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(strip.adjusted(0.5, 0.5, -0.5, -0.5));
    }
};

// The new colour, rounded, a 60% black rim.
class ColorPreview : public QWidget {
public:
    ColorPreview(const EditorSession &session, QWidget *parent) : QWidget(parent), m_session(session) {}

protected:
    void paintEvent(QPaintEvent *) override
    {
        const std::optional<ColorPickerState> &picker = m_session.colorPicker();
        if (!picker)
            return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath shape;
        shape.addRoundedRect(QRectF(rect()), 5, 5);
        painter.fillPath(shape, picker->color().color());
        painter.setPen(QPen(QColor(0, 0, 0, 153), 1));
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4.5, 4.5);
    }

private:
    const EditorSession &m_session;
};

QLabel *label(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setFixedWidth(14);
    return label;
}
}

PickerField::PickerField(std::function<void()> commit, std::function<void(int)> step, QWidget *parent)
    : QLineEdit(parent), m_commit(std::move(commit)), m_step(std::move(step))
{
}

void PickerField::keyPressEvent(QKeyEvent *event)
{
    // Swift's arrowSteps: one a press, ten with Shift.
    if (m_step && (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)) {
        m_step((event->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1) * (event->key() == Qt::Key_Up ? 1 : -1));
        return;
    }
    // Return commits, then goes on to OK, the default.
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        m_commit();
    QLineEdit::keyPressEvent(event);
}

void releaseFocus(QWidget &owner)
{
    // Destroyed focused, it would commit into freed members.
    if (QWidget *focus = owner.focusWidget(); focus && focus->hasFocus())
        focus->clearFocus();
}

void PickerField::focusOutEvent(QFocusEvent *event)
{
    // A menu or popup borrows the focus: the typing stays.
    if (event->reason() != Qt::MenuBarFocusReason && event->reason() != Qt::PopupFocusReason)
        m_commit();
    QLineEdit::focusOutEvent(event);
}

ColorPickerSheet::ColorPickerSheet(EditorSession &session, std::function<void(bool)> finish, QWidget *parent)
    : QWidget(parent), m_session(session),
      m_field(new SaturationBrightness(session, [this](QPointF point) {
          pick([point](PickerHSB &hsb) {
              hsb.saturation = std::clamp(point.x() / fieldSize, 0.0, 1.0);
              hsb.brightness = 1 - std::clamp(point.y() / fieldSize, 0.0, 1.0);
          });
      }, this)),
      m_hue(new HueStrip(session, [this](QPointF point) {
          pick([point](PickerHSB &hsb) { hsb.hue = (1 - std::clamp(point.y() / fieldSize, 0.0, 1.0)) * 360; });
      }, this)),
      m_preview(new ColorPreview(session, this)), m_channels{channel(0), channel(1), channel(2)},
      m_hex(new PickerField([this] { commitHex(); }, nullptr, this)),
      m_ok(new QPushButton(QStringLiteral("OK"), this)), m_cancel(new QPushButton(QStringLiteral("Cancel"), this))
{
    m_field->setObjectName(QStringLiteral("saturationBrightness"));
    m_field->setFixedSize(int(fieldSize), int(fieldSize));
    m_field->setAccessibleName(QStringLiteral("Saturation and brightness"));
    m_hue->setObjectName(QStringLiteral("hueStrip"));
    m_hue->setFixedSize(34, int(fieldSize));
    m_hue->setAccessibleName(QStringLiteral("Hue"));
    m_preview->setObjectName(QStringLiteral("newColor"));
    m_preview->setFixedSize(64, 64);
    m_preview->setAccessibleName(QStringLiteral("New color"));
    m_ok->setObjectName(QStringLiteral("pickerOK"));
    m_ok->setDefault(true);
    // Swift's configuredNativeShortcut: Return and Escape, as remapped.
    NativeShortcut::bind(*this, m_ok, m_cancel);
    m_ok->setFixedWidth(90);
    m_cancel->setObjectName(QStringLiteral("pickerCancel"));
    m_cancel->setAutoDefault(false);
    m_cancel->setFixedWidth(90);
    connect(m_ok, &QPushButton::clicked, this, [finish] { finish(true); });
    connect(m_cancel, &QPushButton::clicked, this, [finish] { finish(false); });
    auto *buttons = new QVBoxLayout;
    buttons->setSpacing(8);
    buttons->addWidget(m_ok);
    buttons->addWidget(m_cancel);
    buttons->addStretch();
    auto *top = new QHBoxLayout;
    top->setSpacing(16);
    top->addWidget(m_preview, 0, Qt::AlignTop);
    top->addLayout(buttons);
    auto *fields = new QGridLayout;
    fields->setHorizontalSpacing(8);
    fields->setVerticalSpacing(6);
    for (int index = 0; index < 3; ++index) {
        QLabel *name = label(QString(QStringLiteral("RGB").at(index)), this);
        // Swift's scrubbable channel: whole levels, replacing typing.
        new NumericScrub(name, {.sensitivity = 1, .low = 0, .high = 255, .step = 1,
                                .value = [this, index] {
                                    const PaletteColor color = m_session.colorPicker().value().color();
                                    return std::round((index == 0 ? color.red : index == 1 ? color.green : color.blue) * 255);
                                },
                                .set = [this, index](double value) {
                                    setChannel(index, value);
                                    m_channels[size_t(index)]->setModified(false);
                                    synchronize();
                                }});
        fields->addWidget(name, index, 0);
        fields->addWidget(m_channels[size_t(index)], index, 1, Qt::AlignLeft);
    }
    m_hex->setObjectName(QStringLiteral("hex"));
    m_hex->setPlaceholderText(QStringLiteral("Hex"));
    m_hex->setAccessibleName(QStringLiteral("Hex color"));
    m_hex->setFixedWidth(84);
    m_hex->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    fields->addWidget(label(QStringLiteral("#"), this), 3, 0);
    fields->addWidget(m_hex, 3, 1, Qt::AlignLeft);
    auto *hint = new QLabel(QStringLiteral("Click the canvas to sample"), this);
    QFont small = hint->font();
    small.setPixelSize(10);
    hint->setFont(small);
    hint->setForegroundRole(QPalette::PlaceholderText);
    auto *column = new QVBoxLayout;
    column->setSpacing(0);
    column->addLayout(top);
    column->addStretch();
    column->addSpacing(12);
    column->addLayout(fields);
    column->addSpacing(8);
    column->addWidget(hint);
    auto *side = new QWidget(this);
    side->setFixedSize(180, int(fieldSize));
    side->setLayout(column);
    column->setContentsMargins(0, 0, 0, 0);
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(20, 20, 20, 20);
    row->setSpacing(14);
    row->addWidget(m_field, 0, Qt::AlignTop);
    row->addWidget(m_hue, 0, Qt::AlignTop);
    row->addWidget(side, 0, Qt::AlignTop);
    connect(&m_session, &EditorSession::changed, this, &ColorPickerSheet::synchronize);
    synchronize();
}

PickerField *ColorPickerSheet::channel(int index)
{
    auto *field = new PickerField([this, index] {
        // Typed numbers clamp, as the binding; overflows are 255.
        PickerField &edited = *m_channels[size_t(index)];
        if (edited.isModified() && !edited.text().isEmpty()) {
            bool fits = false;
            const qlonglong typed = edited.text().toLongLong(&fits);
            setChannel(index, fits ? double(typed) : 255);
        }
        synchronize();
    }, [this, index](int step) {
        const PaletteColor color = m_session.colorPicker().value().color();
        const double value = std::round((index == 0 ? color.red : index == 1 ? color.green : color.blue) * 255);
        setChannel(index, value + step);
        m_channels[size_t(index)]->setModified(false);
        synchronize();
    }, this);
    field->setObjectName(QString(QStringLiteral("rgb").at(index)));
    // Swift's title shows while empty; the label names the channel.
    field->setPlaceholderText(QString(QStringLiteral("RGB").at(index)));
    field->setAccessibleName(QStringList{QStringLiteral("Red"), QStringLiteral("Green"), QStringLiteral("Blue")}.at(index));
    field->setFixedWidth(52);
    field->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9]*")), field));
    return field;
}

void ColorPickerSheet::change(const std::function<void(PickerHSB &)> &edit)
{
    const std::optional<ColorPickerState> &picker = m_session.colorPicker();
    if (!picker)
        return;
    PickerHSB hsb = picker->hsb;
    edit(hsb);
    m_session.setColorPickerHSB(hsb);
}

void ColorPickerSheet::pick(const std::function<void(PickerHSB &)> &edit)
{
    change(edit);
    for (PickerField *channel : m_channels)
        channel->setModified(false);
    synchronize();
}

// Swift's channel binding: the 8-bit colour, one channel set.
void ColorPickerSheet::setChannel(int channel, double value)
{
    const std::optional<ColorPickerState> &picker = m_session.colorPicker();
    if (!picker)
        return;
    PaletteColor rgb = picker->color();
    (channel == 0 ? rgb.red : channel == 1 ? rgb.green : rgb.blue) = std::clamp(value, 0.0, 255.0) / 255;
    change([&rgb](PickerHSB &hsb) { hsb.setRGB(rgb); });
}

void ColorPickerSheet::commitHex()
{
    if (const std::optional<PaletteColor> parsed = PaletteColor::fromHex(m_hex->text()))
        change([&parsed](PickerHSB &hsb) { hsb.setRGB(*parsed); });
    if (m_session.colorPicker())
        m_hex->setText(m_session.colorPicker()->color().hex());
}

void ColorPickerSheet::synchronize()
{
    const std::optional<ColorPickerState> &picker = m_session.colorPicker();
    if (!picker)
        return;
    const PaletteColor color = picker->color();
    const std::array<double, 3> values{color.red, color.green, color.blue};
    // An entry being typed in keeps its typing.
    for (size_t index = 0; index < 3; ++index) {
        if (!m_channels[index]->hasFocus() || !m_channels[index]->isModified())
            m_channels[index]->setText(QString::number(std::lround(values[index] * 255)));
    }
    // Swift's hex draft follows only while its field is unfocused.
    if (!m_hex->hasFocus())
        m_hex->setText(color.hex());
    // Qt's plain widgets hold no value: the degrees describe it.
    m_hue->setAccessibleDescription(QStringLiteral("%1 degrees").arg(std::lround(picker->hsb.hue)));
    m_field->update();
    // The arrows reach five points past the strip.
    update(m_hue->geometry().adjusted(0, -5, 0, 5));
    m_preview->update();
}

// The strip's arrows overflow it, as Swift's; widgets clip.
void ColorPickerSheet::paintEvent(QPaintEvent *)
{
    const std::optional<ColorPickerState> &picker = m_session.colorPicker();
    if (!picker)
        return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF strip(m_hue->geometry());
    const double y = strip.top() + (1 - picker->hsb.hue / 360) * fieldSize;
    QPainterPath arrows;
    arrows.addPolygon(QPolygonF({QPointF(strip.left(), y - 5), QPointF(strip.left() + 7, y), QPointF(strip.left(), y + 5)}));
    arrows.addPolygon(QPolygonF({QPointF(strip.right(), y - 5), QPointF(strip.right() - 7, y), QPointF(strip.right(), y + 5)}));
    painter.fillPath(arrows, palette().color(QPalette::WindowText));
}

ColorPickerPanelController::ColorPickerPanelController(QWidget &owner) : m_panel(identifier(), owner) {}

void ColorPickerPanelController::show(const ColorPickerState &state, EditorSession &session)
{
    // Its close button cancels; closing none does nothing.
    m_panel.onClose = [&session] { session.closeColorPicker(false); };
    m_panel.show(state.target.title(), new ColorPickerSheet(session, [&session](bool commit) { session.closeColorPicker(commit); }));
}

void ColorPickerPanelController::close()
{
    m_panel.close();
}

void ColorPickerPanelController::refocus()
{
    FloatingPanel::refocus(identifier());
}

ColorPickerSheet::~ColorPickerSheet()
{
    releaseFocus(*this);
}
