// Swift's SelectionClipboard extension: Copy, Copy Merged, Paste.
public:
    // Ctrl+X: copy, then clear the selected pixels.
    void cutSelection(std::function<void()> done = {});
    // What Paste puts back in place, until another copy.
    const std::optional<PixelClipboard> &pixelClipboard() const;
    // The layers Copy took whole, until another copy.
    const std::optional<CopiedLayer> &copiedLayer() const;
    // Whether the clipboard still holds this very copy.
    static bool clipboardHolds(const QPointer<QMimeData> &data);
    // Every selected layer, a folder with all it holds.
    void duplicateActiveLayer();
    // Each layer copied as one step; the copies end selected.
    void duplicateLayers(const std::vector<QUuid> &ids, const QString &editName = QStringLiteral("Duplicate Layer"));
    bool duplicateLayer(QUuid id, std::optional<QUuid> parent, std::optional<QUuid> above = std::nullopt, bool atBottom = false);
    std::optional<QRectF> selectionCopyRegion() const;
    bool canCopyPixels() const;
    // Copy with no selection takes the layers themselves.
    bool canCopyLayer() const;
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

    std::vector<QUuid> copiedLayerIDs() const;
    std::optional<QUuid> insertCopy(QUuid id);
