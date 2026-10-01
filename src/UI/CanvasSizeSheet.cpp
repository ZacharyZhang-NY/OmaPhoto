#include "UI/CanvasSizeSheet.h"
#include "Document/DocumentLimits.h"
#include "UI/KeyboardShortcuts.h"
#include "UI/ByteCounts.h"
#include "UI/ColorPickerSheet+Dialog.h"
#include "UI/ColorPickerSheet.h"
#include "UI/NumericScrub.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

namespace {
const std::array<QString, 9> anchorNames = {QStringLiteral("Top left"),     QStringLiteral("Top center"),    QStringLiteral("Top right"),
                                            QStringLiteral("Middle left"),  QStringLiteral("Center"),        QStringLiteral("Middle right"),
                                            QStringLiteral("Bottom left"),  QStringLiteral("Bottom center"), QStringLiteral("Bottom right")};

// Swift's scrubRange: the pixel limits, shown in the draft's unit.
std::pair<double, double> scrubRange(const CanvasSizeDraft &draft, bool widthAxis)
{
    const double original = double(widthAxis ? draft.originalWidth : draft.originalHeight);
    const double other = double(widthAxis ? draft.originalHeight : draft.originalWidth);
    const double lower = draft.locked ? std::max(1.0, original / other) : 1.0;
    const double upper = draft.locked ? std::min(30000.0, 30000 * original / other) : 30000.0;
    const auto displayed = [&](double pixels) {
        const double difference = pixels - (draft.relative ? original : 0);
        switch (draft.unit) {
        case CanvasUnit::pixels: return difference;
        case CanvasUnit::percent: return difference / original * 100;
        case CanvasUnit::inches: return difference / draft.resolution;
        case CanvasUnit::centimeters: return difference / draft.resolution * 2.54;
        }
        throw std::logic_error("no such unit");
    };
    return {displayed(lower), displayed(upper)};
}

// Swift's scrubSensitivity: a point is a pixel in any unit.
double scrubSensitivity(const CanvasSizeDraft &draft, bool widthAxis)
{
    switch (draft.unit) {
    case CanvasUnit::pixels: return 1;
    case CanvasUnit::percent: return 100 / double(widthAxis ? draft.originalWidth : draft.originalHeight);
    case CanvasUnit::inches: return 1 / draft.resolution;
    case CanvasUnit::centimeters: return 2.54 / draft.resolution;
    }
    throw std::logic_error("no such unit");
}

QLabel *text(const QString &words, int pixels, QFont::Weight weight, QPalette::ColorRole role, QWidget *parent)
{
    auto *label = new QLabel(words, parent);
    label->setTextFormat(Qt::PlainText);
    QFont font = label->font();
    font.setPixelSize(pixels);
    font.setWeight(weight);
    label->setFont(font);
    label->setForegroundRole(role);
    return label;
}

// Swift's `.number`: three places at most, none trailing, ties even.
QString shown(double value, QLocale locale)
{
    locale.setNumberOptions(QLocale::OmitGroupSeparator);
    QString number = locale.toString(std::nearbyint(value * 1000) / 1000, 'f', 3);
    while (number.endsWith(locale.zeroDigit()))
        number.chop(locale.zeroDigit().size());
    if (number.endsWith(locale.decimalPoint()))
        number.chop(locale.decimalPoint().size());
    return number;
}

QString grouped(qint64 value)
{
    return QLocale(QLocale::English, QLocale::UnitedStates).toString(value);
}

// Swift's anchor: a circle, filled with the accent when chosen.
class AnchorButton : public QToolButton {
public:
    using QToolButton::QToolButton;
    bool chosen = false;

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QToolButton::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QRectF dot(0, 0, 11, 11);
        dot.moveCenter(QRectF(rect()).center());
        if (chosen) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(palette().color(QPalette::Highlight));
        } else {
            painter.setPen(QPen(palette().color(QPalette::PlaceholderText), 1.2));
        }
        painter.drawEllipse(dot);
    }
};
}

