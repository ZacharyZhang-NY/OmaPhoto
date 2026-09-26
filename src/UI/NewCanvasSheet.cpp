#include "UI/NewCanvasSheet.h"
#include "UI/KeyboardShortcuts.h"
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QGridLayout>
#include <QImageReader>
#include <QShortcut>
#include <QVBoxLayout>

namespace {
QLabel *text(const QString &words, int pixels, QFont::Weight weight, QPalette::ColorRole role, QWidget *parent)
{
    auto *label = new QLabel(words, parent);
    QFont font = label->font();
    font.setPixelSize(pixels);
    font.setWeight(weight);
    label->setFont(font);
    label->setForegroundRole(role);
    return label;
}
}

NewCanvasSheet::NewCanvasSheet(EditorSession &session, std::function<void(int, int)> onCreate, std::function<void()> onOpen, QWidget *parent)
    : QWidget(parent), m_session(session), m_onCreate(std::move(onCreate)), m_width(new QLineEdit(QStringLiteral("1920"), this)),
      m_height(new QLineEdit(QStringLiteral("1080"), this)), m_note(text(QString(), 12, QFont::Normal, QPalette::PlaceholderText, this)),
      m_create(new QPushButton(QStringLiteral("Create canvas"), this))
{
    // Swift's fields stretch the sheet to its full 500 points.
    setFixedWidth(500);
    m_width->setObjectName(QStringLiteral("widthInput"));
    m_height->setObjectName(QStringLiteral("heightInput"));
    m_note->setObjectName(QStringLiteral("canvasNote"));
    m_create->setObjectName(QStringLiteral("createCanvas"));
    m_create->setDefault(true);
    // Swift's configuredNativeShortcut: Return and Escape, as remapped.
    NativeShortcut::bind(*this, m_create, nullptr);
    auto *open = new QPushButton(QStringLiteral("Open project"), this);
    open->setObjectName(QStringLiteral("openProject"));
    auto *import = new QPushButton(QStringLiteral("Import image"), this);
    import->setObjectName(QStringLiteral("importImage"));

    auto *fields = new QGridLayout;
    fields->setHorizontalSpacing(16);
    fields->setVerticalSpacing(8);
    fields->addWidget(text(QStringLiteral("Width"), 12, QFont::Medium, QPalette::WindowText, this), 0, 0);
    fields->addWidget(text(QStringLiteral("Height"), 12, QFont::Medium, QPalette::WindowText, this), 0, 3);
    fields->addWidget(m_width, 1, 0);
    fields->addWidget(text(QStringLiteral("px"), 12, QFont::Normal, QPalette::PlaceholderText, this), 1, 1);
    fields->addWidget(text(QStringLiteral("×"), 14, QFont::Normal, QPalette::PlaceholderText, this), 1, 2);
    fields->addWidget(m_height, 1, 3);
    fields->addWidget(text(QStringLiteral("px"), 12, QFont::Normal, QPalette::PlaceholderText, this), 1, 4);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    buttons->addWidget(open);
    buttons->addWidget(import);
    buttons->addStretch(1);
    buttons->addWidget(m_create);

    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(28, 28, 28, 28);
    column->setSpacing(24);
    auto *heading = new QVBoxLayout;
    heading->setSpacing(6);
    heading->addWidget(text(QStringLiteral("New canvas"), 17, QFont::DemiBold, QPalette::WindowText, this));
    heading->addWidget(text(QStringLiteral("A blank space for your next composition."), 13, QFont::Normal, QPalette::PlaceholderText, this));
    column->addLayout(heading);
    column->addLayout(fields);
    column->addWidget(m_note);
    column->addLayout(buttons);

    connect(m_width, &QLineEdit::textChanged, this, &NewCanvasSheet::validate);
    connect(m_height, &QLineEdit::textChanged, this, &NewCanvasSheet::validate);
    // Return anywhere on the sheet is the default action.
    std::vector<QShortcut *> returns;
    for (const Qt::Key key : {Qt::Key_Return, Qt::Key_Enter})
        returns.push_back(new QShortcut(QKeySequence(key), this, this, &NewCanvasSheet::create, Qt::WidgetWithChildrenShortcut));
    // Until Apply is moved elsewhere, as Swift's native shortcut.
    const auto follow = [returns] {
        for (QShortcut *shortcut : returns)
            shortcut->setEnabled(ShortcutSettings::shared().native(ShortcutChord(QStringLiteral("\r"))) == ShortcutChord(QStringLiteral("\r")));
    };
    connect(&ShortcutSettings::shared(), &ShortcutSettings::changed, this, follow);
    follow();
    connect(m_create, &QPushButton::clicked, this, &NewCanvasSheet::create);
    connect(open, &QPushButton::clicked, this, [onOpen = std::move(onOpen)] { onOpen(); });
    connect(import, &QPushButton::clicked, this, [this] { m_session.setShowsImporter(true); });
    connect(&m_session, &EditorSession::changed, this, &NewCanvasSheet::synchronize);
    synchronize();
    validate();
}

void NewCanvasSheet::synchronize()
{
    setEnabled(!m_session.isImporting() && !m_session.showsBusy());
}

void NewCanvasSheet::validate()
{
    const bool valid = CanvasDocument::validDimension(m_width->text()) && CanvasDocument::validDimension(m_height->text());
    m_note->setText(valid ? QStringLiteral("Transparent canvas · sRGB") : QStringLiteral("Enter whole numbers from 1 to 30,000 pixels."));
    // The palette's bright text is the theme's warning colour.
    m_note->setForegroundRole(valid ? QPalette::PlaceholderText : QPalette::BrightText);
    m_create->setEnabled(valid);
}

void NewCanvasSheet::create()
{
    const std::optional<int> width = CanvasDocument::validDimension(m_width->text()), height = CanvasDocument::validDimension(m_height->text());
    if (width && height)
        m_onCreate(*width, *height);
}

void NewCanvasSheet::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!m_suggestedClipboardSize) {
        m_suggestedClipboardSize = true;
        if (m_session.skipsInitialClipboardCanvasSize) {
            m_session.skipsInitialClipboardCanvasSize = false;
        } else if (const std::optional<QSize> size = clipboardDimensions(QApplication::clipboard()->mimeData())) {
            m_width->setText(QString::number(size->width()));
            m_height->setText(QString::number(size->height()));
        }
    }
    m_width->setFocus();
}

std::optional<QSize> NewCanvasSheet::clipboardDimensions(const QMimeData *clipboard)
{
    for (const QString &type : {QStringLiteral("image/png"), QStringLiteral("image/tiff")}) {
        QByteArray data = clipboard ? clipboard->data(type) : QByteArray();
        QBuffer buffer(&data);
        QImageReader reader(&buffer);
        QSize size = reader.size();
        // Stored on its side, a picture swaps its sides.
        if (reader.transformation() & QImageIOHandler::TransformationRotate90)
            size.transpose();
        // In range on one side alone is no canvas.
        if (CanvasDocument::validDimension(QString::number(size.width())) && CanvasDocument::validDimension(QString::number(size.height())))
            return size;
    }
    return std::nullopt;
}
