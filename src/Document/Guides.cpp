#include "Document/Guides.h"
#include "Document/EditorSession.h"
#include "Logging.h"
#include <algorithm>
#include <cmath>

CanvasGuide CanvasGuide::offset(double x, double y) const
{
    CanvasGuide guide = *this;
    guide.position += axis == Axis::vertical ? x : y;
    return guide;
}

CanvasGuide CanvasGuide::scaled(double x, double y) const
{
    CanvasGuide guide = *this;
    guide.position *= axis == Axis::vertical ? x : y;
    return guide;
}

CanvasGuide CanvasGuide::mirrored(bool horizontally, double center) const
{
    CanvasGuide guide = *this;
    if ((horizontally && axis == Axis::vertical) || (!horizontally && axis == Axis::horizontal))
        guide.position = 2 * center - guide.position;
    return guide;
}

QString rawValue(CanvasGuide::Axis axis)
{
    return axis == CanvasGuide::Axis::vertical ? QStringLiteral("vertical") : QStringLiteral("horizontal");
}

std::optional<CanvasGuide::Axis> guideAxis(const QString &text)
{
    if (text == QLatin1String("vertical"))
        return CanvasGuide::Axis::vertical;
    if (text == QLatin1String("horizontal"))
        return CanvasGuide::Axis::horizontal;
    return std::nullopt;
}

LayoutGrid::LayoutGrid(int spacing, int subdivisions)
    : spacing(std::clamp(spacing, spacingLow, spacingHigh)), subdivisions(std::min({std::max(subdivisions, subdivisionLow), subdivisionHigh, this->spacing}))
{
}

// Counted, not summed: uneven steps stay on the majors.
std::vector<double> LayoutGrid::lines(double length) const
{
    if (!(length >= 0))
        return {0};
    const int count = int(std::floor(length / step() + 0.001));
    std::vector<double> result;
    for (int index = 0; index <= count; ++index)
        result.push_back(std::round(index * step()));
    return result;
}

bool LayoutGrid::isMajor(double value) const
{
    return std::abs(std::fmod(std::round(value), double(spacing))) < 0.001;
}

LayoutGrid LayoutGrid::stored()
{
    return LayoutGrid(ToolDefaults::integer(QStringLiteral("gridSpacing"), 64), ToolDefaults::integer(QStringLiteral("gridSubdivisions"), 8));
}

void LayoutGrid::store() const
{
    ToolDefaults::set(spacing, QStringLiteral("gridSpacing"));
    ToolDefaults::set(subdivisions, QStringLiteral("gridSubdivisions"));
}

QString rawValue(GridAppearance::Preset preset)
{
    using Preset = GridAppearance::Preset;
    switch (preset) {
    case Preset::lightGray: return QStringLiteral("Light Gray");
    case Preset::lightBlue: return QStringLiteral("Light Blue");
    case Preset::lightRed: return QStringLiteral("Light Red");
    case Preset::green: return QStringLiteral("Green");
    case Preset::mediumBlue: return QStringLiteral("Medium Blue");
    case Preset::yellow: return QStringLiteral("Yellow");
    case Preset::magenta: return QStringLiteral("Magenta");
    case Preset::cyan: return QStringLiteral("Cyan");
    case Preset::black: return QStringLiteral("Black");
    case Preset::custom: return QStringLiteral("Custom");
    }
    throw std::logic_error("unknown grid preset");
}

QString rawValue(GridAppearance::Style style)
{
    switch (style) {
    case GridAppearance::Style::lines: return QStringLiteral("Lines");
    case GridAppearance::Style::dashedLines: return QStringLiteral("Dashed Lines");
    case GridAppearance::Style::dots: return QStringLiteral("Dots");
    }
    throw std::logic_error("unknown grid style");
}

