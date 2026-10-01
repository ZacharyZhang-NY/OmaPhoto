// Swift's Crop extension: snapping, the frame, the crop itself.
public:
    SnapGuides transformSnapTargets(const QSet<QUuid> &moving) const;
    LayerTransform snappedMove(const LayerTransform &draft, const QSet<QUuid> &moving, double tolerance);
    SnapGuides cropSnapTargets() const;
    // A drawn corner, each axis to a target within reach.
    QPointF snappedPoint(QPointF point, double tolerance);
    // A moved selection's edges or middle; a locked axis stays.
    QSizeF snappedSelectionOffset(QSizeF offset, double tolerance, bool horizontal = true, bool vertical = true);
    // The Crop tool's frame: the canvas until one is drawn.
    std::optional<QRectF> visibleCropRect() const;
    std::optional<double> cropRatio() const;
    void cancelCrop();
    void changeCropRatio();
    // Off the UI thread; `done` follows the step or refusal.
    void commitCrop(std::function<void()> done = {});

private:
    void finishCropCommit();
