#pragma once
#include <QByteArray>
#include <QString>
#include <map>
#include <optional>
#include <variant>
#include <vector>

// Photoshop's text-engine dictionary, a small PostScript subset.
namespace PSDTextEngine {
struct Engine;
using Dictionary = std::map<QString, Engine>;
using Array = std::vector<Engine>;

struct Engine {
    std::variant<double, bool, QString, Dictionary, Array> value;
};

std::optional<Engine> parse(const QByteArray &data);
// Swift's `walk`: nested dictionary keys, else none.
const Engine *walk(const Engine *value, std::initializer_list<const char *> keys);
std::optional<double> number(const Engine *value);
std::optional<bool> boolean(const Engine *value);
std::optional<QString> string(const Engine *value);
// An array's items; anything else reads empty.
Array array(const Engine *value);
}
