#include "Document/ToolDefaults.h"
#include "Logging.h"
#include <QSettings>

namespace {
const QString prefix = QStringLiteral("tool.");
// Swift's XCTest check: only the app keeps the toggles.
bool enabled = false;
}

void ToolDefaults::enable()
{
    enabled = true;
}

bool ToolDefaults::boolean(const QString &key, bool fallback)
{
    if (!enabled)
        return fallback;
    // The settings file keeps text; this run's writes are booleans.
    const QString stored = QSettings().value(prefix + key).toString();
    if (stored == QLatin1String("true") || stored == QLatin1String("false"))
        return stored == QLatin1String("true");
    if (!stored.isEmpty())
        qCWarning(lcApp).noquote() << "ignoring the tool setting" << key << "of" << stored;
    return fallback;
}

void ToolDefaults::set(bool value, const QString &key)
{
    if (enabled)
        QSettings().setValue(prefix + key, value);
}

int ToolDefaults::integer(const QString &key, int fallback)
{
    if (!enabled)
        return fallback;
    const QString stored = QSettings().value(prefix + key).toString();
    bool number = false;
    const int value = stored.toInt(&number);
    if (number)
        return value;
    if (!stored.isEmpty())
        qCWarning(lcApp).noquote() << "ignoring the tool setting" << key << "of" << stored;
    return fallback;
}

void ToolDefaults::set(int value, const QString &key)
{
    if (enabled)
        QSettings().setValue(prefix + key, value);
}

QString ToolDefaults::text(const QString &key)
{
    return enabled ? QSettings().value(prefix + key).toString() : QString();
}

void ToolDefaults::set(const QString &value, const QString &key)
{
    if (enabled)
        QSettings().setValue(prefix + key, value);
}
