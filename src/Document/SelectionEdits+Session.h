// Swift's SelectionEdits extension: fills, clears, moves and inverts.
public:
    // The outline the canvas draws.
    std::optional<DocumentSelection> displayedSelection() const;
    enum class FillSource { foreground, background };
    bool canEditPixels() const;
    void fillSelection(FillSource source, std::function<void()> done = {});
    void clearSelectedPixels(std::function<void()> done = {});
    void deleteKeyPressed();
    // Swift's PixelMove: Ctrl-drag and Ctrl-arrow move selected pixels.
    bool beginPixelMove(bool duplicate = false);
    void movePixels(QSizeF offset);
    void finishPixelMove(std::function<void()> done = {});
    void cancelPixelMove();
    void nudgePixels(double dx, double dy, std::function<void()> done = {});
    void deleteLayerOrMask();
    bool canInvert() const;
    // Off the UI thread; `done` follows the step or refusal.
    void invertPixels(std::function<void()> done = {});

private:
    void finishInvert();
    void applyPixelEdit(const ImageLayer &layer, const QString &name, const std::function<void(BrushStroke &)> &paint, std::function<void()> done);
