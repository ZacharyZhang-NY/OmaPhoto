#include "IO/PSD/PSDVector.h"
#include "Document/DocumentLimits.h"
#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include <QPainter>
#include <QtEndian>
#include <bit>
#include <cmath>

namespace {
std::optional<QByteArray> entry(const PSDVector::Extra &extra, const char *key)
{
    const QString name = QString::fromLatin1(key);
    if (!extra.contains(name))
        return std::nullopt;
    return extra.at(name);
}

qint32 i32(const QByteArray &bytes, qsizetype at)
{
    return qFromBigEndian<qint32>(bytes.constData() + at);
}

qint16 i16(const QByteArray &bytes, qsizetype at)
{
    return qFromBigEndian<qint16>(bytes.constData() + at);
}

std::optional<qsizetype> offset(const QByteArray &key, const QByteArray &data, qsizetype start = 0)
{
    const qsizetype found = start < data.size() ? data.indexOf(key, start) : -1;
    if (found < 0)
        return std::nullopt;
    return found;
}

std::optional<double> doubleAt(qsizetype at, const QByteArray &data)
{
    if (at + 8 > data.size())
        return std::nullopt;
    return std::bit_cast<double>(qFromBigEndian<quint64>(data.constData() + at));
}

std::optional<double> number(const QByteArray &data, const QByteArray &key)
{
    const std::optional<qsizetype> start = offset(key, data);
    if (!start)
        return std::nullopt;
    const qsizetype type = *start + key.size();
    if (type + 12 > data.size() || data.mid(type, 4) != "doub")
        return std::nullopt;
    return doubleAt(type + 4, data);
}

std::optional<qint32> int32(const QByteArray &data, const QByteArray &key)
{
    const std::optional<qsizetype> start = offset(key, data);
    if (!start)
        return std::nullopt;
    const qsizetype type = *start + key.size();
    if (type + 8 > data.size() || data.mid(type, 4) != "long")
        return std::nullopt;
    return i32(data, type + 4);
}

QPointF point(const QByteArray &bytes, qsizetype at, QSizeF canvas)
{
    const double y = double(i32(bytes, at)) / 0x1000000;
    const double x = double(i32(bytes, at + 4)) / 0x1000000;
    return QPointF(x * canvas.width(), y * canvas.height());
}

// CGRect.integral, in doubles, so a wild box never overflows.
QRectF integral(const QRectF &box)
{
    const double left = std::floor(box.left()), top = std::floor(box.top());
    return QRectF(left, top, std::ceil(box.right()) - left, std::ceil(box.bottom()) - top);
}

// Within the side limit and the pixel budget, else tooLarge.
QSize pixelSize(QSizeF size, qint64 remainingPixels)
{
    if (std::abs(size.width()) > DocumentLimits::maxSide || std::abs(size.height()) > DocumentLimits::maxSide)
        throw ImageImportError(ImageImportError::Kind::tooLarge);
    const qint64 budget = std::min(EditorSession::maxShapePixels, std::max<qint64>(0, remainingPixels));
    // Boxes are whole here, so the whole pixels decide alone.
    const int width = std::max(1, int(size.width())), height = std::max(1, int(size.height()));
    if (qint64(width) * height > budget)
        throw ImageImportError(ImageImportError::Kind::tooLarge);
    return QSize(width, height);
}

struct Origination {
    ShapeKind kind;
    QRectF bounds;
    double cornerRadius = 0;
};

// Photoshop's `vogk`: 1 and 2 rectangles, 5 an ellipse.
std::optional<Origination> origination(const std::optional<QByteArray> &data)
{
    const std::optional<qint32> type = data ? int32(*data, "keyOriginType") : std::nullopt;
    if (!type)
        return std::nullopt;
    ShapeKind kind;
    switch (*type) {
    case 1:
    case 2:
        kind = ShapeKind::rectangle;
        break;
    case 5:
        kind = ShapeKind::ellipse;
        break;
    default:
        return std::nullopt;
    }
    const qsizetype from = offset("keyOriginShapeBBox", *data).value_or(0);
    const std::optional<double> left = PSDVector::unit(*data, "Left", from), top = PSDVector::unit(*data, "Top ", from),
                                right = PSDVector::unit(*data, "Rght", from), bottom = PSDVector::unit(*data, "Btom", from);
    if (!left || !top || !right || !bottom)
        return std::nullopt;
    const QRectF bounds(*left, *top, *right - *left, *bottom - *top);
    if (!(bounds.width() >= 1) || !(bounds.height() >= 1) || !std::isfinite(bounds.x()) || !std::isfinite(bounds.y())
        || !std::isfinite(bounds.width()) || !std::isfinite(bounds.height()))
        return std::nullopt;
    Origination origin{kind, bounds};
    const std::optional<qsizetype> radiiAt = kind == ShapeKind::rectangle ? offset("keyOriginRRectRadii", *data) : std::nullopt;
    if (radiiAt) {
        std::vector<double> radii;
        for (const char *key : {"topLeft", "topRight", "bottomRight", "bottomLeft"})
            if (const std::optional<double> radius = PSDVector::unit(*data, key, *radiiAt))
                radii.push_back(*radius);
        if (radii.size() == 4) {
            const auto [lo, hi] = std::minmax_element(radii.begin(), radii.end());
            if (*hi - *lo > 0.5)
                return std::nullopt;
            origin.cornerRadius = *hi;
        }
    }
    return origin;
}

// Four sharp anchors in one subpath read as a rectangle.
std::optional<Origination> sharpRect(const std::optional<QByteArray> &data, QSizeF canvas)
{
    const std::optional<QPainterPath> outline = data ? PSDVector::path(*data, canvas) : std::nullopt;
    if (!outline)
        return std::nullopt;
    qsizetype at = 8;
    int remaining = 0;
    std::vector<QPointF> anchors;
    bool sharp = true;
    while (at + 26 <= data->size()) {
        const int type = i16(*data, at);
        const QByteArray body = data->mid(at + 2, 24);
        at += 26;
        if (type == 0 || type == 3) {
            if (!anchors.empty())
                return std::nullopt;
            remaining = i16(body, 0);
        } else if ((type == 1 || type == 2 || type == 4 || type == 5) && remaining > 0) {
            remaining -= 1;
            const QPointF incoming = point(body, 0, canvas), anchor = point(body, 8, canvas), outgoing = point(body, 16, canvas);
            if (std::hypot(incoming.x() - anchor.x(), incoming.y() - anchor.y()) > 0.5
                || std::hypot(outgoing.x() - anchor.x(), outgoing.y() - anchor.y()) > 0.5)
                sharp = false;
            anchors.push_back(anchor);
        }
    }
    if (!sharp || anchors.size() != 4)
        return std::nullopt;
    const QRectF box = outline->boundingRect();
    if (!(box.width() >= 1) || !(box.height() >= 1))
        return std::nullopt;
    return Origination{ShapeKind::rectangle, box};
}
}

