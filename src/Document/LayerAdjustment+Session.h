// Swift's LayerAdjustment extension: adding and updating adjustments.
public:
    // Above the active layer, one step; then its editor opens.
    void addAdjustment(AdjustmentKind kind);
    // Valid settings alone, with no step of their own.
    void updateAdjustment(QUuid id, const LayerAdjustment &value);
