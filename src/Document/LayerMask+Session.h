// Swift's LayerMask extension: masks on layers and folders.
public:
    void selectLayerTarget(QUuid id, bool mask);
    // Layers and folders alike take a mask.
    bool canEditMask() const;
    void addLayerMask(bool revealing = true);
    void toggleLayerMask();
    void deleteLayerMask();
    bool canCopyMask(QUuid source, QUuid target) const;
    void copyMask(QUuid source, QUuid target);
    void toggleMaskLink(QUuid id);
    // Where a mask shows now; nil while covering its layer.
    std::optional<LayerTransform> displayedMaskPlacement(const ImageLayer &layer) const;
    // An unlinked mask distorted alone, in the layer's grid.
    std::optional<QImage> maskDistortPreview(const ImageLayer &layer) const;
    // The selection made a mask; without one, a solid mask.
    void addMask(bool revealing = true);
    // The layer whose mask the canvas shows alone, if any.
    std::optional<ImageLayer> maskAloneLayer() const;
    // Alt-click on a mask thumbnail: alone, or the composite again.
    void toggleMaskAlone(QUuid id);

private:
    // Swift's didSet: the pixels targeted end the mask view.
    void setIsMaskSelected(bool value);
    void commitMaskTransform();
