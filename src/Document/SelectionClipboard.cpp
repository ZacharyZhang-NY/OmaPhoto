#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include "Document/PixelAdjust.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include "Rendering/LayerRenderer.h"
#include <QBuffer>
#include <QClipboard>
#include <QColorSpace>
#include <QGuiApplication>
#include <QImageWriter>
#include <QMimeData>
#include <cmath>

namespace {
// The working format, whatever another app copied.
QImage sRGBCopy(const QImage &image)
{
    // CoreGraphics converts colour spaces as it draws; Qt does not.
    const bool tagged = image.colorSpace().isValid() && image.colorSpace() != QColorSpace::SRgb;
    const QImage source = tagged ? image.convertedToColorSpace(QColorSpace::SRgb) : image;
    QImage copy = BrushRaster::context(source.width(), source.height(), false);
    QPainter painter(&copy);
    BrushRaster::draw(source, QRectF(0, 0, source.width(), source.height()), painter);
    copy.setColorSpace(QColorSpace::SRgb);
    return copy;
}

// Swift clips the context; QPainter multiplies the coverage in.
void clipTo(const std::optional<SelectionClip> &clip, QPainter &painter)
{
    if (!clip)
        return;
    painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    painter.drawImage(clip->rect, BrushRaster::alphaView(*clip->coverage));
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
}

const QMimeData *clipboardData()
{
    return QGuiApplication::clipboard()->mimeData();
}
}

// Swift's SelectionClipboard extension; Cut arrives with 8.2.
std::optional<QRectF> EditorSession::selectionCopyRegion() const
{
    if (!m_document)
        return std::nullopt;
    const QRectF canvas(QPointF(0, 0), m_document->size());
    const std::optional<DocumentSelection> current = selection();
    const QRectF bounds = current ? current->coverageBounds() : canvas;
    // Path operations leave float noise: round with a tolerance.
    constexpr double tolerance = 0.001;
    const double minX = std::floor(bounds.left() + tolerance), minY = std::floor(bounds.top() + tolerance);
    const QRectF region = QRectF(minX, minY, std::ceil(bounds.right() - tolerance) - minX, std::ceil(bounds.bottom() - tolerance) - minY).intersected(canvas);
    if (region.width() < 1 || region.height() < 1)
        return std::nullopt;
    return region;
}

bool EditorSession::canCopyPixels() const
{
    const std::optional<ImageLayer> layer = activeLayer();
    const std::optional<DocumentSelection> current = selection();
    if (!canEditLayers() || !layer || (layer->isGroup && !m_isMaskSelected) || (current && current->isEmpty()))
        return false;
    return m_isMaskSelected ? layer->mask.has_value() : layer->asset.has_value();
}

// The layer's pixels as shown, through the selection.
std::optional<CopiedPixels> EditorSession::renderSelectedPixels(const ImageLayer &layer, bool mask) const
{
    if (!m_document)
        return std::nullopt;
    const std::optional<DocumentSelection> current = selection();
    const std::optional<SelectionClip> clip = current ? std::optional(current->clip(m_document->size())) : std::nullopt;
    if (clip && !clip->coverage)
        return std::nullopt;
    const std::optional<QRectF> region = selectionCopyRegion();
    if (!region)
        return std::nullopt;
    QImage image = BrushRaster::context(int(region->width()), int(region->height()), false);
    QPainter painter(&image);
    painter.translate(-region->left(), -region->top());
    const LayerTransform transform = displayedTransform(layer);
    if (mask && layer.mask) {
        // A mask as opaque gray: ground, then white through it.
        const std::optional<LayerTransform> placement = displayedMaskPlacement(layer);
        const double ground = placement ? LayerMask::background(layer.mask->asset.thumbnail) : 0;
        painter.fillRect(*region, QColor::fromRgbF(ground, ground, ground));
        clipTo(clip, painter);
        // Each pass is clipped alone, so soft edges stack.
        QImage white = BrushRaster::context(image.width(), image.height(), false);
        QPainter second(&white);
        second.translate(-region->left(), -region->top());
        LayerRenderer::drawCoverage(layer.mask->asset.image(), placement.value_or(transform), second);
        clipTo(clip, second);
        second.end();
        painter.drawImage(*region, white);
    } else if (!mask && layer.asset) {
        LayerRenderer::draw(layer.asset->image(), transform, transform.center(), painter, {});
        clipTo(clip, painter);
    } else {
        return std::nullopt;
    }
    painter.end();
    return CopiedPixels{image, *region};
}

bool EditorSession::canCopyMerged() const
{
    const std::optional<DocumentSelection> current = selection();
    if (!canEditLayers() || (current && current->isEmpty()))
        return false;
    for (const ImageLayer &layer : m_document->renderLayers()) {
        if (layer.asset)
            return true;
    }
    return false;
}

