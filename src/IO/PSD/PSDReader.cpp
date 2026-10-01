#include "IO/PSD/PSDReader.h"
#include "Document/DocumentLimits.h"
#include "IO/PSD/PSDChannelCoder.h"
#include "IO/PSD/PSDReader+Records.h"
#include "IO/PSD/PSDVector.h"
#include "Logging.h"
#include <QColorSpace>
#include <QFile>

namespace {
using namespace PSDRecords;

PSDLayerKind kindOf(const RawLayer &layer, bool isGroup)
{
    if (isGroup)
        return PSDLayerKind::group;
    if (layer.has({"TySh", "tySh", "txt2"}))
        return PSDLayerKind::text;
    if (layer.has({"vmsk", "vsms", "vogk"}))
        return PSDLayerKind::vector;
    if (layer.has({"SoLd", "SoLE"}))
        return PSDLayerKind::smartObject;
    if (layer.has({"lfx2", "lrFX", "lmfx"}))
        return PSDLayerKind::effects;
    for (const QString &key : PSDReader::adjustmentKeys)
        if (layer.extra.contains(key))
            return PSDLayerKind::adjustment;
    return PSDLayerKind::raster;
}

std::vector<PSDRecord> assemble(const std::vector<RawLayer> &raw, QSizeF canvas, qint64 remainingPixels)
{
    std::vector<PSDRecord> result;
    std::vector<QUuid> groups;
    qint64 remaining = std::max<qint64>(0, remainingPixels);
    for (const RawLayer &layer : raw) {
        // Groups run bottom to top: divider, children, then the folder.
        if (layer.section == 3) {
            groups.push_back(QUuid::createUuid());
            continue;
        }
        const bool isGroup = layer.section == 1 || layer.section == 2;
        QUuid id = QUuid::createUuid();
        if (isGroup && !groups.empty()) {
            id = groups.back();
            groups.pop_back();
        }
        PSDRecord record;
        record.id = id;
        record.name = layer.name.isEmpty() ? QStringLiteral("Layer") : layer.name;
        if (!groups.empty())
            record.parentID = groups.back();
        record.isGroup = isGroup;
        record.isVisible = !layer.hidden;
        const bool passes = layer.blendKey == QLatin1String("pass") || layer.blendKey == QLatin1String("norm");
        record.blendKey = isGroup && passes ? QStringLiteral("pass") : layer.blendKey;
        record.clipping = layer.clipping;
        record.kind = kindOf(layer, isGroup);
        record.croppedToCanvas = layer.cropped;
        const bool hasEffects = record.kind == PSDLayerKind::effects || layer.has({"lfx2", "lrFX", "lmfx"});
        record.opacity = hasEffects && layer.fill != 255 ? layer.opacity / 255.0 : (layer.opacity / 255.0) * (layer.fill / 255.0);
        record.bounds = isGroup ? QRectF(QPointF(0, 0), canvas) : QRectF(layer.left, layer.top, std::max<qint64>(0, layer.right - layer.left), std::max<qint64>(0, layer.bottom - layer.top));
        record.image = isGroup ? std::nullopt : layer.image;
        if (record.kind == PSDLayerKind::text)
            record.text = PSDText::parse(layer.extra);
        std::optional<PSDVector::Live> live = isGroup || record.text ? std::nullopt : PSDVector::live(layer.extra, canvas, remaining);
        if (live) {
            record.image = live->image;
            record.bounds = live->bounds;
            record.shape = live->style;
            record.shapeNotes = live->notes;
            record.kind = PSDLayerKind::vector;
            remaining = std::max<qint64>(0, remaining - qint64(live->image.width()) * live->image.height());
        } else if (!record.text && !record.image && !isGroup) {
            if (const std::optional<PSDVector::Raster> raster = PSDVector::raster(layer.extra, canvas, remaining)) {
                record.image = raster->image;
                record.bounds = raster->bounds;
                record.kind = PSDLayerKind::vector;
                remaining = std::max<qint64>(0, remaining - qint64(raster->image.width()) * raster->image.height());
            }
        }
        record.mask = layer.maskFromRender ? std::nullopt : layer.maskImage;
        record.maskBounds = QRectF(layer.maskLeft, layer.maskTop, layer.maskRight - layer.maskLeft, layer.maskBottom - layer.maskTop);
        record.maskDefault = layer.maskDefault;
        record.maskEnabled = !layer.maskDisabled;
        record.maskLinked = layer.maskLinked;
        if (!isGroup)
            record.adjustment = PSDAdjustments::parse(layer.extra);
        if (record.adjustment)
            record.kind = PSDLayerKind::adjustment;
        result.push_back(std::move(record));
    }
    if (!groups.empty())
        throw PSDError(PSDError::Kind::truncated);
    return result;
}

// What precedes the layers: the canvas, its channels, its profile.
struct Header {
    bool isPSB;
    int channels;
    qint64 width;
    qint64 height;
    double resolution = 72;
    QByteArray profile;
};

// A layered canvas stays one surface; merged, the budget.
Header readHeader(Cursor &cursor, bool surface)
{
    if (cursor.string(4) != QLatin1String("8BPS"))
        throw ImageImportError(ImageImportError::Kind::unreadable);
    // Version 2 is a Large Document, some lengths wider.
    const int version = cursor.u16();
    if (version != 1 && version != 2)
        throw PSDError(PSDError::Kind::unsupportedVersion);
    cursor.skip(6);
    Header header{version == 2, cursor.u16(), 0, 0, 72, {}};
    header.height = cursor.u32();
    header.width = cursor.u32();
    const int depth = cursor.u16(), mode = cursor.u16();
    if (header.width < 1 || header.width > DocumentLimits::maxSide || header.height < 1 || header.height > DocumentLimits::maxSide
        || (surface && header.width * header.height > DocumentLimits::maxSurfacePixels))
        throw ImageImportError(ImageImportError::Kind::tooLarge);
    if (depth != 8)
        throw PSDError(PSDError::Kind::unsupportedDepth);
    if (mode != 3)
        throw PSDError(PSDError::Kind::unsupportedColorMode);
    cursor.skip(cursor.u32());
    const qint64 resourcesLength = cursor.u32();
    const qint64 resourcesEnd = cursor.offset + resourcesLength;
    while (cursor.offset + 12 <= resourcesEnd) {
        if (cursor.string(4) != QLatin1String("8BIM"))
            break;
        const int id = cursor.u16();
        const int nameLength = cursor.u8();
        cursor.skip(nameLength);
        if ((nameLength + 1) % 2 == 1)
            cursor.skip(1);
        const qint64 length = cursor.u32();
        const qint64 dataStart = cursor.offset;
        if (id == 1005 && length >= 4) {
            header.resolution = cursor.u32() / 65536.0;
            // A fixed-point number is finite; below one reads as 72.
            header.resolution = std::min(9600.0, header.resolution < 1 ? 72 : header.resolution);
        }
        // Only the merged image reads the profile, as Swift's.
        if (id == 1039 && !surface)
            header.profile = cursor.bytes(length);
        cursor.offset = dataStart + length;
        if (length % 2 == 1)
            cursor.skip(1);
    }
    cursor.offset = resourcesEnd;
    return header;
}

PSDDocument parse(const QByteArray &data, qint64 remainingPixels)
{
    Cursor cursor{data};
    const Header header = readHeader(cursor, true);
    const qint64 layerSection = cursor.length(header.isPSB);
    const PSDDocument empty{int(header.width), int(header.height), header.resolution, {}};
    if (layerSection < 4)
        return empty;
    // The layer info length is skipped, as in Swift.
    cursor.length(header.isPSB);
    const qint64 count = std::abs(qint64(cursor.i16()));
    if (count > 10'000)
        throw ImageImportError(ImageImportError::Kind::tooLarge);
    std::vector<RawLayer> raw;
    raw.reserve(size_t(count));
    for (qint64 index = 0; index < count; ++index)
        raw.push_back(readRecord(cursor, header.isPSB));
    // Too large whole: cut to the canvas, judged again.
    if (!fitsBudget(raw, remainingPixels)) {
        for (RawLayer &layer : raw)
            cropToCanvas(layer, header.width, header.height);
        if (!fitsBudget(raw, remainingPixels))
            throw ImageImportError(ImageImportError::Kind::tooLarge);
    }
    qint64 usedPixels = 0;
    for (RawLayer &layer : raw) {
        decodeChannels(cursor, layer, remainingPixels - usedPixels, header.isPSB);
        if (layer.image)
            usedPixels += qint64(layer.image->width()) * layer.image->height();
    }
    return PSDDocument{int(header.width), int(header.height), header.resolution,
                       assemble(raw, QSizeF(header.width, header.height), remainingPixels - usedPixels)};
}

// The merged image, as ImageIO reads a layerless file.
QImage mergedImage(const QByteArray &data, qint64 remainingPixels)
{
    Cursor cursor{data};
    const Header header = readHeader(cursor, false);
    if (header.width * header.height > remainingPixels)
        throw ImageImportError(ImageImportError::Kind::tooLarge);
    if (header.channels < 3)
        throw PSDError(PSDError::Kind::unsupportedColorMode);
    cursor.skip(cursor.length(header.isPSB));
    const int compression = cursor.u16();
    const QByteArray planes = data.mid(cursor.offset);
    // Opaque: a file of a background alone keeps no transparency.
    const std::vector<std::vector<uchar>> decoded = PSDChannelCoder::mergedPlanes(compression, int(header.width), int(header.height), header.channels, 3, planes, header.isPSB);
    QImage image = PSDChannelCoder::rgbaImage(int(header.width), int(header.height), decoded[0], decoded[1], decoded[2],
                                              std::vector<uchar>(size_t(header.width * header.height), 255));
    // Its own profile, else sRGB, as ImageIO reads it.
    const QColorSpace profile = QColorSpace::fromIccProfile(header.profile);
    image.setColorSpace(profile.isValid() ? profile : QColorSpace(QColorSpace::SRgb));
    return image;
}
}

const std::set<QString> PSDReader::adjustmentKeys{
    QStringLiteral("levl"), QStringLiteral("curv"), QStringLiteral("hue2"), QStringLiteral("hue "), QStringLiteral("expA"), QStringLiteral("grdm"),
    QStringLiteral("brit"), QStringLiteral("blnc"), QStringLiteral("nvrt"), QStringLiteral("thrs"), QStringLiteral("post"), QStringLiteral("mixr"),
    QStringLiteral("selc"), QStringLiteral("blwh"), QStringLiteral("phfl"), QStringLiteral("vibA"),
};

bool PSDReader::matches(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) && file.read(4) == "8BPS";
}

