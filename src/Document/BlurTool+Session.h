// Swift's BlurTool extension: what a Blur stroke paints.
public:
    // The active layer, or its mask, softened at document size.
    std::optional<QImage> blurSample(const CanvasDocument &document, bool mask = false) const;
