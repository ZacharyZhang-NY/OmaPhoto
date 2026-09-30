#pragma once
#include <QByteArray>
#include <QString>

// Swift's ProjectDigest: the manifest, and each image's name and size.
namespace ProjectDigest {
// Throws std::runtime_error when the manifest cannot be read.
QByteArray compute(const QString &path);
}
