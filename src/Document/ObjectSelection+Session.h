// Swift's ObjectSelection extension, and the Magic tool's mode.
public:
    WandMode wandMode() const { return m_wandMode; }
    void setWandMode(WandMode mode);
    const ObjectSelectionSettings &objectSelectionSettings() const { return m_objectSelectionSettings; }
    void setObjectSelectionSettings(const ObjectSelectionSettings &settings);
    // Off the UI thread; `done` follows the step or refusal.
    void selectObject(QPointF point, SelectionMode mode, std::function<void()> done = {});
