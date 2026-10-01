#include "UI/TypeControls.h"
#include <QAbstractItemView>
#include <QStylePainter>
#include <QSignalBlocker>
#include "UI/ColorPaletteControls.h"
#include "Document/EditorSession.h"
#include "Rendering/TextLayout.h"
#include <QComboBox>
#include <QFocusEvent>
#include <QHBoxLayout>
#include "UI/NumericScrub.h"
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <cmath>

namespace {
// Swift's `.number`: the locale's mark, three decimals at most.
QString number(double value, QLocale locale)
{
    locale.setNumberOptions(QLocale::OmitGroupSeparator);
    QString text = locale.toString(value, 'f', 3);
    while (text.endsWith(locale.zeroDigit()))
        text.chop(locale.zeroDigit().size());
    if (text.endsWith(locale.decimalPoint()))
        text.chop(locale.decimalPoint().size());
    return text;
}

// SF Symbols' text.align glyphs: four lines against one side.
QPixmap alignmentGlyph(TextAlignment alignment, const QColor &ink, double ratio)
{
    QPixmap pixmap(QSize(18, 18) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(ink, 1.6, Qt::SolidLine, Qt::RoundCap));
    for (int row = 0; row < 4; ++row) {
        const double length = row % 2 == 0 ? 14 : 9, y = 4 + row * 3.4;
        const double left = alignment == TextAlignment::left ? 2 : alignment == TextAlignment::center ? 9 - length / 2 : 16 - length;
        painter.drawLine(QPointF(left, y), QPointF(left + length, y));
    }
    return pixmap;
}
}

TextStyleField::TextStyleField(std::function<QString()> shown, std::function<void(const QString &)> typed, std::function<void(double)> step,
                               QWidget *parent)
    : QLineEdit(parent), m_shown(std::move(shown)), m_typed(std::move(typed)), m_step(std::move(step))
{
    setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    // Swift's binding takes each edit it can read.
    connect(this, &QLineEdit::textEdited, this, [this](const QString &text) { m_typed(text); });
}

void TextStyleField::sync()
{
    if (!hasFocus() && !m_borrowed)
        setText(m_shown());
}

void TextStyleField::showValue()
{
    setText(m_shown());
}

void TextStyleField::keyPressEvent(QKeyEvent *event)
{
    // Swift's bar keeps the focus: Return shows the number, selected.
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        setText(m_shown());
        selectAll();
        return;
    }
    if (event->key() != Qt::Key_Up && event->key() != Qt::Key_Down) {
        QLineEdit::keyPressEvent(event);
        return;
    }
    m_step((event->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1) * (event->key() == Qt::Key_Up ? 1 : -1));
    setText(m_shown());
}

void TextStyleField::focusOutEvent(QFocusEvent *event)
{
    m_borrowed = event->reason() == Qt::MenuBarFocusReason || event->reason() == Qt::PopupFocusReason;
    if (!m_borrowed)
        setText(m_shown());
    QLineEdit::focusOutEvent(event);
}

TypeFontPicker::TypeFontPicker(QWidget *parent) : QComboBox(parent)
{
    setAccessibleName(QStringLiteral("Font"));
}

namespace {
const QString multiple = QStringLiteral("(Multiple)");
}

bool TypeFontPicker::isMultiple(int index) const
{
    return itemData(index).toString() == multiple;
}

void TypeFontPicker::showMultiple()
{
    if (!isMultiple(0))
        insertItem(0, multiple, multiple);
    setCurrentIndex(0);
}

void TypeFontPicker::hideMultiple()
{
    if (isMultiple(0))
        removeItem(0);
}

void TypeFontPicker::sync(const QString &name)
{
    if (view()->isVisible())
        return;
    if (name.isEmpty()) {
        showMultiple();
        return;
    }
    hideMultiple();
    if (findText(name) < 0)
        addItem(name);
    setCurrentIndex(findText(name));
}

void TypeFontPicker::showPopup()
{
    if (!m_loaded) {
        // Every installed face, and the style's own though missing.
        const QString selected = isMultiple(currentIndex()) ? QString() : currentText();
        QStringList names = TextLayout::availableFonts();
        if (!selected.isEmpty() && !names.contains(selected))
            names << selected;
        names.sort();
        const QSignalBlocker quiet(this);
        clear();
        addItems(names);
        if (selected.isEmpty())
            showMultiple();
        else
            setCurrentIndex(findText(selected));
        m_loaded = true;
    }
    QComboBox::showPopup();
}

void TypeFontPicker::paintEvent(QPaintEvent *)
{
    QStylePainter painter(this);
    QStyleOptionComboBox option;
    initStyleOption(&option);
    painter.drawComplexControl(QStyle::CC_ComboBox, option);
    const QRect field = style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, this);
    // The label draws inset a pixel each side.
    option.currentText = fontMetrics().elidedText(option.currentText, Qt::ElideRight, field.width() - 2);
    painter.drawControl(QStyle::CE_ComboBoxLabel, option);
}

