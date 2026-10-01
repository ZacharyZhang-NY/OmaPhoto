#pragma once
#include "Document/BrushStroke.h"
#include "Document/EditorSession+Model.h"
#include <QImage>
#include <array>
#include <optional>
#include <vector>

// The Brush's two modes.
enum class BrushToolMode { paint, erase };
QString rawValue(BrushToolMode mode);

// The Smear's modes: Liquify and Smudge push pixels, Blur softens.
enum class BlurToolMode { liquify, blur, smudge };
inline constexpr std::array allBlurToolModes{BlurToolMode::liquify, BlurToolMode::blur, BlurToolMode::smudge};
QString rawValue(BlurToolMode mode);

// Swift's WarpStroke: Smudge or Liquify on a document-sized copy.
class WarpStroke {
public:
    WarpStroke(const ImageLayer &layer, const QImage &image, const LayerTransform &transform, QSizeF canvas, BlurToolMode mode,
               const BrushSettings &settings);

    const ImageLayer layer;
    const BlurToolMode mode;
    const double diameter;
    const double hardness;
    const double strength;
    const int width;
    const int height;
    // Every dab's centre, for painting the result into the layer.
    const std::vector<QPointF> &points() const { return m_points; }
    // The layer as the stroke has reshaped it so far.
    const QImage &image() const { return m_context; }
    // Dabs along the way to `point`.
    void append(QPointF point);

private:
    int radius() const;
    // How far a dab moves pixels: 0 centre, 1 rim.
    float weight(float u) const;
    void pickUp(QPointF center);
    void smudge(QPointF center);
    // Forward warp on offsets; pixels drawn afresh from the original.
    void push(QPointF from, QPointF to);

    QImage m_context;
    std::vector<QPointF> m_points;
    std::optional<QPointF> m_last;
    // Smudge: the colour the brush carries, a (2r+1)² RGBA square.
    std::vector<float> m_carried;
    // Liquify: the layer as found, and each pixel's source offset.
    QImage m_original;
    std::vector<float> m_offsets;
    // Liquify: the dab's offsets as they were before it.
    std::vector<float> m_scratch;
};
