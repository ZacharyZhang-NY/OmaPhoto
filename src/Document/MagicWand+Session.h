// Swift's MagicWand extension.
public:
    // Off the UI thread; `done` follows the step or refusal.
    void magicWand(QPointF point, SelectionMode mode, std::function<void()> done = {});

private:
    void finishWand();
    std::optional<QImage> wandSample(const CanvasDocument &document) const;
