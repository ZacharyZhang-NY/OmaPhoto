#include "UI/HeldModifiers.h"
#include <QApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QWidget>

HeldModifiers &HeldModifiers::shared()
{
    static HeldModifiers *held = new HeldModifiers;
    return *held;
}

HeldModifiers::HeldModifiers() : QObject(qApp)
{
    qApp->installEventFilter(this);
    // A key let go behind another app is never seen.
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        update(state == Qt::ApplicationActive ? QGuiApplication::queryKeyboardModifiers() : Qt::NoModifier);
    });
}

void HeldModifiers::update(Qt::KeyboardModifiers raw)
{
    // An NSText first responder: a widget taking typed text.
    const QWidget *focus = QApplication::focusWidget();
    const bool typing = focus && focus->testAttribute(Qt::WA_InputMethodEnabled);
    const Qt::KeyboardModifiers held =
        typing ? Qt::NoModifier : raw & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
    if (held == m_flags)
        return;
    m_flags = held;
    emit changed();
}

bool HeldModifiers::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type type = event->type();
    if (type == QEvent::KeyPress || type == QEvent::KeyRelease) {
        const auto *key = static_cast<QKeyEvent *>(event);
        Qt::KeyboardModifiers raw = key->modifiers();
        // A modifier's own event may not count it yet.
        const Qt::KeyboardModifier own = key->key() == Qt::Key_Control ? Qt::ControlModifier
            : key->key() == Qt::Key_Shift                               ? Qt::ShiftModifier
            : key->key() == Qt::Key_Alt                                 ? Qt::AltModifier
            : key->key() == Qt::Key_Meta                                ? Qt::MetaModifier
                                                                        : Qt::NoModifier;
        raw.setFlag(own, type == QEvent::KeyPress);
        update(raw);
    } else if (type == QEvent::MouseButtonPress || type == QEvent::MouseButtonRelease) {
        if (static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton)
            update(static_cast<QMouseEvent *>(event)->modifiers());
    } else if (type == QEvent::Hide && qobject_cast<QMenu *>(watched)) {
        // Nor is one let go while a menu is open.
        update(QGuiApplication::queryKeyboardModifiers());
    }
    return false;
}
