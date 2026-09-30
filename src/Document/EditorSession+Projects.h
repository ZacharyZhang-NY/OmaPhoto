// Swift's EditorSession+Projects extension: snapshots in and out.
public:
    std::optional<ProjectSnapshot> projectSnapshot() const;
    // Only a package that loaded and validated whole is installed.
    void installProject(const ProjectSnapshot &snapshot, const QString &path);
    // Swift's reloadProject: what the package holds now, the view kept.
    void reloadProject(const ProjectSnapshot &snapshot);
    void clearProject();
    void createNewProject(int width, int height);
