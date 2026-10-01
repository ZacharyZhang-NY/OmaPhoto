#include "UI/TransformInspector.h"
#include "UI/HeldModifiers.h"
#include "UI/KeyboardShortcuts.h"
#include "UI/LayerIcons.h"
#include "UI/NumericScrub.h"
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <cmath>

TransformValueField::TransformValueField(const QString &label, const QString &suffix, double low, double high, EditorSession &session,
                                         std::function<void()> finish, std::function<void(double)> change, QWidget *parent)
    : QWidget(parent), field(new QLineEdit(this)), m_session(session), m_finish(std::move(finish)), m_change(std::move(change))
{
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);
    auto *name = new QLabel(label, this);
    name->setFont(ToolHeaderStyle::controlFont());
    name->setForegroundRole(QPalette::PlaceholderText);
    // A scrub writes over typing; an unfocused field then follows.
    m_scrub = new NumericScrub(name, {.sensitivity = 1, .low = low, .high = high, .step = 1, .value = [this] { return m_value; }, .set = [this](double value) {
                                field->setText(formatted(value));
                                m_change(value);
                            }, .onEnd = m_finish});
    row->addWidget(name);
    field->setObjectName(QStringLiteral("transform") + label);
    field->setAccessibleName(label);
    field->setFont(ToolHeaderStyle::controlFont());
    field->installEventFilter(this);
    row->addWidget(field, 1);
    if (!suffix.isEmpty()) {
        auto *unit = new QLabel(suffix, this);
        unit->setFont(ToolHeaderStyle::controlFont());
        unit->setForegroundRole(QPalette::PlaceholderText);
        row->addWidget(unit);
    }
    // Typing applies at once; previewTransform refuses what is no box.
    connect(field, &QLineEdit::textEdited, this, [this](const QString &text) {
        bool number = false;
        const double typed = text.toDouble(&number);
        if (number)
            m_change(typed);
    });
}

void TransformValueField::sync(double value)
{
    m_value = value;
    if (!field->hasFocus() && !m_borrowed)
        field->setText(formatted(value));
}

void TransformValueField::endScrub()
{
    m_scrub->end();
}

QString TransformValueField::formatted(double value)
{
    return std::abs(value - std::round(value)) < 0.005 ? QString::number(std::lround(value)) : QString::number(value, 'f', 2);
}

bool TransformValueField::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::FocusOut) {
        const Qt::FocusReason reason = static_cast<QFocusEvent *>(event)->reason();
        // Another window in front keeps the field, as AppKit does.
        m_borrowed = reason == Qt::MenuBarFocusReason || reason == Qt::PopupFocusReason || reason == Qt::ActiveWindowFocusReason;
        if (!m_borrowed) {
            m_finish();
            field->setText(formatted(m_value));
        }
    }
    if (event->type() != QEvent::KeyPress)
        return QWidget::eventFilter(watched, event);
    const auto *key = static_cast<QKeyEvent *>(event);
    // Return and Escape hand the canvas the keys.
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter || key->key() == Qt::Key_Escape) {
        // Escape throws the fields' own change away, as Swift's Cancel.
        const std::optional<TransformEdit> &edit = m_session.transformEdit();
        if (key->key() == Qt::Key_Escape && edit && edit->fromFields)
            m_session.cancelTransform();
        field->clearFocus();
        m_session.requestCanvasFocus();
        return true;
    }
    if (key->key() != Qt::Key_Up && key->key() != Qt::Key_Down)
        return QWidget::eventFilter(watched, event);
    // A step is no typing: it writes what it applied.
    const double amount = (key->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1) * (key->key() == Qt::Key_Up ? 1 : -1);
    const double stepped = m_value + amount;
    m_change(stepped);
    field->setText(formatted(stepped));
    return true;
}

