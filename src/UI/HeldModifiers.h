#pragma once
#include <QObject>

// Swift's HeldModifiers: keys held now, for the Move bar.
class HeldModifiers : public QObject {
    Q_OBJECT
public:
    static HeldModifiers &shared();
    Qt::KeyboardModifiers flags() const { return m_flags; }
    // Keys held while typing in a field count as none.
    void update(Qt::KeyboardModifiers raw);

signals:
    void changed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    HeldModifiers();

    Qt::KeyboardModifiers m_flags;
};
