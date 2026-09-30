#include "IO/ProjectDigest.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtEndian>
#include <algorithm>
#include <stdexcept>

QByteArray ProjectDigest::compute(const QString &path)
{
    QCryptographicHash hasher(QCryptographicHash::Sha256);
    QFile manifest(path + QStringLiteral("/manifest.json"));
    if (!manifest.open(QIODevice::ReadOnly))
        throw std::runtime_error(manifest.errorString().toStdString());
    // A failed read comes up short; Swift's Data(contentsOf:) throws.
    const QByteArray bytes = manifest.readAll();
    if (bytes.size() != manifest.size())
        throw std::runtime_error("the manifest could not be read whole");
    hasher.addData(bytes);
    // Assets are not read: a save would wait on each.
    const QDir images(path + QStringLiteral("/images"));
    QStringList names = images.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Unsorted);
    std::sort(names.begin(), names.end());
    for (const QString &name : names) {
        const QFileInfo info(images.filePath(name));
        // Gone mid-listing: half written, as Swift's resourceValues throws.
        if (!info.exists() && !info.isSymLink())
            throw std::runtime_error("an image went while the package was read");
        if (info.isSymLink() || !info.isFile())
            continue;
        hasher.addData(name.toUtf8());
        const quint64 size = qToLittleEndian(quint64(info.size()));
        hasher.addData(QByteArrayView(reinterpret_cast<const char *>(&size), sizeof size));
    }
    return hasher.result();
}
