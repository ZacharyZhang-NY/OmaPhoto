#include "UI/ColorRangeSheet.h"
#include "UI/ColorPickerSheet.h"
#include "UI/KeyboardShortcuts.h"
#include "UI/NumericScrub.h"
#include "UI/SampleButton.h"
#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include <cmath>

namespace {
QString rangeHelp(HueSampleMode mode)
{
    switch (mode) {
    case HueSampleMode::replace: return QStringLiteral("Click the image to select that color");
    case HueSampleMode::add: return QStringLiteral("Click the image to add that color to the selection");
    case HueSampleMode::remove: return QStringLiteral("Click the image to take that color out of the selection");
    }
    throw std::logic_error("unknown sample mode");
}

// The selection in black and white, shaped like the canvas.
class RangePreview : public QWidget {
public:
    RangePreview(EditorSession &session, QWidget *parent) : QWidget(parent), m_session(session)
    {
        const QSizeF limit = ColorRangeEdit::previewSize;
        const QSizeF size = m_session.colorRange() ? QSizeF(m_session.colorRange()->image.size()) : limit;
        const double scale = std::min(limit.width() / size.width(), limit.height() / size.height());
        setFixedSize(int(std::round(size.width() * scale)), int(std::round(size.height() * scale)));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::black);
        if (m_session.colorRange() && !m_session.colorRange()->preview.isNull()) {
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawImage(QRectF(rect()), m_session.colorRange()->preview);
        }
        // Swift's strokeBorder: a white line inside, at 20%.
        painter.setPen(QPen(QColor(255, 255, 255, 51), 1));
        painter.drawRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
    }

private:
    EditorSession &m_session;
};
}