CanvasSizeSheet::CanvasSizeSheet(const CanvasDocument &document, EditorSession &session, std::function<void(std::optional<CanvasSizeOptions>)> finish,
                                 QWidget *parent)
    : QWidget(parent), m_session(session), m_foreground(session.foregroundColor()), m_background(session.backgroundColor()), m_finish(std::move(finish)),
      m_draft(document.width, document.height, document.resolution), m_width(dimension(true)), m_height(dimension(false)),
      m_note(text(QString(), 12, QFont::Normal, QPalette::PlaceholderText, this)),
      m_anchors(new QButtonGroup(this)), m_anchorName(text(QString(), 12, QFont::Bold, QPalette::WindowText, this)), m_extension(new QComboBox(this)),
      m_customRow(new QWidget(this)), m_customSwatch(new DialogColorSwatch(
          QStringLiteral("Extension Color"), [this] { return m_custom; }, [this](const PaletteColor &custom) { m_custom = custom; }, session, m_customRow)),
      m_ok(new QPushButton(QStringLiteral("OK"), this))
{
    setFixedWidth(450);
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(24, 24, 24, 24);
    column->setSpacing(16);
    column->addWidget(text(QStringLiteral("Canvas Size"), 17, QFont::Bold, QPalette::WindowText, this));
    column->addWidget(text(QStringLiteral("Current: %1 × %2 pixels").arg(grouped(document.width), grouped(document.height)), 13, QFont::Normal,
                           QPalette::WindowText, this));
    column->addWidget(text(ByteCounts::memory(qint64(document.width) * document.height * 4) + QStringLiteral(" uncompressed RGBA canvas"), 12, QFont::Normal,
                           QPalette::PlaceholderText, this));
    auto *divider = new QFrame(this);
    divider->setFrameShape(QFrame::HLine);
    divider->setForegroundRole(QPalette::Mid);
    column->addWidget(divider);

    auto *fields = new QGridLayout;
    fields->setHorizontalSpacing(8);
    fields->setVerticalSpacing(16);
    fields->setColumnMinimumWidth(0, 60);
    auto *units = new QComboBox(this);
    auto *unitsLabel = new QLabel(QStringLiteral("Units"), this);
    unitsLabel->setBuddy(units);
    units->setObjectName(QStringLiteral("canvasUnits"));
    for (const CanvasUnit unit : {CanvasUnit::pixels, CanvasUnit::percent, CanvasUnit::inches, CanvasUnit::centimeters})
        units->addItem(rawValue(unit), int(unit));
    connect(units, &QComboBox::activated, this, [this, units](int index) {
        // A wheel leaves the focus: pending typing commits first.
        units->setFocus();
        m_draft.unit = CanvasUnit(units->itemData(index).toInt());
        synchronize();
    });
    fields->addWidget(unitsLabel, 0, 0);
    fields->addWidget(units, 0, 1);
    auto *width = new QLabel(QStringLiteral("Width"), this);
    auto *height = new QLabel(QStringLiteral("Height"), this);
    m_scrubs = {scrub(width, true), scrub(height, false)};
    fields->addWidget(width, 1, 0);
    fields->addWidget(m_width, 1, 1);
    fields->addWidget(height, 2, 0);
    fields->addWidget(m_height, 2, 1);
    column->addLayout(fields);

    auto *relative = new QCheckBox(QStringLiteral("Relative to current dimensions"), this);
    relative->setObjectName(QStringLiteral("canvasRelative"));
    connect(relative, &QCheckBox::clicked, this, [this](bool on) {
        m_draft.relative = on;
        synchronize();
    });
    auto *locked = new QCheckBox(QStringLiteral("Lock original aspect ratio"), this);
    locked->setObjectName(QStringLiteral("canvasLocked"));
    // Swift's onChange: locking takes the width's proportions.
    connect(locked, &QCheckBox::clicked, this, [this](bool on) {
        m_draft.locked = on;
        if (on)
            m_draft.set(m_draft.displayed(true), true);
        synchronize();
    });
    column->addWidget(relative);
    column->addWidget(locked);
    m_note->setObjectName(QStringLiteral("canvasNote"));
    column->addWidget(m_note);

    auto *grid = new QGridLayout;
    grid->setSpacing(3);
    for (int index = 0; index < 9; ++index) {
        auto *button = new AnchorButton(this);
        button->setObjectName(QStringLiteral("anchor%1").arg(index));
        button->setFixedSize(25, 25);
        button->setToolTip(anchorNames[size_t(index)]);
        button->setAccessibleName(anchorNames[size_t(index)]);
        m_anchors->addButton(button, index);
        grid->addWidget(button, index / 3, index % 3);
    }
    connect(m_anchors, &QButtonGroup::idClicked, this, [this](int index) {
        m_anchor = index;
        synchronize();
    });
    auto *anchorColumn = new QVBoxLayout;
    anchorColumn->setSpacing(8);
    anchorColumn->addWidget(new QLabel(QStringLiteral("Anchor"), this));
    anchorColumn->addLayout(grid);
    m_anchorName->setObjectName(QStringLiteral("anchorName"));
    auto *explanation = text(QStringLiteral("Keeps this point fixed. Artwork is not scaled; cropped content remains outside the canvas."), 12,
                             QFont::Normal, QPalette::PlaceholderText, this);
    explanation->setWordWrap(true);
    auto *named = new QVBoxLayout;
    named->setContentsMargins(0, 28, 0, 0);
    named->setSpacing(8);
    named->addWidget(m_anchorName);
    named->addWidget(explanation);
    named->addStretch(1);
    auto *anchorRow = new QHBoxLayout;
    anchorRow->setSpacing(24);
    anchorRow->addLayout(anchorColumn);
    anchorRow->addLayout(named, 1);
    column->addLayout(anchorRow);

    auto *extension = new QHBoxLayout;
    auto *extensionLabel = new QLabel(QStringLiteral("Canvas extension"), this);
    extensionLabel->setBuddy(m_extension);
    m_extension->setObjectName(QStringLiteral("canvasExtension"));
    m_extension->addItems({QStringLiteral("Transparent"), QStringLiteral("Foreground"), QStringLiteral("Background"), QStringLiteral("Black"),
                           QStringLiteral("White"), QStringLiteral("Custom")});
    connect(m_extension, &QComboBox::activated, this, &CanvasSizeSheet::synchronize);
    extension->addWidget(extensionLabel);
    extension->addWidget(m_extension, 1);
    column->addLayout(extension);
    // Its label and the app's picker on a swatch.
    m_customRow->setObjectName(QStringLiteral("extensionColorRow"));
    m_customSwatch->setObjectName(QStringLiteral("extensionColor"));
    m_customSwatch->setToolTip(QStringLiteral("Color for the added canvas"));
    auto *customLayout = new QHBoxLayout(m_customRow);
    customLayout->setContentsMargins(0, 0, 0, 0);
    customLayout->setSpacing(8);
    customLayout->addWidget(new QLabel(QStringLiteral("Extension color"), m_customRow));
    customLayout->addWidget(m_customSwatch);
    customLayout->addStretch(1);
    column->addWidget(m_customRow);

    auto *cancel = new QPushButton(QStringLiteral("Cancel"), this);
    cancel->setObjectName(QStringLiteral("canvasCancel"));
    cancel->setAutoDefault(false);
    connect(cancel, &QPushButton::clicked, this, [this] {
        DialogColorSwatch::closePicker(m_session);
        m_finish(std::nullopt);
    });
    m_ok->setObjectName(QStringLiteral("canvasOK"));
    m_ok->setDefault(true);
    // Swift's configuredNativeShortcut: Return and Escape, as remapped.
    NativeShortcut::bind(*this, m_ok, cancel);
    // OK rests while the draft is invalid: no guard needed.
    connect(m_ok, &QPushButton::clicked, this, [this] {
        // An open picker's colour counts: it closes first.
        DialogColorSwatch::closePicker(m_session);
        m_finish(CanvasSizeOptions{.width = qint64(std::round(m_draft.width)), .height = qint64(std::round(m_draft.height)), .anchor = m_anchor,
                                   .fill = fill()});
    });
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(cancel);
    buttons->addStretch(1);
    buttons->addWidget(m_ok);
    column->addLayout(buttons);
    synchronize();
}

