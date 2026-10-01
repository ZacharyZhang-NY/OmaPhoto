    // Select › Color Range's panel; its selection shows until OK.
    const std::optional<ColorRangeEdit> &colorRange() const { return m_colorRange; }
    bool canSelectColorRange() const;
    void beginColorRange();
    // A canvas click: Shift adds, Alt takes away.
    void sampleColorRange(QPointF point, bool shift, bool option);
    // Matches off the UI thread; a newer change supersedes it.
    void updateColorRange();
    void commitColorRange();
    void cancelColorRange();
    // The sheet's and the canvas's writes, Swift's edit fields.
    void setColorRangeSampleMode(HueSampleMode mode);
    void setColorRangeHeld(std::optional<HueSampleMode> held);
    void setColorRangeFuzziness(double fuzziness);
    void setColorRangeInvert(bool invert);
