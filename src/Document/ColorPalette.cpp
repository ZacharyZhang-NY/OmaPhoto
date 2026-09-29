#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include <QPainter>
#include <algorithm>
#include <cmath>

PaletteColor PaletteColor::quantized() const
{
    const auto snap = [](double value) { return std::round(value * 255) / 255; };
    return {snap(red), snap(green), snap(blue)};
}

QString PaletteColor::hex() const
{
    const auto byte = [](double value) { return int(std::round(value * 255)); };
    return QString::asprintf("%02X%02X%02X", byte(red), byte(green), byte(blue));
}

std::optional<PaletteColor> PaletteColor::fromHex(const QString &hex)
{
    // Swift trims spaces and tabs, not line breaks.
    const auto blank = [](QChar character) { return character.category() == QChar::Separator_Space || character == u'\t'; };
    qsizetype start = 0, end = hex.size();
    while (start < end && blank(hex.at(start)))
        ++start;
    while (end > start && blank(hex.at(end - 1)))
        --end;
    QString text = hex.mid(start, end - start);
    if (text.startsWith(u'#'))
        text.remove(0, 1);
    if (text.size() == 3)
        text = QString(text.at(0)) + text.at(0) + text.at(1) + text.at(1) + text.at(2) + text.at(2);
    if (text.size() != 6)
        return std::nullopt;
    // Swift's UInt32(_:radix:): a sign may lead; minus holds zero alone.
    const bool negative = text.front() == u'-';
    uint value = 0;
    for (const QChar character : QStringView(text).mid(negative || text.front() == u'+' ? 1 : 0)) {
        const char16_t unit = character.unicode();
        const int digit = unit >= u'0' && unit <= u'9' ? unit - u'0' : unit >= u'a' && unit <= u'f' ? unit - u'a' + 10 : unit >= u'A' && unit <= u'F' ? unit - u'A' + 10 : -1;
        if (digit < 0)
            return std::nullopt;
        value = value * 16 + uint(digit);
    }
    if (negative && value != 0)
        return std::nullopt;
    return PaletteColor{double((value >> 16) & 0xFF) / 255, double((value >> 8) & 0xFF) / 255, double(value & 0xFF) / 255};
}

PickerHSB::PickerHSB(const PaletteColor &color) : hue(0), saturation(0), brightness(0)
{
    setRGB(color);
}

PaletteColor PickerHSB::rgb() const
{
    const double h = std::fmod(std::fmod(hue, 360) + 360, 360) / 60;
    const double c = brightness * saturation;
    const double x = c * (1 - std::abs(std::fmod(h, 2) - 1));
    const double m = brightness - c;
    switch (int(h)) {
    case 0:
        return {c + m, x + m, m};
    case 1:
        return {x + m, c + m, m};
    case 2:
        return {m, c + m, x + m};
    case 3:
        return {m, x + m, c + m};
    case 4:
        return {x + m, m, c + m};
    default:
        return {c + m, m, x + m};
    }
}

void PickerHSB::setRGB(const PaletteColor &color)
{
    const double high = std::max({color.red, color.green, color.blue});
    const double low = std::min({color.red, color.green, color.blue});
    const double delta = high - low;
    brightness = high;
    if (high > 0)
        saturation = delta / high;
    if (delta <= 0)
        return;
    double h;
    if (high == color.red)
        h = (color.green - color.blue) / delta;
    else if (high == color.green)
        h = (color.blue - color.red) / delta + 2;
    else
        h = (color.red - color.green) / delta + 4;
    h *= 60;
    hue = h < 0 ? h + 360 : h;
}

QString ColorPickerTarget::title() const
{
    if (kind == Kind::text)
        return QStringLiteral("Color Picker (Text Color)");
    if (kind == Kind::gradientMap)
        return highlights ? QStringLiteral("Color Picker (Gradient Map Highlights)") : QStringLiteral("Color Picker (Gradient Map Shadows)");
    if (kind == Kind::effect)
        return QStringLiteral("Color Picker (%1 Color)").arg(rawValue(effect));
    if (kind == Kind::vignette)
        return QStringLiteral("Color Picker (Vignette Color)");
    return background ? QStringLiteral("Color Picker (Background Color)") : QStringLiteral("Color Picker (Foreground Color)");
}

// Swift's ColorPalette extension: the palette and its picker.
PaletteColor EditorSession::foregroundColor() const
{
    return {m_brushSettings.red, m_brushSettings.green, m_brushSettings.blue};
}

void EditorSession::setForegroundColor(const PaletteColor &color)
{
    BrushSettings settings = m_brushSettings;
    settings.red = color.red;
    settings.green = color.green;
    settings.blue = color.blue;
    setBrushSettings(settings);
}

void EditorSession::setBackgroundColor(const PaletteColor &color)
{
    m_backgroundColor = color;
    refreshGradient();
    notify();
}

bool EditorSession::canEditPalette() const
{
    return !m_isProjectBusy && !m_brushStroke;
}

PaletteColor EditorSession::paletteColor(bool background) const
{
    if (m_isMaskSelected)
        return (background ? !m_maskPaintWhite : m_maskPaintWhite) ? PaletteColor::white() : PaletteColor::black();
    return background ? m_backgroundColor : foregroundColor();
}

