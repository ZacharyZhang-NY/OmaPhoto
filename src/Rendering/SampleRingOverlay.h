#pragma once
#include "Document/ColorPalette.h"
#include <QPainter>
#include <QRegion>
#include <optional>

// Swift's sample ring: the sampled colour over the original.
class SampleRingOverlay {
public:
    // Rings a view point or hides; returns the areas touched.
    QRegion update(std::optional<QPointF> point, const PaletteColor &original, const PaletteColor &sampled);
    void draw(QPainter &painter) const;
    std::optional<QRectF> frame() const { return m_frame; }
    PaletteColor original() const { return m_original; }
    PaletteColor sampled() const { return m_sampled; }

private:
    std::optional<QRectF> m_frame;
    PaletteColor m_original = PaletteColor::black();
    PaletteColor m_sampled = PaletteColor::black();
};
