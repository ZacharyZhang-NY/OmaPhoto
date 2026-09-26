// Swift's Levels extension: one Levels edit at a time.
public:
    void beginLevels();
    void updateLevels(const LevelsSettings &settings, bool preview);
    // The sheet's eyedroppers: the point a click sets, or none.
    void setLevelsSampleMode(std::optional<LevelsSample> mode);
    void cancelLevels();
    // Off the UI thread; `done` follows the step or refusal.
    void commitLevels(std::function<void()> done = {});

private:
    // Off the UI thread, for the open edit.
    void countLevelsHistogram();
    void renderLevelsPreview();
    void finishLevelsPreview();
    void finishLevelsHistogram();
    void finishLevelsCommit();
