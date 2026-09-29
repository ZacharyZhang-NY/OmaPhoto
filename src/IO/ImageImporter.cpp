#include "IO/ImageImporter.h"
#include "Document/BrushStroke.h"
#include "Document/DocumentLimits.h"
#include "Document/PixelAdjust.h"
#include "IO/PSD/PSDDocumentBuilder.h"
#include "IO/PSD/PSDReader.h"
#include "Logging.h"
#include "Rendering/RasterSnapshot.h"
#include <QColorSpace>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QSvgRenderer>
#include <QTransform>
#include <cmath>
#include <libheif/heif.h>

namespace {
QString description(ImageImportError::Kind kind)
{
    switch (kind) {
    case ImageImportError::Kind::unreadable:
        return QStringLiteral("The image could not be read. It may be damaged or unavailable.");
    case ImageImportError::Kind::unsupported:
        return QStringLiteral("Choose a JPEG, PNG, HEIC, TIFF, or Photoshop (PSD) file.");
    case ImageImportError::Kind::tooLarge:
        return QStringLiteral("This import exceeds the current %1-megapixel document budget or %2-pixel side limit.").arg(DocumentLimits::documentBudgetMegapixels()).arg(DocumentLimits::maxSideText());
    case ImageImportError::Kind::whiteBalance:
        return QStringLiteral("This camera can’t record that white balance. Move Temperature or Tint back toward the shot.");
    }
    Q_UNREACHABLE();
}

[[noreturn]] void refuse(ImageImportError::Kind kind, const QString &path, const QString &reason)
{
    qCWarning(lcIO).noquote() << "cannot import" << path + ":" << reason;
    throw ImageImportError(kind);
}

void checkSize(const QSize &size, qint64 remainingPixels, const QString &path)
{
    if (size.width() <= 0 || size.height() <= 0)
        refuse(ImageImportError::Kind::unreadable, path, QStringLiteral("it states no size"));
    if (size.width() > DocumentLimits::maxSide || size.height() > DocumentLimits::maxSide || qint64(size.width()) * size.height() > remainingPixels)
        refuse(ImageImportError::Kind::tooLarge, path, QStringLiteral("%1 x %2 pixels").arg(size.width()).arg(size.height()));
}

// HEVC in HEIF, by libheif's own reading of the brand.
bool isHEIC(const QByteArray &head)
{
    const char *type = heif_get_file_mime_type(reinterpret_cast<const uint8_t *>(head.constData()), int(head.size()));
    return qstrcmp(type, "image/heic") == 0;
}

// BT.709's curve, shared by BT.601 and BT.2020, as a table.
QList<uint16_t> videoCurve()
{
    QList<uint16_t> table(4096);
    for (int index = 0; index < table.size(); ++index) {
        const double value = index / double(table.size() - 1);
        table[index] = uint16_t(std::lround(65535 * (value < 0.081 ? value / 4.5 : std::pow((value + 0.099) / 1.099, 1 / 0.45))));
    }
    return table;
}

// A HEIC's colours: its ICC profile, else its nclx box.
QColorSpace statedColorSpace(const heif_image_handle *handle, const QString &path)
{
    QByteArray profile(qsizetype(heif_image_handle_get_raw_color_profile_size(handle)), Qt::Uninitialized);
    if (heif_image_handle_get_raw_color_profile(handle, profile.data()).code == heif_error_Ok)
        return QColorSpace::fromIccProfile(profile);
    heif_color_profile_nclx *box = nullptr;
    const heif_error error = heif_image_handle_get_nclx_color_profile(handle, &box);
    // No profile at all: untagged pixels count as sRGB.
    if (error.code == heif_error_Color_profile_does_not_exist)
        return QColorSpace();
    if (error.code != heif_error_Ok)
        refuse(ImageImportError::Kind::unreadable, path, QStringLiteral("its nclx colour box: ") + QString::fromUtf8(error.message));
    const std::unique_ptr<heif_color_profile_nclx, void (*)(heif_color_profile_nclx *)> nclx(box, heif_nclx_color_profile_free);
    // Unspecified primaries or curve count as sRGB's.
    QColorSpace stated(QColorSpace::SRgb);
    if (nclx->color_primaries != heif_color_primaries_unspecified)
        stated = QColorSpace(QPointF(nclx->color_primary_white_x, nclx->color_primary_white_y),
                             QPointF(nclx->color_primary_red_x, nclx->color_primary_red_y),
                             QPointF(nclx->color_primary_green_x, nclx->color_primary_green_y),
                             QPointF(nclx->color_primary_blue_x, nclx->color_primary_blue_y), QColorSpace::TransferFunction::SRgb);
    if (!stated.isValid())
        refuse(ImageImportError::Kind::unreadable, path,
               QStringLiteral("colour primaries %1 have no conversion to sRGB here").arg(int(nclx->color_primaries)));
    switch (nclx->transfer_characteristics) {
    case heif_transfer_characteristic_IEC_61966_2_1:
    case heif_transfer_characteristic_unspecified:
        return stated;
    case heif_transfer_characteristic_linear:
        stated.setTransferFunction(QColorSpace::TransferFunction::Linear);
        return stated;
    case heif_transfer_characteristic_ITU_R_BT_470_6_System_M:
        stated.setTransferFunction(QColorSpace::TransferFunction::Gamma, 2.2f);
        return stated;
    case heif_transfer_characteristic_ITU_R_BT_470_6_System_B_G:
        stated.setTransferFunction(QColorSpace::TransferFunction::Gamma, 2.8f);
        return stated;
    // xvYCC and BT.1361 are BT.709's curve between zero and one.
    case heif_transfer_characteristic_IEC_61966_2_4:
    case heif_transfer_characteristic_ITU_R_BT_1361:
    case heif_transfer_characteristic_ITU_R_BT_709_5:
    case heif_transfer_characteristic_ITU_R_BT_601_6:
    case heif_transfer_characteristic_ITU_R_BT_2020_2_10bit:
    case heif_transfer_characteristic_ITU_R_BT_2020_2_12bit:
        // Qt 6.4's constructor from points and a table is broken.
        stated.setTransferFunction(videoCurve());
        return stated;
    default:
        refuse(ImageImportError::Kind::unreadable, path,
               QStringLiteral("transfer characteristics %1 have no conversion to sRGB here").arg(int(nclx->transfer_characteristics)));
    }
}

// libheif turns and crops the picture as the file says.
QImage decodeHEIC(const QString &path, qint64 remainingPixels)
{
    // libheif's documented frame: deinit frees what init loaded.
    struct Library {
        Library() { heif_init(nullptr); }
        ~Library() { heif_deinit(); }
    } library;
    const std::unique_ptr<heif_context, void (*)(heif_context *)> context(heif_context_alloc(), heif_context_free);
    heif_error error = heif_context_read_from_file(context.get(), QFile::encodeName(path).constData(), nullptr);
    if (error.code != heif_error_Ok)
        refuse(ImageImportError::Kind::unreadable, path, QString::fromUtf8(error.message));
    heif_image_handle *primary = nullptr;
    error = heif_context_get_primary_image_handle(context.get(), &primary);
    if (error.code != heif_error_Ok)
        refuse(ImageImportError::Kind::unreadable, path, QString::fromUtf8(error.message));
    const std::unique_ptr<heif_image_handle, void (*)(const heif_image_handle *)> handle(primary, heif_image_handle_release);
    checkSize(QSize(heif_image_handle_get_width(primary), heif_image_handle_get_height(primary)), remainingPixels, path);
    const QColorSpace stated = statedColorSpace(primary, path);
    heif_image *decoded = nullptr;
    error = heif_decode_image(primary, &decoded, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, nullptr);
    if (error.code != heif_error_Ok)
        refuse(ImageImportError::Kind::unreadable, path, QString::fromUtf8(error.message));
    const std::unique_ptr<heif_image, void (*)(const heif_image *)> picture(decoded, heif_image_release);
    int stride = 0;
    const uint8_t *pixels = heif_image_get_plane_readonly(decoded, heif_channel_interleaved, &stride);
    if (!pixels)
        refuse(ImageImportError::Kind::unreadable, path, QStringLiteral("libheif gave no pixels"));
    const bool premultiplied = heif_image_handle_is_premultiplied_alpha(primary);
    // The copy outlives libheif's buffer.
    QImage image = QImage(pixels, heif_image_get_width(decoded, heif_channel_interleaved), heif_image_get_height(decoded, heif_channel_interleaved),
                          stride, premultiplied ? QImage::Format_RGBA8888_Premultiplied : QImage::Format_RGBA8888).copy();
    image.setColorSpace(stated);
    return image;
}

// Into sRGB with the profile still attached, then turned upright.
ImportedImage finish(QImage image, const QString &path, QImageIOHandler::Transformations orientation)
{
    image.convertToColorSpace(QColorSpace::SRgb);
    image = std::move(image).convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    // Qt 6.9 renamed mirrored as flipped; the floor stays 6.4.
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    Qt::Orientations flips;
    flips.setFlag(Qt::Horizontal, orientation.testFlag(QImageIOHandler::TransformationMirror));
    flips.setFlag(Qt::Vertical, orientation.testFlag(QImageIOHandler::TransformationFlip));
    image = std::move(image).flipped(flips);
#else
    image = std::move(image).mirrored(orientation.testFlag(QImageIOHandler::TransformationMirror),
                                      orientation.testFlag(QImageIOHandler::TransformationFlip));
#endif
    if (orientation.testFlag(QImageIOHandler::TransformationRotate90))
        image = image.transformed(QTransform().rotate(90));
    if (image.isNull())
        refuse(ImageImportError::Kind::unreadable, path, QStringLiteral("out of memory"));
    image.setColorSpace(QColorSpace::SRgb);
    // Swift: scale = min(1, 96 / longest), extent rounded outward.
    const qint64 longest = std::max(image.width(), image.height());
    const QSize small(int((image.width() * 96 + longest - 1) / longest), int((image.height() * 96 + longest - 1) / longest));
    const QImage thumbnail = longest <= 96 ? image : image.scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (thumbnail.isNull())
        refuse(ImageImportError::Kind::unreadable, path, QStringLiteral("out of memory"));
    const QString name = QFileInfo(path).completeBaseName();
    qCInfo(lcIO).noquote() << "imported" << path << image.width() << "x" << image.height();
    return ImportedImage(image, thumbnail, name.isEmpty() ? QFileInfo(path).fileName() : name);
}
}

