#pragma once
#include <QString>

// Foundation's ByteCountFormatter, its memory and file styles, in English.
namespace ByteCounts {
// Binary units: bytes, KB whole, MB one place, GB two.
QString memory(qint64 bytes);
// Decimal units, as files count, with the same places.
QString file(qint64 bytes);
}
