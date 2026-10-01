#pragma once
#include "Document/EditorSession.h"
#include "UI/ColorPaletteControls.h"

// Swift's DialogColorSwatch: a dialog's colour in the app's picker.
class DialogColorSwatch : public SwatchButton {
    Q_OBJECT
public:
    DialogColorSwatch(const QString &title, std::function<PaletteColor()> colour, std::function<void(const PaletteColor &)> change, EditorSession &session,
                      QWidget *parent);
    // Puts the picker away with the dialog, keeping its colour.
    static void closePicker(EditorSession &session);

protected:
    // Swift's onDisappear: the picker goes with the swatch.
    void hideEvent(QHideEvent *event) override;

private:
    EditorSession &m_session;
    // Swift's onChange: the picker's colour as last seen.
    std::optional<PaletteColor> m_seen;
};
