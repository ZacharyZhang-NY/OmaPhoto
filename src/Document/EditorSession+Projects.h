// Swift's EditorSession+Projects extension: snapshots in and out.
public:
    std::optional<ProjectSnapshot> projectSnapshot() const;
    // Only a package that loaded and validated whole is installed.
    void installProject(const ProjectSnapshot &snapshot, const QString &path);
    void clearProject();
    void createNewProject(int width, int height);
