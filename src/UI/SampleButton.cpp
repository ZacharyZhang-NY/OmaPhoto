#include "UI/SampleButton.h"
#include "Rendering/EyedropperIcon.h"
#include <QPainter>
#include <QPainterPath>

SampleButton::SampleButton(std::function<void(QPainter &, const QColor &)> glyph, QWidget *parent) : QToolButton(parent), m_glyph(std::move(glyph))
{
    setCheckable(true);
    setFixedSize(24, 20);
}

void SampleButton::paintEvent(QPaintEvent *)
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

void sampleEyedropper(QPainter &painter, HueSampleMode mode, const QColor &ink)
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
