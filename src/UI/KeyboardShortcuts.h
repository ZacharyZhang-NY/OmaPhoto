#pragma once
#include <QHash>
#include <QKeySequence>
#include <QObject>
#include <QPointer>
#include <QPushButton>
#include <QString>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QAbstractButton;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QShortcut;
class QWidget;

// One key with its modifiers, stored as Swift stores it.
struct ShortcutChord {
    // One character; Backspace is 0x7f, the arrows Cocoa's F700s.
    QString key;
    // Bits: Ctrl 1, Alt 2, Meta 4, Shift 8.
    int modifiers = 0;

    ShortcutChord() = default;
    ShortcutChord(QString key, int modifiers = 0) : key(std::move(key)), modifiers(modifiers) {}
    explicit ShortcutChord(const QKeyEvent &event);
    explicit ShortcutChord(QKeyCombination combination);
    QKeyCombination combination() const;
    QString label() const;
    // The same press, as though this chord were typed.
    std::unique_ptr<QKeyEvent> event(const QKeyEvent &like) const;
    friend bool operator==(const ShortcutChord &, const ShortcutChord &) = default;
};

struct ShortcutDefinition {
    QString title;
    QString group;
    ShortcutChord original;
    QString id() const { return group + QLatin1Char(':') + title; }
    bool isMenu() const { return group == QLatin1String("Menus"); }
    static const std::vector<ShortcutDefinition> &all();
};

// Swift's ShortcutSettings: overrides, kept in QSettings.
class ShortcutSettings : public QObject {
    Q_OBJECT
public:
    static ShortcutSettings &shared();
    static constexpr const char *storageKey = "keyboardShortcuts.v1";
    const QHash<QString, ShortcutChord> &overrides() const { return m_overrides; }
    ShortcutChord chord(const ShortcutDefinition &definition) const;
    // A menu entry's key, or a sheet's, as remapped.
    QKeySequence menu(const QKeySequence &original) const;
    ShortcutChord native(const ShortcutChord &original) const;
    // Saves valid overrides; answers whether it did.
    bool save(const QHash<QString, ShortcutChord> &values);
    static std::optional<QString> problem(const QHash<QString, ShortcutChord> &values);
    // Null swallows the press; else the press to handle.
    std::unique_ptr<QKeyEvent> canvasEvent(const QKeyEvent &event) const;
    std::unique_ptr<QKeyEvent> textEvent(const QKeyEvent &event) const;
    // Reads the stored overrides again, as at launch.
    void reload();

signals:
    void changed();

private:
    ShortcutSettings();
    QHash<QString, ShortcutChord> m_overrides;
};

// Swift's configuredNativeShortcut on a sheet's button.
class NativeShortcut : public QObject {
    Q_OBJECT
public:
    NativeShortcut(const ShortcutChord &original, QWidget &scope, QAbstractButton &target);
    // A sheet's default (Return) and cancel (Escape) buttons.
    static void bind(QWidget &scope, QAbstractButton *apply, QAbstractButton *cancel);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void synchronize();

    const ShortcutChord m_original;
    QWidget &m_scope;
    QPointer<QAbstractButton> m_target;
    QShortcut *const m_shortcut;
};

// A shortcut's button: click, then press the new keys.
class ShortcutRecorder : public QPushButton {
    Q_OBJECT
public:
    ShortcutRecorder(std::function<void()> start, std::function<void(ShortcutChord)> finish, QWidget *parent);
    void display(const ShortcutChord &chord, bool recording);

protected:
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    const std::function<void()> m_start;
    const std::function<void(ShortcutChord)> m_finish;
    bool m_recording = false;
};

// Swift's KeyboardShortcutsSheet, in the "keyboardShortcuts" panel.
class KeyboardShortcutsSheet : public QWidget {
    Q_OBJECT
public:
    KeyboardShortcutsSheet(std::function<void()> close, QWidget *parent = nullptr);

private:
    void synchronize();

    const std::function<void()> m_close;
    QHash<QString, ShortcutChord> m_draft;
    std::optional<QString> m_recording;
    QLineEdit *const m_search;
    QLabel *const m_problem;
    QPushButton *const m_save;
    struct Row {
        QWidget *row;
        ShortcutRecorder *recorder;
        const ShortcutDefinition *definition;
    };
    std::vector<Row> m_rows;
};
