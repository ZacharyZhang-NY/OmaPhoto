// Swift's FloatingSelection extension: Ctrl+T on selected pixels.
public:
    bool canTransformSelection() const;
    void transformCommand();
    void beginSelectionTransform(std::function<void()> done = {});
    std::optional<QTransform> floatingSelectionTransform(const TransformEdit &edit) const;

private:
    void mergeFloatingTransform(const TransformEdit &edit, const FloatingTransform &floating);
    void cancelFloatingTransform(const FloatingTransform &floating);