std::optional<PaletteColor> presetColor(GridAppearance::Preset preset)
{
    using Preset = GridAppearance::Preset;
    switch (preset) {
    case Preset::lightGray: return PaletteColor{0.7, 0.7, 0.7};
    case Preset::lightBlue: return PaletteColor{0.29, 0.78, 1};
    case Preset::lightRed: return PaletteColor{1, 0.4, 0.4};
    case Preset::green: return PaletteColor{0.25, 0.8, 0.25};
    case Preset::mediumBlue: return PaletteColor{0.2, 0.4, 1};
    case Preset::yellow: return PaletteColor{1, 1, 0};
    case Preset::magenta: return PaletteColor{1, 0, 1};
    case Preset::cyan: return PaletteColor{0, 1, 1};
    case Preset::black: return PaletteColor::black();
    case Preset::custom: return std::nullopt;
    }
    throw std::logic_error("unknown grid preset");
}

std::vector<double> dashes(GridAppearance::Style style)
{
    switch (style) {
    case GridAppearance::Style::lines: return {};
    case GridAppearance::Style::dashedLines: return {4, 3};
    case GridAppearance::Style::dots: return {1, 2};
    }
    throw std::logic_error("unknown grid style");
}

PaletteColor GridAppearance::color() const
{
    return presetColor(preset).value_or(customColor);
}

// A grid that is on never disappears: 1% at least.
double GridAppearance::majorAlpha() const
{
    return std::clamp(opacity, opacityLow, opacityHigh) / 100.0;
}

namespace {
// A stored name, else the default and a warning.
template <typename Value, typename Values>
Value named(const QString &key, const Values &values, Value fallback)
{
    const QString stored = ToolDefaults::text(key);
    for (const Value value : values)
        if (rawValue(value) == stored)
            return value;
    if (!stored.isEmpty())
        qCWarning(lcApp).noquote() << "ignoring the tool setting" << key << "of" << stored;
    return fallback;
}
}

GridAppearance GridAppearance::stored()
{
    GridAppearance appearance;
    appearance.preset = named(QStringLiteral("gridColor"), allGridPresets, appearance.preset);
    const QString custom = ToolDefaults::text(QStringLiteral("gridCustomColor"));
    if (const std::optional<PaletteColor> color = PaletteColor::fromHex(custom))
        appearance.customColor = *color;
    else if (!custom.isEmpty())
        qCWarning(lcApp).noquote() << "ignoring the tool setting gridCustomColor of" << custom;
    appearance.style = named(QStringLiteral("gridStyle"), allGridStyles, appearance.style);
    appearance.opacity = ToolDefaults::integer(QStringLiteral("gridOpacity"), appearance.opacity);
    return appearance;
}

void GridAppearance::store() const
{
    ToolDefaults::set(rawValue(preset), QStringLiteral("gridColor"));
    ToolDefaults::set(customColor.hex(), QStringLiteral("gridCustomColor"));
    ToolDefaults::set(rawValue(style), QStringLiteral("gridStyle"));
    ToolDefaults::set(opacity, QStringLiteral("gridOpacity"));
}

void EditorSession::setLayoutGrid(const LayoutGrid &grid)
{
    if (grid == m_layoutGrid)
        return;
    m_layoutGrid = grid;
    grid.store();
    notify();
}

void EditorSession::setGridAppearance(const GridAppearance &appearance)
{
    if (appearance == m_gridAppearance)
        return;
    m_gridAppearance = appearance;
    appearance.store();
    notify();
}

void EditorSession::setShowsGrid(bool shows)
{
    m_showsGrid = shows;
    ToolDefaults::set(shows, QStringLiteral("grid"));
    notify();
}

void EditorSession::setShowsGuides(bool shows)
{
    m_showsGuides = shows;
    ToolDefaults::set(shows, QStringLiteral("guides"));
    notify();
}

void EditorSession::setShowsRulers(bool shows)
{
    m_showsRulers = shows;
    ToolDefaults::set(shows, QStringLiteral("rulers"));
    notify();
}

void EditorSession::setSnapEnabled(bool enabled)
{
    m_snapEnabled = enabled;
    ToolDefaults::set(enabled, QStringLiteral("snap"));
    notify();
}

void EditorSession::setSnapToGuides(bool snaps)
{
    m_snapToGuides = snaps;
    ToolDefaults::set(snaps, QStringLiteral("snapGuides"));
    notify();
}

void EditorSession::setSnapToGrid(bool snaps)
{
    m_snapToGrid = snaps;
    ToolDefaults::set(snaps, QStringLiteral("snapGrid"));
    notify();
}

