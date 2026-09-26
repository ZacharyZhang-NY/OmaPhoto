#pragma once
#include "Document/EditorSession.h"
#include "IO/ProjectController.h"
#include "Rendering/EditorCanvas.h"
#include "UI/FloatingPanel.h"
#include "UI/ToolIcons.h"
#include <QGridLayout>
#include <QLabel>
#include <QPointer>
#include <QStackedLayout>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

class LayersPanel;
class ToolButton;
class ProjectWorkspace;
class QFileDialog;
class QMimeData;
class QMessageBox;
class QProgressBar;

// One project's editor area: bar, rail, canvas, status.
class ContentView : public QWidget {
    Q_OBJECT
public:
    // Without a controller, Open project has nowhere to go.
    explicit ContentView(EditorSession &session, ProjectController *projects = nullptr, QWidget *parent = nullptr);
    ~ContentView() override;

    // The status bar's words for a tool, by its kind.
    static QString hint(NavigationTool tool, ToolIconKind kind = ToolIconKind::plain, BlurToolMode smear = BlurToolMode::liquify,
                        ShapeKind shape = ShapeKind::rectangle);
    // Zoom as Swift prints it: percent, one decimal at most.
    static QString percent(double zoom);
    // The Layers panel's width, remembered across launches.
    static double layersPanelWidth();
    static void setLayersPanelWidth(double width);
    LayersPanel &layersPanel() const { return *m_layersPanel; }
    // Drops of layers from other tabs go to the workspace.
    void setWorkspace(ProjectWorkspace *workspace) { m_workspace = workspace; }
    bool acceptsDrop(const QMimeData &data) const;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void synchronize();
    // Swift's onChange of levels: its panel opens and closes.
    void synchronizePanels();
    ToolIconKind iconKind(NavigationTool tool) const;
    void showHeader(NavigationTool tool);
    void showWelcome(bool shown);
    void showImporter();
    void showAlert(QPointer<QMessageBox> &alert, const QString &title, const std::optional<QString> &message,
                   const std::function<void()> &dismiss);

    EditorSession &m_session;
    const QPointer<ProjectController> m_projects;
    QVBoxLayout *const m_column;
    QWidget *m_header = nullptr;
    std::optional<NavigationTool> m_shownTool;
    std::vector<std::pair<NavigationTool, ToolButton *>> m_toolButtons;
    QGridLayout *const m_canvasSlot;
    CanvasView *const m_canvas;
    LayersPanel *const m_layersPanel;
    // An accent ring while a drop may land.
    QWidget *const m_dropRing;
    ProjectWorkspace *m_workspace = nullptr;
    // A document that appears hands the canvas the keys.
    bool m_hadDocument = false;
    QWidget *m_welcome = nullptr;
    QLabel *const m_zoom;
    QLabel *const m_dimensions;
    QLabel *const m_colour;
    QLabel *const m_activity;
    QProgressBar *const m_spinner;
    QPointer<QFileDialog> m_importer;
    QPointer<QMessageBox> m_importAlert;
    QPointer<QMessageBox> m_brushAlert;
    QPointer<QMessageBox> m_cropAlert;
    FloatingPanel m_levelsPanel{QStringLiteral("levelsPanel"), *this};
    bool m_levelsShown = false;
    FloatingPanel m_adjustmentPanel{QStringLiteral("adjustmentPanel"), *this};
    bool m_hueSaturationShown = false;
    FloatingPanel m_filterPanel{QStringLiteral("filterPanel"), *this};
    bool m_filterShown = false;
    FloatingPanel m_effectsPanel{QStringLiteral("effectsPanel"), *this};
    std::optional<LayerEffectSelection> m_effectsShown;
};