TransformInspector::TransformInspector(EditorSession &session, QWidget *parent)
    : ToolHeaderBar(QStringLiteral("Transform"), parent), m_session(session), m_autoSelect(new QCheckBox(QStringLiteral("Auto Select"), this)),
      m_showControls(new QCheckBox(QStringLiteral("Show Controls"), this)), m_fields(new QScrollArea(this)),
      m_x(new TransformValueField(QStringLiteral("X"), QString(), -30000, 30000, session, [this] { finish(); }, [this](double number) { change([number](LayerTransform &value) { value.origin.setX(number); }); })),
      m_y(new TransformValueField(QStringLiteral("Y"), QString(), -30000, 30000, session, [this] { finish(); }, [this](double number) { change([number](LayerTransform &value) { value.origin.setY(number); }); })),
      m_width(new TransformValueField(QStringLiteral("W"), QString(), 1, 30000, session, [this] { finish(); }, [this](double number) { resizeBox(number, true); })),
      m_height(new TransformValueField(QStringLiteral("H"), QString(), 1, 30000, session, [this] { finish(); }, [this](double number) { resizeBox(number, false); })),
      m_lock(new QToolButton(this)),
      m_scale(new TransformValueField(QStringLiteral("Scale"), QStringLiteral("%"), 0.1, 30000, session, [this] { finish(); }, [this](double number) {
          change([&](LayerTransform &value) { value = value.scaled(number, pixelSize()); });
      })),
      m_rotation(new TransformValueField(QStringLiteral("°"), QString(), -360, 360, session, [this] { finish(); }, [this](double number) {
          change([number](LayerTransform &value) { value.rotation = std::fmod(number, 360.0); });
      })),
      m_sampling(new QComboBox(this)), m_cancel(new QPushButton(QStringLiteral("Cancel"), this)), m_apply(new QPushButton(QStringLiteral("Apply"), this))
{
    m_autoSelect->setObjectName(QStringLiteral("transformAutoSelect"));
    m_autoSelect->setToolTip(QStringLiteral("Select layers by clicking the canvas. Hold Ctrl to turn it the other way while you click."));
    // Held Ctrl flips Auto Select, and the box shows it.
    connect(m_autoSelect, &QCheckBox::toggled, this, [this](bool picks) {
        m_session.setTransformAutoSelect(picks != HeldModifiers::shared().flags().testFlag(Qt::ControlModifier));
    });
    m_showControls->setObjectName(QStringLiteral("showTransformControls"));
    m_showControls->setToolTip(QStringLiteral("Show the transform box and handles (Ctrl+H). When hidden, drag anywhere to move the layer."));
    connect(m_showControls, &QCheckBox::toggled, this, [this](bool shows) { m_session.setShowsTransformControls(shows); });
    // The numbers scroll sideways when the window is narrow.
    auto *numbers = new QWidget;
    numbers->setObjectName(QStringLiteral("transformFields"));
    auto *fields = new QHBoxLayout(numbers);
    fields->setContentsMargins(18, 0, 18, 0);
    fields->setSpacing(12);
    for (const auto &[field, width] : {std::pair(m_x, 85), std::pair(m_y, 85), std::pair(m_width, 85), std::pair(m_height, 85)}) {
        field->setFixedWidth(width);
        fields->addWidget(field);
    }
    m_lock->setObjectName(QStringLiteral("locksTransformRatio"));
    m_lock->setCheckable(true);
    m_lock->setAutoRaise(true);
    m_lock->setToolTip(QStringLiteral("Lock aspect ratio. Hold Shift while dragging a handle to turn it the other way."));
    applyLockIcon();
    // Held Shift flips the lock, and the button shows it.
    connect(m_lock, &QToolButton::toggled, this, [this](bool locks) {
        m_session.setLocksTransformRatio(locks != HeldModifiers::shared().flags().testFlag(Qt::ShiftModifier));
    });
    fields->addWidget(m_lock);
    m_scale->setFixedWidth(110);
    m_scale->setToolTip(QStringLiteral("Scale width and height together, about the center"));
    fields->addWidget(m_scale);
    m_rotation->setFixedWidth(75);
    fields->addWidget(m_rotation);
    m_sampling->setObjectName(QStringLiteral("transformSampling"));
    m_sampling->setFixedWidth(170);
    for (const LayerSampling sampling : {LayerSampling::nearest, LayerSampling::smooth, LayerSampling::high})
        m_sampling->addItem(rawValue(sampling), int(sampling));
    connect(m_sampling, &QComboBox::activated, this, [this](int index) {
        change([index](LayerTransform &value) { value.sampling = LayerSampling(index); });
    });
    fields->addWidget(m_sampling);
    auto *flipH = new QPushButton(QStringLiteral("Flip H"), numbers);
    flipH->setObjectName(QStringLiteral("flipHorizontal"));
    connect(flipH, &QPushButton::clicked, this, [this] { change([](LayerTransform &value) { value.flipX = !value.flipX; }); });
    fields->addWidget(flipH);
    auto *flipV = new QPushButton(QStringLiteral("Flip V"), numbers);
    flipV->setObjectName(QStringLiteral("flipVertical"));
    connect(flipV, &QPushButton::clicked, this, [this] { change([](LayerTransform &value) { value.flipY = !value.flipY; }); });
    fields->addWidget(flipV);
    // Swift's scroll content keeps its size at the leading edge.
    fields->addStretch(1);
    m_fields->setObjectName(QStringLiteral("transformScroll"));
    m_fields->setWidget(numbers);
    m_fields->setWidgetResizable(true);
    m_fields->setFrameShape(QFrame::NoFrame);
    m_fields->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_fields->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_fields->setFixedHeight(numbers->sizeHint().height());
    m_cancel->setObjectName(QStringLiteral("cancelTransform"));
    connect(m_cancel, &QPushButton::clicked, this, [this] { m_session.cancelTransform(); });
    m_apply->setObjectName(QStringLiteral("applyTransform"));
    // Swift's configuredNativeShortcut: Return and Escape, as remapped.
    NativeShortcut::bind(*this, m_apply, m_cancel);
    connect(m_apply, &QPushButton::clicked, this, [this] { m_session.commitTransform(); });
    // Before the stretch ending the row, in Swift's order.
    for (QWidget *widget : std::initializer_list<QWidget *>{m_autoSelect, m_showControls, m_fields, m_cancel, m_apply})
        row->insertWidget(row->count() - 1, widget, widget == m_fields ? 1 : 0);
    // The scroll view takes the spare room, as SwiftUI's does.
    row->setStretch(row->count() - 1, 0);
    connect(&m_session, &EditorSession::changed, this, &TransformInspector::synchronize);
    connect(&HeldModifiers::shared(), &HeldModifiers::changed, this, &TransformInspector::synchronize);
    synchronize();
}

