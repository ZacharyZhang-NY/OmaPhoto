#include "Document/MaskTracing.h"
#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include <QPainter>
#include <functional>
#include <map>
#include <vector>

namespace {
// Outer loops run clockwise, holes counterclockwise, for winding fill.
std::optional<QPainterPath> trace(const QImage &image, bool alpha, const std::function<bool(uchar)> &test)
{
    const int width = image.width(), height = image.height();
    if (width <= 0 || height <= 0)
        return std::nullopt;
    const int channels = alpha ? 4 : 1;
    // Drawn over a cleared bitmap, as the Swift tracer does.
    QImage pixels(width, height, alpha ? QImage::Format_RGBA8888_Premultiplied : QImage::Format_Grayscale8);
    if (pixels.isNull())
        return std::nullopt;
    pixels.fill(0);
    QPainter(&pixels).drawImage(QRectF(0, 0, width, height), image);
    const auto selected = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < width && y < height
            && test(pixels.constScanLine(y)[x * channels + channels - 1]);
    };
    // Directed unit edges, keyed by their start vertex.
    const int stride = width + 1;
    std::map<int, std::vector<int>> outgoing;
    const auto edge = [&](int x0, int y0, int x1, int y1) { outgoing[y0 * stride + x0].push_back(y1 * stride + x1); };
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (!selected(x, y))
                continue;
            if (!selected(x, y - 1))
                edge(x, y, x + 1, y);
            if (!selected(x + 1, y))
                edge(x + 1, y, x + 1, y + 1);
            if (!selected(x, y + 1))
                edge(x + 1, y + 1, x, y + 1);
            if (!selected(x - 1, y))
                edge(x, y + 1, x, y);
        }
    }
    if (outgoing.empty())
        return std::nullopt;
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    while (!outgoing.empty()) {
        const int start = outgoing.begin()->first;
        std::vector<int> loop;
        int current = start;
        do {
            const auto found = outgoing.find(current);
            if (found == outgoing.end())
                break;
            const int end = found->second.back();
            found->second.pop_back();
            if (found->second.empty())
                outgoing.erase(found);
            loop.push_back(current);
            current = end;
        } while (current != start);
        // Keep corners; drop vertices that continue straight.
        std::vector<QPointF> corners;
        const int count = int(loop.size());
        for (int index = 0; index < count; ++index) {
            const int vertex = loop[index], previous = loop[(index + count - 1) % count], following = loop[(index + 1) % count];
            const int inX = vertex % stride - previous % stride, inY = vertex / stride - previous / stride;
            const int outX = following % stride - vertex % stride, outY = following / stride - vertex / stride;
            if (inX != outX || inY != outY)
                corners.push_back(QPointF(vertex % stride, vertex / stride));
        }
        if (corners.size() < 3)
            continue;
        path.moveTo(corners.front());
        for (std::size_t index = 1; index < corners.size(); ++index)
            path.lineTo(corners[index]);
        path.closeSubpath();
    }
    return path.isEmpty() ? std::nullopt : std::optional(path);
}
}

std::optional<QPainterPath> MaskTracing::darkPixels(const QImage &image)
{
    return trace(image, false, [](uchar value) { return value < 128; });
}

std::optional<QPainterPath> MaskTracing::whitePixels(const QImage &image)
{
    return trace(image, false, [](uchar value) { return value >= 128; });
}

std::optional<QPainterPath> MaskTracing::opaquePixels(const QImage &image)
{
    return trace(image, true, [](uchar value) { return value >= 128; });
}

void EditorSession::loadMaskSelection(QUuid layerID, SelectionMode mode)
{
    if (!canEditSelection())
        return;
    const int index = indexOf(m_document->layers, layerID);
    if (index < 0 || !m_document->layers[size_t(index)].mask)
        return;
    const ImageLayer &layer = m_document->layers[size_t(index)];
    QImage mask;
    // A painted mask flattens here; a failure loads nothing.
    try {
        mask = layer.mask->asset.image();
    } catch (const ExportError &error) {
        qCWarning(lcApp).noquote() << "cannot read the mask of" << layer.name << ":" << error.what();
        return;
    }
    const std::optional<QPainterPath> traced = MaskTracing::darkPixels(mask);
    // Swift beeps: nothing black to select.
    if (!traced) {
        qCWarning(lcApp).noquote() << "no black to select in the mask of" << layer.name;
        return;
    }
    applySelection(BrushRaster::pixelToDocument(layer.maskTransform(), mask.width(), mask.height()).map(*traced), mode,
                   QStringLiteral("Load Mask Selection"));
}

void EditorSession::loadLayerSelection(QUuid layerID, SelectionMode mode)
{
    const int index = canEditSelection() ? indexOf(m_document->layers, layerID) : -1;
    // Swift beeps: no pixels, or none opaque.
    if (index < 0 || m_document->layers[size_t(index)].isGroup || !m_document->layers[size_t(index)].asset) {
        qCWarning(lcApp) << "no pixels to select for layer" << layerID.toString();
        return;
    }
    const ImageLayer &layer = m_document->layers[size_t(index)];
    QImage image;
    try {
        image = layer.asset->image();
    } catch (const ExportError &error) {
        qCWarning(lcApp).noquote() << "cannot read the pixels of" << layer.name << ":" << error.what();
        return;
    }
    const std::optional<QPainterPath> traced = MaskTracing::opaquePixels(image);
    if (!traced) {
        qCWarning(lcApp).noquote() << "no opaque pixels to select in" << layer.name;
        return;
    }
    applySelection(BrushRaster::pixelToDocument(layer.transform, image.width(), image.height()).map(*traced), mode,
                   QStringLiteral("Load Layer Selection"));
}
