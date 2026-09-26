// Swift's LayerAppearance extension: opacity and blend modes.
public:
    LayerBlendMode displayedBlendMode(const ImageLayer &layer) const;
    void previewBlendMode(std::optional<LayerBlendMode> mode, std::optional<QUuid> id);
    bool canEditAppearance() const;
    // A slider drag: many values, one undo step.
    void beginOpacityEdit();
    void finishOpacityEdit();
    void setLayerOpacity(double opacity);
    void setSelectedLayersOpacity(double opacity);
    void cycleBlendMode(bool forward);
    void setLayerBlendMode(LayerBlendMode mode);
