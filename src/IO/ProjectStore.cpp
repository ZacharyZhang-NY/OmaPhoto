#include "IO/ProjectStore.h"
#include "Document/LayerGroups.h"
#include "Document/LayerMask.h"
#include "Document/LiveLayerMask.h"
#include "Logging.h"
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QSet>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <functional>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

namespace {
QString description(ProjectError::Kind kind)
{
    switch (kind) {
    case ProjectError::Kind::invalid:
        return QStringLiteral("This is not a valid OmaPhoto project, or its metadata is damaged.");
    case ProjectError::Kind::version:
        throw std::logic_error("a version error needs its version number");
    case ProjectError::Kind::missingImage:
        return QStringLiteral("An image inside the project is missing or damaged. The current document has not been replaced.");
    case ProjectError::Kind::tooLarge:
        return QStringLiteral("This project exceeds the supported canvas, layer, file-size, or 100-megapixel image limit.");
    case ProjectError::Kind::encode:
        return QStringLiteral("An image could not be saved. The previous project has not been replaced.");
    }
    throw std::logic_error("unknown ProjectError kind");
}

constexpr qint64 manifestBytes = 4 * 1024 * 1024, assetBytes = 512 * 1024 * 1024;

// Guides came with version 8: a thousand, each once, bounded.
void validateGuides(const ProjectManifest &manifest)
{
    const std::vector<CanvasGuide> guides = manifest.guides.value_or(std::vector<CanvasGuide>());
    if (manifest.version < 8) {
        if (!guides.empty())
            throw ProjectError(ProjectError::Kind::invalid);
        return;
    }
    if (guides.size() > 1'000)
        throw ProjectError(ProjectError::Kind::tooLarge);
    QSet<QUuid> ids;
    for (const CanvasGuide &guide : guides) {
        // The range refuses NaN and infinity as well.
        if (ids.contains(guide.id) || !(std::abs(guide.position) <= 1'000'000))
            throw ProjectError(ProjectError::Kind::invalid);
        ids.insert(guide.id);
    }
}

void validate(const ProjectManifest &manifest)
{
    if (manifest.format != QLatin1String("com.compositor.project"))
        throw ProjectError(ProjectError::Kind::invalid);
    if (manifest.version < 1 || manifest.version > 8)
        throw ProjectError::unsupportedVersion(manifest.version);
    if (manifest.colorSpace != QLatin1String("sRGB"))
        throw ProjectError(ProjectError::Kind::invalid);
    // The range refuses NaN and infinity as well.
    if (manifest.resolution && !(*manifest.resolution >= 1 && *manifest.resolution <= 9600))
        throw ProjectError(ProjectError::Kind::invalid);
    if (manifest.width < 1 || manifest.width > 30'000 || manifest.height < 1 || manifest.height > 30'000 || manifest.layers.size() > 10'000)
        throw ProjectError(ProjectError::Kind::tooLarge);
    for (const ProjectLayerRecord &layer : manifest.layers) {
        // Text needs a valid style and pixels to show it.
        if (layer.text && (!layer.text->isValid() || !layer.imageFile))
            throw ProjectError(ProjectError::Kind::invalid);
        // Version 7's adjustments hold settings, never pixels or children.
        if (layer.adjustment && (manifest.version < 7 || layer.isGroup == true || layer.imageFile || !layer.adjustment->isValid()))
            throw ProjectError(ProjectError::Kind::invalid);
        const bool group = layer.isGroup == true;
        // Layer masks came with version 4, folder masks with 6.
        const bool maskAllowed = !layer.maskFile
            || (manifest.version >= (group ? 6 : 4) && *layer.maskFile == uuidString(layer.id) + QLatin1String(".mask.png"));
        const bool placementAllowed = !layer.maskPlacement || (layer.maskPlacement->isValid() && layer.maskFile);
        const double opacity = layer.opacity.value_or(1);
        const bool normal = layer.blendMode.value_or(LayerBlendMode::normal) == LayerBlendMode::normal;
        const bool plain = opacity == 1 && normal;
        // Folders dim from version 8; they always pass through.
        if (!maskAllowed || (layer.maskEnabled && !layer.maskFile) || !placementAllowed || !std::isfinite(opacity) || opacity < 0
            || opacity > 1 || (manifest.version < 3 && !plain) || (group && !(normal && (manifest.version >= 8 || opacity == 1))))
            throw ProjectError(ProjectError::Kind::invalid);
    }
    LayerHierarchy::validate(manifest.layers);
    LiveMaskGraph::validate(manifest.layers);
    QSet<QUuid> ids;
    for (const ProjectLayerRecord &layer : manifest.layers) {
        // A child needs a folder, so folders alone mark nesting.
        if ((manifest.version < 5 && layer.maskSourceID) || (manifest.version == 1 && layer.isGroup == true))
            throw ProjectError(ProjectError::Kind::invalid);
    }
    for (const ProjectLayerRecord &layer : manifest.layers) {
        const bool named = !layer.name.trimmed().isEmpty() && layer.name.toUtf8().size() <= 16'384;
        const bool filed = !layer.imageFile || *layer.imageFile == uuidString(layer.id) + QLatin1String(".png");
        if (!layer.transform.isValid() || !named || !filed)
            throw ProjectError(ProjectError::Kind::invalid);
        ids.insert(layer.id);
    }
    if (manifest.activeLayerID && !ids.contains(*manifest.activeLayerID))
        throw ProjectError(ProjectError::Kind::invalid);
    validateGuides(manifest);
}

// Images share 100 million pixels; masks share another.
void checkSize(QSize size, qint64 &used)
{
    const qint64 pixels = qint64(size.width()) * size.height();
    if (size.width() < 1 || size.width() > 30'000 || size.height() < 1 || size.height() > 30'000 || pixels > 100'000'000 - used)
        throw ProjectError(ProjectError::Kind::tooLarge);
    used += pixels;
}

// A project's files are plain files inside it, never links.
void checkFile(const QString &file, const QString &package, qint64 maximumBytes, ProjectError::Kind missing)
{
    const QFileInfo info(file);
    if (!info.exists() && !info.isSymLink())
        throw ProjectError(missing);
    const QString root = QFileInfo(package).canonicalFilePath() + QLatin1Char('/');
    if (!info.canonicalFilePath().startsWith(root))
        throw ProjectError(ProjectError::Kind::invalid);
    if (!info.isFile() || info.isSymLink() || info.size() > maximumBytes)
        throw ProjectError(ProjectError::Kind::tooLarge);
}

QByteArray png(const QImage &image)
{
    QByteArray data;
    QBuffer buffer(&data);
    QImageWriter writer(&buffer, "png");
    if (!writer.write(image))
        throw ProjectError(ProjectError::Kind::encode);
    return data;
}

[[noreturn]] void fail(const QString &what, const QString &path, const QString &reason)
{
    const QString message = QStringLiteral("%1 %2: %3").arg(what, path, reason);
    qCWarning(lcIO).noquote() << message;
    throw std::runtime_error(message.toStdString());
}

// Synced to disk: a swap must never expose empty files.
void writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.flush() || ::fsync(file.handle()) != 0)
        fail(QStringLiteral("could not write"), path, file.error() == QFile::NoError ? QString::fromLocal8Bit(std::strerror(errno)) : file.errorString());
}

void syncDirectory(const QString &path)
{
    const int descriptor = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_DIRECTORY);
    const bool synced = descriptor >= 0 && ::fsync(descriptor) == 0;
    const QString reason = QString::fromLocal8Bit(std::strerror(errno));
    if (descriptor >= 0)
        ::close(descriptor);
    if (!synced)
        fail(QStringLiteral("could not sync"), path, reason);
}

