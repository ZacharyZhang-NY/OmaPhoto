#include "UI/KeyboardShortcuts.h"
#include <QKeyEvent>
#include <QSet>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QtTest>

// Swift's shortcut model: chords, definitions, rules, rewriting.
namespace {
QKeyEvent press(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier, const QString &text = QString())
{
    return QKeyEvent(QEvent::KeyPress, key, modifiers, text);
}

const ShortcutDefinition &named(const QString &title)
{
    for (const ShortcutDefinition &definition : ShortcutDefinition::all()) {
        if (definition.title == title)
            return definition;
    }
    throw std::runtime_error("no shortcut named " + title.toStdString());
}

void clear()
{
    QSettings().remove(QLatin1String(ShortcutSettings::storageKey));
    ShortcutSettings::shared().reload();
}
}

class KeyboardShortcutsTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanup();
    void chordsReadKeysAsSwiftDoes();
    void theListIsSwiftsWithoutHide();
    void problemsNameWhatIsWrong();
    void savedOverridesPersistAndBadOnesAreIgnored();
    void menusAndSheetsTakeTheirRemappedKeys();
    void theCanvasTranslatesRemappedKeys();
    void openTextTranslatesItsOwnKeys();
};

void KeyboardShortcutsTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    clear();
}

void KeyboardShortcutsTests::cleanup()
{
    clear();
}

void KeyboardShortcutsTests::chordsReadKeysAsSwiftDoes()
{
    QCOMPARE(ShortcutChord(press(Qt::Key_B)), ShortcutChord("b"));
    QCOMPARE(ShortcutChord(press(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier)), ShortcutChord("z", 9));
    QCOMPARE(ShortcutChord(press(Qt::Key_H, Qt::AltModifier | Qt::MetaModifier)), ShortcutChord("h", 6));
    // Shifted brackets and signs are their own keys.
    QCOMPARE(ShortcutChord(press(Qt::Key_BraceLeft, Qt::ShiftModifier)), ShortcutChord("[", 8));
    QCOMPARE(ShortcutChord(press(Qt::Key_BraceRight, Qt::ShiftModifier)), ShortcutChord("]", 8));
    QCOMPARE(ShortcutChord(press(Qt::Key_Plus, Qt::ShiftModifier)), ShortcutChord("=", 8));
    QCOMPARE(ShortcutChord(press(Qt::Key_Underscore, Qt::ShiftModifier)), ShortcutChord("-", 8));
    // Both deletes, returns and tabs are one key each.
    QCOMPARE(ShortcutChord(press(Qt::Key_Backspace)), ShortcutChord("\x7f"));
    QCOMPARE(ShortcutChord(press(Qt::Key_Delete)), ShortcutChord("\x7f"));
    QCOMPARE(ShortcutChord(press(Qt::Key_Enter)), ShortcutChord("\r"));
    QCOMPARE(ShortcutChord(press(Qt::Key_Backtab, Qt::ShiftModifier)), ShortcutChord("\t", 8));
    QCOMPARE(ShortcutChord(press(Qt::Key_Escape)), ShortcutChord("\x1b"));
    QCOMPARE(ShortcutChord(press(Qt::Key_Left)), ShortcutChord(QString(QChar(0xf702))));
    QCOMPARE(ShortcutChord(press(Qt::Key_Down)).key, QString(QChar(0xf701)));
    QCOMPARE(ShortcutChord(press(Qt::Key_F1)).key.size(), 2);
    // Labels and key combinations, as the menus show them.
    QCOMPARE(ShortcutChord("s", 11).label(), QString("Ctrl+Alt+Shift+S"));
    QCOMPARE(ShortcutChord("\x7f", 2).label(), QString("Alt+Backspace"));
    QCOMPARE(ShortcutChord("[", 8).combination(), QKeyCombination(Qt::ShiftModifier, Qt::Key_BraceLeft));
    QCOMPARE(ShortcutChord(QString(QChar(0xf700)), 1).combination(), QKeyCombination(Qt::ControlModifier, Qt::Key_Up));
    QCOMPARE(ShortcutChord(QKeyCombination(Qt::ControlModifier, Qt::Key_Equal)), ShortcutChord("=", 1));
    // A chord typed again carries its key, text and repeat.
    const QKeyEvent like(QEvent::KeyPress, Qt::Key_K, Qt::NoModifier, QStringLiteral("k"), true);
    const std::unique_ptr<QKeyEvent> typed = ShortcutChord("b").event(like);
    QVERIFY(typed->key() == Qt::Key_B && typed->modifiers() == Qt::NoModifier && typed->text() == "b" && typed->isAutoRepeat());
    const std::unique_ptr<QKeyEvent> space = ShortcutChord(" ").event(like);
    QVERIFY(space->key() == Qt::Key_Space && space->text().isEmpty());
    const std::unique_ptr<QKeyEvent> hard = ShortcutChord("]", 8).event(like);
    QVERIFY(hard->key() == Qt::Key_BraceRight && hard->modifiers() == Qt::ShiftModifier && hard->text() == "}");
}

