// Swift's BlurTool extension: what a Blur stroke paints.
public:
    // The sharp layer or mask, document-sized; `render` softens parts.
    std::optional<BrushStroke::Clone> blurSample(const CanvasDocument &document, bool mask = false) const;