ImportedImage::ImportedImage(QImage image, QImage thumbnail, QString name)
    : thumbnail(std::move(thumbnail)), name(std::move(name)), m_image(std::move(image))
{
}

ImportedImage::ImportedImage(std::shared_ptr<const RasterSnapshot> raster, QImage thumbnail, QString name)
    : thumbnail(std::move(thumbnail)), name(std::move(name)), raster(std::move(raster))
{
}

QSize ImportedImage::size() const
{
    return raster ? QSize(raster->width, raster->height) : m_image.size();
}

QImage ImportedImage::image() const
{
    return raster ? raster->makeImage() : m_image;
}

ImageIdentity ImportedImage::identity() const
{
    return raster ? ImageIdentity{raster.get(), 0} : ImageIdentity{nullptr, m_image.cacheKey()};
}

qint64 ImportedImage::byteCount() const
{
    if (!raster)
        return m_image.sizeInBytes();
    return qint64(raster->width) * raster->height * (raster->isMask ? 1 : 4);
}

ImageImportError::ImageImportError(Kind kind) : std::runtime_error(description(kind).toStdString()), kind(kind) {}

void ImageImporter::liftAllocationLimit()
{
    static const bool lifted = (QImageReader::setAllocationLimit(0), true);
    Q_UNUSED(lifted)
}