// Every visible layer as shown, through the selection.
std::optional<CopiedPixels> EditorSession::renderMergedPixels() const
{
    if (!m_document)
        return std::nullopt;
    const std::optional<DocumentSelection> current = selection();
    const std::optional<SelectionClip> clip = current ? std::optional(current->clip(m_document->size())) : std::nullopt;
    if (clip && !clip->coverage)
        return std::nullopt;
    const std::optional<QRectF> region = selectionCopyRegion();
    if (!region)
        return std::nullopt;
    // Composited first: Color Burn and Dodge read their backdrop.
    QImage merged = BrushRaster::context(int(region->width()), int(region->height()), false);
    QPainter composite(&merged);
    composite.translate(-region->left(), -region->top());
    drawLiveComposite(*m_document, composite);
    composite.end();
    if (clip) {
        QPainter painter(&merged);
        painter.translate(-region->left(), -region->top());
        painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        painter.drawImage(clip->rect, BrushRaster::alphaView(*clip->coverage));
    }
    return CopiedPixels{merged, *region};
}

void EditorSession::copyMergedSelection()
{
    if (!canCopyMerged())
        return;
    try {
        const std::optional<CopiedPixels> copied = renderMergedPixels();
        if (!copied) {
            qCWarning(lcApp) << "nothing to copy merged";
            return;
        }
        store(*copied);
    } catch (const ExportError &error) {
        setBrushError(QString::fromUtf8(error.what()));
    }
}

void EditorSession::copySelection()
{
    const std::optional<ImageLayer> layer = activeLayer();
    if (!canCopyPixels() || !layer)
        return;
    try {
        const std::optional<CopiedPixels> copied = renderSelectedPixels(*layer, m_isMaskSelected);
        if (!copied) {
            qCWarning(lcApp) << "nothing to copy";
            return;
        }
        store(*copied);
    } catch (const ExportError &error) {
        setBrushError(QString::fromUtf8(error.what()));
    }
}

// Keeps pixels for Paste; the system clipboard gets PNG.
void EditorSession::store(const CopiedPixels &copied)
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    // Counted from the first copy: another since makes Paste external.
    if (!m_watchingClipboard) {
        m_watchingClipboard = true;
        connect(clipboard, &QClipboard::dataChanged, this, [this] { ++m_clipboardChanges; });
    }
    auto *data = new QMimeData;
    data->setImageData(copied.image);
    QByteArray png;
    QBuffer buffer(&png);
    QImageWriter writer(&buffer, "png");
    if (writer.write(copied.image))
        data->setData(QStringLiteral("image/png"), png);
    else
        qCWarning(lcApp) << "the copied pixels could not be encoded as PNG:" << writer.errorString();
    clipboard->setMimeData(data);
    m_pixelClipboard = PixelClipboard{copied.image, copied.region.topLeft(), m_clipboardChanges};
}

void EditorSession::cutSelection(std::function<void()> done)
{
    if (!selection() || !canCopyPixels()) {
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
        return;
    }
    copySelection();
    clearSelectedPixels(std::move(done));
}

bool EditorSession::canPaste() const
{
    if (!m_document || !canEditLayers())
        return false;
    if (m_pixelClipboard && m_clipboardChanges == m_pixelClipboard->changeCount)
        return true;
    // An empty clipboard has no data object at all.
    const QMimeData *data = clipboardData();
    return data && data->hasImage();
}

// Pixels copied here go back in place; others centre.
void EditorSession::paste()
{
    if (!canPaste())
        return;
    if (m_pixelClipboard && m_clipboardChanges == m_pixelClipboard->changeCount) {
        addPixelLayer(m_pixelClipboard->image, m_pixelClipboard->origin, nextLayerName(), QStringLiteral("Paste"));
        return;
    }
    const QImage external = qvariant_cast<QImage>(clipboardData()->imageData());
    if (external.isNull()) {
        qCWarning(lcApp) << "the clipboard's image could not be read";
        return;
    }
    try {
        const QImage image = sRGBCopy(external);
        const QPointF origin(std::floor((m_document->size().width() - image.width()) / 2), std::floor((m_document->size().height() - image.height()) / 2));
        addPixelLayer(image, origin, nextLayerName(), QStringLiteral("Paste"));
    } catch (const ExportError &error) {
        // Swift's `try?` beeps here, where a layer's copy alerts.
        qCWarning(lcApp) << "the clipboard's image could not be copied:" << error.what();
    }
}

