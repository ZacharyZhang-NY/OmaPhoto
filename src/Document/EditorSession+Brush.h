// Swift's EditorSession+Brush extension: a stroke from press to commit.
public:
    bool canPaint() const;
    std::unique_ptr<BrushStroke> makeRasterEdit(const ImageLayer &layer, const BrushSettings &settings = BrushSettings(), bool growsMask = false) const;
    void beginBrush(QPointF point);
    void continueBrush(QPointF point);
    // Where a Shift-click's line starts; nil on another target.
    std::optional<QPointF> shiftLineStart() const;
    void cancelBrush();
    // Mouse-up's own call; false while busy.
    bool finishBrushImmediately();
    void finishBrush();
    void commitPaintSnapshot(const BrushStroke &stroke);
    // Off the UI thread; `done` follows the step or refusal.
    void commitRasterEdit(std::shared_ptr<const BrushStroke> stroke, const QString &name, std::function<void()> alsoApply = {},
                          std::function<void()> done = {});
    bool usesOpacityKeys() const;
    // Digits set 10%…100%; two quick ones, an exact value.
    void typeOpacityDigit(int digit, std::optional<double> time = std::nullopt);
    void changeBrushHardness(bool increase);
    void changeBrushSize(bool increase);

private:
    void finishRasterCommit();
    // A mask painted: grown or placed, it keeps its place.
    static LayerMask placedMask(const LayerMask &mask, const ImportedImage &asset, const QRectF &bounds, const LayerTransform &transform,
                                const BrushStroke &stroke);
    // Where Smoothing lets the brush go; none while slack.
    std::optional<QPointF> smoothed(QPointF point);
