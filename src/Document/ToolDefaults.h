#pragma once
#include <QString>

// Swift's ToolDefaults: toggles kept for the person, across launches.
namespace ToolDefaults {
// The app turns them on; tests keep the compiled defaults.
void enable();
// The stored toggle, else the fallback.
bool boolean(const QString &key, bool fallback);
void set(bool value, const QString &key);
}
