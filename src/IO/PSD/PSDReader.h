#pragma once
#include "IO/PSD/PSDTypes.h"
#include <QByteArray>
#include <QString>
#include <map>
#include <set>

// Photoshop `.psd` from Adobe's 2019 specification; no GPL code.
namespace PSDReader {
bool matches(const QString &path);
bool matches(const QByteArray &data);
PSDDocument read(const QString &path, qint64 remainingPixels = 100'000'000);
PSDDocument read(const QByteArray &data, qint64 remainingPixels = 100'000'000);
extern const std::set<QString> adjustmentKeys;
}

// Levels, Curves and Hue/Saturation layers, as Swift reads them.
namespace PSDAdjustments {
std::optional<LayerAdjustment> parse(const std::map<QString, QByteArray> &extra);
}