void EditorSession::setSnapToLayers(bool snaps)
{
    m_snapToLayers = snaps;
    ToolDefaults::set(snaps, QStringLiteral("snapLayers"));
    notify();
}

void EditorSession::setSnapToDocumentBounds(bool snaps)
{
    m_snapToDocumentBounds = snaps;
    ToolDefaults::set(snaps, QStringLiteral("snapBounds"));
    notify();
}

void EditorSession::setLocksGuides(bool locks)
{
    m_locksGuides = locks;
    ToolDefaults::set(locks, QStringLiteral("lockGuides"));
    notify();
}

bool EditorSession::canClearGuides() const
{
    return m_document && !m_document->guides.empty();
}

bool EditorSession::canEditGuides() const
{
    return m_document && !m_locksGuides && !m_isProjectBusy && !m_isImporting && !m_showsNewDocument && !m_levels && !m_hueSaturation
        && !m_filterEdit && !m_renamingLayerID;
}

std::vector<CanvasGuide> EditorSession::displayedGuides() const
{
    std::vector<CanvasGuide> guides = m_document ? m_document->guides : std::vector<CanvasGuide>();
    if (!m_guideDrag)
        return guides;
    const CanvasGuide current{m_guideDrag->id, m_guideDrag->axis, m_guideDrag->position};
    const auto found = std::find_if(guides.begin(), guides.end(), [&](const CanvasGuide &guide) { return guide.id == current.id; });
    if (found != guides.end())
        guides[size_t(found - guides.begin())] = current;
    else if (m_guideDrag->isNew)
        guides.push_back(current);
    return guides;
}

std::optional<CanvasGuide> EditorSession::hitGuide(QPointF viewPoint, double tolerance) const
{
    if (!m_showsGuides || m_locksGuides || !m_document)
        return std::nullopt;
    std::optional<CanvasGuide> best;
    double nearest = 0;
    for (const CanvasGuide &guide : displayedGuides()) {
        const QPointF at = viewport.viewPoint(QPointF(guide.position, guide.position), m_document->size());
        const double distance = guide.axis == CanvasGuide::Axis::vertical ? std::abs(viewPoint.x() - at.x()) : std::abs(viewPoint.y() - at.y());
        if (distance <= tolerance && (!best || distance < nearest)) {
            best = guide;
            nearest = distance;
        }
    }
    return best;
}

void EditorSession::beginGuideCreation(CanvasGuide::Axis axis, double position)
{
    if (!canEditGuides())
        return;
    m_showsGuides = true;
    ToolDefaults::set(true, QStringLiteral("guides"));
    m_guideDrag = GuideDrag{QUuid::createUuid(), axis, snappedGuidePosition(position, axis, std::nullopt), true, std::nullopt};
    notify();
}

void EditorSession::beginGuideMove(const CanvasGuide &guide)
{
    if (!canEditGuides())
        return;
    m_guideDrag = GuideDrag{guide.id, guide.axis, guide.position, false, guide.position};
    notify();
}

void EditorSession::moveGuideDrag(double position)
{
    if (!m_guideDrag)
        return;
    m_guideDrag->position = snappedGuidePosition(position, m_guideDrag->axis, m_guideDrag->id);
    notify();
}

void EditorSession::finishGuideDrag(bool removing)
{
    if (!m_guideDrag)
        return;
    const GuideDrag drag = *std::exchange(m_guideDrag, std::nullopt);
    if (removing) {
        // A new guide dropped on a ruler was never there.
        if (!drag.isNew) {
            beginEdit(QStringLiteral("Delete Guide"));
            std::erase_if(m_document->guides, [&](const CanvasGuide &guide) { return guide.id == drag.id; });
            endEdit();
        }
        notify();
        return;
    }
    if (drag.isNew) {
        beginEdit(QStringLiteral("New Guide"));
        m_document->guides.push_back({drag.id, drag.axis, drag.position});
        endEdit();
    } else if (drag.original != drag.position) {
        beginEdit(QStringLiteral("Move Guide"));
        for (CanvasGuide &guide : m_document->guides) {
            if (guide.id == drag.id)
                guide.position = drag.position;
        }
        endEdit();
    }
    notify();
}

