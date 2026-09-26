// Swift's ColorPalette extension: foreground and background colours.
public:
    // The brush's own colour, as Swift's computed property.
    PaletteColor foregroundColor() const;
    void setForegroundColor(const PaletteColor &color);
    bool canEditPalette() const;
    // A mask's palette is black and white: its paint.
    PaletteColor paletteColor(bool background) const;
    void setPaletteColor(const PaletteColor &color, bool background);
    void swapPaletteColors();
    void resetPaletteColors();
    void openColorPicker(bool background);
    // The Type bar's swatch: open text's colour, else the foreground.
    PaletteColor typeColor() const;
    void openTextColorPicker();
    void closeColorPicker(bool commit);
    // The picker on an end of the open Gradient Map.
    void openGradientMapColorPicker(bool highlights);
    // The Gradient Map follows the picker's working colour.
    void previewGradientMapColor();
    // The picker on an effect of the open panel.
    void openEffectColorPicker(LayerEffectKind kind);
    // The effect follows the picker's working colour.
    void previewEffectColor();
    // Swift writes the picker's hsb in place; here, the session.
    void setColorPickerHSB(const PickerHSB &hsb);
    void sampleIntoColorPicker(QPointF point);
    // The visible layers' sRGB at a pixel; none where clear.
    std::optional<PaletteColor> sampleCompositeColor(QPointF point) const;

private:
    void setGradientMapColor(const PaletteColor &color, bool highlights);
