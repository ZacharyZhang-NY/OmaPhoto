#include "Document/LayerAdjustment.h"
#include "Document/EditorSession.h"
#include "Document/PixelInvert.h"
#include <QRandomGenerator>
#include <cmath>

QString rawValue(AdjustmentKind kind)
{
    switch (kind) {
    case AdjustmentKind::hsv: return QStringLiteral("Hue/Saturation");
    case AdjustmentKind::levels: return QStringLiteral("Levels");
    case AdjustmentKind::curves: return QStringLiteral("Curves");
    case AdjustmentKind::exposure: return QStringLiteral("Exposure");
    case AdjustmentKind::gradientMap: return QStringLiteral("Gradient Map");
    case AdjustmentKind::grain: return QStringLiteral("Grain");
    case AdjustmentKind::invert: return QStringLiteral("Invert");
    case AdjustmentKind::blackWhite: return QStringLiteral("Black & White");
    case AdjustmentKind::colorBalance: return QStringLiteral("Color Balance");
    }
    throw std::logic_error("unknown adjustment kind");
}

std::optional<AdjustmentKind> adjustmentKind(const QString &text)
{
    for (const AdjustmentKind kind : allAdjustmentKinds) {
        if (rawValue(kind) == text)
            return kind;
    }
    return std::nullopt;
}

std::optional<FilterKind> filterKind(AdjustmentKind kind)
{
    switch (kind) {
    case AdjustmentKind::curves: return FilterKind::curves;
    case AdjustmentKind::exposure: return FilterKind::exposure;
    case AdjustmentKind::gradientMap: return FilterKind::gradientMap;
    case AdjustmentKind::grain: return FilterKind::grain;
    case AdjustmentKind::blackWhite: return FilterKind::blackWhite;
    case AdjustmentKind::colorBalance: return FilterKind::colorBalance;
    // Panels of their own; Invert has nothing to set.
    case AdjustmentKind::hsv:
    case AdjustmentKind::levels:
    case AdjustmentKind::invert: return std::nullopt;
    }
    throw std::logic_error("unknown adjustment kind");
}

bool isEditable(AdjustmentKind kind)
{
    return kind != AdjustmentKind::invert;
}

HueSaturationSettings LayerAdjustment::resolvedHSV() const
{
    return hsvSettings.value_or(HueSaturationSettings(hue, saturation, lightness, colorize));
}

ExposureSettings LayerAdjustment::exposure() const
{
    return exposureSettings.value_or(ExposureSettings());
}

void LayerAdjustment::setExposure(const ExposureSettings &value)
{
    exposureSettings = value;
}

GradientMapSettings LayerAdjustment::gradientMap() const
{
    return gradientMapSettings.value_or(GradientMapSettings());
}

void LayerAdjustment::setGradientMap(const GradientMapSettings &value)
{
    gradientMapSettings = value;
}

GrainSettings LayerAdjustment::grain() const
{
    return grainSettings.value_or(GrainSettings());
}

void LayerAdjustment::setGrain(const GrainSettings &value)
{
    grainSettings = value;
}

BlackWhiteSettings LayerAdjustment::blackWhite() const
{
    return blackWhiteSettings.value_or(BlackWhiteSettings());
}

void LayerAdjustment::setBlackWhite(const BlackWhiteSettings &value)
{
    blackWhiteSettings = value;
}

ColorBalanceSettings LayerAdjustment::colorBalance() const
{
    return colorBalanceSettings.value_or(ColorBalanceSettings());
}

void LayerAdjustment::setColorBalance(const ColorBalanceSettings &value)
{
    colorBalanceSettings = value;
}