void KeyboardShortcutsTests::theListIsSwiftsWithoutHide()
{
    const std::vector<ShortcutDefinition> &all = ShortcutDefinition::all();
    // Swift's 107, less Hide Compositor.
    QCOMPARE(int(all.size()), 106);
    QSet<QString> ids;
    for (const ShortcutDefinition &definition : all)
        ids.insert(definition.id());
    QCOMPARE(int(ids.size()), int(all.size()));
    QVERIFY(!ids.contains("Menus:Hide Compositor"));
    QCOMPARE(named("Export JPEG").original, ShortcutChord("s", 11));
    QVERIFY(named("Export JPEG").isMenu());
    QCOMPARE(named("Content-Aware Fill").original, ShortcutChord("\x7f", 8));
    QCOMPARE(named("Temporary Hand tool (hold)").original, ShortcutChord(" "));
    QCOMPARE(named("Opacity digit 7 (type two for exact %)").original, ShortcutChord("7"));
    QCOMPARE(named("Move selected pixels Down 10 px").original, ShortcutChord(QString(QChar(0xf701)), 9));
    QCOMPARE(named("Increase leading by 10").group, QString("Text Editing"));
    QCOMPARE(named("Increase leading by 10").original, ShortcutChord(QString(QChar(0xf701)), 10));
    QCOMPARE(named("Toggle Levels preview").group, QString("Canvas & Layers"));
    QCOMPARE(named("Undo").id(), QString("Menus:Undo"));
    // Swift's defaults hold together.
    QVERIFY(!ShortcutSettings::problem({}));
}

void KeyboardShortcutsTests::problemsNameWhatIsWrong()
{
    const QString brush = named("Brush tool").id(), finish = named("Finish editing text").id();
    QCOMPARE(ShortcutSettings::problem({{brush, ShortcutChord("f1")}}).value(), QString("Choose a single key with optional modifiers."));
    QCOMPARE(ShortcutSettings::problem({{brush, ShortcutChord("k", 16)}}).value(), QString("Choose a single key with optional modifiers."));
    QCOMPARE(ShortcutSettings::problem({{brush, ShortcutChord("k", -1)}}).value(), QString("Choose a single key with optional modifiers."));
    QCOMPARE(ShortcutSettings::problem({{finish, ShortcutChord("\r", 8)}}).value(),
             QString("Text-editing shortcuts need Ctrl, Alt or Meta so they do not replace normal typing."));
    QVERIFY(!ShortcutSettings::problem({{finish, ShortcutChord("\r", 4)}}));
    QCOMPARE(ShortcutSettings::problem({{brush, ShortcutChord("v")}}).value(), QString("V is assigned to both Move / Transform tool and Brush tool."));
    // Moving both apart is fine.
    QVERIFY(!ShortcutSettings::problem({{brush, ShortcutChord("v")}, {named("Move / Transform tool").id(), ShortcutChord("b")}}));
}

