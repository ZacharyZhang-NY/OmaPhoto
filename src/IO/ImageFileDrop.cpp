#include "IO/ImageFileDrop.h"
#include "Document/ProjectWorkspace.h"
#include "Logging.h"
#include <QDir>
#include <QFile>
#include <QMimeDatabase>
#include <algorithm>

namespace {
// Swift's order: PNG, JPEG, HEIC, TIFF, then any picture.
QStringList pictureFormats(const QMimeData &data)
{
    QStringList formats;
    for (const QString &format : {QStringLiteral("image/png"), QStringLiteral("image/jpeg"), QStringLiteral("image/heic"), QStringLiteral("image/tiff")})
        if (data.hasFormat(format))
            formats << format;
    for (const QString &format : data.formats())
        if (format.startsWith(QLatin1String("image/")) && !formats.contains(format))
            formats << format;
    return formats;
}

// A dropped picture copied to a file for the importer.
std::optional<QUrl> temporaryFile(const QMimeData &data)
{
    const QStringList formats = pictureFormats(data);
    if (formats.isEmpty())
        return std::nullopt;
    const QString suffix = QMimeDatabase().mimeTypeForName(formats.first()).preferredSuffix();
    const QString name = QStringLiteral("Dropped-%1.%2").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper(), suffix.isEmpty() ? QStringLiteral("png") : suffix);
    QFile file(QDir::temp().filePath(name));
    const QByteArray bytes = data.data(formats.first());
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.flush()) {
        qCWarning(lcIO) << "could not keep a dropped picture:" << file.errorString();
        // A failed flush fails `remove()` too: close, then unlink.
        file.close();
        QFile::remove(file.fileName());
        return std::nullopt;
    }
    return QUrl::fromLocalFile(file.fileName());
}
}

bool ImageFileDrop::holdsImages(const QMimeData &data)
{
    for (const QUrl &url : data.urls())
        if (url.isLocalFile())
            return true;
    return !pictureFormats(data).isEmpty();
}

std::vector<std::shared_ptr<QMimeData>> ImageFileDrop::providers(const QMimeData &data)
{
    std::vector<std::shared_ptr<QMimeData>> each;
    const QList<QUrl> urls = data.urls();
    if (std::any_of(urls.begin(), urls.end(), [](const QUrl &url) { return url.isLocalFile(); })) {
        for (const QUrl &url : urls) {
            auto provider = std::make_shared<QMimeData>();
            provider->setUrls({url});
            each.push_back(provider);
        }
        return each;
    }
    // No file: a picture with its address is one item.
    auto whole = std::make_shared<QMimeData>();
    // Swift asks for the preferred picture alone.
    const QStringList formats = pictureFormats(data);
    if (!formats.isEmpty())
        whole->setData(formats.first(), data.data(formats.first()));
    each.push_back(whole);
    return each;
}

void ImageFileDrop::importProviders(const QMimeData &data, EditorSession &session, std::optional<QPointF> point, ProjectWorkspace *workspace,
                                    std::optional<QUuid> destination, std::function<void()> done)
{
    QList<QUrl> urls;
    bool unreadable = false;
    for (const QUrl &url : data.urls()) {
        if (url.isLocalFile())
            urls << url;
        else
            unreadable = true;
    }
    // Not a file on disk: a browser's or screenshot's picture.
    if (urls.isEmpty()) {
        const std::optional<QUrl> copy = temporaryFile(data);
        unreadable = !copy;
        if (copy)
            urls << *copy;
    }
    const auto finished = [&session, unreadable, done] {
        if (unreadable) {
            const QString message = QStringLiteral("Some dropped items couldn’t be read. Drag JPEG, PNG, HEIC, or TIFF files from the file manager.");
            session.setImportError(session.importError() ? *session.importError() + QStringLiteral("\n\n") + message : message);
        }
        if (done)
            done();
    };
    if (workspace)
        workspace->receive(urls, destination, point, finished);
    else
        session.importImages(urls, point, finished);
}
