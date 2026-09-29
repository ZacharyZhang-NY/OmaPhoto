#pragma once
#include <QColor>
#include <QString>
#include <QUuid>
#include <optional>

// Swift's PaletteColor: an sRGB colour without alpha.
struct PaletteColor {
    double red;
    double green;
    double blue;
    static constexpr PaletteColor black() { return {0, 0, 0}; }
    static constexpr PaletteColor white() { return {1, 1, 1}; }
    QColor color(double alpha = 1) const { return QColor::fromRgbF(float(red), float(green), float(blue), float(alpha)); }
    // The 8-bit values painting and export store.
    PaletteColor quantized() const;
    QString hex() const;
    // Swift's init?(hex:): RRGGBB or RGB, with or without #.
    static std::optional<PaletteColor> fromHex(const QString &text);
    friend bool operator==(const PaletteColor &, const PaletteColor &) = default;
};

// The picker's own truth, so hue survives grays and black.
struct PickerHSB {
    PickerHSB(double hue, double saturation, double brightness) : hue(hue), saturation(saturation), brightness(brightness) {}
    explicit PickerHSB(const PaletteColor &color);
    double hue;
    double saturation;
    double brightness;
    PaletteColor rgb() const;
    // Grays keep the hue, black the saturation, as Photoshop's.
    void setRGB(const PaletteColor &color);
    friend bool operator==(const PickerHSB &, const PickerHSB &) = default;
};

enum class LayerEffectKind;

// The picker's targets: palette, text, effect, a filter's colour.
struct ColorPickerTarget {
    enum class Kind { palette, text, gradientMap, effect, vignette };
    Kind kind;
    bool background = false;
    std::optional<QUuid> draftID = std::nullopt;
    // A Gradient Map's end: highlights, else shadows.
    bool highlights = false;
    // An effect's colour: which effect.
    LayerEffectKind effect{};
    QString title() const;
    friend bool operator==(const ColorPickerTarget &, const ColorPickerTarget &) = default;
};

// The open picker's working colour: nothing is written until OK.
struct ColorPickerState {
    ColorPickerState(const ColorPickerTarget &target, const PaletteColor &original) : target(target), original(original), hsb(original) {}
    ColorPickerTarget target;
    PaletteColor original;
    PickerHSB hsb;
    // Swift's object identity: a new picker opens its panel anew.
    QUuid id = QUuid::createUuid();
    // The foreground picker over open text: that text, its colour.
    struct EditedText {
        QUuid draftID;
        PaletteColor color;
    };
    std::optional<EditedText> editedText;
    PaletteColor color() const { return hsb.rgb().quantized(); }
};