TypeControls::TypeControls(EditorSession &session, QWidget *parent)
    : ToolHeaderBar(QStringLiteral("Type"), parent), m_session(session), m_font(new TypeFontPicker),
      m_size(new TextStyleField(
          [this] { return number(m_session.currentTextStyle().fontSize, locale()); },
          [this](const QString &text) {
              // No number reads as zero, which no size passes.
              const double size = locale().toDouble(text);
              m_session.changeTextStyle([size](LayerTextStyle &style) { style.fontSize = size; });
          },
          [this](double amount) {
              const double size = std::min(2000.0, std::max(1.0, m_session.currentTextStyle().fontSize + amount));
              m_session.changeTextStyle([size](LayerTextStyle &style) { style.fontSize = size; });
          },
          nullptr)),
      m_colour(new SwatchButton([&session] { return session.typeColor(); }, 3, 0, 0.5, nullptr)),
      m_alignments{alignment(TextAlignment::left), alignment(TextAlignment::center), alignment(TextAlignment::right)},
      m_tracking(new TextStyleField(
          [this] { return number(m_session.currentTextStyle().tracking, locale()); },
          [this](const QString &text) {
              bool read = false;
              const double tracking = locale().toDouble(text, &read);
              if (read)
                  m_session.changeTextStyle([tracking](LayerTextStyle &style) { style.tracking = tracking; });
          },
          [this](double amount) {
              const double tracking = m_session.currentTextStyle().tracking + amount;
              m_session.changeTextStyle([tracking](LayerTextStyle &style) { style.tracking = tracking; });
          },
          nullptr)),
      m_leading(new TextStyleField(
          [this] {
              // Auto shows as the empty field's placeholder.
              const double leading = m_session.currentTextStyle().leading;
              return leading > 0 ? QString::number(std::lround(leading)) : QString();
          },
          [this](const QString &text) {
              const double typed = text.toDouble();
              m_session.changeTextStyle([typed](LayerTextStyle &style) { style.leading = std::max(0.0, std::min(5000.0, typed)); });
          },
          [this](double amount) {
              // Steps count from what Auto works out to.
              const double leading = std::max(0.0, m_session.currentTextStyle().lineHeight() + amount);
              m_session.changeTextStyle([leading](LayerTextStyle &style) { style.leading = leading; });
          },
          nullptr)),
      m_cancel(new QPushButton(QStringLiteral("Cancel"), this)), m_done(new QPushButton(QStringLiteral("Done"), this)),
      m_edit(new QPushButton(QStringLiteral("Edit Text"), this))
{
    auto *controls = new QWidget;
    controls->setObjectName(QStringLiteral("typeFields"));
    auto *fields = new QHBoxLayout(controls);
    fields->setContentsMargins(0, 0, 0, 0);
    fields->setSpacing(10);
    m_font->setObjectName(QStringLiteral("typeFont"));
    m_font->setFixedWidth(210);
    m_font->setToolTip(QStringLiteral("Font face, including bold and italic variants"));
    connect(m_font, &QComboBox::activated, this, [this](int index) {
        const QString name = m_font->itemText(index);
        // (Multiple), or the face already shown, changes nothing.
        if (m_font->isMultiple(index) || name == shownFont())
            return;
        const TextSpan selection = m_session.textDraft() ? m_session.textDraft().value().selection : TextSpan();
        m_session.changeTextStyle([&name, selection](LayerTextStyle &style) { style.setFont(name, selection); });
    });
    m_size->setObjectName(QStringLiteral("typeSize"));
    m_size->setFixedWidth(52);
    m_tracking->setObjectName(QStringLiteral("typeTracking"));
    m_tracking->setFixedWidth(45);
    m_leading->setObjectName(QStringLiteral("typeLeading"));
    m_leading->setFixedWidth(52);
    m_leading->setPlaceholderText(QStringLiteral("Auto"));
    m_leading->setToolTip(QStringLiteral("Line height, baseline to baseline. Empty or 0 is Auto: 120% of the font size."));
    m_colour->setObjectName(QStringLiteral("typeColor"));
    m_colour->setFixedSize(36, 18);
    m_colour->setToolTip(QStringLiteral("Text color"));
    m_colour->setAccessibleName(QStringLiteral("Text color"));
    connect(m_colour, &QAbstractButton::clicked, this, [this] { m_session.openTextColorPicker(); });
    auto *alignments = new QWidget;
    auto *aligned = new QHBoxLayout(alignments);
    aligned->setContentsMargins(0, 0, 0, 0);
    aligned->setSpacing(2);
    for (QToolButton *button : m_alignments)
        aligned->addWidget(button);
    // Swift's scrubbable size unit, tracking and leading: whole numbers.
    const auto scrub = [this](const QString &text, TextStyleField *shown, double low, double high, double LayerTextStyle::*key) {
        auto *label = new QLabel(text);
        new NumericScrub(label, {.sensitivity = 1, .low = low, .high = high, .step = 1,
                                 .value = [this, key] { return m_session.currentTextStyle().*key; }, .set = [this, shown, key](double value) {
                                     m_session.changeTextStyle([&](LayerTextStyle &style) { style.*key = value; });
                                     shown->showValue();
                                 }});
        return label;
    };
    for (QWidget *widget : std::initializer_list<QWidget *>{m_font, m_size, scrub(QStringLiteral("px"), m_size, 1, 2000, &LayerTextStyle::fontSize), m_colour, alignments,
                                                             scrub(QStringLiteral("Tracking"), m_tracking, -100, 1000, &LayerTextStyle::tracking), m_tracking,
                                                             scrub(QStringLiteral("Leading"), m_leading, 0, 5000, &LayerTextStyle::leading), m_leading})
        fields->addWidget(widget);
    // Swift's scroll content keeps its size at the leading edge.
    fields->addStretch(1);
    // Swift's horizontal ScrollView: the controls scroll when narrow.
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("typeScroll"));
    scroll->setWidget(controls);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFixedHeight(controls->sizeHint().height());
    m_cancel->setObjectName(QStringLiteral("typeCancel"));
    connect(m_cancel, &QPushButton::clicked, this, [this] { m_session.cancelText(); });
    m_done->setObjectName(QStringLiteral("typeDone"));
    connect(m_done, &QPushButton::clicked, this, [this] { m_session.finishText(); });
    m_edit->setObjectName(QStringLiteral("typeEdit"));
    connect(m_edit, &QPushButton::clicked, this, [this] { m_session.editActiveText(); });
    for (QWidget *widget : std::initializer_list<QWidget *>{scroll, m_cancel, m_done, m_edit})
        row->insertWidget(row->count() - 1, widget, widget == scroll ? 1 : 0);
    // The scroll view takes the spare room, as SwiftUI's does.
    row->setStretch(row->count() - 1, 0);
    applyGlyphs();
    connect(&m_session, &EditorSession::changed, this, &TypeControls::synchronize);
    synchronize();
}