// Puts `staged` where `destination` is; a failed sync undoes it.
void replace(const QString &staged, const QString &destination, const QString &parent, bool &keepSibling)
{
    const QString besides = staged + QStringLiteral(".old");
    const QByteArray from = QFile::encodeName(staged), to = QFile::encodeName(destination), aside = QFile::encodeName(besides);
    const auto moved = [](const QByteArray &source, const QByteArray &target) { return ::rename(source.constData(), target.constData()) == 0; };
    const auto exchanged = [&] { return ::renameat2(AT_FDCWD, from.constData(), AT_FDCWD, to.constData(), RENAME_EXCHANGE) == 0; };
    // Each way in has its way back.
    std::function<bool()> undo;
    // Where the old project sits once the new is in.
    QString previous;
    if (!QFileInfo::exists(destination) && !QFileInfo(destination).isSymLink()) {
        if (!moved(from, to))
            fail(QStringLiteral("could not move the project to"), destination, QString::fromLocal8Bit(std::strerror(errno)));
        undo = [&] { return moved(to, from); };
    } else if (exchanged()) {
        undo = exchanged;
        previous = staged;
    } else {
        if (errno != EINVAL && errno != ENOSYS && errno != ENOTSUP)
            fail(QStringLiteral("could not replace"), destination, QString::fromLocal8Bit(std::strerror(errno)));
        // No exchange here: two moves, the old kept until then.
        qCWarning(lcIO) << "this file system cannot exchange paths; replacing" << destination << "in two steps";
        if (!moved(to, aside))
            fail(QStringLiteral("could not replace"), destination, QString::fromLocal8Bit(std::strerror(errno)));
        if (!moved(from, to)) {
            const QString reason = QString::fromLocal8Bit(std::strerror(errno));
            if (!moved(aside, to))
                qCCritical(lcIO) << "the previous project is left at" << besides;
            fail(QStringLiteral("could not replace"), destination, reason);
        }
        undo = [&] { return moved(to, from) && moved(aside, to); };
        previous = besides;
    }
    try {
        syncDirectory(parent);
    } catch (const std::runtime_error &) {
        // A way back that fails must delete nothing.
        keepSibling = !undo();
        const QString stays = previous.isEmpty() ? QStringLiteral("the unsynced project could not be taken from ") + destination
                                                 : QStringLiteral("the previous project could not be put back; it stays at ") + previous;
        if (keepSibling)
            qCCritical(lcIO).noquote() << stays;
        throw;
    }
    // The old project waits at the sibling's path for removal.
    if (previous == besides && !moved(aside, from))
        qCWarning(lcIO) << "the previous project is left at" << besides;
}

