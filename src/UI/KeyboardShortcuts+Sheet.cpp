#include "UI/KeyboardShortcuts.h"
#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {
QLabel *headline(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    QFont font = label->font();
    font.setPixelSize(13);
    font.setWeight(QFont::DemiBold);
    label->setFont(font);
    return label;
}

QLabel *words(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    return label;
}

QFrame *divider(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setForegroundRole(QPalette::Mid);
    return line;
}

bool isModifier(int key)
{
    return key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Meta || key == Qt::Key_AltGr
        || key == Qt::Key_Super_L || key == Qt::Key_Super_R;
}
}

ShortcutRecorder::ShortcutRecorder(std::function<void()> start, std::function<void(ShortcutChord)> finish, QWidget *parent)
    : QPushButton(parent), m_start(std::move(start)), m_finish(std::move(finish))
{
    setAutoDefault(false);
    setFixedSize(150, 26);
    connect(this, &QPushButton::clicked, this, [this] {
        setFocus();
        m_start();
    });
}

void ShortcutRecorder::display(const ShortcutChord &chord, bool recording)
{
    m_recording = recording;
    setText(recording ? QStringLiteral("Press keys…") : chord.label());
    setAccessibleName(recording ? QStringLiteral("Press a shortcut") : chord.label());
}

// While recording every key is the recorder's: menus' and Tab.
bool ShortcutRecorder::event(QEvent *event)
{
    if (m_recording && event->type() == QEvent::ShortcutOverride) {
        event->accept();
        return true;
    }
    if (m_recording && event->type() == QEvent::KeyPress) {
        keyPressEvent(static_cast<QKeyEvent *>(event));
        return true;
    }
    return QPushButton::event(event);
}

void ShortcutRecorder::keyPressEvent(QKeyEvent *event)
{
    if (!m_recording) {
        QPushButton::keyPressEvent(event);
        return;
    }
    // A modifier alone is still being held down.
    if (isModifier(event->key()))
        return;
    const ShortcutChord chord(*event);
    if (chord.key.size() != 1) {
        QApplication::beep();
        return;
    }
    m_finish(chord);
    clearFocus();
}

KeyboardShortcutsSheet::KeyboardShortcutsSheet(std::function<void()> close, QWidget *parent)
    : QWidget(parent), m_close(std::move(close)), m_draft(ShortcutSettings::shared().overrides()), m_search(new QLineEdit(this)),
      m_problem(new QLabel(this)), m_save(new QPushButton(QStringLiteral("Save"), this))
{
    setObjectName(QStringLiteral("keyboardShortcutsSheet"));
    setFixedWidth(660);
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(24, 24, 24, 24);
    column->setSpacing(10);
    QLabel *intro = words(QStringLiteral("Click a shortcut, then press its new key combination. Changes apply when you save."), this);
    intro->setForegroundRole(QPalette::PlaceholderText);
    column->addWidget(intro);
    m_search->setObjectName(QStringLiteral("shortcutSearch"));
    m_search->setPlaceholderText(QStringLiteral("Search shortcuts"));
    column->addWidget(m_search);
    auto *scroll = new QScrollArea(this);
    scroll->setFixedHeight(465);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *list = new QWidget(scroll);
    auto *rows = new QVBoxLayout(list);
    rows->setContentsMargins(0, 0, 8, 0);
    rows->setSpacing(6);
    for (const QString &group : {QStringLiteral("Menus"), QStringLiteral("Canvas & Layers"), QStringLiteral("Text Editing")}) {
        rows->addSpacing(8);
        rows->addWidget(headline(group, list));
        for (const ShortcutDefinition &definition : ShortcutDefinition::all()) {
            if (definition.group != group)
                continue;
            const QString id = definition.id();
            auto *row = new QWidget(list);
            auto *line = new QHBoxLayout(row);
            line->setContentsMargins(0, 0, 0, 0);
            line->addWidget(new QLabel(definition.title, row));
            line->addStretch(1);
            auto *recorder = new ShortcutRecorder([this, id] {
                m_recording = id;
                synchronize();
            }, [this, id](const ShortcutChord &chord) {
                m_draft.insert(id, chord);
                m_recording = std::nullopt;
                synchronize();
            }, row);
            recorder->setObjectName(QStringLiteral("recorder:") + id);
            line->addWidget(recorder);
            rows->addWidget(row);
            m_rows.push_back({row, recorder, &definition});
        }
    }
    rows->addWidget(divider(list));
    rows->addWidget(headline(QStringLiteral("Contextual keys & mouse gestures"), list));
    rows->addWidget(words(QStringLiteral("Text fields keep their standard editing keys. Dialogs share the Apply and Cancel assignments above. "
                                         "Numeric fields use Up and Down, with Shift for larger steps. The shortcut editor itself always "
                                         "uses Return to save and Esc to cancel when not recording."), list));
    rows->addWidget(words(QStringLiteral("Alt temporarily selects the eyedropper in painting tools. Shift constrains shapes and movement or adds to "
                                         "a selection; Alt subtracts from selections or draws from the centre. Ctrl-drag moves selected pixels; "
                                         "Ctrl-Alt-drag copies them. Alt-drag duplicates layers, folders and effects; Alt-click at a layer boundary "
                                         "toggles clipping. Ctrl-click a thumbnail loads its selection. Ctrl bypasses snapping. Right-drag adjusts "
                                         "brush size. Modifier-and-mouse gestures are fixed."), list));
    rows->addStretch(1);
    scroll->setWidget(list);
    column->addWidget(scroll);
    m_problem->setObjectName(QStringLiteral("shortcutProblem"));
    m_problem->setForegroundRole(QPalette::BrightText);
    QFont callout = m_problem->font();
    callout.setPixelSize(12);
    m_problem->setFont(callout);
    m_problem->setWordWrap(true);
    column->addWidget(m_problem);
    column->addWidget(divider(this));
    auto *buttons = new QHBoxLayout;
    auto *restore = new QPushButton(QStringLiteral("Restore Defaults"), this);
    restore->setObjectName(QStringLiteral("restoreShortcuts"));
    restore->setAutoDefault(false);
    auto *cancel = new QPushButton(QStringLiteral("Cancel"), this);
    cancel->setObjectName(QStringLiteral("cancelShortcuts"));
    cancel->setAutoDefault(false);
    m_save->setObjectName(QStringLiteral("saveShortcuts"));
    m_save->setDefault(true);
    buttons->addWidget(restore);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(m_save);
    column->addLayout(buttons);
    connect(m_search, &QLineEdit::textChanged, this, &KeyboardShortcutsSheet::synchronize);
    connect(restore, &QPushButton::clicked, this, [this] {
        m_recording = std::nullopt;
        m_draft.clear();
        synchronize();
    });
    connect(cancel, &QPushButton::clicked, this, [this] { m_close(); });
    connect(m_save, &QPushButton::clicked, this, [this] {
        if (ShortcutSettings::shared().save(m_draft))
            m_close();
    });
    synchronize();
}

void KeyboardShortcutsSheet::synchronize()
{
    const QString search = m_search->text();
    for (const Row &each : m_rows) {
        const QString id = each.definition->id();
        each.row->setVisible(search.isEmpty() || each.definition->title.contains(search, Qt::CaseInsensitive));
        each.recorder->display(m_draft.value(id, each.definition->original), m_recording == id);
    }
    // Only a conflict takes room above the buttons.
    const std::optional<QString> problem = ShortcutSettings::problem(m_draft);
    m_problem->setText(problem.value_or(QString()));
    m_problem->setVisible(problem.has_value());
    m_save->setEnabled(!m_recording && !problem);
}
