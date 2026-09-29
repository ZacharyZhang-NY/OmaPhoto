#include "Document/EditorSession.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectStore.h"
#include <stdexcept>

QString rawValue(GradientStyle style)
{
    switch (style) {
    case GradientStyle::foregroundToBackground:
        return QStringLiteral("Foreground to Background");
    case GradientStyle::foregroundToTransparent:
        return QStringLiteral("Foreground to Transparent");
    }
    throw std::logic_error("unknown gradient style");
}

QString rawValue(GradientShape shape)
{
    switch (shape) {
    case GradientShape::linear:
        return QStringLiteral("Linear");
    case GradientShape::radial:
        return QStringLiteral("Radial");
    }
    throw std::logic_error("unknown gradient shape");
}

void EditorSession::setGradientSettings(const GradientSettings &settings)
{
    m_gradientSettings = settings;
    refreshGradient();
    notify();
}

void EditorSession::beginGradient(QPointF point)
{
    const std::optional<ImageLayer> layer = activeLayer();
    if (m_tool != NavigationTool::gradient || !(canPaint() || m_gradientEdit) || !layer)
        return;
    // A new line on this target replaces the pending one.
    if (m_gradientEdit && m_gradientEdit->raster->layer.id == layer->id && m_gradientEdit->raster->isMask == m_isMaskSelected) {
        m_gradientEdit->start = point;
        m_gradientEdit->end = point;
        refreshGradient();
        return;
    }
    if (!canPaint())
        return;
    finishOpacityEdit();
    try {
        m_gradientEdit = GradientEdit{std::shared_ptr<BrushStroke>(makeRasterEdit(*layer, BrushSettings(), true)), point, point};
        ++m_brushRevision;
        notify();
    } catch (const ProjectError &error) {
        setBrushError(QString::fromUtf8(error.what()));
    } catch (const ExportError &error) {
        setBrushError(QString::fromUtf8(error.what()));
    }
}

void EditorSession::moveGradient(std::optional<QPointF> start, std::optional<QPointF> end)
{
    if (!m_gradientEdit)
        return;
    if (start)
        m_gradientEdit->start = *start;
    if (end)
        m_gradientEdit->end = *end;
    refreshGradient();
}

void EditorSession::refreshGradient()
{
    if (!m_gradientEdit)
        return;
    if (m_gradientEdit->hasLine()) {
        try {
            m_gradientEdit->raster->fillGradient(m_gradientSettings.shape, m_gradientEdit->start, m_gradientEdit->end,
                                                 gradientColors(m_gradientEdit->raster->isMask), m_gradientSettings.opacity);
        } catch (const ProjectError &error) {
            cancelGradient();
            setBrushError(QString::fromUtf8(error.what()));
            return;
        } catch (const ExportError &error) {
            cancelGradient();
            setBrushError(QString::fromUtf8(error.what()));
            return;
        }
    }
    ++m_brushRevision;
    notify();
}

std::array<QColor, 2> EditorSession::gradientColors(bool mask) const
{
    // A mask's colours are gray, the palette's red.
    const auto color = [mask](const PaletteColor &value, double alpha) {
        return mask ? QColor::fromRgbF(float(value.red), float(value.red), float(value.red), float(alpha)) : value.color(alpha);
    };
    const PaletteColor foreground = paletteColor(false);
    std::array<QColor, 2> colors = m_gradientSettings.style == GradientStyle::foregroundToBackground
                                       ? std::array{color(foreground, 1), color(paletteColor(true), 1)}
                                       : std::array{color(foreground, 1), color(foreground, 0)};
    if (m_gradientSettings.reversed)
        std::swap(colors[0], colors[1]);
    return colors;
}

void EditorSession::endGradientDrag()
{
    if (m_gradientEdit && !m_gradientEdit->hasLine())
        cancelGradient();
}

void EditorSession::cancelGradient()
{
    if (!m_gradientEdit)
        return;
    m_gradientEdit = std::nullopt;
    ++m_brushRevision;
    notify();
}

void EditorSession::commitGradient(std::function<void()> done)
{
    if (!m_gradientEdit || m_isProjectBusy) {
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
        return;
    }
    if (!m_gradientEdit->hasLine()) {
        cancelGradient();
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
        return;
    }
    const std::shared_ptr<BrushStroke> raster = m_gradientEdit->raster;
    commitRasterEdit(raster, raster->isMask ? QStringLiteral("Gradient Mask") : QStringLiteral("Gradient"), {}, [this, raster, done] {
        // Swift's `gradientEdit === edit`: the same edit ends here.
        if (m_gradientEdit && m_gradientEdit->raster == raster)
            cancelGradient();
        if (done)
            done();
    });
}

// Swift's `Task`: the commit starts once the caller returns.
void EditorSession::resolveGradient()
{
    if (m_gradientEdit)
        QMetaObject::invokeMethod(this, [this] { commitGradient(); }, Qt::QueuedConnection);
}