std::optional<PSDVector::Live> PSDVector::live(const Extra &extra, QSizeF canvas, qint64 remainingPixels)
{
    const std::optional<QByteArray> stroke = entry(extra, "vstk"), solid = entry(extra, "SoCo");
    const bool fillEnabled = (stroke ? boolean(*stroke, "fillEnabled") : std::nullopt).value_or(solid.has_value());
    const bool strokeEnabled = (stroke ? boolean(*stroke, "strokeEnabled") : std::nullopt).value_or(false);
    const std::optional<RGB> fill = solid ? rgb(*solid) : std::nullopt;
    if (!fillEnabled || !fill)
        return std::nullopt;
    std::optional<Origination> origin = origination(entry(extra, "vogk"));
    if (!origin)
        origin = sharpRect(extra.contains(QStringLiteral("vmsk")) ? entry(extra, "vmsk") : entry(extra, "vsms"), canvas);
    if (!origin)
        return std::nullopt;
    QRectF box = integral(origin->bounds);
    box.setSize(QSizeF(pixelSize(box.size(), remainingPixels)));
    const LayerShapeStyle style{origin->kind, fill->r, fill->g, fill->b, origin->cornerRadius};
    const QImage image = EditorSession::shapeImage(style.kind, box.size(), style.color(), style.cornerRadius);
    std::vector<QString> notes;
    if (strokeEnabled)
        notes.push_back(QStringLiteral("The Photoshop stroke isn’t supported on shape layers and was omitted."));
    return Live{style, box, image, notes};
}