void EditorSession::setPaletteColor(const PaletteColor &color, bool background)
{
    if (!canEditPalette())
        return;
    if (m_isMaskSelected) {
        const bool white = color == PaletteColor::white();
        setMaskPaintWhite(background ? !white : white);
    } else if (background) {
        setBackgroundColor(color);
    } else {
        setForegroundColor(color);
        // Open text follows the swatch; drafts live under Type.
        if (m_textDraft) {
            changeTextStyle([&color](LayerTextStyle &style) {
                style.red = color.red;
                style.green = color.green;
                style.blue = color.blue;
            });
        }
    }
}

void EditorSession::swapPaletteColors()
{
    if (!canEditPalette())
        return;
    if (m_isMaskSelected) {
        setMaskPaintWhite(!m_maskPaintWhite);
    } else {
        const PaletteColor old = foregroundColor();
        setPaletteColor(m_backgroundColor, false);
        setBackgroundColor(old);
    }
}

void EditorSession::resetPaletteColors()
{
    if (!canEditPalette())
        return;
    if (m_isMaskSelected) {
        setMaskPaintWhite(false);
    } else {
        setPaletteColor(PaletteColor::black(), false);
        setBackgroundColor(PaletteColor::white());
    }
}

void EditorSession::openColorPicker(bool background)
{
    if (!canEditPalette() || m_isMaskSelected)
        return;
    ColorPickerState picker({ColorPickerTarget::Kind::palette, background}, paletteColor(background));
    // Open text follows the foreground: it previews, Cancel restores.
    if (!background && m_tool == NavigationTool::type && m_textDraft)
        picker.editedText = ColorPickerState::EditedText{m_textDraft->id, {m_textDraft->style.red, m_textDraft->style.green, m_textDraft->style.blue}};
    m_colorPicker = picker;
    notify();
}

PaletteColor EditorSession::typeColor() const
{
    if (!m_textDraft)
        return foregroundColor();
    return {m_textDraft->style.red, m_textDraft->style.green, m_textDraft->style.blue};
}

void EditorSession::openTextColorPicker()
{
    if (!canEditPalette() || m_colorPicker || m_tool != NavigationTool::type)
        return;
    const std::optional<QUuid> draftID = m_textDraft ? std::optional(m_textDraft->id) : std::nullopt;
    m_colorPicker = ColorPickerState({ColorPickerTarget::Kind::text, false, draftID}, typeColor());
    notify();
}

void EditorSession::closeColorPicker(bool commit)
{
    if (!m_colorPicker)
        return;
    // Closed first, so each signal sees it gone.
    const ColorPickerState picker = *std::exchange(m_colorPicker, std::nullopt);
    const PaletteColor color = picker.color();
    const std::optional<QUuid> draftID = m_textDraft ? std::optional(m_textDraft->id) : std::nullopt;
    if (commit && picker.target.kind == ColorPickerTarget::Kind::palette && !m_isMaskSelected) {
        setPaletteColor(color, picker.target.background);
    } else if (!commit && picker.target.kind == ColorPickerTarget::Kind::palette && picker.editedText && m_tool == NavigationTool::type
               && draftID == picker.editedText->draftID) {
        paintText(picker.editedText->color);
    } else if (picker.target.kind == ColorPickerTarget::Kind::text && m_tool == NavigationTool::type && draftID == picker.target.draftID) {
        // The text previewed the working colour; Cancel puts it back.
        const PaletteColor chosen = commit ? color : picker.original;
        if (draftID) {
            paintText(chosen);
        } else if (commit) {
            m_textDefaults.red = chosen.red;
            m_textDefaults.green = chosen.green;
            m_textDefaults.blue = chosen.blue;
        }
        // The text colour is the foreground: the swatch follows.
        if (commit && !m_isMaskSelected)
            setForegroundColor(color);
    } else if (picker.target.kind == ColorPickerTarget::Kind::gradientMap) {
        // The end previewed the working colour; Cancel puts it back.
        setGradientMapColor(commit ? color : picker.original, picker.target.highlights);
    } else if (picker.target.kind == ColorPickerTarget::Kind::vignette) {
        setVignetteColor(commit ? color : picker.original);
    } else if (picker.target.kind == ColorPickerTarget::Kind::effect) {
        const PaletteColor chosen = commit ? color : picker.original;
        changeEffects([&](LayerEffects &effects) { effects.setColor(chosen, picker.target.effect); });
    }
    notify();
}

void EditorSession::openGradientMapColorPicker(bool highlights)
{
    // A commit keeps the project busy: no `committing` term.
    if (!canEditPalette() || m_colorPicker || !m_filterEdit || m_filterEdit->kind != FilterKind::gradientMap)
        return;
    const AdjustmentColor value = highlights ? m_filterEdit->settings.gradientMap.highlights : m_filterEdit->settings.gradientMap.shadows;
    m_colorPicker = ColorPickerState({ColorPickerTarget::Kind::gradientMap, false, std::nullopt, highlights}, PaletteColor{value.red, value.green, value.blue});
    notify();
}

