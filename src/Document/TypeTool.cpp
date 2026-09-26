#include "Document/EditorSession.h"
#include "Document/PixelAdjust.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include "Rendering/TextLayout.h"
#include <QPainter>
#include <QTextBoundaryFinder>
#include <cmath>
#include <stdexcept>

QString rawValue(TextAlignment alignment)
{
    switch (alignment) {
    case TextAlignment::left:
        return QStringLiteral("Left");
    case TextAlignment::center:
        return QStringLiteral("Center");
    case TextAlignment::right:
        return QStringLiteral("Right");
    }
    throw std::logic_error("unknown text alignment");
}

std::optional<TextAlignment> textAlignment(const QString &text)
{
    for (const TextAlignment alignment : allTextAlignments) {
        if (rawValue(alignment) == text)
            return alignment;
    }
    return std::nullopt;
}

bool LayerTextStyle::boxIsValid() const
{
    if (!boxSize)
        return true;
    const double width = boxSize->width(), height = boxSize->height();
    return width >= 16 && width <= 30'000 && height >= 16 && height <= 30'000 && width * height <= 100'000'000;
}

bool LayerTextStyle::isValid() const
{
    // Ranges refuse NaN and infinities: Swift's isFinite adds nothing.
    const auto within = [](double value, double low, double high) { return value >= low && value <= high; };
    return content.size() <= 100'000 && boxIsValid() && within(fontSize, 1, 2000) && within(red, 0, 1) && within(green, 0, 1)
        && within(blue, 0, 1) && within(tracking, -100, 1000) && within(leading, 0, 5000);
}

std::optional<LayerText> LayerText::loaded(const std::optional<LayerTextStyle> &style, const std::optional<ImportedImage> &image)
{
    if (!style || !style->isValid() || !image)
        return std::nullopt;
    return LayerText{*style, image->identity()};
}

std::optional<LayerText> ImageLayer::liveText() const
{
    if (!text || !asset || asset->identity() != text->image)
        return std::nullopt;
    return text;
}

void EditorSession::setTextDraft(std::optional<TextDraft> draft)
{
    m_textDraft = std::move(draft);
    resumeFileRequests();
    notify();
}

void EditorSession::beginText(QPointF point, bool newLayer)
{
    if (!canEditLayers() || !std::isfinite(point.x()) || !std::isfinite(point.y()))
        return;
    // The topmost visible text under the point, if wanted.
    std::optional<ImageLayer> target;
    const QSet<QUuid> visible = m_document->effectiveVisibleIDs();
    for (auto layer = m_document->layers.rbegin(); !newLayer && !target && layer != m_document->layers.rend(); ++layer) {
        if (visible.contains(layer->id) && layer->liveText() && layer->transform.contains(point))
            target = *layer;
    }
    if (target)
        selectLayer(target->id);
    LayerTextStyle style = target ? target->liveText()->style : m_textDefaults;
    if (!target) {
        style.content = QString();
        // New text takes the foreground colour, as painting does.
        if (!m_isMaskSelected) {
            style.red = foregroundColor().red;
            style.green = foregroundColor().green;
            style.blue = foregroundColor().blue;
        }
        // A click makes point text: no box of its own.
        style.boxSize = std::nullopt;
    }
    m_tool = NavigationTool::type;
    setTextDraft(TextDraft{.documentID = m_document->id, .layerID = target ? std::optional(target->id) : std::nullopt,
                           .origin = target ? target->origin() : point,
                           .transform = target ? std::optional(target->transform) : std::nullopt, .style = style});
}

void EditorSession::editActiveText()
{
    const std::optional<ImageLayer> layer = activeLayer();
    if (!canEditLayers() || !layer || !layer->liveText())
        return;
    m_tool = NavigationTool::type;
    setTextDraft(TextDraft{.documentID = m_document->id, .layerID = layer->id, .origin = layer->origin(), .transform = layer->transform,
                           .style = layer->liveText()->style});
}

bool EditorSession::applyText(TextDraft draft)
{
    if (!m_document || m_document->id != draft.documentID || !draft.style.isValid())
        return false;
    // Cleared first, as the gate reads it; restored if refused.
    const std::optional<TextDraft> pending = std::exchange(m_textDraft, std::nullopt);
    const auto refuse = [&] {
        m_textDraft = pending;
        return false;
    };
    if (!canEditLayers())
        return refuse();
    const auto succeed = [&] {
        resumeFileRequests();
        notify();
        return true;
    };
    if (!draft.layerID && draft.style.content.trimmed().isEmpty())
        return succeed();
    try {
        const QImage image = textImage(draft.style);
        if (draft.layerID) {
            const int index = indexOf(m_document->layers, *draft.layerID);
            if (index < 0 || !m_document->layers[size_t(index)].liveText())
                return refuse();
            const ImageLayer layer = m_document->layers[size_t(index)];
            if (layer.liveText()->style == draft.style && (!draft.transform || *draft.transform == layer.transform))
                return succeed();
            const QImage thumbnail = PixelAdjust::thumbnail(image);
            LayerTransform transform = draft.transform.value_or(layer.transform);
            // The corner stays; so do the scale, turn and flips.
            const QPointF anchor = transform.point(QPointF(0, 0));
            if (!draft.transform || !draft.style.boxSize) {
                transform.size = QSizeF(image.width() * transform.size.width() / layer.asset->size().width(),
                                        image.height() * transform.size.height() / layer.asset->size().height());
                transform.origin += anchor - transform.point(QPointF(0, 0));
            }
            if (!transform.isValid())
                throw ProjectError(ProjectError::Kind::tooLarge);
            beginEdit(QStringLiteral("Edit Text"));
            ImageLayer &edited = m_document->layers[size_t(index)];
            if (edited.mask && !edited.mask->placement)
                edited.mask->placement = layer.maskTransform();
            edited.asset = ImportedImage(image, thumbnail, layer.asset->name);
            edited.text = LayerText{draft.style, edited.asset->identity()};
            edited.transform = transform;
            endEdit();
        } else {
            addPixelLayer(image, draft.origin, layerName(draft.style.content), QStringLiteral("New Text Layer"), false, std::nullopt, draft.style);
        }
        m_textDefaults = draft.style;
        requestCanvasFocus();
        return succeed();
    } catch (const ProjectError &error) {
        // The draft is back before the error is announced.
        refuse();
        setBrushError(QString::fromUtf8(error.what()));
    } catch (const ExportError &error) {
        refuse();
        setBrushError(QString::fromUtf8(error.what()));
    }
    return false;
}

