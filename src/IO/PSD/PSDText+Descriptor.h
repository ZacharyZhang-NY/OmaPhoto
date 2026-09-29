#pragma once
#include <QByteArray>
#include <QRectF>
#include <QString>
#include <map>
#include <optional>
#include <variant>
#include <vector>

// Adobe's action descriptors, as far as a type layer needs.
namespace PSDDescriptor {
struct Value;
using Items = std::map<QString, Value>;

struct Enumeration {
    QString name;
};

struct Value {
    std::variant<QString, double, Enumeration, QByteArray, Items, std::vector<Value>> value;
};

std::optional<QString> string(const Items &items, const QString &key);
std::optional<QString> enumeration(const Items &items, const QString &key);
std::optional<QByteArray> data(const Items &items, const QString &key);
// Standardised, as CGRect's reads are.
std::optional<QRectF> rect(const Items &items, const QString &key);
// Big-endian UTF-16, a lone surrogate read as U+FFFD.
QString utf16(const QByteArray &raw);

// Big-endian reads over a block; each fails past its end.
struct Reader {
    const QByteArray &data;
    qsizetype offset = 0;
    int depth = 0;
    qsizetype remaining() const { return data.size() - offset; }
    std::optional<Items> descriptor(bool versioned);
    std::optional<QByteArray> bytes(qint64 count);
    std::optional<uchar> u8();
    std::optional<quint16> u16();
    std::optional<quint32> u32();
    std::optional<qint32> i32();
    std::optional<double> f64();

private:
    std::optional<Value> value(const QString &type);
    std::optional<Value> read(const QString &type);
    bool reference();
    std::optional<QString> unicode();
    std::optional<QString> identifier();
    std::optional<QString> fourCC();
};
}
