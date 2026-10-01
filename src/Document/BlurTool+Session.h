// Swift's BlurTool extension: what a Blur stroke paints.
public:
    // The stroke's pixels or mask softened, in its own grid.
    std::optional<BrushStroke::Clone> blurSample(const BrushStroke &stroke) const;