bool EditorSession::finishText()
{
    if (!m_textDraft)
        return true;
    return applyText(*m_textDraft);
}

void EditorSession::cancelText()
{
    setTextDraft(std::nullopt);
    requestCanvasFocus();
}

void EditorSession::beginText(const QRectF &rect)
{
    if (!canEditLayers() || !std::isfinite(rect.width()) || !std::isfinite(rect.height()))
        return;
    LayerTextStyle style = m_textDefaults;
    style.boxSize = QSizeF(std::max(16.0, std::round(rect.width())), std::max(16.0, std::round(rect.height())));
    if (!style.boxIsValid()) {
        setBrushError(QStringLiteral("That text box exceeds the 30,000-pixel or 100-megapixel limit."));
        return;
    }
    beginText(rect.topLeft(), true);
    if (m_textDraft) {
        TextDraft draft = *m_textDraft;
        draft.style.boxSize = style.boxSize;
        setTextDraft(draft);
    }
}

bool EditorSession::recolorText(QUuid id, const PaletteColor &color)
{
    if (!canEditLayers())
        return false;
    const int index = indexOf(m_document->layers, id);
    if (index < 0 || !m_document->layers[size_t(index)].liveText())
        return false;
    const ImageLayer layer = m_document->layers[size_t(index)];
    LayerTextStyle style = layer.liveText()->style;
    if (style.red == color.red && style.green == color.green && style.blue == color.blue)
        return true;
    style.red = color.red;
    style.green = color.green;
    style.blue = color.blue;
    QImage image, thumbnail;
    try {
        image = textImage(style);
        thumbnail = PixelAdjust::thumbnail(image);
    } catch (const std::runtime_error &error) {
        // An invalid colour too; Swift's try? fills without a word.
        qCWarning(lcApp) << "text could not be repainted:" << error.what();
        return false;
    }
    finishOpacityEdit();
    beginEdit(QStringLiteral("Fill Text"));
    ImageLayer &edited = m_document->layers[size_t(index)];
    edited.asset = ImportedImage(image, thumbnail, layer.asset->name);
    edited.text = LayerText{style, edited.asset->identity()};
    endEdit();
    return true;
}

LayerTextStyle EditorSession::currentTextStyle() const
{
    if (m_textDraft)
        return m_textDraft->style;
    const std::optional<ImageLayer> active = activeLayer();
    return active && active->liveText() ? active->liveText()->style : m_textDefaults;
}

void EditorSession::changeTextStyle(const std::function<void(LayerTextStyle &)> &change)
{
    const std::optional<ImageLayer> active = activeLayer();
    if (!m_textDraft && active && active->liveText())
        editActiveText();
    if (m_textDraft) {
        TextDraft draft = *m_textDraft;
        change(draft.style);
        if (draft.style.isValid())
            setTextDraft(draft);
        return;
    }
    LayerTextStyle style = m_textDefaults;
    change(style);
    if (!style.isValid())
        return;
    m_textDefaults = style;
    notify();
}

QString EditorSession::layerName(const QString &content)
{
    // Whitespace runs become single spaces: the row stays one line.
    const QString flattened = content.simplified();
    if (flattened.isEmpty())
        return QStringLiteral("Text");
    QTextBoundaryFinder characters(QTextBoundaryFinder::Grapheme, flattened);
    for (int count = 0; count < 40 && characters.toNextBoundary() >= 0;)
        ++count;
    // Past the end the position is -1: `left` keeps everything.
    return flattened.left(characters.position());
}

QSizeF EditorSession::textBoxSize(const LayerTextStyle &style)
{
    if (style.boxSize)
        return *style.boxSize;
    const TextLines lines(style, QSizeF(100'000, 100'000));
    const QSizeF measured = lines.usedSize();
    const double padding = LayerTextStyle::padding;
    // A caret's width more; an empty paragraph lays a line.
    return QSizeF(std::ceil(measured.width() + padding * 2 + style.fontSize * 0.1), std::ceil(measured.height() + padding * 2));
}

QImage EditorSession::textImage(const LayerTextStyle &style)
{
    if (!style.isValid())
        throw ProjectError(ProjectError::Kind::invalid);
    const QSizeF size = textBoxSize(style);
    const double width = std::ceil(size.width()), height = std::ceil(size.height());
    // A valid style measures sixteen pixels at least.
    if (!(width <= 30'000 && height <= 30'000 && width * height <= 100'000'000))
        throw ProjectError(ProjectError::Kind::tooLarge);
    QImage context = BrushRaster::context(int(width), int(height), false);
    const double padding = LayerTextStyle::padding;
    const TextLines lines(style, QSizeF(std::max(1.0, width - 2 * padding), std::max(1.0, height - 2 * padding)));
    QPainter painter(&context);
    painter.setPen(QColor::fromRgbF(float(style.red), float(style.green), float(style.blue)));
    lines.draw(painter, QPointF(padding, padding));
    painter.end();
    return context;
}
