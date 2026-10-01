#include "UI/KeyboardShortcuts.h"
#include "Logging.h"
#include <QAbstractButton>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QSettings>
#include <QShortcut>
#include <array>

namespace {
// Swift's special keys, their Qt keys and their names.
struct Special {
    QString key;
    Qt::Key qt;
};
const std::array specials{
    Special{QStringLiteral("\x7f"), Qt::Key_Backspace}, Special{QStringLiteral("\r"), Qt::Key_Return},
    Special{QStringLiteral("\x1b"), Qt::Key_Escape},    Special{QStringLiteral("\t"), Qt::Key_Tab},
    Special{QStringLiteral(" "), Qt::Key_Space},        Special{QString(QChar(0xf702)), Qt::Key_Left},
    Special{QString(QChar(0xf703)), Qt::Key_Right},     Special{QString(QChar(0xf701)), Qt::Key_Down},
    Special{QString(QChar(0xf700)), Qt::Key_Up},
};

int bits(Qt::KeyboardModifiers modifiers)
{
    return (modifiers.testFlag(Qt::ControlModifier) ? 1 : 0) | (modifiers.testFlag(Qt::AltModifier) ? 2 : 0)
        | (modifiers.testFlag(Qt::MetaModifier) ? 4 : 0) | (modifiers.testFlag(Qt::ShiftModifier) ? 8 : 0);
}

Qt::KeyboardModifiers flags(int bits)
{
    Qt::KeyboardModifiers result;
    result.setFlag(Qt::ControlModifier, bits & 1);
    result.setFlag(Qt::AltModifier, bits & 2);
    result.setFlag(Qt::MetaModifier, bits & 4);
    result.setFlag(Qt::ShiftModifier, bits & 8);
    return result;
}

// A key as Swift's charactersIgnoringModifiers reads it, lowered.
QString keyText(int key)
{
    if (key == Qt::Key_Delete || key == Qt::Key_Backspace)
        return QStringLiteral("\x7f");
    if (key == Qt::Key_Enter)
        return QStringLiteral("\r");
    if (key == Qt::Key_Backtab)
        return QStringLiteral("\t");
    for (const Special &special : specials) {
        if (special.qt == key)
            return special.key;
    }
    const QString typed = QKeySequence(key).toString(QKeySequence::PortableText).toLower();
    // Shifted brackets and signs count as their own keys.
    static const QHash<QString, QString> unshifted{{QStringLiteral("{"), QStringLiteral("[")}, {QStringLiteral("}"), QStringLiteral("]")},
                                                   {QStringLiteral("+"), QStringLiteral("=")}, {QStringLiteral("_"), QStringLiteral("-")}};
    return unshifted.value(typed, typed);
}

// Swift's shifted characters for the keys Shift changes.
QString shiftedText(const QString &key, bool shifted)
{
    static const QHash<QString, QString> shifts{{QStringLiteral("["), QStringLiteral("{")}, {QStringLiteral("]"), QStringLiteral("}")},
                                                {QStringLiteral("="), QStringLiteral("+")}, {QStringLiteral("-"), QStringLiteral("_")}};
    return shifted ? shifts.value(key, key) : key;
}

Qt::Key qtKey(const QString &key, bool shifted)
{
    for (const Special &special : specials) {
        if (special.key == key)
            return special.qt;
    }
    const QString typed = shiftedText(key, shifted);
    return typed.size() == 1 ? Qt::Key(typed.at(0).toUpper().unicode()) : Qt::Key_unknown;
}

ShortcutDefinition entry(const QString &title, const QString &key, int modifiers, bool menu)
{
    return {title, menu ? QStringLiteral("Menus") : QStringLiteral("Canvas & Layers"), ShortcutChord(key, modifiers)};
}

QJsonObject encoded(const QHash<QString, ShortcutChord> &values)
{
    QJsonObject object;
    for (auto value = values.cbegin(); value != values.cend(); ++value)
        object.insert(value.key(), QJsonObject{{QStringLiteral("key"), value->key}, {QStringLiteral("modifiers"), value->modifiers}});
    return object;
}
}

ShortcutChord::ShortcutChord(const QKeyEvent &event) : key(keyText(event.key())), modifiers(bits(event.modifiers())) {}

ShortcutChord::ShortcutChord(QKeyCombination combination)
    : key(keyText(combination.key())), modifiers(bits(combination.keyboardModifiers()))
{
}

QKeyCombination ShortcutChord::combination() const
{
    return QKeyCombination(flags(modifiers), qtKey(key, modifiers & 8));
}

