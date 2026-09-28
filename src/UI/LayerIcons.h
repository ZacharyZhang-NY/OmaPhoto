#pragma once
#include "Document/LayerAdjustment.h"
#include <QColor>
#include <QPainter>
#include <QPixmap>

// The layers panel's icons, drawn: SF Symbols have no twin.
enum class LayerIcon {
    eye, eyeSlash, chevronRight, chevronDown, folder, link, newLayer, newFolder, addMask, trash, layers, text,
    halfFilledCircle, sliders, curvePath, plusMinusCircle, paintPalette, circleGrid, sparkles, rightHalfCircle, hatchedCircle, axes,
    scope, lineDiagonal, drop, wind, dottedCircle
};

namespace LayerIcons {
// Paints the icon into a `side` point square, one colour.
void paint(QPainter &painter, LayerIcon icon, QPointF origin, double side, const QColor &colour);
// The icon as a button's pixmap at a pixel ratio.
QPixmap pixmap(LayerIcon icon, double side, const QColor &colour, double ratio);
// Swift's AdjustmentKind.symbol: the glyph that names each kind.
LayerIcon symbol(AdjustmentKind kind);
// An adjustment row's 36-point thumbnail: its glyph 1.2 times up.
QPixmap adjustmentThumbnail(AdjustmentKind kind, const QColor &colour, double ratio);
}
