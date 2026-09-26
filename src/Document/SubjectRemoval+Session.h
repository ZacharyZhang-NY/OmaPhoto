// Swift's SubjectRemoval extension: Select → Subject.
public:
    bool canSelectSubject() const;
    // The canvas's foreground, as Remove Background finds it, selected.
    void selectSubject(SelectionMode mode, std::function<void()> done);

private:
    void finishSubject();