// Bounds refuse what is no number: no isFinite terms.
bool LayerAdjustment::isValid() const
{
    const auto within = [](const RangeAdjustment &values) {
        return std::abs(values.hue) <= 360 && std::abs(values.saturation) <= 100 && std::abs(values.lightness) <= 100;
    };
    if (!within(RangeAdjustment{hue, saturation, lightness}))
        return false;
    const HueSaturationSettings hsv = resolvedHSV();
    for (const auto &[range, values] : hsv.adjustments) {
        if (!within(values))
            return false;
    }
    for (const auto &[range, band] : hsv.bands) {
        for (const double handle : band.handles()) {
            if (!std::isfinite(handle))
                return false;
        }
    }
    for (const LevelRange &range : levels.ranges) {
        if (!(range == range.normalized()))
            return false;
    }
    return curves.isValid() && exposure().isValid() && gradientMap().isValid() && grain().isValid() && blackWhite().isValid()
        && colorBalance().isValid();
}

QImage LayerAdjustment::apply(const QImage &image, std::optional<QRectF> region) const
{
    switch (kind) {
    case AdjustmentKind::hsv:
        return HueSaturationFilter::run(HueSaturationJob{image, resolvedHSV(), std::nullopt, QTransform(), false}).image;
    case AdjustmentKind::levels: return LevelsFilter::run(LevelsJob{image, levels, std::nullopt, QTransform()});
    case AdjustmentKind::curves: return curves.apply(image);
    case AdjustmentKind::exposure: return exposure().apply(image);
    case AdjustmentKind::gradientMap: return gradientMap().apply(image);
    case AdjustmentKind::blackWhite: return blackWhite().apply(image);
    case AdjustmentKind::colorBalance: return colorBalance().apply(image);
    case AdjustmentKind::invert: return PixelInvert::run(PixelInvert::Job{image, false, QTransform(), std::nullopt});
    case AdjustmentKind::grain: {
        // Grain sits in the document, however the image is cut.
        const QRectF area = region.value_or(QRectF(0, 0, image.width(), image.height()));
        return grain().apply(image, area.topLeft(), area.width() / std::max(1, image.width()));
    }
    }
    throw std::logic_error("unknown adjustment kind");
}

void EditorSession::addAdjustment(AdjustmentKind kind)
{
    if (!canEditLayers() || m_document->layers.size() >= 10'000)
        return;
    ImageLayer layer(rawValue(kind), m_document->size());
    LayerAdjustment adjustment{kind};
    // A Gradient Map runs from foreground to background, as Photoshop's.
    if (kind == AdjustmentKind::gradientMap)
        adjustment.setGradientMap(GradientMapSettings{AdjustmentColor(foregroundColor()), AdjustmentColor(m_backgroundColor)});
    // Each Grain layer rolls a pattern of its own.
    if (kind == AdjustmentKind::grain) {
        GrainSettings grain = adjustment.grain();
        grain.seed = QRandomGenerator::global()->generate();
        adjustment.setGrain(grain);
    }
    layer.adjustment = adjustment;
    const std::optional<ImageLayer> active = activeLayer();
    layer.parentID = active && active->isGroup ? m_activeLayerID : active ? active->parentID : std::nullopt;
    const int activeIndex = indexOf(m_document->layers, m_activeLayerID);
    const int index = activeIndex >= 0 ? activeIndex + 1 : int(m_document->layers.size());
    beginEdit(QStringLiteral("New %1 Adjustment").arg(rawValue(kind)));
    m_document->layers.insert(m_document->layers.begin() + index, layer);
    if (layer.parentID)
        m_collapsedGroupIDs.remove(*layer.parentID);
    setActiveLayerID(layer.id);
    endEdit();
    // Invert has nothing to set: it applies, no editor opens.
    if (isEditable(kind))
        setAdjustmentEditingID(layer.id);
}

void EditorSession::updateAdjustment(QUuid id, const LayerAdjustment &value)
{
    const int index = m_document ? indexOf(m_document->layers, id) : -1;
    if (index < 0 || !value.isValid())
        return;
    m_document->layers[size_t(index)].adjustment = value;
    ++m_brushRevision;
    notify();
}