std::optional<PSDVector::Raster> PSDVector::raster(const Extra &extra, QSizeF canvas, qint64 remainingPixels)
{
    const std::optional<QByteArray> mask = extra.contains(QStringLiteral("vmsk")) ? entry(extra, "vmsk") : entry(extra, "vsms");
    std::optional<QPainterPath> outline = mask ? path(*mask, canvas) : std::nullopt;
    if (!outline)
        return std::nullopt;
    const std::optional<QByteArray> solid = entry(extra, "SoCo"), stroke = entry(extra, "vstk");
    const std::optional<RGB> fill = solid ? rgb(*solid) : std::nullopt;
    const bool fillEnabled = (stroke ? boolean(*stroke, "fillEnabled") : std::nullopt).value_or(fill.has_value());
    const bool strokeEnabled = (stroke ? boolean(*stroke, "strokeEnabled") : std::nullopt).value_or(false);
    const std::optional<RGB> strokeColor = stroke ? rgb(*stroke) : std::nullopt;
    const double strokeWidth = (stroke ? unit(*stroke, "strokeStyleLineWidth") : std::nullopt).value_or(1);
    if (!((fillEnabled && fill) || (strokeEnabled && strokeColor)))
        return std::nullopt;
    if (!std::isfinite(strokeWidth))
        return std::nullopt;
    if (strokeEnabled && !(strokeWidth >= 0 && strokeWidth <= DocumentLimits::maxSide))
        throw ImageImportError(ImageImportError::Kind::tooLarge);
    QRectF box = outline->boundingRect();
    if (strokeEnabled) {
        const double grow = std::ceil(strokeWidth / 2 + 1);
        box.adjust(-grow, -grow, grow, grow);
    }
    box = integral(box);
    const QSize size = pixelSize(box.size(), remainingPixels);
    QImage image = BrushRaster::context(size.width(), size.height(), false);
    QPainter painter(&image);
    painter.translate(-box.x(), -box.y());
    painter.setRenderHint(QPainter::Antialiasing);
    // Both CoreGraphics fills here are nonzero winding.
    outline->setFillRule(Qt::WindingFill);
    if (fillEnabled && fill)
        painter.fillPath(*outline, QColor::fromRgbF(float(fill->r), float(fill->g), float(fill->b)));
    if (strokeEnabled && strokeColor) {
        // SVG's miter rule is CoreGraphics': a bevel past the limit.
        QPen pen(QColor::fromRgbF(float(strokeColor->r), float(strokeColor->g), float(strokeColor->b)), strokeWidth, Qt::SolidLine, Qt::FlatCap,
                 Qt::SvgMiterJoin);
        pen.setMiterLimit(10);
        painter.strokePath(*outline, pen);
    }
    painter.end();
    return Raster{image, QRectF(box.topLeft(), QSizeF(size))};
}

std::optional<QPainterPath> PSDVector::path(const QByteArray &data, QSizeF canvas)
{
    if (data.size() < 8 || !(canvas.width() > 0) || !(canvas.height() > 0))
        return std::nullopt;
    QPainterPath result;
    qsizetype at = 8;
    int remaining = 0;
    bool closed = true, first = true;
    QPointF previousOut;
    while (at + 26 <= data.size()) {
        const int type = i16(data, at);
        const QByteArray body = data.mid(at + 2, 24);
        at += 26;
        if (type == 0 || type == 3) {
            if (!first && closed)
                result.closeSubpath();
            remaining = i16(body, 0);
            closed = type == 0;
            first = true;
        } else if ((type == 1 || type == 2 || type == 4 || type == 5) && remaining > 0) {
            remaining -= 1;
            const QPointF incoming = point(body, 0, canvas), anchor = point(body, 8, canvas), outgoing = point(body, 16, canvas);
            if (first) {
                result.moveTo(anchor);
                first = false;
            } else {
                result.cubicTo(previousOut, incoming, anchor);
            }
            previousOut = outgoing;
        }
    }
    if (!first && closed)
        result.closeSubpath();
    // A CGPath is empty only without elements.
    if (result.elementCount() == 0)
        return std::nullopt;
    return result;
}

std::optional<PSDVector::RGB> PSDVector::rgb(const QByteArray &data)
{
    const std::optional<double> r = number(data, "Rd  "), g = number(data, "Grn "), b = number(data, "Bl  ");
    if (!r || !g || !b)
        return std::nullopt;
    const auto channel = [](double value) { return value > 1 ? std::min(255.0, std::max(0.0, value)) / 255 : std::min(1.0, std::max(0.0, value)); };
    return RGB{channel(*r), channel(*g), channel(*b)};
}

std::optional<bool> PSDVector::boolean(const QByteArray &data, const QByteArray &key)
{
    const std::optional<qsizetype> start = offset(key, data);
    if (!start)
        return std::nullopt;
    const qsizetype type = *start + key.size();
    if (type + 5 > data.size() || data.mid(type, 4) != "bool")
        return std::nullopt;
    return data[type + 4] != 0;
}

std::optional<double> PSDVector::unit(const QByteArray &data, const QByteArray &key, qsizetype from)
{
    const std::optional<qsizetype> keyAt = offset(key, data, from);
    const std::optional<qsizetype> units = keyAt ? offset("UntF", data, *keyAt) : std::nullopt;
    if (!units)
        return std::nullopt;
    return doubleAt(*units + 8, data);
}
