// Swift's LayerGroups extension: folders and the multi-selection.
public:
    QSet<QUuid> descendantIDs(QUuid id) const;
    void addGroup();
    void toggleGroupExpansion(QUuid id);
    // The primary layer becomes active when the document holds it.
    void selectLayers(const QSet<QUuid> &ids, std::optional<QUuid> primary);
    void extendSelection(QUuid id);
    void groupSelectedLayers();
    // The active layer is a folder: something to unwrap.
    bool canUngroupLayers() const;
    void ungroupLayers();
    std::vector<LayerHierarchy::Entry> layerRows() const;
    bool canPlaceLayer(QUuid id, std::optional<QUuid> parent) const;
    bool placeLayer(QUuid id, std::optional<QUuid> parent, std::optional<QUuid> above = std::nullopt, bool atBottom = false);
    void moveActiveLayerOutOfGroup();