// Ctrl+J: the selection as a layer; without one, a duplicate.
void EditorSession::layerViaCopy()
{
    const std::optional<ImageLayer> layer = activeLayer();
    const std::optional<DocumentSelection> current = selection();
    if (!canEditLayers() || !layer || layer->isGroup || (current && current->isEmpty()))
        return;
    if (!current) {
        duplicateActiveLayer();
        return;
    }
    try {
        const std::optional<CopiedPixels> copied = renderSelectedPixels(*layer, m_isMaskSelected);
        if (!copied) {
            qCWarning(lcApp) << "nothing to copy into a layer";
            return;
        }
        addPixelLayer(copied->image, copied->region.topLeft(), nextLayerName(), QStringLiteral("Layer via Copy"));
    } catch (const ExportError &error) {
        setBrushError(QString::fromUtf8(error.what()));
    }
}

// A new layer above the active one, in its folder.
void EditorSession::addPixelLayer(const QImage &image, QPointF origin, const QString &name, const QString &editName, bool dropsSelection,
                                  const std::optional<LayerShapeStyle> &shape, const std::optional<LayerTextStyle> &text)
{
    if (!m_document)
        return;
    QImage thumbnail;
    try {
        thumbnail = PixelAdjust::thumbnail(image);
    } catch (const ExportError &error) {
        qCWarning(lcApp) << "no thumbnail for the new layer:" << error.what();
        return;
    }
    ImageLayer layer(ImportedImage(image, thumbnail, name), origin);
    if (shape)
        layer.shape = LayerShape{*shape, layer.asset->identity()};
    if (text)
        layer.text = LayerText{*text, layer.asset->identity()};
    const std::optional<ImageLayer> active = activeLayer();
    layer.parentID = active && active->isGroup ? m_activeLayerID : active ? active->parentID : std::nullopt;
    const int activeIndex = indexOf(m_document->layers, m_activeLayerID);
    const int index = activeIndex >= 0 ? activeIndex + 1 : int(m_document->layers.size());
    finishOpacityEdit();
    beginEdit(editName);
    m_document->layers.insert(m_document->layers.begin() + index, layer);
    // Pasting drops the selection, as Photoshop; a shape keeps it.
    if (dropsSelection)
        m_document->selection = std::nullopt;
    setActiveLayerID(layer.id);
    endEdit();
}

QString EditorSession::nextLayerName() const
{
    QSet<QString> names;
    for (const ImageLayer &layer : m_document->layers)
        names.insert(layer.name);
    int number = 1;
    while (names.contains(QStringLiteral("Layer %1").arg(number)))
        ++number;
    return QStringLiteral("Layer %1").arg(number);
}

void EditorSession::duplicateActiveLayer()
{
    const int index = canEditLayers() ? indexOf(m_document->layers, m_activeLayerID) : -1;
    if (index < 0)
        return;
    // A folder carries its whole tree, links kept inside it.
    const QUuid id = *m_activeLayerID;
    QSet<QUuid> included = descendantIDs(id);
    included.insert(id);
    std::vector<ImageLayer> copies;
    for (const ImageLayer &layer : m_document->layers) {
        if (included.contains(layer.id))
            copies.push_back(layer);
    }
    if (m_document->layers.size() + copies.size() > 10'000)
        return;
    QHash<QUuid, QUuid> mapping;
    for (const ImageLayer &copy : copies)
        mapping.insert(copy.id, QUuid::createUuid());
    const auto renamed = [&mapping](std::optional<QUuid> &link) {
        if (link && mapping.contains(*link))
            link = mapping.value(*link);
    };
    for (ImageLayer &copy : copies) {
        if (copy.id == id)
            copy.name += QStringLiteral(" copy");
        copy.id = mapping.value(copy.id);
        renamed(copy.parentID);
        renamed(copy.maskSourceID);
    }
    beginEdit(QStringLiteral("Duplicate Layer"));
    m_document->layers.insert(m_document->layers.begin() + index + 1, copies.begin(), copies.end());
    for (auto original = mapping.cbegin(); original != mapping.cend(); ++original) {
        if (m_collapsedGroupIDs.contains(original.key()))
            m_collapsedGroupIDs.insert(original.value());
    }
    setActiveLayerID(mapping.value(id));
    endEdit();
}

// Alt-drag in the Layers panel: a copy placed where dropped.
bool EditorSession::duplicateLayer(QUuid id, std::optional<QUuid> parent, std::optional<QUuid> above, bool atBottom)
{
    // canPlaceLayer reads canEditLayers, Swift's first term.
    if (!canPlaceLayer(id, parent))
        return false;
    beginEdit(QStringLiteral("Duplicate Layer"));
    selectLayer(id);
    duplicateActiveLayer();
    // Past the layer limit no copy was made.
    const bool placed = m_activeLayerID != id && placeLayer(*m_activeLayerID, parent, above, atBottom);
    endEdit();
    return placed;
}
