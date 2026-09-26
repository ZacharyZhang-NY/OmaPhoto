// Swift's Selection extension: outlines drawn, moved and combined.
public:
    std::optional<DocumentSelection> selection() const;
    bool canEditSelection() const;
    SelectionMode selectionMode(bool shift, bool option) const;
    // An outline in progress keeps its starting mode.
    SelectionMode lassoCursorMode(bool shift, bool option) const;
    SelectionMode displayedSelectionMode() const;
    void updateHeldSelectionKeys(bool shift, bool option);
    void beginLasso(QPointF point, SelectionMode mode);
    // Shift squares the box; `fromCenter` grows it around the anchor.
    void dragMarquee(QPointF point, bool square, bool fromCenter);
    void extendLasso(QPointF point);
    void moveLassoCursor(std::optional<QPointF> point);
    void removeLastLassoPoint();
    void cancelLasso();
    void pressMarqueeKey();
    void pressLassoKey();
    void toggleMarqueeKind();
    void toggleLassoKind();
    // Closes the outline and combines it with the selection.
    void finishLasso();
    void applySelection(const QPainterPath &shape, SelectionMode mode, const QString &name);
    void setSelection(std::optional<DocumentSelection> value, const QString &name);
    // Dragging in New mode moves the outline, never pixels.
    bool canMoveSelection(QPointF point) const;
    bool beginSelectionMove();
    void moveSelection(QSizeF offset);
    void endSelectionMove();
    void nudgeSelection(double dx, double dy);
    bool canModifySelection() const;
    void expandSelection(int amount);
    void contractSelection(int amount);
    // Menu commands ask an amount; the bar applies directly.
    void promptSelectionAmount(SelectionAmountOperation operation);
    void confirmSelectionAmount(int amount);
    // Softens the edge further each time, as blurs stack.
    void featherSelection(int amount);
    void selectAll();
    void deselect();
    void invertSelection();

private:
    void resizeSelection(double delta, const QString &name);
