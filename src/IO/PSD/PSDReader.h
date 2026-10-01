#pragma once
#include "Document/DocumentLimits.h"
#include "IO/PSD/PSDTypes.h"
#include <QByteArray>
#include <QImage>
#include <QString>
#include <map>
#include <set>

// Photoshop `.psd` from Adobe's 2019 specification; no GPL code.
namespace PSDReader {
bool matches(const QString &path);
bool matches(const QByteArray &data);
PSDDocument read(const QString &path, qint64 remainingPixels = DocumentLimits::documentPixelBudget());
PSDDocument read(const QByteArray &data, qint64 remainingPixels = DocumentLimits::documentPixelBudget());
// A file with no layers: its merged image, profile attached.
QImage merged(const QString &path, qint64 remainingPixels = DocumentLimits::documentPixelBudget());
extern const std::set<QString> adjustmentKeys;
}

// Levels, Curves and Hue/Saturation layers, as Swift reads them.
namespace PSDAdjustments {
std::optional<LayerAdjustment> parse(const std::map<QString, QByteArray> &extra);
// 'levl': each channel's levels, gamma in hundredths.
std::optional<LayerAdjustment> levels(const QByteArray &data);
// 'hue2': Colorize or the Master, then each range's values.
std::optional<LayerAdjustment> hue(const QByteArray &data);
}
