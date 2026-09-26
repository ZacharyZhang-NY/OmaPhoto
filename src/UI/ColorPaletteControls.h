#pragma once
#include "Document/EditorSession.h"
#include "UI/ColorPickerSheet.h"
#include <QAbstractButton>
#include <QPointer>
#include <QWidget>
#include <functional>

// A colour in a rounded rect; rims vary by bar.
class SwatchButton : public QAbstractButton {
    Q_OBJECT
public:
    SwatchButton(std::function<PaletteColor()> colour, double radius, double whiteRing, double rimAlpha, QWidget *parent);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    const std::function<PaletteColor()> m_colour;
    const double m_radius;
    const double m_whiteRing;
    const double m_rimAlpha;
};

// Swift's ColorPaletteControls: the swatches, swap, defaults, picker.
class ColorPaletteControls : public QWidget {
    Q_OBJECT
public:
    explicit ColorPaletteControls(EditorSession &session, QWidget *parent = nullptr);

private:
    // A mask's swatch offers black or white, as Swift's popover.
    void chooseMask(bool background);
    void synchronize();

    EditorSession &m_session;
    SwatchButton *const m_background;
    SwatchButton *const m_foreground;
    QAbstractButton *const m_swap;
    QAbstractButton *const m_reset;
    ColorPickerPanelController m_pickerPanel;
    // The picker the panel shows: a new one shows anew.
    std::optional<QUuid> m_shownPicker;
    bool m_masked;
    QPointer<QWidget> m_maskChoice;
};
