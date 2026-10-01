// Swift's BlurTool extension: what a Blur stroke paints.
public:
    // The stroke's pixels or mask, sharp; `render` softens parts.
    std::optional<BrushStroke::Clone> blurSample(const BrushStroke &stroke) const;
