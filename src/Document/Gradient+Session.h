// Swift's Gradient extension: a pending gradient, drawn until applied.
public:
    void beginGradient(QPointF point);
    void moveGradient(std::optional<QPointF> start, std::optional<QPointF> end);
    // Redraws the pending gradient from its ends, settings and palette.
    void refreshGradient();
    std::array<QColor, 2> gradientColors(bool mask) const;
    // Ends a drag; a click without a line leaves nothing.
    void endGradientDrag();
    void cancelGradient();
    // Off the UI thread; `done` follows the step or refusal.
    void commitGradient(std::function<void()> done = {});
    // Switching tools, layers or targets applies it, as Photoshop.
    void resolveGradient();
