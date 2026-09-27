#include "ContentView.h"
#include <QLocale>
#include <map>

// The status bar's words: zoom, and each tool's hint.
QString ContentView::percent(double zoom)
{
    QString number = QLocale(QLocale::English, QLocale::UnitedStates).toString(zoom * 100, 'f', 1);
    if (number.endsWith(QLatin1String(".0")))
        number.chop(2);
    return number + QLatin1Char('%');
}

// The kind two tools' icons and hints follow.
ToolIconKind ContentView::iconKind(NavigationTool tool) const
{
    if (tool == NavigationTool::marquee && m_session.marqueeKind() == LassoKind::ellipse)
        return ToolIconKind::ellipse;
    if (tool == NavigationTool::lasso && m_session.lassoKind() == LassoKind::polygonal)
        return ToolIconKind::polygonal;
    if (tool == NavigationTool::brush && m_session.brushMode() == BrushToolMode::erase)
        return ToolIconKind::eraser;
    if (tool == NavigationTool::wand && m_session.wandMode() == WandMode::object)
        return ToolIconKind::object;
    return ToolIconKind::plain;
}

QString ContentView::hint(NavigationTool tool, ToolIconKind kind, BlurToolMode smear, ShapeKind shape)
{
    if (tool == NavigationTool::marquee && kind == ToolIconKind::ellipse)
        return QStringLiteral("Drag an ellipse · Shift add · Alt subtract · Shift again mid-drag circle · Drag inside to move · Delete clears · Ctrl+D deselect");
    if (tool == NavigationTool::lasso && kind == ToolIconKind::polygonal)
        return QStringLiteral("Click corners · Click start, double-click or Enter to close · Delete removes corner · Escape cancel");
    if (tool == NavigationTool::wand && kind == ToolIconKind::object)
        return QStringLiteral("Click an object to select its outline · Tab for Wand · Shift add · Alt subtract · Drag inside to move · Ctrl-drag moves pixels · Delete clears · Ctrl+D deselect");
    if (tool == NavigationTool::brush && kind == ToolIconKind::eraser)
        return QStringLiteral("Drag to erase · [ ] size · Shift-[ ] hardness · 1–0 opacity · Escape cancel · Space to pan");
    if (tool == NavigationTool::blur)
        return (smear == BlurToolMode::blur     ? QStringLiteral("Drag to soften")
                : smear == BlurToolMode::smudge ? QStringLiteral("Drag to smudge")
                                                : QStringLiteral("Drag to push pixels"))
            + QStringLiteral(" · [ ] size · Shift-[ ] hardness · 1–0 strength · Space to pan");
    if (tool == NavigationTool::shape)
        return QStringLiteral("Drag to draw a shape on a new layer · Shift %1 · Alt from center · Shift-U or Tab for the next shape · Escape cancel · Space to pan")
            .arg(shape == ShapeKind::line ? QStringLiteral("45°") : shape == ShapeKind::rectangle ? QStringLiteral("square") : QStringLiteral("circle"));
    // Swift's last branch: the Eyedropper shows Zoom's words too.
    static const std::map<NavigationTool, const char *> hints = {
        {NavigationTool::marquee, "Drag a rectangle · Shift add · Alt subtract · Shift again mid-drag square · Drag inside to move · Ctrl-drag moves pixels · Delete clears · Ctrl+D deselect"},
        {NavigationTool::wand, "Click to select similar colors · Tab for Object · Shift add · Alt subtract · Drag inside to move · Ctrl-drag moves pixels · Delete clears · Ctrl+D deselect"},
        {NavigationTool::lasso, "Drag to select · Drag inside to move · Shift add · Alt subtract · Delete clears · Alt+Backspace/Ctrl+Backspace fill · Ctrl+D deselect"},
        {NavigationTool::brush, "Drag to paint · [ ] size · Shift-[ ] hardness · 1–0 opacity · Escape cancel · Space to pan"},
        {NavigationTool::cloneStamp, "Alt-click to set the source · Drag to clone · [ ] size · Shift-[ ] hardness · 1–0 opacity · Space to pan"},
        {NavigationTool::spotHealing, "Drag over blemishes to heal · [ ] size · Shift-[ ] hardness · Escape cancel · Space to pan"},
        {NavigationTool::type, "Drag a text box · Click text to edit · Drag box handles to resize · Ctrl+Return finish · Escape cancel"},
        {NavigationTool::gradient, "Drag to draw · Drag ends to adjust · Shift 45° · 1–0 opacity · Enter apply · Escape cancel"},
        {NavigationTool::crop, "Drag to crop · Enter apply · Escape cancel · Space to pan"},
        {NavigationTool::move, "Drag to move · Handles to resize · Circle to rotate · 1–0 layer opacity · Space to pan"},
        {NavigationTool::hand, "Drag to pan · Pinch to zoom"},
        {NavigationTool::idle, "No tool selected · Press a tool's key to pick one · Space to pan"},
        {NavigationTool::eyedropper, "Click to zoom in · Alt-click to zoom out · Drag right or left to zoom smoothly · Space to pan"},
        {NavigationTool::zoom, "Click to zoom in · Alt-click to zoom out · Drag right or left to zoom smoothly · Space to pan"},
    };
    return QString::fromUtf8(hints.at(tool));
}