void TypeControls::applyGlyphs()
{
    for (size_t index = 0; index < m_alignments.size(); ++index)
        m_alignments[index]->setIcon(alignmentGlyph(allTextAlignments[index], palette().color(QPalette::WindowText), devicePixelRatio()));
}

// The theme's ink reaches the glyphs, as the panel's icons.
void TypeControls::changeEvent(QEvent *event)
{
    ToolHeaderBar::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        applyGlyphs();
}

// One of Swift's three alignment buttons.
QToolButton *TypeControls::alignment(TextAlignment value)
{
    auto *button = new QToolButton;
    button->setObjectName(QStringLiteral("typeAlign") + rawValue(value));
    button->setCheckable(true);
    button->setAutoRaise(true);
    button->setFixedSize(30, 26);
    button->setToolTip(QStringLiteral("Align ") + rawValue(value).toLower());
    button->setAccessibleName(button->toolTip());
    connect(button, &QToolButton::clicked, this, [this, value] {
        m_session.changeTextStyle([value](LayerTextStyle &style) { style.alignment = value; });
    });
    return button;
}

QString TypeControls::shownFont() const
{
    const std::optional<TextDraft> &draft = m_session.textDraft();
    if (!draft)
        return m_session.currentTextStyle().fontName;
    const TextSpan selection = draft->selection;
    if (selection.length == 0)
        return draft->style.fontNameAt(std::max<qint64>(0, selection.location - 1));
    // Several faces: (Multiple), so any face chosen applies.
    return draft->style.uniformFontName(selection).value_or(QString());
}

void TypeControls::synchronize()
{
    // Swift's onChange: open text previews the picker's colour.
    const std::optional<PaletteColor> picked = m_session.colorPicker() ? std::optional(m_session.colorPicker()->color()) : std::nullopt;
    if (picked != m_pickerColour) {
        m_pickerColour = picked;
        m_session.previewTextColor();
    }
    const LayerTextStyle style = m_session.currentTextStyle();
    m_font->sync(shownFont());
    for (size_t index = 0; index < m_alignments.size(); ++index)
        m_alignments[index]->setChecked(style.alignment == allTextAlignments[index]);
    for (TextStyleField *field : {m_size, m_tracking, m_leading})
        field->sync();
    m_colour->update();
    const bool drafting = m_session.textDraft().has_value();
    m_cancel->setVisible(drafting);
    m_done->setVisible(drafting);
    m_edit->setVisible(!drafting);
    const std::optional<ImageLayer> active = m_session.activeLayer();
    m_edit->setEnabled(active && active->liveText());
    setEnabled(m_session.document().has_value() && !m_session.showsBusy());
}
