// Swift's AdjustmentEditing extension: the colour editors write layer settings.
public:
    // Draws what lies beneath off the UI thread; `done` follows.
    void beginAdjustmentEditing(QUuid id, std::function<void()> done = {});
    // Writes the edited settings, or the original with Preview off.
    bool previewAdjustmentEditing(bool preview);
    // OK keeps the edited settings; Cancel puts the original back.
    bool finishAdjustmentEditing(bool commit);

private:
    LayerAdjustment editedAdjustment() const;
    void openAdjustmentEditor(QUuid id, const LayerAdjustment &original, const AdjustmentInput &made);
