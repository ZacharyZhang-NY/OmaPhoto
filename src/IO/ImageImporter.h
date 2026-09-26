#pragma once
#include <QImage>
#include <QString>
#include <compare>
#include <memory>
#include <stdexcept>

class RasterSnapshot;

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
    enum class Kind { unreadable, unsupported, tooLarge };
    explicit ImageImportError(Kind kind);
    Kind kind;
};

namespace ImageImporter {
// Qt refuses decodes over 128 MB; the app's budgets rule.
void liftAllocationLimit();
// JPEG, PNG, TIFF or HEIC: upright, sRGB, premultiplied.
ImportedImage decode(const QString &path, qint64 remainingPixels = 100'000'000);
}