// The pixels and a thumbnail of at most 96 pixels.
ImportedImage asset(const QString &file, bool isMask, const QString &name, qint64 &used)
{
    ImageImporter::liftAllocationLimit();
    QImageReader reader(file);
    const QImage::Format stored = reader.imageFormat();
    const bool deep = stored == QImage::Format_RGBA64 || stored == QImage::Format_RGBA64_Premultiplied || stored == QImage::Format_RGBX64
        || stored == QImage::Format_Grayscale16;
    if (reader.format() != "png" || !reader.size().isValid() || deep)
        throw ProjectError(ProjectError::Kind::missingImage);
    checkSize(reader.size(), used);
    QImage image = reader.read();
    if (!isMask)
        image = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    if (image.isNull())
        throw ProjectError(ProjectError::Kind::missingImage);
    if (isMask && !LayerMask::isValid(image))
        throw ProjectError(ProjectError::Kind::invalid);
    const int longest = std::max(image.width(), image.height());
    const QImage thumbnail = longest <= 96 ? image : image.scaled(image.size().scaled(96, 96, Qt::KeepAspectRatio).expandedTo(QSize(1, 1)),
                                                                   Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (thumbnail.isNull())
        throw ProjectError(ProjectError::Kind::missingImage);
    return ImportedImage(image, thumbnail, name);
}
}

ProjectError::ProjectError(Kind kind) : ProjectError(kind, std::nullopt, description(kind)) {}

ProjectError ProjectError::unsupportedVersion(qint64 version)
{
    return ProjectError(Kind::version, version,
                        QStringLiteral("This project uses format version %1. This app supports versions 1–8.").arg(version));
}

ProjectError::ProjectError(Kind kind, std::optional<qint64> version, const QString &description)
    : std::runtime_error(description.toStdString()), kind(kind), version(version)
{
}

