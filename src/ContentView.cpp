#include "ContentView.h"
#include "UI/CanvasRulers.h"
#include "UI/ColorPaletteControls.h"
#include "Logging.h"
#include "Document/ProjectWorkspace.h"
#include "IO/ImageFileDrop.h"
#include "UI/LayersPanel.h"
#include "UI/BrushControls.h"
#include "UI/CropControls.h"
#include "UI/GradientControls.h"
#include "UI/ShapeControls.h"
#include "UI/TypeControls.h"
#include "UI/LassoControls.h"
#include "UI/NavigationToolHeader.h"
#include "UI/TransformInspector.h"
#include "UI/NewCanvasSheet.h"
#include "UI/ToolHeaderStyle.h"
#include "UI/ToolIcons.h"
#include <QCheckBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFrame>
#include <QLocale>
#include <QPainter>
#include <QProgressBar>
#include <QMouseEvent>
#include <QScrollArea>
#include <QSettings>
#include <cmath>
#include <map>

namespace {
const NavigationTool railTools[] = {NavigationTool::move, NavigationTool::marquee, NavigationTool::lasso, NavigationTool::wand,
                                    NavigationTool::crop, NavigationTool::brush, NavigationTool::spotHealing, NavigationTool::cloneStamp,
                                    NavigationTool::blur, NavigationTool::gradient, NavigationTool::shape, NavigationTool::type,
                                    NavigationTool::eyedropper, NavigationTool::hand, NavigationTool::zoom};

// Swift's Eyedropper bar: its title and the ring's checkbox.
ToolHeaderBar *eyedropperBar(EditorSession &session, QWidget *parent)
{
    auto *bar = new ToolHeaderBar(QStringLiteral("Eyedropper"), parent);
    auto *ring = new QCheckBox(QStringLiteral("Sample Ring"), bar);
    ring->setObjectName(QStringLiteral("sampleRing"));
    ring->setChecked(session.showsSampleRing());
    QObject::connect(ring, &QCheckBox::toggled, &session, &EditorSession::setShowsSampleRing);
    // It follows the session, which others may set.
    QObject::connect(&session, &EditorSession::changed, ring, [ring, &session] {
        const QSignalBlocker quiet(ring);
        ring->setChecked(session.showsSampleRing());
    });
    bar->row->setSpacing(16);
    bar->row->insertWidget(1, ring);
    return bar;
}
}

// A rail button: its tool's icon; a plate when chosen.
class ToolButton : public QToolButton {
public:
    ToolButton(NavigationTool tool, QWidget *parent) : QToolButton(parent), m_tool(tool)
    {
        setCheckable(true);
        setFixedSize(36, 36);
        setFocusPolicy(Qt::NoFocus);
        setToolTip(label(tool));
        setAccessibleName(label(tool));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor ink = palette().color(QPalette::WindowText);
        if (isChecked()) {
            QColor plate = ink, edge = ink;
            plate.setAlphaF(0.12);
            edge.setAlphaF(0.14);
            painter.setPen(edge);
            painter.setBrush(plate);
            painter.drawRoundedRect(QRectF(0.5, 0.5, 35, 35), 7, 7);
        }
        ToolIcons::paint(painter, m_tool, QPointF(9, 9), ToolIcons::points, ink, m_kind);
    }

public:
    // The Marquee's and the Lasso's icons follow their kinds.
    void setKind(ToolIconKind kind)
    {
        if (kind == m_kind)
            return;
        m_kind = kind;
        update();
    }

private:
    const NavigationTool m_tool;
    ToolIconKind m_kind = ToolIconKind::plain;
};

namespace {
// Swift's PanelResizeEdge: a divider with an 8-point grip.
class PanelResizeEdge : public QWidget {
public:
    PanelResizeEdge(LayersPanel &panel, QWidget *parent) : QWidget(parent), m_panel(panel)
    {
        setObjectName(QStringLiteral("layersPanelEdge"));
        setFixedWidth(8);
        setCursor(Qt::SplitHCursor);
        setToolTip(QStringLiteral("Drag to resize the panel"));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(QRect(width() / 2, 0, 1, height()), palette().color(QPalette::Mid));
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            m_start = std::pair(event->globalPosition().x(), double(m_panel.width()));
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_start)
            return;
        // Dragged left, the panel widens; whole points, within range.
        const double wanted = std::round(m_start->second - (event->globalPosition().x() - m_start->first));
        const double width = std::min(LayersPanel::maximumWidth, std::max(LayersPanel::minimumWidth, wanted));
        if (width == m_panel.width())
            return;
        m_panel.setFixedWidth(int(width));
        ContentView::setLayersPanelWidth(width);
    }
    void mouseReleaseEvent(QMouseEvent *) override { m_start = std::nullopt; }

private:
    LayersPanel &m_panel;
    std::optional<std::pair<double, double>> m_start;
};