QString ShortcutChord::label() const
{
    return QKeySequence(combination()).toString(QKeySequence::NativeText);
}

std::unique_ptr<QKeyEvent> ShortcutChord::event(const QKeyEvent &like) const
{
    const Qt::Key typed = qtKey(key, modifiers & 8);
    const QString text = typed == Qt::Key_Space || key.size() != 1 || key.at(0).unicode() >= 0xf700 || key.at(0).unicode() < 0x20
        ? QString()
        : shiftedText(key, modifiers & 8);
    return std::make_unique<QKeyEvent>(like.type(), typed, flags(modifiers), text, like.isAutoRepeat(), like.count());
}

const std::vector<ShortcutDefinition> &ShortcutDefinition::all()
{
    static const std::vector<ShortcutDefinition> definitions = [] {
        const QString backspace = QStringLiteral("\x7f");
        // Swift's list; Hide Compositor has no twin on Linux.
        std::vector<ShortcutDefinition> result{
            entry("Undo", "z", 1, true), entry("Redo", "z", 9, true), entry("New Canvas", "n", 1, true),
            entry("Open Project", "o", 1, true), entry("Save", "s", 1, true), entry("Save As", "s", 9, true),
            entry("Export PNG", "e", 9, true), entry("Export JPEG", "s", 11, true), entry("Close Project", "w", 1, true),
            entry("Fit Canvas", "0", 1, true), entry("Actual Pixels", "1", 1, true), entry("Zoom In", "=", 1, true),
            entry("Zoom Out", "-", 1, true), entry("Show Transform Controls", "h", 1, true), entry("Cut", "x", 1, true),
            entry("Copy", "c", 1, true), entry("Copy Merged", "c", 9, true), entry("Paste", "v", 1, true),
            entry("Fill with Foreground", backspace, 2, true), entry("Fill with Background", backspace, 1, true),
            entry("Content-Aware Fill", backspace, 8, true), entry("Select All", "a", 1, true), entry("Deselect", "d", 1, true),
            entry("Inverse Selection", "i", 9, true), entry("Select Subject", "a", 3, true), entry("Curves", "m", 1, true),
            entry("Levels", "l", 1, true), entry("Hue/Saturation", "u", 1, true), entry("Invert Pixels / Mask", "i", 1, true),
            entry("Canvas Size", "c", 3, true), entry("Image Size", "i", 3, true), entry("Transform Layer / Selection", "t", 1, true),
            entry("Duplicate / Layer via Copy", "j", 1, true), entry("Toggle Clipping Mask", "g", 3, true),
            entry("Group Layers", "g", 1, true), entry("Ungroup Layers", "g", 9, true), entry("New Blank Layer", "n", 9, true), entry("Move Layer Up", "]", 1, true),
            entry("Move Layer Down", "[", 1, true), entry("Merge Layers", "e", 1, true), entry("Show Grid", "'", 1, true),
            entry("Show Guides", ";", 1, true), entry("Show Rulers", "r", 1, true), entry("Snap", ";", 9, true), entry("Lock Guides", ";", 3, true)};
        const std::vector<std::pair<const char *, QString>> tools{
            {"Select tool", "a"}, {"Move / Transform tool", "v"}, {"Hand tool", "h"}, {"Zoom tool", "z"}, {"Brush tool", "b"},
            {"Eraser", "e"}, {"Spot Healing", "j"}, {"Clone Stamp", "s"}, {"Type tool", "t"}, {"Gradient tool", "g"},
            {"Shape tool", "u"}, {"Eyedropper tool", "i"}, {"Marquee / cycle shape", "m"}, {"Magic", "w"},
            {"Lasso / cycle mode", "l"}, {"Blur / Smudge / Liquify", "r"}, {"Crop tool", "c"}, {"Swap foreground/background", "x"},
            {"Reset colors", "d"}, {"Cycle tool mode", "\t"}, {"Temporary Hand tool (hold)", " "},
            {"Delete selection / layer / effect / lasso point", backspace}, {"Apply current canvas operation", "\r"},
            {"Cancel current canvas operation", "\x1b"}, {"Decrease brush size", "["}, {"Increase brush size", "]"}};
        for (const auto &[title, key] : tools)
            result.push_back(entry(title, key, 0, false));
        for (const auto &[title, key] : std::vector<std::pair<const char *, const char *>>{
                 {"Decrease brush hardness", "["}, {"Increase brush hardness", "]"}, {"Previous blend mode", "-"}, {"Next blend mode", "="}, {"Cycle shape kind", "u"}})
            result.push_back(entry(title, key, 8, false));
        for (int digit = 0; digit <= 9; ++digit)
            result.push_back(entry(QStringLiteral("Opacity digit %1 (type two for exact %)").arg(digit), QString::number(digit), 0, false));
        for (const auto &[direction, key] : std::vector<std::pair<QString, QString>>{
                 {"Left", QString(QChar(0xf702))}, {"Right", QString(QChar(0xf703))}, {"Up", QString(QChar(0xf700))}, {"Down", QString(QChar(0xf701))}}) {
            result.push_back(entry(QStringLiteral("Nudge %1 1 px").arg(direction), key, 0, false));
            result.push_back(entry(QStringLiteral("Nudge %1 10 px").arg(direction), key, 8, false));
            result.push_back(entry(QStringLiteral("Move selected pixels %1 1 px").arg(direction), key, 1, false));
            result.push_back(entry(QStringLiteral("Move selected pixels %1 10 px").arg(direction), key, 9, false));
        }
        const QString text = QStringLiteral("Text Editing");
        result.push_back({QStringLiteral("Finish editing text"), text, ShortcutChord(QStringLiteral("\r"), 1)});
        for (const auto &[title, key] : std::vector<std::pair<QString, QString>>{{"Decrease tracking", QString(QChar(0xf702))},
                                                                                 {"Increase tracking", QString(QChar(0xf703))},
                                                                                 {"Decrease leading", QString(QChar(0xf700))},
                                                                                 {"Increase leading", QString(QChar(0xf701))}}) {
            result.push_back({title, text, ShortcutChord(key, 2)});
            result.push_back({title + QStringLiteral(" by 10"), text, ShortcutChord(key, 10)});
        }
        result.push_back(entry("Toggle Levels preview", "p", 2, false));
        return result;
    }();
    return definitions;
}

