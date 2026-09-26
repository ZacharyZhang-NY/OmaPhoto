// Swift's Filters extension: one filter at a time.
public:
    bool canContentAwareFill() const;
    void beginFilter(FilterKind kind);
    // The settings normalized; Preview off drops the queue and preview.
    void updateFilter(const FilterSettings &settings, bool preview);
    void cancelFilter();
    // Off the UI thread; `done` follows the step or refusal.
    void commitFilter(std::function<void()> done = {});

private:
    void renderFilterPreview();
    void finishFilterPreview();
    void finishFilterCommit();
    // Swift's commitBackgroundMask: the subject kept by a mask.
    void commitBackgroundMask(const FilterEdit &edit);
