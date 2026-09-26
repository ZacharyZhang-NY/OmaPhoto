#include "UI/LayerMaskMenu.h"
#include "UI/LayerIcons.h"
#include <QEvent>

LayerMaskMenu::LayerMaskMenu(EditorSession &session, QWidget *parent) : QToolButton(parent), m_session(session)
{
    setObjectName(QStringLiteral("addLayerMask"));
    setAccessibleName(QStringLiteral("Add layer mask"));
    setAutoRaise(true);
    setFixedSize(32, 40);
    connect(this, &QToolButton::clicked, this, [this] { m_session.addMask(); });
    connect(&m_session, &EditorSession::changed, this, &LayerMaskMenu::synchronize);
    synchronize();
}

void LayerMaskMenu::synchronize()
{
    const std::optional<ImageLayer> active = m_session.activeLayer();
    setEnabled(m_session.canEditMask() && !(active && active->mask));
    setToolTip(m_session.selection() ? QStringLiteral("Add layer mask (the selection becomes black)") : QStringLiteral("Add layer mask"));
    setIcon(LayerIcons::pixmap(LayerIcon::addMask, 16, palette().color(QPalette::PlaceholderText), devicePixelRatio()));
}

void LayerMaskMenu::changeEvent(QEvent *event)
{
    QToolButton::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        synchronize();
}