// Swift's overlay: a 3-point accent ring inside the canvas.
class DropRing : public QWidget {
public:
    explicit DropRing(QWidget *parent) : QWidget(parent)
    {
        setObjectName(QStringLiteral("dropRing"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        hide();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(palette().color(QPalette::Highlight), 3));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(rect()).adjusted(4.5, 4.5, -4.5, -4.5), 8, 8);
    }
};

QFrame *divider(QFrame::Shape shape, QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setFrameShape(shape);
    line->setFrameShadow(QFrame::Plain);
    line->setForegroundRole(QPalette::Mid);
    return line;
}

QLabel *statusText(QLabel *label)
{
    QFont font = label->font();
    font.setPixelSize(11);
    label->setFont(font);
    label->setForegroundRole(QPalette::PlaceholderText);
    return label;
}

// A row's widest item shrinks first; this ends in dots.
class ElidedLabel : public QLabel {
public:
    using QLabel::QLabel;

protected:
    void paintEvent(QPaintEvent *) override
    {
        // A widget's painter starts with its foreground role's pen.
        QPainter painter(this);
        painter.drawText(rect(), Qt::AlignRight | Qt::AlignVCenter, fontMetrics().elidedText(text(), Qt::ElideRight, width()));
    }
};
}

ContentView::ContentView(EditorSession &session, ProjectController *projects, QWidget *parent)
    : QWidget(parent), m_session(session), m_projects(projects), m_column(new QVBoxLayout(this)), m_canvasSlot(new QGridLayout),
      m_canvas(new CanvasView(session, this)), m_layersPanel(new LayersPanel(session, this)), m_dock(new QWidget(this)), m_dropRing(new DropRing(this)), m_zoom(statusText(new QLabel(this))), m_dimensions(statusText(new QLabel(this))), m_colour(statusText(new QLabel(this))),
      m_activity(statusText(new ElidedLabel(this))),
      m_spinner(new QProgressBar(this))
{
    setMinimumSize(800, 520);
    m_column->setContentsMargins(0, 0, 0, 0);
    m_column->setSpacing(0);

    auto *tools = new QWidget(this);
    auto *toolColumn = new QVBoxLayout(tools);
    // Room for the palette's overhang; the tools stay centred.
    toolColumn->setContentsMargins(7, 16, 7, 12);
    toolColumn->setSpacing(10);
    for (const NavigationTool tool : railTools) {
        auto *button = new ToolButton(tool, tools);
        // A refused click stays silent: set the buttons again.
        connect(button, &QToolButton::clicked, this, [this, tool] {
            m_session.selectTool(tool);
            synchronize();
        });
        toolColumn->addWidget(button, 0, Qt::AlignHCenter);
        m_toolButtons.push_back({tool, button});
    }
    // Swift's 8 points above the swatches, less their overhang.
    toolColumn->addSpacing(5);
    toolColumn->addWidget(new ColorPaletteControls(session, tools), 0, Qt::AlignHCenter);
    toolColumn->addStretch(1);
    // Too short a window scrolls the rail, not the bars.
    auto *rail = new QScrollArea(this);
    rail->setObjectName(QStringLiteral("toolRail"));
    rail->setWidget(tools);
    rail->setWidgetResizable(true);
    rail->setFixedWidth(56);
    rail->setFrameShape(QFrame::NoFrame);
    rail->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rail->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *canvas = new QWidget(this);
    canvas->setLayout(m_canvasSlot);
    // Swift's canvas fills its area: no layout gutter.
    m_canvasSlot->setContentsMargins(0, 0, 0, 0);
    // The welcome sits over the canvas, as Swift's ZStack.
    m_canvasSlot->addWidget(m_canvas, 0, 0);
    m_canvasSlot->addWidget(m_dropRing, 0, 0);
    setAcceptDrops(true);

    auto *middle = new QHBoxLayout;
    middle->setSpacing(0);
    middle->addWidget(rail);
    middle->addWidget(divider(QFrame::VLine, this));
    // The rulers frame the canvas above and left, Swift's stacks.
    auto *framed = new QWidget(this);
    auto *rulers = new QGridLayout(framed);
    rulers->setContentsMargins(0, 0, 0, 0);
    rulers->setSpacing(0);
    m_rulers = {new CanvasRulerCorner(framed), new CanvasRulerView(session, CanvasGuide::Axis::horizontal, *m_canvas, framed),
                new CanvasRulerView(session, CanvasGuide::Axis::vertical, *m_canvas, framed)};
    rulers->addWidget(m_rulers[0], 0, 0);
    rulers->addWidget(m_rulers[1], 0, 1);
    rulers->addWidget(m_rulers[2], 1, 0);
    rulers->addWidget(canvas, 1, 1);
    middle->addWidget(framed, 1);
    m_layersPanel->setFixedWidth(int(layersPanelWidth()));
    middle->addWidget(new PanelResizeEdge(*m_layersPanel, this));
    middle->addWidget(m_layersPanel);
    m_dock->setObjectName(QStringLiteral("panelDock"));
    m_dock->setFixedWidth(FloatingPanel::dockedWidth);
    auto *docked = new QVBoxLayout(m_dock);
    docked->setContentsMargins(0, 0, 0, 0);
    m_dock->hide();
    middle->addWidget(m_dock);

    m_zoom->setObjectName(QStringLiteral("zoomStatus"));
    m_zoom->setFixedWidth(62);
    m_dimensions->setObjectName(QStringLiteral("canvasDimensions"));
    m_colour->setObjectName(QStringLiteral("colourStatus"));
    m_activity->setObjectName(QStringLiteral("activityStatus"));
    m_spinner->setRange(0, 0);
    m_spinner->setTextVisible(false);
    m_spinner->setFixedSize(36, 6);
    auto *status = new QWidget(this);
    status->setObjectName(QStringLiteral("statusBar"));
    status->setFixedHeight(30);
    auto *statusRow = new QHBoxLayout(status);
    statusRow->setContentsMargins(18, 0, 18, 0);
    statusRow->setSpacing(16);
    statusRow->addWidget(m_zoom);
    statusRow->addWidget(m_dimensions);
    statusRow->addWidget(m_colour);
    statusRow->addWidget(m_spinner, 0, Qt::AlignRight);
    // The hint takes what is left, and shrinks first.
    statusRow->addWidget(m_activity, 1);

    m_column->addWidget(divider(QFrame::HLine, this));
    m_column->addLayout(middle, 1);
    m_column->addWidget(divider(QFrame::HLine, this));
    m_column->addWidget(status);

    // The filter panel follows the session by itself.
    connect(&m_session, &EditorSession::changed, this, &ContentView::synchronize);
    synchronize();
}

