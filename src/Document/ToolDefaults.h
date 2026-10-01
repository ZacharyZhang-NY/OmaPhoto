#pragma once
#include <QString>

// Swift's ToolDefaults: toggles kept for the person, across launches.
namespace ToolDefaults {
// The app turns them on; tests keep the compiled defaults.
void enable();
// The stored toggle, else the fallback.
bool boolean(const QString &key, bool fallback);
void set(bool value, const QString &key);
// The grid's numbers and names, as Swift's int and string.
int integer(const QString &key, int fallback);
void set(int value, const QString &key);
// Empty when nothing is stored, as Swift's "" fallback.
QString text(const QString &key);
void set(const QString &value, const QString &key);
}
