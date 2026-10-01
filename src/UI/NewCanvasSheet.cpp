#include "UI/NewCanvasSheet.h"
#include "Document/DocumentLimits.h"
#include "UI/KeyboardShortcuts.h"
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QGridLayout>
#include <QActionGroup>
#include <QImageReader>
#include <QMenu>
#include <QPainter>
#include <QShortcut>
#include <QVBoxLayout>
#include <algorithm>

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

// Swift's More button: three dots drawn exactly, flush right.
class MoreButton : public QToolButton {
public:
    using QToolButton::QToolButton;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette().color(QPalette::WindowText));
        const double dot = 2.5, top = (height() - 5 * dot) / 2;
        for (int index = 0; index < 3; ++index)
            painter.drawEllipse(QRectF(width() - dot, top + 2 * dot * index, dot, dot));
    }
};
}

const std::vector<std::vector<CanvasPreset>> CanvasPreset::groups = {
    {
        {QStringLiteral("4K"), 3840, 2160},
        {QStringLiteral("1440p"), 2560, 1440},
        {QStringLiteral("1080p"), 1920, 1080},
    },
    {
        {QStringLiteral("iPhone 18 Pro"), 1206, 2622},
        {QStringLiteral("iPhone 18 Pro Max"), 1320, 2868},
        {QStringLiteral("MacBook Pro 14\""), 3024, 1964},
        {QStringLiteral("MacBook Pro 16\""), 3456, 2234},
        {QStringLiteral("Studio Display"), 5120, 2880},
    },
    {
        {QStringLiteral("Instagram Square"), 1080, 1080},
        {QStringLiteral("Instagram Portrait"), 1080, 1350},
        {QStringLiteral("Instagram Story"), 1080, 1920},
        {QStringLiteral("YouTube Thumb"), 1080, 608},
    },
};

const std::vector<CanvasPreset> CanvasPreset::all = [] {
    std::vector<CanvasPreset> presets;
    for (const std::vector<CanvasPreset> &group : groups)
        presets.insert(presets.end(), group.begin(), group.end());
    return presets;
}();

NewCanvasSheet::NewCanvasSheet(EditorSession &session, std::function<void(int, int)> onCreate, std::function<void()> onOpen, QWidget *parent)
    : QWidget(parent), m_session(session), m_onCreate(std::move(onCreate)), m_width(new QLineEdit(QStringLiteral("1920"), this)),
      m_height(new QLineEdit(QStringLiteral("1080"), this)), m_note(text(QString(), 12, QFont::Normal, QPalette::PlaceholderText, this)),
      m_create(new QPushButton(QStringLiteral("Create canvas"), this)), m_presets(new MoreButton(this))
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
    auto *heading = new QHBoxLayout;
    heading->addWidget(text(QStringLiteral("New canvas"), 17, QFont::DemiBold, QPalette::WindowText, this));
    heading->addStretch(1);
    heading->addWidget(m_presets);
    column->addLayout(heading);
    column->addLayout(fields);
    column->addWidget(m_note);
    column->addLayout(buttons);

    m_presets->setObjectName(QStringLiteral("presetSizes"));
    m_presets->setFixedSize(28, 28);
    m_presets->setPopupMode(QToolButton::InstantPopup);
    m_presets->setToolTip(QStringLiteral("Preset sizes for screens and common formats"));
    m_presets->setAccessibleName(QStringLiteral("Preset sizes"));
    auto *menu = new QMenu(m_presets);
    auto *choices = new QActionGroup(menu);
    // Custom changes nothing; the match stays checked.
    QAction *custom = choices->addAction(menu->addAction(QStringLiteral("Custom")));
    custom->setCheckable(true);
    custom->setData(-1);
    connect(custom, &QAction::triggered, this, &NewCanvasSheet::checkPreset);
    int index = 0;
    for (const std::vector<CanvasPreset> &group : CanvasPreset::groups) {
        menu->addSeparator();
        for (const CanvasPreset &preset : group) {
            QAction *action = choices->addAction(menu->addAction(preset.title));
            action->setCheckable(true);
            action->setData(index++);
            connect(action, &QAction::triggered, this, [this, preset] {
                m_width->setText(QString::number(preset.width));
                m_height->setText(QString::number(preset.height));
            });
        }
    }
    m_presets->setMenu(menu);

    connect(m_width, &QLineEdit::textChanged, this, &NewCanvasSheet::validate);
    connect(m_height, &QLineEdit::textChanged, this, &NewCanvasSheet::validate);
    connect(m_width, &QLineEdit::textChanged, this, &NewCanvasSheet::checkPreset);
    connect(m_height, &QLineEdit::textChanged, this, &NewCanvasSheet::checkPreset);
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
    checkPreset();
}

void NewCanvasSheet::checkPreset()
{
    const auto matched = std::ranges::find_if(CanvasPreset::all, [this](const CanvasPreset &preset) {
        return QString::number(preset.width) == m_width->text() && QString::number(preset.height) == m_height->text();
    });
    const QVariant index(matched == CanvasPreset::all.end() ? -1 : int(matched - CanvasPreset::all.begin()));
    for (QAction *action : m_presets->menu()->actions())
        if (action->data() == index)
            action->setChecked(true);
}

void NewCanvasSheet::synchronize()
{
    setEnabled(!m_session.isImporting() && !m_session.showsBusy());
}

void NewCanvasSheet::validate()
{
    const bool valid = CanvasDocument::validDimension(m_width->text()) && CanvasDocument::validDimension(m_height->text());
    m_note->setText(valid ? QStringLiteral("Transparent canvas · sRGB") : QStringLiteral("Enter whole numbers from 1 to %1 pixels.").arg(DocumentLimits::maxSideText()));
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
