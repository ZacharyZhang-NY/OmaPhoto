// Swift's ShapeTool extension: shapes dragged out as new layers.
public:
    // Pixels one shape layer may hold, an import's budget.
    static constexpr qint64 maxShapePixels = DocumentLimits::maxSurfacePixels;
    ShapeKind shapeKind() const { return m_shapeKind; }
    void setShapeKind(ShapeKind kind);
    // A rectangle's corner radius, in pixels; 0 keeps corners square.
    double shapeCornerRadius() const { return m_shapeCornerRadius; }
    void setShapeCornerRadius(double radius);
    // A line's thickness, in document pixels.
    double shapeLineWidth() const { return m_shapeLineWidth; }
    void setShapeLineWidth(double width);
    const std::optional<ShapeDraft> &shapeDraft() const { return m_shapeDraft; }
    void beginShape(QPointF point);
    // The line being dragged, from its start to the pointer.
    std::optional<std::pair<QPointF, QPointF>> shapeLineEnds() const;
    // Shift squares or snaps a line; Alt grows from centre.
    void dragShape(QPointF point, bool square, bool fromCenter);
    void cancelShape();
    // Shift-U and Tab: Rectangle, Ellipse, Line.
    void toggleShapeKind();
    // The draft becomes a layer above the active one.
    void finishShape();
    // "Rectangle 1", "Ellipse 2", skipping names already used.
    QString nextShapeName(ShapeKind kind) const;
    // A resized shape layer draws its shape again.
    void redrawShape(int index);
    // A rounded rectangle at its dragged size, 2048 at most.
    QImage shapeTransformPreview(const ImageLayer &layer, const LayerTransform &transform) const;
    static QImage shapeImage(ShapeKind kind, QSizeF size, const PaletteColor &color, double cornerRadius = 0, double lineWidth = 0,
                             std::optional<QPointF> start = std::nullopt, std::optional<QPointF> end = std::nullopt);