void EditorSession::openVignetteColorPicker()
{
    // A commit keeps the project busy: no `committing` term.
    if (!canEditPalette() || m_colorPicker || !m_filterEdit || m_filterEdit->kind != FilterKind::vignette)
        return;
    const AdjustmentColor value = m_filterEdit->settings.vignetteColor;
    m_colorPicker = ColorPickerState({ColorPickerTarget::Kind::vignette}, PaletteColor{value.red, value.green, value.blue});
    notify();
}

void EditorSession::openEffectColorPicker(LayerEffectKind kind)
{
    if (!canEditPalette() || m_colorPicker || !m_effectsEditing)
        return;
    m_colorPicker = ColorPickerState({ColorPickerTarget::Kind::effect, false, std::nullopt, false, kind}, editingEffects().color(kind).value_or(PaletteColor::black()));
    notify();
}

void EditorSession::previewTextColor()
{
    if (!m_colorPicker)
        return;
    const ColorPickerTarget &target = m_colorPicker->target;
    const std::optional<QUuid> draftID = target.kind == ColorPickerTarget::Kind::text                          ? target.draftID
                                         : target.kind == ColorPickerTarget::Kind::palette && m_colorPicker->editedText
                                             ? std::optional(m_colorPicker->editedText->draftID)
                                             : std::nullopt;
    if (draftID && m_tool == NavigationTool::type && m_textDraft && m_textDraft->id == *draftID)
        paintText(m_colorPicker->color());
}

// The open draft's colour.
void EditorSession::paintText(const PaletteColor &color)
{
    changeTextStyle([&color](LayerTextStyle &style) {
        style.red = color.red;
        style.green = color.green;
        style.blue = color.blue;
    });
}

void EditorSession::previewEffectColor()
{
    if (!m_colorPicker || m_colorPicker->target.kind != ColorPickerTarget::Kind::effect)
        return;
    const PaletteColor color = m_colorPicker->color();
    const LayerEffectKind kind = m_colorPicker->target.effect;
    changeEffects([&](LayerEffects &effects) { effects.setColor(color, kind); });
}

void EditorSession::previewGradientMapColor()
{
    if (m_colorPicker && m_colorPicker->target.kind == ColorPickerTarget::Kind::gradientMap)
        setGradientMapColor(m_colorPicker->color(), m_colorPicker->target.highlights);
}

void EditorSession::previewVignetteColor()
{
    if (m_colorPicker && m_colorPicker->target.kind == ColorPickerTarget::Kind::vignette)
        setVignetteColor(m_colorPicker->color());
}

// Its picker lives only while its Vignette edit is open.
void EditorSession::setVignetteColor(const PaletteColor &color)
{
    FilterSettings settings = m_filterEdit.value().settings;
    settings.vignetteColor = AdjustmentColor(color);
    if (settings == m_filterEdit->settings)
        return;
    updateFilter(settings, m_filterEdit->preview);
}

void EditorSession::setGradientMapColor(const PaletteColor &color, bool highlights)
{
    // An adjustment's editor can close under its picker (9.5c).
    if (!m_filterEdit || m_filterEdit->kind != FilterKind::gradientMap)
        return;
    const FilterEdit &edit = *m_filterEdit;
    FilterSettings settings = edit.settings;
    (highlights ? settings.gradientMap.highlights : settings.gradientMap.shadows) = AdjustmentColor(color);
    if (settings == edit.settings)
        return;
    updateFilter(settings, edit.preview);
}

void EditorSession::setColorPickerHSB(const PickerHSB &hsb)
{
    if (!m_colorPicker || m_colorPicker->hsb == hsb)
        return;
    m_colorPicker->hsb = hsb;
    notify();
}

void EditorSession::sampleIntoColorPicker(QPointF point)
{
    if (!m_colorPicker)
        return;
    const std::optional<PaletteColor> color = sampleCompositeColor(point);
    if (!color)
        return;
    PickerHSB hsb = m_colorPicker->hsb;
    hsb.setRGB(*color);
    setColorPickerHSB(hsb);
}

std::optional<PaletteColor> EditorSession::sampleCompositeColor(QPointF point) const
{
    if (!m_document || !(point.x() >= 0 && point.y() >= 0 && point.x() < m_document->width && point.y() < m_document->height))
        return std::nullopt;
    try {
        // The pixel under the point, drawn as the canvas shows.
        QImage pixel = BrushRaster::context(1, 1, false);
        QPainter painter(&pixel);
        painter.translate(-std::floor(point.x()), -std::floor(point.y()));
        drawLiveComposite(*m_document, painter);
        painter.end();
        const uchar *bytes = pixel.constBits();
        const int alpha = bytes[3];
        if (alpha == 0)
            return std::nullopt;
        const auto channel = [alpha](int value) { return std::round(double(std::min(alpha, value)) / alpha * 255) / 255; };
        return PaletteColor{channel(bytes[0]), channel(bytes[1]), channel(bytes[2])};
    } catch (const ExportError &error) {
        qCWarning(lcApp) << "the colour under the pointer cannot be read:" << error.what();
        return std::nullopt;
    }
}
