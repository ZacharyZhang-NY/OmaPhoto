// Swift's LayerMerge extension: selected layers made one.
public:
    bool canMergeLayers() const;
    QString mergeTitle() const;
    void mergeLayers();

private:
    std::optional<MergePlan> mergePlan() const;
