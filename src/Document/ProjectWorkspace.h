#pragma once
#include "Document/EditorSession.h"
#include "IO/ProjectController.h"
#include <QMimeData>
#include <memory>
#include <vector>

// One open project: its session and its file operations.
class ProjectTab {
public:
    explicit ProjectTab(QString name);

    const QUuid id = QUuid::createUuid();
    const QString defaultName;
    // The controller refers to the session: this order.
    EditorSession session;
    ProjectController controller;

    QString title() const;
    static QString nameWithoutSuffix(const QString &path);
};

// The projects open in one window, one in front.
class ProjectWorkspace : public QObject {
    Q_OBJECT
public:
    ProjectWorkspace();

    const std::vector<std::shared_ptr<ProjectTab>> &tabs() const { return m_tabs; }
    QUuid selectedID() const { return m_selectedID; }
    bool isManaging() const { return m_isManaging; }
    QPointer<QWidget> window;

    ProjectTab &current() const;
    std::shared_ptr<ProjectTab> tab(QUuid id) const;
    bool canSwitch() const;
    ProjectTab &addTab(bool reuseEmpty = true);
    void select(QUuid id);
    // A dragged tab lands at `index`, clamped; chrome, never undone.
    void moveTab(QUuid id, int index);
    void newCanvas();
    // Each `done` runs from the event loop, never inline.
    void open(std::optional<QString> path = std::nullopt, std::function<void(bool)> done = {});
    void close(QUuid id, std::function<void()> done = {});
    void removeTab(QUuid id);
    std::vector<std::shared_ptr<ProjectTab>> quitOrder() const;
    void confirmQuit(std::function<void(bool)> done);
    void closeWindow(QWidget *closing);
    void receive(const QList<QUrl> &urls, std::optional<QUuid> destination = std::nullopt,
                 std::optional<QPointF> point = std::nullopt, std::function<void()> done = {});
    // A dragged layer row carries its id under this type.
    static const QString layerType;
    void receiveProviders(const QMimeData &data, std::optional<QUuid> destination = std::nullopt,
                          std::optional<QPointF> point = std::nullopt, std::function<void()> done = {});
    // The tab a dragged layer lives in, read mid-air.
    std::optional<QUuid> draggedLayerSource(const QMimeData &data) const;
    // A layer's own tab is no target; others are.
    bool canReceiveDrag(const QMimeData &data, std::optional<QUuid> destination) const;
    // A drag's ids, one a line; other lines skipped.
    static std::vector<QUuid> layerIDs(const QMimeData &data);
    // The id a text names; all zeros counts.
    static std::optional<QUuid> layerID(const QString &text);
    void copyLayer(QUuid id, std::optional<QUuid> destination, std::optional<QPointF> point = std::nullopt,
                   std::function<void()> done = {});
    // Folders with all they hold, as one step there.
    void copyLayers(const std::vector<QUuid> &ids, std::optional<QUuid> destination, std::optional<QPointF> point = std::nullopt,
                    std::function<void()> done = {});
    // Ctrl+V with layers copied whole; false when none.
    bool pasteCopiedLayer();

signals:
    void changed();

private:
    bool finishTextEditing();
    struct Baked {
        EditorSession::BakedImages images;
        std::optional<QString> failure;
    };
    struct Copy {
        std::vector<QUuid> ids;
        std::shared_ptr<ProjectTab> from, target;
        CanvasDocument original;
        std::vector<ImageLayer> layers;
        QSet<QUuid> included;
        std::optional<QPointF> point;
    };

    void setManaging(bool managing);
    void finish(const std::function<void()> &done);
    void finish(const std::function<void(bool)> &done, bool value);
    void loadProject(const QString &path, std::function<void(bool)> then);
    void askNext(std::vector<std::shared_ptr<ProjectTab>> order, size_t index, std::function<void(bool)> done);
    void receiveNext(QList<QUrl> urls, std::optional<QUuid> destination, std::optional<QPointF> point, std::function<void()> done);
    void copyNext(std::vector<QUuid> ids, std::optional<QUuid> destination, std::optional<QPointF> point, std::function<void()> done);
    void importNext(std::vector<std::shared_ptr<QMimeData>> providers, std::optional<QUuid> destination, std::optional<QPointF> point,
                    std::function<void()> done);
    void place(const Copy &copy, const EditorSession::BakedImages &baked);

    std::vector<std::shared_ptr<ProjectTab>> m_tabs;
    QUuid m_selectedID;
    bool m_isManaging = false;
    int m_nextNumber = 2;
};
