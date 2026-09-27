// Swift's MagicWand extension.
public:
    // Off the UI thread; `done` follows the step or refusal.
    void magicWand(QPointF point, SelectionMode mode, std::function<void()> done = {});

    // What selection-from-image tools read, at document size.
    std::optional<QImage> selectionSample(const CanvasDocument &document, bool sampleAllLayers) const;

private:
    void finishWand();