ColorRangeSheet::ColorRangeSheet(EditorSession &session, QWidget *parent)
    : QWidget(parent), m_session(session), m_preview(new RangePreview(session, this)), m_caption(new QLabel(this)), m_slider(new QSlider(Qt::Horizontal, this)),
      m_field(new PickerField(
          [this] {
              bool number = false;
              const double typed = m_field->locale().toDouble(m_field->text(), &number);
              if (m_field->isModified() && number && std::isfinite(typed))
                  m_session.setColorRangeFuzziness(typed);
              m_field->setModified(false);
              synchronize();
          },
          nullptr, this)),
      m_invert(new QCheckBox(QStringLiteral("Invert"), this)), m_error(new QLabel(this)), m_ok(new QPushButton(QStringLiteral("OK"), this))
{
    setFixedWidth(340);
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(24, 24, 24, 24);
    column->setSpacing(16);
    auto *droppers = new QHBoxLayout;
    droppers->setSpacing(6);
    for (size_t index = 0; index < allHueSampleModes.size(); ++index) {
        const HueSampleMode mode = allHueSampleModes[index];
        auto *button = new SampleButton([mode](QPainter &painter, const QColor &ink) { sampleEyedropper(painter, mode, ink); }, this);
        button->setObjectName(QStringLiteral("colorRange") + rawValue(mode));
        button->setToolTip(rangeHelp(mode));
        button->setAccessibleName(rawValue(mode) + QStringLiteral(" color"));
        connect(button, &QAbstractButton::clicked, this, [this, mode] { m_session.setColorRangeSampleMode(mode); });
        m_eyedroppers[index] = button;
        droppers->addWidget(button);
    }
    droppers->addStretch(1);
    column->addLayout(droppers);
    column->addWidget(m_preview, 0, Qt::AlignHCenter);
    m_caption->setObjectName(QStringLiteral("colorRangeCaption"));
    m_caption->setWordWrap(true);
    QFont callout = m_caption->font();
    callout.setPixelSize(12);
    m_caption->setFont(callout);
    m_caption->setForegroundRole(QPalette::PlaceholderText);
    column->addWidget(m_caption);
    // Fuzziness: a scrubbable title, the slider, the field.
    auto *row = new QWidget(this);
    row->setToolTip(QStringLiteral("How far a color may be from the picked ones and still be selected"));
    auto *fuzziness = new QHBoxLayout(row);
    fuzziness->setContentsMargins(0, 0, 0, 0);
    fuzziness->setSpacing(10);
    auto *title = new QLabel(QStringLiteral("Fuzziness"), row);
    new NumericScrub(title, {.sensitivity = 1, .low = ColorRangeEdit::fuzzinessLow, .high = ColorRangeEdit::fuzzinessHigh, .step = std::nullopt,
                             .value = [this] { return m_session.colorRange() ? m_session.colorRange()->fuzziness : 40; },
                             .set = [this](double value) {
                                 // Its value replaces the field's typing.
                                 m_field->setModified(false);
                                 m_session.setColorRangeFuzziness(value);
                             }});
    title->setBuddy(m_slider);
    m_slider->setObjectName(QStringLiteral("fuzzinessSlider"));
    m_slider->setRange(int(ColorRangeEdit::fuzzinessLow), int(ColorRangeEdit::fuzzinessHigh));
    connect(m_slider, &QSlider::valueChanged, this, [this](int value) {
        m_field->setModified(false);
        m_session.setColorRangeFuzziness(value);
    });
    m_field->setObjectName(QStringLiteral("fuzzinessField"));
    m_field->setAccessibleName(QStringLiteral("Fuzziness"));
    m_field->setPlaceholderText(QStringLiteral("Fuzziness"));
    m_field->setAlignment(Qt::AlignRight);
    m_field->setFixedWidth(48);
    fuzziness->addWidget(title);
    fuzziness->addWidget(m_slider, 1);
    fuzziness->addWidget(m_field);
    column->addWidget(row);
    m_invert->setObjectName(QStringLiteral("colorRangeInvert"));
    m_invert->setToolTip(QStringLiteral("Select everything except those colors, such as all but a green screen"));
    connect(m_invert, &QCheckBox::clicked, this, [this](bool checked) { m_session.setColorRangeInvert(checked); });
    column->addWidget(m_invert);
    m_error->setObjectName(QStringLiteral("colorRangeError"));
    m_error->setWordWrap(true);
    m_error->setForegroundRole(QPalette::BrightText);
    column->addWidget(m_error);
    auto *divider = new QFrame(this);
    divider->setFrameShape(QFrame::HLine);
    divider->setForegroundRole(QPalette::Mid);
    column->addWidget(divider);
    auto *buttons = new QHBoxLayout;
    auto *cancel = new QPushButton(QStringLiteral("Cancel"), this);
    cancel->setAutoDefault(false);
    connect(cancel, &QPushButton::clicked, this, [this] { m_session.cancelColorRange(); });
    m_ok->setDefault(true);
    connect(m_ok, &QPushButton::clicked, this, [this] { m_session.commitColorRange(); });
    buttons->addWidget(cancel);
    buttons->addStretch(1);
    buttons->addWidget(m_ok);
    column->addLayout(buttons);
    NativeShortcut::bind(*this, m_ok, cancel);
    connect(&m_session, &EditorSession::changed, this, &ColorRangeSheet::synchronize);
    synchronize();
}

ColorRangeSheet::~ColorRangeSheet()
{
    releaseFocus(*this);
}

void ColorRangeSheet::synchronize()
{
    const std::optional<ColorRangeEdit> &edit = m_session.colorRange();
    if (!edit)
        return;
    for (size_t index = 0; index < m_eyedroppers.size(); ++index)
        m_eyedroppers[index]->setChecked(edit->effectiveMode() == allHueSampleModes[index]);
    m_caption->setText(edit->hasColors() ? QStringLiteral("Shift-click adds a color, Alt-click takes one away.")
                                         : QStringLiteral("Click the image to pick the color to select."));
    {
        const QSignalBlocker quiet(m_slider);
        m_slider->setValue(int(edit->fuzziness));
    }
    if (!m_field->hasFocus() || !m_field->isModified())
        m_field->setText(m_field->locale().toString(edit->fuzziness, 'f', 0));
    m_invert->setChecked(edit->invert);
    m_error->setText(edit->error.value_or(QString()));
    m_error->setVisible(edit->error.has_value());
    m_preview->update();
}