// ~QWidget closes the window after members go; stop listening first.
ContentView::~ContentView()
{
    disconnect(&m_session, &EditorSession::changed, this, &ContentView::synchronize);
}

// Another tab's layer, files or a picture, while free.
bool ContentView::acceptsDrop(const QMimeData &data) const
{
    if (!(data.hasFormat(ProjectWorkspace::layerType) || ImageFileDrop::holdsImages(data)))
        return false;
    // Swift's session guards; a workspace's are canSwitch's own.
    if (!m_workspace)
        return !m_session.levels() && !m_session.isProjectBusy() && !m_session.showsNewDocument() && !m_session.showsImporter()
            && !m_session.renamingLayerID();
    return m_workspace->canSwitch() && m_workspace->canReceiveDrag(data, m_workspace->current().id);
}

void ContentView::dragEnterEvent(QDragEnterEvent *event)
{
    if (!acceptsDrop(*event->mimeData()))
        return;
    event->setDropAction(Qt::CopyAction);
    event->accept();
    m_dropRing->raise();
    m_dropRing->show();
}

void ContentView::dragMoveEvent(QDragMoveEvent *event)
{
    if (!acceptsDrop(*event->mimeData())) {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::CopyAction);
    event->accept();
}

void ContentView::dragLeaveEvent(QDragLeaveEvent *)
{
    m_dropRing->hide();
}

void ContentView::dropEvent(QDropEvent *event)
{
    m_dropRing->hide();
    if (!acceptsDrop(*event->mimeData()))
        return;
    event->setDropAction(Qt::CopyAction);
    event->accept();
    // Over the canvas the drop names a document point.
    std::optional<QPointF> point;
    const QPointF inCanvas = m_canvas->mapFrom(this, event->position().toPoint());
    if (m_session.document() && m_canvas->rect().contains(inCanvas.toPoint()))
        point = m_session.viewport.documentPoint(inCanvas, m_session.document()->size());
    if (m_workspace)
        m_workspace->receiveProviders(*event->mimeData(), m_workspace->current().id, point);
    else
        ImageFileDrop::importProviders(*event->mimeData(), m_session, point);
}

double ContentView::layersPanelWidth()
{
    const double stored = QSettings().value(QStringLiteral("layersPanelWidth"), LayersPanel::defaultWidth).toDouble();
    // A stored width outside the range is the default, logged.
    if (stored >= LayersPanel::minimumWidth && stored <= LayersPanel::maximumWidth)
        return stored;
    qCWarning(lcApp) << "layersPanelWidth" << stored << "is out of range, using" << LayersPanel::defaultWidth;
    return LayersPanel::defaultWidth;
}

void ContentView::setLayersPanelWidth(double width)
{
    QSettings().setValue(QStringLiteral("layersPanelWidth"), width);
}

