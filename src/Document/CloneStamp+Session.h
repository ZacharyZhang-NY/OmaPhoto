// Swift's CloneStamp extension: where Clone Stamp copies from.
public:
    // Alt-click: the source; a new source starts a new alignment.
    void setCloneSource(QPointF point);
    // The whole-pixel offset a stroke from `point` copies with.
    std::optional<QSizeF> cloneStrokeOffset(QPointF point) const;
    // The source for a brush at `point`: the canvas's crosshair.
    std::optional<QPointF> cloneSamplePoint(QPointF point) const;
    // What `stroke` copies, `offset` away: its pixels or the canvas.
    std::optional<BrushStroke::Clone> cloneSample(const CanvasDocument &document, const BrushStroke &stroke, QSizeF offset) const;