ShortcutSettings &ShortcutSettings::shared()
{
    static ShortcutSettings settings;
    return settings;
}

ShortcutSettings::ShortcutSettings()
{
    reload();
}

void ShortcutSettings::reload()
{
    m_overrides.clear();
    const QByteArray data = QSettings().value(QLatin1String(storageKey)).toByteArray();
    if (!data.isEmpty()) {
        QHash<QString, ShortcutChord> saved;
        const QJsonObject object = QJsonDocument::fromJson(data).object();
        for (auto value = object.constBegin(); value != object.constEnd(); ++value) {
            const QJsonObject chord = value->toObject();
            saved.insert(value.key(), ShortcutChord(chord.value(QLatin1String("key")).toString(), chord.value(QLatin1String("modifiers")).toInt(-1)));
        }
        // Swift keeps stored overrides only while they hold together.
        if (const std::optional<QString> wrong = problem(saved))
            qCWarning(lcApp).noquote() << "stored keyboard shortcuts ignored:" << *wrong;
        else
            m_overrides = saved;
    }
    emit changed();
}

ShortcutChord ShortcutSettings::chord(const ShortcutDefinition &definition) const
{
    return m_overrides.value(definition.id(), definition.original);
}

QKeySequence ShortcutSettings::menu(const QKeySequence &original) const
{
    // An empty sequence reads as no key, which nothing matches.
    const ShortcutChord typed(original[0]);
    for (const ShortcutDefinition &definition : ShortcutDefinition::all()) {
        if (definition.isMenu() && definition.original == typed)
            return QKeySequence(chord(definition).combination());
    }
    return original;
}

ShortcutChord ShortcutSettings::native(const ShortcutChord &original) const
{
    for (const ShortcutDefinition &definition : ShortcutDefinition::all()) {
        if (!definition.isMenu() && definition.original == original)
            return chord(definition);
    }
    return original;
}

bool ShortcutSettings::save(const QHash<QString, ShortcutChord> &values)
{
    if (problem(values))
        return false;
    m_overrides = values;
    QSettings().setValue(QLatin1String(storageKey), QJsonDocument(encoded(values)).toJson(QJsonDocument::Compact));
    emit changed();
    return true;
}

std::optional<QString> ShortcutSettings::problem(const QHash<QString, ShortcutChord> &values)
{
    QHash<QString, QString> assigned;
    for (const ShortcutDefinition &definition : ShortcutDefinition::all()) {
        const ShortcutChord chord = values.value(definition.id(), definition.original);
        if (chord.key.size() != 1 || chord.modifiers < 0 || chord.modifiers > 15)
            return QStringLiteral("Choose a single key with optional modifiers.");
        if (definition.group == QLatin1String("Text Editing") && !(chord.modifiers & 7))
            return QStringLiteral("Text-editing shortcuts need Ctrl, Alt or Meta so they do not replace normal typing.");
        const QString name = QString::number(chord.modifiers) + QLatin1Char(':') + chord.key;
        if (assigned.contains(name))
            return QStringLiteral("%1 is assigned to both %2 and %3.").arg(chord.label(), assigned.value(name), definition.title);
        assigned.insert(name, definition.title);
    }
    return std::nullopt;
}