bool PSDReader::matches(const QByteArray &data)
{
    return data.startsWith("8BPS");
}

PSDDocument PSDReader::read(const QString &path, qint64 remainingPixels)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcIO).noquote() << "cannot open the Photoshop file" << path << file.errorString();
        throw ImageImportError(ImageImportError::Kind::unreadable);
    }
    // Swift's `mappedIfSafe`: mapped where it can be, else read.
    if (uchar *mapped = file.map(0, file.size()))
        return read(QByteArray::fromRawData(reinterpret_cast<const char *>(mapped), file.size()), remainingPixels);
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        qCWarning(lcIO).noquote() << "cannot read the Photoshop file" << path << file.errorString();
        throw ImageImportError(ImageImportError::Kind::unreadable);
    }
    return read(bytes, remainingPixels);
}

QImage PSDReader::merged(const QString &path, qint64 remainingPixels)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcIO).noquote() << "cannot open the Photoshop file" << path << file.errorString();
        throw ImageImportError(ImageImportError::Kind::unreadable);
    }
    // Swift asks the size first: the header, before the file.
    const QByteArray head = file.peek(26);
    Cursor cursor{head};
    cursor.skip(12);
    cursor.u16();
    const qint64 height = cursor.u32(), width = cursor.u32();
    if (width > DocumentLimits::maxSide || height > DocumentLimits::maxSide || width * height > remainingPixels)
        throw ImageImportError(ImageImportError::Kind::tooLarge);
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        qCWarning(lcIO).noquote() << "cannot read the Photoshop file" << path << file.errorString();
        throw ImageImportError(ImageImportError::Kind::unreadable);
    }
    return mergedImage(bytes, remainingPixels);
}

PSDDocument PSDReader::read(const QByteArray &data, qint64 remainingPixels)
{
    try {
        return parse(data, remainingPixels);
    } catch (const std::runtime_error &error) {
        qCWarning(lcIO) << "a Photoshop file was refused:" << error.what();
        throw;
    }
}