void KeyboardShortcutsTests::savedOverridesPersistAndBadOnesAreIgnored()
{
    ShortcutSettings &settings = ShortcutSettings::shared();
    QSignalSpy changes(&settings, &ShortcutSettings::changed);
    const QString brush = named("Brush tool").id();
    QVERIFY(!settings.save({{brush, ShortcutChord("v")}}));
    QVERIFY(settings.overrides().isEmpty() && changes.isEmpty());
    QVERIFY(settings.save({{brush, ShortcutChord("k")}}));
    QCOMPARE(changes.count(), 1);
    QCOMPARE(settings.chord(named("Brush tool")), ShortcutChord("k"));
    QCOMPARE(settings.chord(named("Eraser")), ShortcutChord("e"));
    // Read again as at launch, from QSettings.
    settings.reload();
    QCOMPARE(settings.overrides(), (QHash<QString, ShortcutChord>{{brush, ShortcutChord("k")}}));
    QCOMPARE(QSettings().value(QLatin1String(ShortcutSettings::storageKey)).toByteArray(),
             QByteArray(R"({"Canvas & Layers:Brush tool":{"key":"k","modifiers":0}})"));
    // Stored overrides that clash are dropped with a warning.
    QSettings().setValue(QLatin1String(ShortcutSettings::storageKey), QByteArray(R"({"Canvas & Layers:Brush tool":{"key":"v","modifiers":0}})"));
    QTest::ignoreMessage(QtWarningMsg, "stored keyboard shortcuts ignored: V is assigned to both Move / Transform tool and Brush tool.");
    settings.reload();
    QVERIFY(settings.overrides().isEmpty());
}

void KeyboardShortcutsTests::menusAndSheetsTakeTheirRemappedKeys()
{
    ShortcutSettings &settings = ShortcutSettings::shared();
    const QKeySequence undo(Qt::CTRL | Qt::Key_Z);
    QCOMPARE(settings.menu(undo), undo);
    QVERIFY(settings.save({{named("Undo").id(), ShortcutChord("u", 3)}, {named("Hue/Saturation").id(), ShortcutChord("h", 3)},
                           {named("Apply current canvas operation").id(), ShortcutChord("k")}}));
    QCOMPARE(settings.menu(undo), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_U));
    // An unlisted entry keeps its key; none stays none.
    QCOMPARE(settings.menu(QKeySequence(Qt::CTRL | Qt::Key_L)), QKeySequence(Qt::CTRL | Qt::Key_L));
    QCOMPARE(settings.menu(QKeySequence(Qt::CTRL | Qt::Key_H)), QKeySequence(Qt::CTRL | Qt::Key_H));
    QCOMPARE(settings.menu(QKeySequence()), QKeySequence());
    // Sheets read the canvas's Apply and Cancel.
    QCOMPARE(settings.native(ShortcutChord("\r")), ShortcutChord("k"));
    QCOMPARE(settings.native(ShortcutChord("\x1b")), ShortcutChord("\x1b"));
    QCOMPARE(settings.native(ShortcutChord("y", 1)), ShortcutChord("y", 1));
    // A menu's chord is no sheet's.
    QCOMPARE(settings.native(ShortcutChord("z", 1)), ShortcutChord("z", 1));
}