// Swift's value binding: a readable number sets the draft.
PickerField *CanvasSizeSheet::dimension(bool widthAxis)
{
    auto *field = new PickerField([this, widthAxis] {
        PickerField &edited = widthAxis ? *m_width : *m_height;
        bool number = false;
        const double typed = edited.locale().toDouble(edited.text(), &number);
        if (edited.isModified() && number && std::isfinite(typed))
            m_draft.set(typed, widthAxis);
        edited.setModified(false);
        synchronize();
    }, nullptr, this);
    field->setObjectName(widthAxis ? QStringLiteral("canvasWidth") : QStringLiteral("canvasHeight"));
    field->setPlaceholderText(widthAxis ? QStringLiteral("Width") : QStringLiteral("Height"));
    field->setAccessibleName(widthAxis ? QStringLiteral("Width") : QStringLiteral("Height"));
    return field;
}

NumericScrub *CanvasSizeSheet::scrub(QLabel *title, bool widthAxis)
{
    // Dragged sizes snap to whole units; synchronize sets the limits.
    return new NumericScrub(title, {.sensitivity = 1, .low = 0, .high = 0, .step = 1, .value = [this, widthAxis] { return m_draft.displayed(widthAxis); },
                                    .set = [this, widthAxis](double value) {
                                        m_draft.set(value, widthAxis);
                                        (widthAxis ? m_width : m_height)->setModified(false);
                                        synchronize();
                                    }});
}

