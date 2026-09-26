#include "UI/ByteCounts.h"
#include <QLocale>
#include <array>

namespace {
// Adaptive: places by unit, trailing zeros dropped, digits grouped.
QString counted(qint64 bytes, int base)
{
    const QLocale english(QLocale::English, QLocale::UnitedStates);
    // Nothing counted here holds one byte or a terabyte.
    if (bytes < base)
        return english.toString(bytes) + QStringLiteral(" bytes");
    const std::array<QString, 3> units = {QStringLiteral("KB"), QStringLiteral("MB"), QStringLiteral("GB")};
    double value = double(bytes) / base;
    size_t unit = 0;
    while (value >= base) {
        value /= base;
        ++unit;
    }
    QString number = english.toString(value, 'f', int(unit));
    while (unit > 0 && number.endsWith(QLatin1Char('0')))
        number.chop(1);
    if (number.endsWith(QLatin1Char('.')))
        number.chop(1);
    return number + QLatin1Char(' ') + units.at(unit);
}
}

QString ByteCounts::memory(qint64 bytes)
{
    return counted(bytes, 1024);
}

QString ByteCounts::file(qint64 bytes)
{
    return counted(bytes, 1000);
}
