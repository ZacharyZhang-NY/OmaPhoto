// Swift's TypeTool extension: text typed onto the canvas.
public:
    const std::optional<TextDraft> &textDraft() const { return m_textDraft; }
    // Swift writes the draft freely; waiting file requests look again.
    void setTextDraft(std::optional<TextDraft> draft);
    const LayerTextStyle &textDefaults() const { return m_textDefaults; }
    // A click: the text layer under it, or point text.
    void beginText(QPointF point, bool newLayer = false);
    void editActiveText();
    // By value: finishing passes the very draft this clears.
    bool applyText(TextDraft draft);
    bool finishText();
    void cancelText();
    // A dragged box: a paragraph that wraps inside it.
    void beginText(const QRectF &rect);
    // Live text repainted in `color`, one step.
    bool recolorText(QUuid id, const PaletteColor &color);
    LayerTextStyle currentTextStyle() const;
    void changeTextStyle(const std::function<void(LayerTextStyle &)> &change);
    // A text layer's name: its first words on one line.
    static QString layerName(const QString &content);
    // Point text's size: what it measures, plus padding.
    static QSizeF textBoxSize(const LayerTextStyle &style);
    static QImage textImage(const LayerTextStyle &style);
