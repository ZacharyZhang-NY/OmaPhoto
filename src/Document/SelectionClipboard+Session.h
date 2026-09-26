// Swift's SelectionClipboard extension: Copy, Copy Merged, Paste.
public:
    // Ctrl+X: copy, then clear the selected pixels.
    void cutSelection(std::function<void()> done = {});
    // A copy above the layer, then placed; never a folder.
    void duplicateActiveLayer();
    bool duplicateLayer(QUuid id, std::optional<QUuid> parent, std::optional<QUuid> above = std::nullopt, bool atBottom = false);
    std::optional<QRectF> selectionCopyRegion() const;
    bool canCopyPixels() const;
    std::optional<CopiedPixels> renderSelectedPixels(const ImageLayer &layer, bool mask) const;
    bool canCopyMerged() const;
    std::optional<CopiedPixels> renderMergedPixels() const;
    void copyMergedSelection();
    void copySelection();
    bool canPaste() const;
    void paste();
    void layerViaCopy();
    // A shape or text style is tied to its asset.
    void addPixelLayer(const QImage &image, QPointF origin, const QString &name, const QString &editName, bool dropsSelection = true,
                       const std::optional<LayerShapeStyle> &shape = std::nullopt, const std::optional<LayerTextStyle> &text = std::nullopt);
    QString nextLayerName() const;

private:
    void store(const CopiedPixels &copied);
