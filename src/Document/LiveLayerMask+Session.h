// Swift's LiveLayerMask extension: clipping and live masks.
public:
    // The layers as the export draws them, in document pixels.
    void drawLiveComposite(const CanvasDocument &document, QPainter &context, bool onSurface = false) const;
    // A layer clipped to `source` shows through that layer's alpha.
    bool canLinkMask(QUuid source, QUuid target) const;
    bool linkMask(QUuid source, QUuid target);
    void removeLiveMask(QUuid target);
    // Alt-click clips a layer to its lower sibling, or frees.
    bool canToggleClippingMask(QUuid id) const;
    void toggleClippingMask(QUuid id);
    static void adoptClipping(QUuid id, std::vector<ImageLayer> &layers);
    static void releaseDetachedClipping(std::vector<ImageLayer> &layers);
    // Asks: bake, unlink or cancel. False when nothing depends.
    bool deleteWithLiveMaskChoice(const std::vector<QUuid> &ids);
    using BakedImages = std::map<QUuid, ImportedImage>;
    void finishDeletingLayer(QUuid id, const BakedImages &baked);
    void finishDeletingLayers(const std::vector<QUuid> &ids, const BakedImages &baked);

private:
    std::optional<QUuid> clippingBase(const ImageLayer &layer) const;
    void finishBake();
