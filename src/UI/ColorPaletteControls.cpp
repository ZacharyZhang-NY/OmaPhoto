#include "UI/ColorPaletteControls.h"
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>
#include <cmath>

namespace {
constexpr double swatchSize = 24;
constexpr double swatchOffset = 12;
// Swift's buttons reach 3 points past the swatches' frame.
constexpr QPoint origin(3, 3);

// A 12-point glyph in the secondary ink, as Swift's buttons.
class GlyphButton : public QAbstractButton {
public:
    GlyphButton(std::function<void(QPainter &)> glyph, QWidget *parent) : QAbstractButton(parent), m_glyph(std::move(glyph))
    {
        setFixedSize(12, 12);
        setCursor(Qt::ArrowCursor);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor ink = palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::PlaceholderText);
        painter.setPen(QPen(ink, 1.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        m_glyph(painter);
    }

private:
    const std::function<void(QPainter &)> m_glyph;
};

// SF Symbols' arrow.left.and.right, turned 45 degrees.
void swapGlyph(QPainter &painter)
{
    painter.translate(6, 6);
    painter.rotate(45);
    QPainterPath arrow;
    arrow.moveTo(-4.5, 0);
    arrow.lineTo(4.5, 0);
    arrow.moveTo(-2.5, -2);
    arrow.lineTo(-4.5, 0);
    arrow.lineTo(-2.5, 2);
    arrow.moveTo(2.5, -2);
    arrow.lineTo(4.5, 0);
    arrow.lineTo(2.5, 2);
    painter.drawPath(arrow);
}

// SF Symbols' arrow.counterclockwise: a ring, its head at the top.
void resetGlyph(QPainter &painter)
{
    QPainterPath ring;
    ring.arcMoveTo(QRectF(2.5, 2.5, 7, 7), 100);
    ring.arcTo(QRectF(2.5, 2.5, 7, 7), 100, -290);
    painter.drawPath(ring);
    QPainterPath head;
    head.moveTo(7.2, 1.2);
    head.lineTo(5.3, 2.6);
    head.lineTo(7, 4.2);
    painter.drawPath(head);
}
}

SwatchButton::SwatchButton(std::function<PaletteColor()> colour, double radius, double whiteRing, double rimAlpha, QWidget *parent)
    : QAbstractButton(parent), m_colour(std::move(colour)), m_radius(radius), m_whiteRing(whiteRing), m_rimAlpha(rimAlpha)
{
}

void SwatchButton::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF bounds(rect());
    QPainterPath shape;
    shape.addRoundedRect(bounds, m_radius, m_radius);
    painter.fillPath(shape, m_colour().color());
    painter.setBrush(Qt::NoBrush);
    // Swift's inset(by: 1).strokeBorder(.white): a ring inside the rim.
    if (m_whiteRing > 0) {
        const double inset = 1 + m_whiteRing / 2;
        painter.setPen(QPen(Qt::white, m_whiteRing));
        painter.drawRoundedRect(bounds.adjusted(inset, inset, -inset, -inset), m_radius - inset, m_radius - inset);
    }
    painter.setPen(QPen(QColor::fromRgbF(0, 0, 0, float(m_rimAlpha)), 1));
    painter.drawRoundedRect(bounds.adjusted(0.5, 0.5, -0.5, -0.5), m_radius - 0.5, m_radius - 0.5);
}