void TransformInspector::applyLockIcon()
{
    m_lock->setIcon(LayerIcons::pixmap(LayerIcon::link, 16, palette().color(QPalette::Text), devicePixelRatio()));
}

// The theme's ink reaches the icon, as the panel's icons.
void TransformInspector::changeEvent(QEvent *event)
{
    ToolHeaderBar::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        applyLockIcon();
}

// Swift's default and cancel actions; a field keeps its keys.
void TransformInspector::keyPressEvent(QKeyEvent *event)
{
    const bool enter = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
    if (!m_session.transformEdit() || (!enter && event->key() != Qt::Key_Escape)) {
        ToolHeaderBar::keyPressEvent(event);
        return;
    }
    if (enter)
        m_session.commitTransform();
    else
        m_session.cancelTransform();
}

// The draft, else the active layer's box, else one unit.
LayerTransform TransformInspector::value() const
{
    if (m_session.transformEdit())
        return m_session.transformEdit()->draft;
    const std::optional<ImageLayer> active = m_session.activeLayer();
    return active ? m_session.editedTransform(*active) : LayerTransform{.origin = {0, 0}, .size = {1, 1}};
}

// 100%: the pixels, or a blank layer's size before editing.
QSizeF TransformInspector::pixelSize() const
{
    if (const std::optional<QSizeF> pixels = m_session.transformPixelSize())
        return *pixels;
    const std::optional<ImageLayer> active = m_session.activeLayer();
    return active ? active->size() : value().size;
}

// Shown at once, applied without Apply; Ctrl+T's edit takes it.
void TransformInspector::change(const std::function<void(LayerTransform &)> &update)
{
    if (!m_session.transformEdit())
        m_session.beginTransform(false, true);
    if (!m_session.transformEdit())
        return;
    LayerTransform next = m_session.transformEdit()->draft;
    update(next);
    m_session.previewTransform(next);
}

void TransformInspector::finish()
{
    if (m_session.transformEdit() && m_session.transformEdit()->fromFields)
        m_session.commitTransform();
}

void TransformInspector::resizeBox(double number, bool width)
{
    // A side under a pixel makes a box previewTransform refuses.
    change([&](LayerTransform &next) {
        if (width) {
            if (m_session.locksTransformRatio())
                next.size.setHeight(next.size.height() * number / next.size.width());
            next.size.setWidth(number);
        } else {
            if (m_session.locksTransformRatio())
                next.size.setWidth(next.size.width() * number / next.size.height());
            next.size.setHeight(number);
        }
    });
}

void TransformInspector::synchronize()
{
    // Another active layer makes the fields anew (Swift's `.id`).
    if (m_layerID != m_session.activeLayerID()) {
        m_layerID = m_session.activeLayerID();
        if (QWidget *focused = focusWidget(); focused && isAncestorOf(focused))
            focused->clearFocus();
        for (TransformValueField *field : {m_x, m_y, m_width, m_height, m_scale, m_rotation})
            field->endScrub();
    }
    title->setText(m_session.transformTargetsMask() ? QStringLiteral("Transform Mask") : QStringLiteral("Transform"));
    const auto set = [](QAbstractButton *button, bool checked) {
        const QSignalBlocker blocker(button);
        button->setChecked(checked);
    };
    const Qt::KeyboardModifiers held = HeldModifiers::shared().flags();
    set(m_autoSelect, m_session.transformAutoSelect() != held.testFlag(Qt::ControlModifier));
    set(m_showControls, m_session.showsTransformControls());
    set(m_lock, m_session.locksTransformRatio() != held.testFlag(Qt::ShiftModifier));
    const LayerTransform shown = value();
    m_x->sync(shown.origin.x());
    m_y->sync(shown.origin.y());
    m_width->sync(shown.size.width());
    m_height->sync(shown.size.height());
    m_scale->sync(shown.scalePercent(pixelSize()));
    m_rotation->sync(shown.rotation);
    {
        const QSignalBlocker blocker(m_sampling);
        m_sampling->setCurrentIndex(int(shown.sampling));
    }
    const bool editing = m_session.transformEdit().has_value();
    // While distorted, the handles are the controls.
    const bool distorted = editing && m_session.transformEdit()->corners.has_value();
    m_fields->widget()->setEnabled((m_session.canTransform() || editing) && !distorted);
    m_cancel->setEnabled(editing);
    m_apply->setEnabled(editing);
}
