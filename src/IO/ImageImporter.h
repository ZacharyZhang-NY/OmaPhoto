#pragma once
#include "Document/DocumentLimits.h"
#include <QImage>
#include <QString>
#include <QUuid>
#include <compare>
#include <map>
#include <memory>
#include <stdexcept>

class RasterSnapshot;
struct PSDDocument;

// Which pixels an asset holds, as CGImage identity did.
struct ImageIdentity {
    const RasterSnapshot *raster = nullptr;
    qint64 cacheKey = 0;
    auto operator<=>(const ImageIdentity &) const = default;
};

class ImportedImage {
public:
    ImportedImage(QImage image, QImage thumbnail, QString name);
    ImportedImage(std::shared_ptr<const RasterSnapshot> raster, QImage thumbnail, QString name);

    QSize size() const;
    // Painted tiles flatten on first use.
    QImage image() const;
    ImageIdentity identity() const;
    qint64 byteCount() const;

    QImage thumbnail;
    QString name;
    std::shared_ptr<const RasterSnapshot> raster;

private:
    QImage m_image;
};

class ImageImportError : public std::runtime_error {
public:
    // whiteBalance is Linux's: LibRaw cannot take every white.
    enum class Kind { unreadable, unsupported, tooLarge, whiteBalance };
    explicit ImageImportError(Kind kind);
    Kind kind;
};

namespace ImageImporter {
// Qt refuses decodes over 128 MB; the app's budgets rule.
void liftAllocationLimit();
// JPEG, PNG, TIFF or HEIC: upright, sRGB, premultiplied.
ImportedImage decode(const QString &path, qint64 remainingPixels = DocumentLimits::documentPixelBudget());
PSDDocument loadPhotoshop(const QString &path, qint64 remainingPixels = DocumentLimits::documentPixelBudget());
std::map<QUuid, ImportedImage> photoshopAssets(const PSDDocument &document);
}