ColorPaletteControls::ColorPaletteControls(EditorSession &session, QWidget *parent)
    : QWidget(parent), m_session(session),
      m_background(new SwatchButton([&session] { return session.paletteColor(true); }, 6, 1.5, 1, this)),
      m_foreground(new SwatchButton([&session] { return session.paletteColor(false); }, 6, 1.5, 1, this)),
      m_swap(new GlyphButton(swapGlyph, this)), m_reset(new GlyphButton(resetGlyph, this)), m_pickerPanel(*this),
      m_masked(session.isMaskSelected())
{
    setFixedSize(int(swatchSize + swatchOffset) + 2 * origin.x(), int(swatchSize + swatchOffset) + 2 * origin.y());
    m_background->setObjectName(QStringLiteral("backgroundSwatch"));
    m_background->setGeometry(QRect(origin + QPoint(int(swatchOffset), int(swatchOffset)), QSize(int(swatchSize), int(swatchSize))));
    m_background->setToolTip(QStringLiteral("Background color"));
    m_background->setAccessibleName(QStringLiteral("Background color"));
    m_foreground->setObjectName(QStringLiteral("foregroundSwatch"));
    m_foreground->setGeometry(QRect(origin, QSize(int(swatchSize), int(swatchSize))));
    m_foreground->setToolTip(QStringLiteral("Foreground color"));
    m_foreground->setAccessibleName(QStringLiteral("Foreground color"));
    m_swap->setObjectName(QStringLiteral("swapColors"));
    m_swap->move(origin + QPoint(int(swatchSize) + 3, -3));
    m_swap->setToolTip(QStringLiteral("Swap foreground and background (X)"));
    m_swap->setAccessibleName(QStringLiteral("Swap colors"));
    m_reset->setObjectName(QStringLiteral("defaultColors"));
    m_reset->move(origin + QPoint(-1, int(swatchSize) + 3));
    m_reset->setToolTip(QStringLiteral("Default colors (D)"));
    m_reset->setAccessibleName(QStringLiteral("Default colors"));
    for (const auto &[swatch, background] : {std::pair(m_background, true), std::pair(m_foreground, false)}) {
        connect(swatch, &QAbstractButton::clicked, this, [this, background] {
            if (m_session.isMaskSelected())
                chooseMask(background);
            else
                m_session.openColorPicker(background);
        });
    }
    connect(m_swap, &QAbstractButton::clicked, this, [this] { m_session.swapPaletteColors(); });
    connect(m_reset, &QAbstractButton::clicked, this, [this] { m_session.resetPaletteColors(); });
    connect(&m_session, &EditorSession::changed, this, &ColorPaletteControls::synchronize);
    synchronize();
}

void ColorPaletteControls::chooseMask(bool background)
{
    auto *popup = new QFrame(this, Qt::Popup);
    popup->setObjectName(QStringLiteral("maskColorChoice"));
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setFrameShape(QFrame::StyledPanel);
    auto *headline = new QLabel(background ? QStringLiteral("Mask background") : QStringLiteral("Mask foreground"), popup);
    QFont bold = headline->font();
    bold.setBold(true);
    headline->setFont(bold);
    auto *buttons = new QHBoxLayout;
    for (const auto &[text, colour] : {std::pair(QStringLiteral("Black · Hide"), PaletteColor::black()), std::pair(QStringLiteral("White · Reveal"), PaletteColor::white())}) {
        auto *button = new QPushButton(text, popup);
        connect(button, &QPushButton::clicked, popup, [this, popup, background, colour] {
            m_session.setPaletteColor(colour, background);
            popup->close();
        });
        buttons->addWidget(button);
    }
    auto *column = new QVBoxLayout(popup);
    column->setContentsMargins(16, 16, 16, 16);
    column->setSpacing(12);
    column->addWidget(headline);
    column->addLayout(buttons);
    popup->move(mapToGlobal(QPoint(0, height())));
    popup->show();
    m_maskChoice = popup;
}

void ColorPaletteControls::synchronize()
{
    setEnabled(m_session.canEditPalette());
    // Another target closes the choice; a mask cancels the picker.
    if (m_session.isMaskSelected() != m_masked) {
        m_masked = m_session.isMaskSelected();
        if (m_maskChoice)
            m_maskChoice->close();
        if (m_masked)
            m_session.closeColorPicker(false);
    }
    const std::optional<ColorPickerState> &picker = m_session.colorPicker();
    const std::optional<QUuid> shown = picker ? std::optional(picker->id) : std::nullopt;
    if (shown != m_shownPicker) {
        m_shownPicker = shown;
        if (picker)
            m_pickerPanel.show(*picker, m_session);
        else
            m_pickerPanel.close();
    }
    m_background->update();
    m_foreground->update();
}