void KeyboardShortcutsTests::theCanvasTranslatesRemappedKeys()
{
    ShortcutSettings &settings = ShortcutSettings::shared();
    // Without overrides every key passes as it is.
    std::unique_ptr<QKeyEvent> same = settings.canvasEvent(press(Qt::Key_B, Qt::NoModifier, "b"));
    QVERIFY(same && same->key() == Qt::Key_B);
    QVERIFY(settings.save({{named("Brush tool").id(), ShortcutChord("k")}, {named("Undo").id(), ShortcutChord("u", 3)},
                           {named("Cycle shape kind").id(), ShortcutChord("y", 8)}}));
    // The new key stands for the old; the old goes.
    const std::unique_ptr<QKeyEvent> brush = settings.canvasEvent(press(Qt::Key_K, Qt::NoModifier, "k"));
    QVERIFY(brush && brush->key() == Qt::Key_B && brush->modifiers() == Qt::NoModifier);
    QVERIFY(!settings.canvasEvent(press(Qt::Key_B, Qt::NoModifier, "b")));
    // A menu's old chord is swallowed at the canvas too.
    QVERIFY(!settings.canvasEvent(press(Qt::Key_Z, Qt::ControlModifier)));
    // Shift follows a letter home, unless Shift has its own.
    const std::unique_ptr<QKeyEvent> shifted = settings.canvasEvent(press(Qt::Key_K, Qt::ShiftModifier, "K"));
    QVERIFY(shifted && shifted->key() == Qt::Key_B && shifted->modifiers() == Qt::ShiftModifier);
    QVERIFY(!settings.canvasEvent(press(Qt::Key_B, Qt::ShiftModifier, "B")));
    const std::unique_ptr<QKeyEvent> kind = settings.canvasEvent(press(Qt::Key_Y, Qt::ShiftModifier, "Y"));
    QVERIFY(kind && kind->key() == Qt::Key_U && kind->modifiers() == Qt::ShiftModifier);
    QVERIFY(!settings.canvasEvent(press(Qt::Key_U, Qt::ShiftModifier, "U")));
    // Keys nobody moved pass untouched.
    const std::unique_ptr<QKeyEvent> eraser = settings.canvasEvent(press(Qt::Key_E, Qt::ShiftModifier, "E"));
    QVERIFY(eraser && eraser->key() == Qt::Key_E && eraser->modifiers() == Qt::ShiftModifier);
    const std::unique_ptr<QKeyEvent> other = settings.canvasEvent(press(Qt::Key_Q, Qt::NoModifier, "q"));
    QVERIFY(other && other->key() == Qt::Key_Q);
}

void KeyboardShortcutsTests::openTextTranslatesItsOwnKeys()
{
    ShortcutSettings &settings = ShortcutSettings::shared();
    QVERIFY(settings.save({{named("Finish editing text").id(), ShortcutChord("\r", 4)}, {named("Brush tool").id(), ShortcutChord("k")},
                           {named("Cancel current canvas operation").id(), ShortcutChord("q", 1)}}));
    const std::unique_ptr<QKeyEvent> finish = settings.textEvent(press(Qt::Key_Return, Qt::MetaModifier));
    QVERIFY(finish && finish->key() == Qt::Key_Return && finish->modifiers() == Qt::ControlModifier);
    QVERIFY(!settings.textEvent(press(Qt::Key_Return, Qt::ControlModifier)));
    // Escape follows Cancel; tool letters stay letters.
    const std::unique_ptr<QKeyEvent> cancel = settings.textEvent(press(Qt::Key_Q, Qt::ControlModifier));
    QVERIFY(cancel && cancel->key() == Qt::Key_Escape && cancel->modifiers() == Qt::NoModifier);
    QVERIFY(!settings.textEvent(press(Qt::Key_Escape)));
    const std::unique_ptr<QKeyEvent> letter = settings.textEvent(press(Qt::Key_K, Qt::NoModifier, "k"));
    QVERIFY(letter && letter->key() == Qt::Key_K && letter->text() == "k");
    const std::unique_ptr<QKeyEvent> b = settings.textEvent(press(Qt::Key_B, Qt::NoModifier, "b"));
    QVERIFY(b && b->key() == Qt::Key_B);
    // The canvas leaves a text chord's old key alone.
    const std::unique_ptr<QKeyEvent> canvas = settings.canvasEvent(press(Qt::Key_Return, Qt::ControlModifier));
    QVERIFY(canvas && canvas->key() == Qt::Key_Return && canvas->modifiers() == Qt::ControlModifier);
}

QTEST_MAIN(KeyboardShortcutsTests)
#include "KeyboardShortcutsTests.moc"
