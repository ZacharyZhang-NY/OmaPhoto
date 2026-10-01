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
    // The picker on the open Vignette's colour, which follows it.
    void openVignetteColorPicker();
    void previewVignetteColor();
    // The picker on Dither's dark or light colour.
    void openDitherColorPicker(bool light);
    void previewDitherColor();
    // The picker on an effect of the open panel.
    void openEffectColorPicker(LayerEffectKind kind);
    // The effect follows the picker's working colour.
    void previewEffectColor();
    // The picker on a dialog's colour, which hears each change.
    void openDialogColorPicker(const QString &title, const PaletteColor &color, std::function<void(const PaletteColor &)> change);
    // A dialog covers the canvas: nothing to sample.
    bool pickingForDialog() const;
    void previewDialogColor() const;
    // The open text draft previews the picker's working colour.
    void previewTextColor();
    // Paints the open text's selected letters, or all of it.
    void setDraftTextColor(const PaletteColor &color);
    // Swift writes the picker's hsb in place; here, the session.
    void setColorPickerHSB(const PickerHSB &hsb);
    void sampleIntoColorPicker(QPointF point);
    // The visible layers' sRGB at a pixel; none where clear.
    std::optional<PaletteColor> sampleCompositeColor(QPointF point) const;

private:
    // Puts back the open text's colours from the picker's opening.
    void restoreDraftTextColors(const LayerTextStyle &original);
    void setGradientMapColor(const PaletteColor &color, bool highlights);
    void setVignetteColor(const PaletteColor &color);
    void setDitherColor(const PaletteColor &color, bool light);