std::optional<CanvasExtensionColor> CanvasSizeSheet::fill() const
{
    const auto colour = [](PaletteColor value) { return CanvasExtensionColor{value.red, value.green, value.blue}; };
    switch (m_extension->currentIndex()) {
    case 0: return std::nullopt;
    case 1: return colour(m_foreground);
    case 2: return colour(m_background);
    case 3: return colour(PaletteColor::black());
    case 4: return colour(PaletteColor::white());
    default: return colour(m_custom);
    }
}

// Units, Relative and Lock change only through their own controls.
void CanvasSizeSheet::synchronize()
{
    for (const auto &[field, widthAxis] : {std::pair(m_width, true), std::pair(m_height, false)}) {
        // A field being typed in keeps its typing.
        const auto [low, high] = scrubRange(m_draft, widthAxis);
        m_scrubs[widthAxis ? 0 : 1]->reshape(scrubSensitivity(m_draft, widthAxis), low, high);
        const QString number = shown(m_draft.displayed(widthAxis), field->locale());
        if (!field->isModified() && field->text() != number)
            field->setText(number);
    }
    const bool valid = m_draft.valid();
    if (valid) {
        const qint64 width = qint64(std::round(m_draft.width)), height = qint64(std::round(m_draft.height));
        m_note->setText(QStringLiteral("New: %1 × %2 pixels · %3 uncompressed").arg(grouped(width), grouped(height), ByteCounts::memory(width * height * 4)));
    } else {
        m_note->setText(QStringLiteral("Final dimensions must be 1–%1 pixels per side.").arg(DocumentLimits::maxSideText()));
    }
    m_note->setForegroundRole(valid ? QPalette::PlaceholderText : QPalette::BrightText);
    for (QAbstractButton *each : m_anchors->buttons()) {
        auto *button = static_cast<AnchorButton *>(each);
        button->chosen = m_anchors->id(each) == m_anchor;
        button->setAccessibleDescription(button->chosen ? QStringLiteral("Selected") : QString());
        button->update();
    }
    m_anchorName->setText(anchorNames[size_t(m_anchor)]);
    m_customRow->setVisible(m_extension->currentIndex() == 5);
    m_ok->setEnabled(valid);
}

CanvasSizeSheet::~CanvasSizeSheet()
{
    releaseFocus(*this);
    DialogColorSwatch::closePicker(m_session);
}