void ProjectStore::save(const ProjectSnapshot &snapshot, const QString &path)
{
    validate(snapshot.manifest);
    std::vector<std::pair<QString, QByteArray>> files;
    qint64 pixels = 0, maskPixels = 0;
    for (const ProjectLayerRecord &layer : snapshot.manifest.layers) {
        for (const bool isMask : {false, true}) {
            const std::optional<QString> &filename = isMask ? layer.maskFile : layer.imageFile;
            if (!filename)
                continue;
            const std::map<QUuid, ImportedImage> &assets = isMask ? snapshot.masks : snapshot.images;
            const auto found = assets.find(layer.id);
            if (found == assets.end())
                throw ProjectError(ProjectError::Kind::missingImage);
            const QImage image = found->second.image();
            if (isMask && !LayerMask::isValid(image))
                throw ProjectError(ProjectError::Kind::invalid);
            checkSize(image.size(), isMask ? maskPixels : pixels);
            files.push_back({*filename, png(image)});
        }
    }
    const QByteArray metadata = snapshot.manifest.encoded();
    if (metadata.size() > manifestBytes)
        throw ProjectError(ProjectError::Kind::tooLarge);
    // A sibling is written whole, then takes the project's place.
    const QFileInfo destination(QDir::cleanPath(path));
    const QString staged = destination.absolutePath() + QLatin1String("/.") + destination.fileName() + QLatin1Char('.')
        + uuidString(QUuid::createUuid()) + QLatin1String(".tmp");
    // Whatever lies at the sibling's path goes.
    struct Staging {
        QString path;
        bool keep = false;
        ~Staging()
        {
            const QFileInfo info(path);
            if (!keep && (info.exists() || info.isSymLink())
                && !(info.isDir() && !info.isSymLink() ? QDir(path).removeRecursively() : QFile::remove(path)))
                qCWarning(lcIO) << "could not remove" << path;
        }
    } staging{staged};
    if (!QDir().mkdir(staged) || !QDir().mkdir(staged + QLatin1String("/images")))
        fail(QStringLiteral("could not create"), staged, QStringLiteral("the folder cannot be made"));
    writeFile(staged + QLatin1String("/manifest.json"), metadata);
    for (const auto &[filename, data] : files)
        writeFile(staged + QLatin1String("/images/") + filename, data);
    syncDirectory(staged + QLatin1String("/images"));
    syncDirectory(staged);
    replace(staged, destination.absoluteFilePath(), destination.absolutePath(), staging.keep);
    qCInfo(lcIO) << "saved" << destination.absoluteFilePath() << "with" << snapshot.manifest.layers.size() << "layers and" << files.size() << "images";
}

ProjectSnapshot ProjectStore::load(const QString &path)
{
    const QString metadataPath = path + QLatin1String("/manifest.json");
    checkFile(metadataPath, path, manifestBytes, ProjectError::Kind::invalid);
    QFile metadataFile(metadataPath);
    if (!metadataFile.open(QIODevice::ReadOnly))
        throw ProjectError(ProjectError::Kind::invalid);
    const QByteArray metadata = metadataFile.readAll();
    // Qt hides errors after partial data: the byte count tells.
    if (metadataFile.error() != QFile::NoError || metadata.size() != metadataFile.size()) {
        qCWarning(lcIO) << "could not read" << metadataPath << ":" << metadataFile.errorString();
        throw ProjectError(ProjectError::Kind::invalid);
    }
    // The version comes first: a newer file may not decode.
    const auto [format, version] = ProjectManifest::header(metadata);
    if (format != QLatin1String("com.compositor.project"))
        throw ProjectError(ProjectError::Kind::invalid);
    if (version < 1 || version > 8)
        throw ProjectError::unsupportedVersion(version);
    const ProjectManifest manifest = ProjectManifest::decoded(metadata);
    validate(manifest);
    ProjectSnapshot snapshot{.manifest = manifest, .images = {}, .masks = {}};
    qint64 pixels = 0, maskPixels = 0;
    for (const ProjectLayerRecord &layer : manifest.layers) {
        for (const bool isMask : {false, true}) {
            const std::optional<QString> &filename = isMask ? layer.maskFile : layer.imageFile;
            if (!filename)
                continue;
            const QString file = path + QLatin1String("/images/") + *filename;
            checkFile(file, path, assetBytes, ProjectError::Kind::missingImage);
            (isMask ? snapshot.masks : snapshot.images).insert({layer.id, asset(file, isMask, layer.name, isMask ? maskPixels : pixels)});
        }
    }
    qCInfo(lcIO) << "loaded" << path << "with" << manifest.layers.size() << "layers and" << snapshot.images.size() + snapshot.masks.size() << "images";
    return snapshot;
}