std::unique_ptr<QKeyEvent> ShortcutSettings::canvasEvent(const QKeyEvent &event) const
{
    std::unique_ptr<QKeyEvent> same(event.clone());
    if (m_overrides.isEmpty())
        return same;
    const ShortcutChord input(event);
    const std::vector<ShortcutDefinition> &all = ShortcutDefinition::all();
    for (const ShortcutDefinition &definition : all) {
        if (definition.group == QLatin1String("Canvas & Layers") && chord(definition) == input)
            return definition.original == input ? std::move(same) : definition.original.event(event);
    }
    for (const ShortcutDefinition &definition : all) {
        if (definition.group != QLatin1String("Text Editing") && definition.original == input && chord(definition) != input)
            return nullptr;
    }
    // Tool letters also take Shift, unless Shift has its own.
    if (input.modifiers == 8) {
        const ShortcutChord plain(input.key);
        for (const ShortcutDefinition &definition : all) {
            if (!definition.isMenu() && definition.original.modifiers == 0 && chord(definition) == plain)
                return ShortcutChord(definition.original.key, 8).event(event);
        }
        for (const ShortcutDefinition &definition : all) {
            if (!definition.isMenu() && definition.original == plain && chord(definition) != plain)
                return nullptr;
        }
    }
    return same;
}

std::unique_ptr<QKeyEvent> ShortcutSettings::textEvent(const QKeyEvent &event) const
{
    std::unique_ptr<QKeyEvent> same(event.clone());
    if (m_overrides.isEmpty())
        return same;
    std::vector<ShortcutDefinition> definitions;
    for (const ShortcutDefinition &definition : ShortcutDefinition::all()) {
        if (definition.group == QLatin1String("Text Editing") || definition.original == ShortcutChord(QStringLiteral("\x1b")))
            definitions.push_back(definition);
    }
    const ShortcutChord input(event);
    for (const ShortcutDefinition &definition : definitions) {
        if (chord(definition) == input)
            return definition.original == input ? std::move(same) : definition.original.event(event);
    }
    for (const ShortcutDefinition &definition : definitions) {
        if (definition.original == input && chord(definition) != input)
            return nullptr;
    }
    return same;
}

NativeShortcut::NativeShortcut(const ShortcutChord &original, QWidget &scope, QAbstractButton &target)
    : QObject(&scope), m_original(original), m_scope(scope), m_target(&target), m_shortcut(new QShortcut(&scope))
{
    m_shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    // A disabled button ignores click() itself.
    connect(m_shortcut, &QShortcut::activated, this, [this] {
        if (m_target && m_target->isVisible())
            m_target->click();
    });
    scope.installEventFilter(this);
    connect(&ShortcutSettings::shared(), &ShortcutSettings::changed, this, &NativeShortcut::synchronize);
    synchronize();
}

void NativeShortcut::bind(QWidget &scope, QAbstractButton *apply, QAbstractButton *cancel)
{
    if (apply)
        new NativeShortcut(ShortcutChord(QStringLiteral("\r")), scope, *apply);
    if (cancel)
        new NativeShortcut(ShortcutChord(QStringLiteral("\x1b")), scope, *cancel);
}

void NativeShortcut::synchronize()
{
    const ShortcutChord chord = ShortcutSettings::shared().native(m_original);
    m_shortcut->setKey(QKeySequence(chord.combination()));
    // Its own plain Return or Escape already reaches the button.
    const bool plain = !m_original.modifiers && (m_original.key == QLatin1String("\r") || m_original.key == QLatin1String("\x1b"));
    m_shortcut->setEnabled(!(plain && chord == m_original));
}

bool NativeShortcut::eventFilter(QObject *watched, QEvent *event)
{
    // The original key, moved elsewhere, no longer acts.
    if (watched == &m_scope && event->type() == QEvent::KeyPress) {
        const ShortcutChord typed(*static_cast<QKeyEvent *>(event));
        if (typed == m_original && ShortcutSettings::shared().native(m_original) != m_original)
            return true;
    }
    return QObject::eventFilter(watched, event);
}