void EditorSession::cancelGuideDrag()
{
    m_guideDrag = std::nullopt;
    notify();
}

void EditorSession::clearGuides()
{
    if (!canClearGuides())
        return;
    beginEdit(QStringLiteral("Clear Guides"));
    m_document->guides.clear();
    endEdit();
}

void EditorSession::addGuide(const CanvasGuide &guide)
{
    if (!canEditGuides())
        return;
    m_showsGuides = true;
    ToolDefaults::set(true, QStringLiteral("guides"));
    beginEdit(QStringLiteral("New Guide"));
    m_document->guides.push_back(guide);
    endEdit();
}

SnapGuides EditorSession::alignmentSnapTargets(const QSet<QUuid> &moving, bool includeCenters) const
{
    SnapGuides targets;
    if (!m_snapEnabled || !m_document)
        return targets;
    const QSizeF size = m_document->size();
    if (m_snapToDocumentBounds) {
        targets.xs = {0, size.width()};
        targets.ys = {0, size.height()};
        if (includeCenters) {
            targets.xs.push_back(size.width() / 2);
            targets.ys.push_back(size.height() / 2);
        }
    }
    if (m_snapToLayers) {
        for (const ImageLayer &layer : m_document->renderLayers()) {
            if (!layer.asset || moving.contains(layer.id))
                continue;
            const QRectF box = TransformSnap::box(displayedTransform(layer));
            const QPointF low = box.topLeft(), high = box.bottomRight();
            // Swift's order: low edge, the middle when asked, high edge.
            targets.xs.push_back(std::round(low.x()));
            targets.ys.push_back(std::round(low.y()));
            if (includeCenters) {
                targets.xs.push_back(std::round((low.x() + high.x()) / 2));
                targets.ys.push_back(std::round((low.y() + high.y()) / 2));
            }
            targets.xs.push_back(std::round(high.x()));
            targets.ys.push_back(std::round(high.y()));
        }
    }
    // Hidden extras do not snap, as in Photoshop.
    if (m_snapToGrid && m_showsGrid) {
        for (double x : m_layoutGrid.lines(size.width()))
            targets.xs.push_back(x);
        for (double y : m_layoutGrid.lines(size.height()))
            targets.ys.push_back(y);
    }
    if (m_snapToGuides && m_showsGuides) {
        for (const CanvasGuide &guide : displayedGuides())
            (guide.axis == CanvasGuide::Axis::vertical ? targets.xs : targets.ys).push_back(guide.position);
    }
    return targets;
}

double EditorSession::snappedGuidePosition(double position, CanvasGuide::Axis axis, std::optional<QUuid> excluding) const
{
    if (!m_snapEnabled || !m_document)
        return position;
    const double tolerance = TransformSnap::distance / std::max(viewport.pointsPerPixel(), 0.0001);
    const bool vertical = axis == CanvasGuide::Axis::vertical;
    const double length = vertical ? m_document->size().width() : m_document->size().height();
    std::vector<double> targets;
    if (m_snapToGrid && m_showsGrid)
        targets = m_layoutGrid.lines(length);
    if (m_snapToGuides && m_showsGuides) {
        for (const CanvasGuide &guide : displayedGuides()) {
            if (guide.axis == axis && guide.id != excluding)
                targets.push_back(guide.position);
        }
    }
    if (m_snapToDocumentBounds)
        targets.insert(targets.end(), {0, length / 2, length});
    if (m_snapToLayers) {
        for (const ImageLayer &layer : m_document->renderLayers()) {
            if (!layer.asset)
                continue;
            const QRectF box = TransformSnap::box(displayedTransform(layer));
            const QPointF low = box.topLeft(), high = box.bottomRight();
            const double from = vertical ? low.x() : low.y(), to = vertical ? high.x() : high.y();
            targets.insert(targets.end(), {std::round(from), std::round((from + to) / 2), std::round(to)});
        }
    }
    // The nearest within reach; the first of equals wins.
    std::optional<double> best;
    for (const double target : targets) {
        if (std::abs(target - position) <= tolerance && (!best || std::abs(*best - position) > std::abs(target - position)))
            best = target;
    }
    return best.value_or(position);
}
