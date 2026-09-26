// Swift's Distort extension: four corners dragged apart.
public:
    void beginDistort();
    void previewCorners(const Corners &corners);
    std::optional<DistortPreview> distortPreview(const ImageLayer &layer) const;
    // A layer's effects warped with the pending distortion.
    std::optional<DistortWarp::Warped> distortedEffects(const ImageLayer &layer, const QImage &image, double inset) const;
    // For a known target: the commit's, once the edit ends.
    std::optional<DistortWarp::Warped> distortedEffects(const ImageLayer &layer, const QImage &image, double inset, const DistortTarget &target) const;

private:
    std::optional<DistortTarget> distortTarget(const ImageLayer &layer, const TransformEdit &edit, const Corners &shape) const;
    void commitDistort(const TransformEdit &edit, const Corners &shape);
    void distort(int index, const LayerTransform &transform, const Corners &corners);