ImportedImage ImageImporter::decode(const QString &path, qint64 remainingPixels, bool flattenedPhotoshop)
{
    liftAllocationLimit();
    // A Photoshop file of a background alone: its merged image.
    if (flattenedPhotoshop && PSDReader::matches(path))
        return finish(PSDReader::merged(path, remainingPixels), path, QImageIOHandler::TransformationNone);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        refuse(ImageImportError::Kind::unreadable, path, file.errorString());
    // The type comes from the bytes: Qt checks them too.
    const QByteArray head = file.peek(64);
    QImageReader reader(&file);
    const QByteArray format = reader.format();
    const bool heic = isHEIC(head);
    // Other ISO media is unwanted: AVIF, plain HEIF, video.
    if (!heic && head.mid(4, 4) == "ftyp")
        refuse(ImageImportError::Kind::unsupported, path, QStringLiteral("ISO media that is not HEIC"));
    if (!heic && format.isEmpty())
        refuse(ImageImportError::Kind::unreadable, path, QStringLiteral("no image format is recognised"));
    if (!heic && format != "jpeg" && format != "png" && format != "tiff")
        refuse(ImageImportError::Kind::unsupported, path, QString::fromLatin1(format));
    // Qt's own turning drops the colour space: turn afterwards.
    QImageIOHandler::Transformations orientation = QImageIOHandler::TransformationNone;
    QImage image;
    if (heic) {
        image = decodeHEIC(path, remainingPixels);
    } else {
        checkSize(reader.size(), remainingPixels, path);
        orientation = reader.transformation();
        if (!reader.read(&image))
            refuse(ImageImportError::Kind::unreadable, path, reader.errorString());
    }
    return finish(std::move(image), path, orientation);
}

