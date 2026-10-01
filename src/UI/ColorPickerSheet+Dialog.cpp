#include "UI/ColorPickerSheet+Dialog.h"
#include <QHideEvent>

// The brush's swatch: 34 by 18, a white ring.
DialogColorSwatch::DialogColorSwatch(const QString &title, std::function<PaletteColor()> colour, std::function<void(const PaletteColor &)> change,
                                     EditorSession &session, QWidget *parent)
    : SwatchButton(colour, 4, 1, 1, parent), m_session(session)
{
    setFixedSize(34, 18);
    setAccessibleName(title);
    connect(this, &QAbstractButton::clicked, this, [this, title, colour, change, &session] {
        session.openDialogColorPicker(title, colour(), [this, change](const PaletteColor &picked) {
            change(picked);
            update();
        });
    });
    connect(&session, &EditorSession::changed, this, [this, &session] {
        const std::optional<PaletteColor> now = session.colorPicker() ? std::optional(session.colorPicker()->color()) : std::nullopt;
        if (now == m_seen)
            return;
        m_seen = now;
        session.previewDialogColor();
    });
}

void DialogColorSwatch::hideEvent(QHideEvent *event)
{
    if (!event->spontaneous())
        closePicker(m_session);
    SwatchButton::hideEvent(event);
}

void DialogColorSwatch::closePicker(EditorSession &session)
{
    if (session.pickingForDialog())
        session.closeColorPicker(true);
}