// Shows what the session holds: every line compares, then sets.
void ContentView::synchronize()
{
    m_canvas->consumeFocusRequest(m_session.canvasFocusRequest());
    m_canvas->synchronizeDisplay();
    if (m_session.document() && !m_hadDocument)
        QMetaObject::invokeMethod(&m_session, &EditorSession::requestCanvasFocus, Qt::QueuedConnection);
    m_hadDocument = m_session.document().has_value();
    showHeader(m_session.tool());
    for (const auto &[tool, button] : m_toolButtons) {
        button->setChecked(m_session.tool() == tool);
        button->setKind(iconKind(tool));
    }
    const std::optional<CanvasDocument> &document = m_session.document();
    showWelcome(!document);
    for (QWidget *ruler : m_rulers)
        ruler->setVisible(m_session.showsRulers() && document);
    m_zoom->setVisible(document.has_value());
    m_dimensions->setVisible(document.has_value());
    if (document) {
        const QLocale english(QLocale::English, QLocale::UnitedStates);
        m_zoom->setText(percent(m_session.viewport.zoom()));
        m_dimensions->setText(QStringLiteral("%1 × %2 px").arg(english.toString(document->width), english.toString(document->height)));
    }
    m_colour->setText(document ? QStringLiteral("sRGB · Transparent") : QStringLiteral("Ready when you are"));
    m_spinner->setVisible(m_session.showsBusy() || m_session.isImporting());
    m_activity->setText(m_session.showsBusy() ? QStringLiteral("Working…") : m_session.isImporting() ? QStringLiteral("Importing images…")
                                                                             : hint(m_session.tool(), iconKind(m_session.tool()), m_session.blurMode(), m_session.shapeKind()));
    showImporter();
    showConversionSheet();
    showRawDevelopSheet();
    showAlert(m_importAlert, QStringLiteral("Import couldn’t finish"), m_session.importError(), [this] { m_session.setImportError(std::nullopt); });
    showAlert(m_brushAlert, QStringLiteral("Couldn’t paint"), m_session.brushError(), [this] { m_session.setBrushError(std::nullopt); });
    showAlert(m_cropAlert, QStringLiteral("Couldn’t crop"), m_session.cropError(), [this] { m_session.setCropError(std::nullopt); });
    synchronizePanels();
}

// Swift's `if document == nil` makes a fresh sheet.
void ContentView::showWelcome(bool shown)
{
    if (shown == (m_welcome != nullptr))
        return;
    delete m_welcome;
    m_welcome = nullptr;
    if (!shown)
        return;
    m_welcome = new NewCanvasSheet(
        m_session, [this](int width, int height) { m_session.createNewProject(width, height); },
        [this] {
            if (m_projects)
                m_projects->open();
        },
        this);
    m_canvasSlot->addWidget(m_welcome, 0, 0, Qt::AlignCenter);
    m_welcome->show();
}

void ContentView::showHeader(NavigationTool tool)
{
    // Shared bars: Hand and Zoom, selection tools, brush tools.
    const auto kind = [](NavigationTool each) {
        return each == NavigationTool::hand || each == NavigationTool::zoom ? NavigationTool::hand
               : isSelectionTool(each)                                     ? NavigationTool::lasso
               : isBrushTool(each)                                         ? NavigationTool::brush
                                                                           : each;
    };
    if (m_shownTool && kind(*m_shownTool) == kind(tool))
        return;
    m_shownTool = tool;
    delete m_header;
    // A tool without a bar yet keeps the bar's height.
    if (tool == NavigationTool::idle)
        m_header = new ToolHeaderBar(QStringLiteral("Select a tool"), this);
    else if (tool == NavigationTool::eyedropper)
        m_header = eyedropperBar(m_session, this);
    else if (kind(tool) == NavigationTool::hand)
        m_header = new NavigationToolHeader(m_session, this);
    else if (tool == NavigationTool::move)
        m_header = new TransformInspector(m_session, this);
    else if (isSelectionTool(tool))
        m_header = new LassoControls(m_session, this);
    else if (isBrushTool(tool))
        m_header = new BrushControls(m_session, this);
    else if (tool == NavigationTool::gradient)
        m_header = new GradientControls(m_session, this);
    else if (tool == NavigationTool::shape)
        m_header = new ShapeControls(m_session, this);
    else if (tool == NavigationTool::type)
        m_header = new TypeControls(m_session, this);
    else if (tool == NavigationTool::crop)
        m_header = new CropControls(m_session, this);
    else
        m_header = new ToolHeaderBar(QString(), this);
    m_header->setObjectName(QStringLiteral("toolHeader"));
    m_column->insertWidget(0, m_header);
    // A layout shows a late child only later: show now.
    m_header->show();
}