PSDDocument ImageImporter::loadPhotoshop(const QString &path, qint64 remainingPixels)
{
    return PSDReader::read(path, remainingPixels);
}

std::map<QUuid, ImportedImage> ImageImporter::photoshopAssets(const PSDDocument &document)
{
    return PSDDocumentBuilder::assets(document);
}

bool ImageImporter::isSVG(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix();
    return suffix.compare(QLatin1String("svg"), Qt::CaseInsensitive) == 0 || suffix.compare(QLatin1String("svgz"), Qt::CaseInsensitive) == 0;
}

ImportedImage ImageImporter::decodeSVG(const QString &path, std::optional<QSizeF> fitting, qint64 remainingPixels)
{
    QSvgRenderer svg(path);
    const QSizeF size = svg.defaultSize();
    // A drawing Qt cannot read measures nothing.
    if (size.width() <= 0 || size.height() <= 0)
        throw ImageImportError(ImageImportError::Kind::unreadable);
    const double scale = fitting ? std::min(fitting->width() / size.width(), fitting->height() / size.height()) : 1;
    const double width = std::max(1.0, std::round(size.width() * scale)), height = std::max(1.0, std::round(size.height() * scale));
    if (!(width <= DocumentLimits::maxSide && height <= DocumentLimits::maxSide && width * height <= double(remainingPixels)))
        throw ImageImportError(ImageImportError::Kind::tooLarge);
    QImage image = BrushRaster::context(int(width), int(height), false);
    QPainter painter(&image);
    svg.render(&painter, QRectF(0, 0, width, height));
    painter.end();
    image.setColorSpace(QColorSpace::SRgb);
    const QString name = QFileInfo(path).completeBaseName();
    qCInfo(lcIO).noquote() << "imported" << path << image.width() << "x" << image.height();
    return ImportedImage(image, PixelAdjust::thumbnail(image), name.isEmpty() ? QFileInfo(path).fileName() : name);
}
