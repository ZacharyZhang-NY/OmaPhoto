#include "UI/CanvasThumbnail.h"
#include "UI/LayerIcons.h"
#include "UI/NativeLayerList.h"
#include <QApplication>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <cmath>

LayerCell::LayerCell(NativeLayerList &list)
    : QWidget(nullptr), m_list(list), m_eye(new EyeSwipeButton(list, this)), m_disclosure(new QToolButton(this)),
      m_thumbnail(new LayerThumbnailButton(list, false, this)), m_maskThumbnail(new LayerThumbnailButton(list, true, this)),
      m_link(new QToolButton(this)), m_disabledMaskMark(new QLabel(QStringLiteral("╱"), this)), m_name(new QLabel(this)),
      m_dimensions(new QLabel(this)), m_editor(new QLineEdit(this)), m_fade(new QGraphicsOpacityEffect(this))
{
    setObjectName(QStringLiteral("layerCell"));
    setFixedHeight(rowHeight);
    setGraphicsEffect(m_fade);
    // Hover reaches the list's cursor through every control.
    setMouseTracking(true);
    for (QWidget *child : findChildren<QWidget *>())
        child->setMouseTracking(true);
    m_eye->setObjectName(QStringLiteral("layerEye"));
    m_disclosure->setObjectName(QStringLiteral("layerDisclosure"));
    m_disclosure->setAutoRaise(true);
    m_disclosure->setFixedSize(16, 24);
    m_disclosure->setAccessibleName(QStringLiteral("Expand or collapse folder"));
    connect(m_disclosure, &QToolButton::clicked, this, [this] { m_list.session().toggleGroupExpansion(m_layerID); });
    m_thumbnail->setObjectName(QStringLiteral("layerThumbnail"));
    m_maskThumbnail->setObjectName(QStringLiteral("maskThumbnail"));
    m_maskThumbnail->setToolTip(QStringLiteral("Select layer mask; Shift-click to enable/disable; Ctrl-click to select its black areas (Ctrl-Shift adds, Ctrl-Alt subtracts)"));
    m_link->setObjectName(QStringLiteral("maskLink"));
    m_link->setAutoRaise(true);
    m_link->setFixedSize(9, 20);
    connect(m_link, &QToolButton::clicked, this, [this] { m_list.session().toggleMaskLink(m_layerID); });
    QFont mark = m_disabledMaskMark->font();
    mark.setPixelSize(32);
    mark.setWeight(QFont::Medium);
    m_disabledMaskMark->setFont(mark);
    m_disabledMaskMark->setForegroundRole(QPalette::BrightText);
    m_disabledMaskMark->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_disabledMaskMark->setObjectName(QStringLiteral("maskDisabledMark"));
    QFont name = m_name->font();
    name.setPixelSize(13);
    m_name->setFont(name);
    m_name->setTextFormat(Qt::PlainText);
    m_name->setObjectName(QStringLiteral("layerName"));
    QFont small = m_dimensions->font();
    small.setPixelSize(10);
    m_dimensions->setFont(small);
    m_dimensions->setForegroundRole(QPalette::PlaceholderText);
    m_dimensions->setTextFormat(Qt::PlainText);
    m_dimensions->setObjectName(QStringLiteral("layerDimensions"));
    m_editor->setObjectName(QStringLiteral("layerNameEditor"));
    m_editor->setFont(name);
    m_editor->hide();
    m_editor->installEventFilter(this);
    menuAction(QStringLiteral("renameLayer"), QStringLiteral("Rename…"), [this] {
        if (!m_list.session().canEditLayers())
            return;
        m_list.session().setActiveLayerID(m_layerID);
        m_list.session().setRenamingLayerID(m_layerID);
    });
    menuAction(QStringLiteral("toggleVisibility"), QStringLiteral("Hide/Show Layer"), [this] { m_list.session().toggleLayerVisibility(m_layerID); });
    // With a selection, white hides it, black shows it alone.
    menuAction(QStringLiteral("addWhiteMask"), QStringLiteral("Add White Mask"), [this] {
        m_list.session().selectLayerTarget(m_layerID, false);
        m_list.session().addMask();
    });
    menuAction(QStringLiteral("addBlackMask"), QStringLiteral("Add Black Mask"), [this] {
        m_list.session().selectLayerTarget(m_layerID, false);
        m_list.session().addMask(false);
    });
    menuAction(QStringLiteral("toggleMask"), QStringLiteral("Enable/Disable Mask"), [this] {
        m_list.session().selectLayerTarget(m_layerID, false);
        m_list.session().toggleLayerMask();
    });
    menuAction(QStringLiteral("deleteMask"), QStringLiteral("Delete Mask"), [this] {
        m_list.session().selectLayerTarget(m_layerID, false);
        m_list.session().deleteLayerMask();
    });
    menuAction(QStringLiteral("releaseClippingMask"), QStringLiteral("Release Clipping Mask"), [this] { m_list.session().removeLiveMask(m_layerID); });
    menuAction(QStringLiteral("moveOut"), QStringLiteral("Move Out of Folder"), [this] {
        m_list.session().selectLayer(m_layerID);
        m_list.session().moveActiveLayerOutOfGroup();
    });
    menuAction(QStringLiteral("deleteLayer"), QStringLiteral("Delete Layer / Folder"), [this] {
        // Part of a multi-selection, the whole selection goes.
        EditorSession &session = m_list.session();
        if (session.selectedLayerIDs().size() > 1 && session.selectedLayerIDs().contains(m_layerID))
            session.deleteSelectedLayers();
        else
            session.deleteLayer(m_layerID);
    });
}

QAction *LayerCell::menuAction(const QString &name, const QString &title, const std::function<void()> &run)
{
    auto *action = new QAction(title, this);
    action->setObjectName(name);
    connect(action, &QAction::triggered, this, run);
    addAction(action);
    return action;
}

void LayerCell::configure(const ImageLayer &layer, bool enabled, int depth, bool visible)
{
    EditorSession &session = m_list.session();
    const QColor ink = palette().color(QPalette::WindowText), faint = palette().color(QPalette::PlaceholderText);
    const double ratio = devicePixelRatio();
    // A folder's step and a clipping mask's step add up.
    m_indent = std::min(depth, 8) * 24 + (layer.maskSourceID ? 24 : 0);
    m_isGroup = layer.isGroup;
    m_disclosure->setVisible(layer.isGroup);
    m_disclosure->setEnabled(enabled);
    m_disclosure->setIcon(LayerIcons::pixmap(session.collapsedGroupIDs().contains(layer.id) ? LayerIcon::chevronRight : LayerIcon::chevronDown, 12, ink, ratio));
    // Pixels show the canvas; text, adjustments, folders an icon.
    const QSizeF canvas = session.document() ? session.document()->size() : QSizeF(1, 1);
    m_editableText = layer.liveText().has_value();
    m_isAdjustment = layer.adjustment.has_value();
    m_editableAdjustment = m_isAdjustment && isEditable(layer.adjustment->kind);
    m_thumbnailSize = layer.isGroup || m_editableText || m_isAdjustment ? QSize(36, 36) : CanvasThumbnail::fittedSize(canvas, 36);
    const ThumbnailKey key{layer.asset ? std::optional(layer.asset->identity()) : std::nullopt, layer.transform, canvas, ink, m_editableText};
    if (m_layerID != layer.id || m_thumbnailKey != key) {
        m_thumbnail->setIcon(m_isAdjustment    ? LayerIcons::adjustmentThumbnail(layer.adjustment->kind, ink, ratio)
                             : layer.isGroup   ? LayerIcons::pixmap(LayerIcon::folder, 36 * 0.8, ink, ratio)
                             : m_editableText ? LayerIcons::pixmap(LayerIcon::text, 36, ink, ratio)
                                               : CanvasThumbnail::layer(layer.asset ? layer.asset->thumbnail : QImage(), layer.transform, canvas, 36));
        m_thumbnailKey = key;
    }
    m_maskSize = CanvasThumbnail::fittedSize(canvas, 30);
    const ThumbnailKey maskKey{layer.mask ? std::optional(layer.mask->asset.identity()) : std::nullopt, layer.maskTransform(), canvas, ink};
    if (m_layerID != layer.id || m_maskThumbnailKey != maskKey) {
        m_maskThumbnail->setIcon(layer.mask ? CanvasThumbnail::mask(layer.mask->asset.thumbnail, layer.maskTransform(), canvas, 30) : QPixmap());
        m_maskThumbnailKey = maskKey;
    }
    m_layerID = layer.id;
    m_thumbnail->layerID = layer.id;
    m_maskThumbnail->layerID = layer.id;
    m_hasMask = layer.mask.has_value();
    m_maskThumbnail->setVisible(m_hasMask);
    m_disabledMaskMark->setVisible(layer.mask && !layer.mask->isEnabled);
    m_thumbnail->setEnabled(!session.showsBusy() && !session.isImporting());
    m_maskThumbnail->setEnabled(m_thumbnail->isEnabled());
    m_linkable = layer.mask && !m_isAdjustment && !layer.isGroup;
    m_link->setVisible(m_linkable);
    const bool linked = !layer.mask || layer.mask->isLinked;
    m_link->setIcon(linked ? LayerIcons::pixmap(LayerIcon::link, 9, faint, ratio) : QPixmap());
    m_link->setEnabled(m_thumbnail->isEnabled());
    m_link->setToolTip(linked ? QStringLiteral("Unlink layer and mask to move or transform them separately") : QStringLiteral("Link layer and mask so they move together"));
    m_link->setAccessibleName((linked ? QStringLiteral("Unlink mask: ") : QStringLiteral("Link mask: ")) + layer.name);
    m_thumbnail->setToolTip(m_editableText ? QStringLiteral("Editable text layer") : QStringLiteral("Select image pixels"));
    m_thumbnail->setAccessibleName((m_editableText ? QStringLiteral("Select text: ") : QStringLiteral("Select image: ")) + layer.name);
    m_maskThumbnail->setAccessibleName(QStringLiteral("Select mask: ") + layer.name);
    m_layerName = layer.name;
    // A pending rename closes the edits: rows never move.
    if (!m_renaming)
        m_name->setText((layer.maskSourceID ? QStringLiteral("↳ ") : QString()) + layer.name);
    m_dimensions->setText(m_editableText   ? QStringLiteral("Text")
                          : m_isAdjustment ? QStringLiteral("Adjustment · Double-click to edit")
                          : layer.isGroup  ? QStringLiteral("Folder")
                                          : QStringLiteral("%1 × %2 px").arg(std::lround(layer.size().width())).arg(std::lround(layer.size().height())));
    m_dimensions->setToolTip(QString());
    if (layer.maskSourceID && session.document()) {
        const int source = indexOf(session.document()->layers, layer.maskSourceID);
        const QString sourceName = source >= 0 ? session.document()->layers[source].name : QStringLiteral("Missing source");
        m_dimensions->setText(QStringLiteral("Clipped to %1").arg(sourceName));
        m_dimensions->setToolTip(QStringLiteral("Clipping mask based on %1. Option-click the bottom of its row to release.").arg(sourceName));
    }
    m_eye->setIcon(LayerIcons::pixmap(layer.isVisible ? LayerIcon::eye : LayerIcon::eyeSlash, 16, ink, ratio));
    m_eye->setAccessibleName((layer.isVisible ? QStringLiteral("Hide ") : QStringLiteral("Show ")) + layer.name);
    m_eye->setEnabled(enabled);
    m_eye->layerID = layer.id;
    m_fade->setOpacity(visible ? 1 : 0.35);
    // Effects under the row, made anew when their kinds change.
    const std::vector<LayerEffectKind> kinds = layer.effects ? layer.effects->kinds() : std::vector<LayerEffectKind>();
    std::vector<LayerEffectKind> shown;
    for (const LayerEffectRow *row : m_effectButtons)
        shown.push_back(row->kind);
    if (shown != kinds) {
        // A row's own press may take it away: deleted later.
        for (LayerEffectRow *row : m_effectButtons) {
            row->hide();
            row->deleteLater();
        }
        m_effectButtons.clear();
        for (const LayerEffectKind kind : kinds) {
            m_effectButtons.push_back(new LayerEffectRow(m_list, layer.id, kind, this));
            // Made after the cell shows, a child needs showing.
            m_effectButtons.back()->show();
        }
    }
    setFixedHeight(rowHeight + int(kinds.size()) * effectHeight);
    for (LayerEffectRow *row : m_effectButtons)
        row->configure(layer.effects->isEnabled(row->kind), enabled, m_indent);
    relayout();
}

void LayerCell::updateTarget()
{
    EditorSession &session = m_list.session();
    const bool active = session.activeLayerID() == m_layerID && session.selectedLayerIDs().size() == 1;
    const bool mask = session.isMaskSelected();
    m_thumbnail->setTargeted(active && !mask);
    m_maskThumbnail->setTargeted(active && mask);
    QWidget::update();
}

void LayerCell::relayout()
{
    m_eye->move(8, 26 - 16);
    m_disclosure->move(28 + m_indent, 26 - 12);
    const int thumbnailSlot = 28 + m_indent + 16 - 2;
    m_thumbnail->setFixedSize(m_thumbnailSize);
    m_thumbnail->move(thumbnailSlot + (36 - m_thumbnailSize.width()) / 2, 26 - m_thumbnailSize.height() / 2);
    const int maskSlot = thumbnailSlot + 36 + (m_linkable ? 13 : 5);
    m_link->move(maskSlot - 6 - 5, 26 - 10);
    const int maskWidth = m_hasMask ? 30 : 0;
    m_maskThumbnail->setFixedSize(m_maskSize);
    m_maskThumbnail->move(maskSlot + (maskWidth - m_maskSize.width()) / 2, 26 - m_maskSize.height() / 2);
    m_disabledMaskMark->adjustSize();
    m_disabledMaskMark->move(m_maskThumbnail->geometry().center() - QPoint(m_disabledMaskMark->width() / 2, m_disabledMaskMark->height() / 2));
    const int nameLeft = maskSlot + maskWidth + 5, nameWidth = std::max(10, width() - nameLeft - 8);
    m_name->setGeometry(nameLeft, 9, nameWidth, 17);
    m_editor->setGeometry(nameLeft - 2, 6, nameWidth + 2, 23);
    m_dimensions->setGeometry(nameLeft, 29, nameWidth, 14);
    for (size_t each = 0; each < m_effectButtons.size(); ++each)
        m_effectButtons[each]->setGeometry(0, rowHeight + int(each) * effectHeight, width(), effectHeight);
}

void LayerCell::resizeEvent(QResizeEvent *)
{
    relayout();
}

void LayerCell::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    // A chosen effect hides the rows' selection, as Swift's table.
    if (m_list.session().selectedLayerIDs().contains(m_layerID) && !m_list.session().selectedEffect()) {
        QColor fill = palette().color(QPalette::Highlight);
        fill.setAlphaF(0.35);
        painter.fillRect(rect(), fill);
    }
    // A faint device pixel where the next row begins.
    QColor edge = Qt::white;
    edge.setAlphaF(0.06);
    painter.fillRect(QRectF(0, height() - 1.0 / devicePixelRatio(), width(), 1.0 / devicePixelRatio()), edge);
}

bool LayerCell::isOnControl(QPoint point) const
{
    for (const QWidget *control : {static_cast<QWidget *>(m_eye), static_cast<QWidget *>(m_disclosure), static_cast<QWidget *>(m_thumbnail),
                                   static_cast<QWidget *>(m_link), static_cast<QWidget *>(m_maskThumbnail)}) {
        if (control->isVisible() && control->geometry().contains(point))
            return true;
    }
    return false;
}

void LayerCell::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    disarmDrag();
    // A press that clipped is done, as in Swift.
    if (!m_list.clickRow(*this, event->modifiers(), event->position().toPoint()))
        armDrag(event->position().toPoint(), event->modifiers());
}

void LayerCell::mouseMoveEvent(QMouseEvent *event)
{
    moveDrag(event->position().toPoint(), event->buttons());
}

void LayerCell::armDrag(QPoint cellPoint, Qt::KeyboardModifiers modifiers)
{
    m_press = cellPoint;
    m_pressModifiers = modifiers;
}

// Left held past the distance drags.
void LayerCell::moveDrag(QPoint cellPoint, Qt::MouseButtons buttons)
{
    if (!m_press || !buttons.testFlag(Qt::LeftButton) || (cellPoint - *m_press).manhattanLength() < QApplication::startDragDistance())
        return;
    m_press = std::nullopt;
    m_list.startDrag(*this, m_pressModifiers);
}

void LayerCell::mouseDoubleClickEvent(QMouseEvent *event)
{
    EditorSession &session = m_list.session();
    if (event->button() != Qt::LeftButton || !session.canEditLayers())
        return;
    // With Alt the second press clips again, as in Swift.
    if (event->modifiers().testFlag(Qt::AltModifier) && !event->modifiers().testFlag(Qt::ControlModifier)) {
        m_list.clickRow(*this, event->modifiers(), event->position().toPoint());
        return;
    }
    session.setActiveLayerID(m_layerID);
    // On a control, text or an adjustment's settings open.
    if (isOnControl(event->position().toPoint())) {
        if (m_editableText) {
            session.editActiveText();
            return;
        }
        if (m_editableAdjustment) {
            session.setAdjustmentEditingID(m_layerID);
            return;
        }
    }
    session.setRenamingLayerID(m_layerID);
}

void LayerCell::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu::exec(actions(), event->globalPos(), nullptr, this);
}

void LayerCell::beginRenaming()
{
    EditorSession &session = m_list.session();
    // Not canEditLayers: a pending rename closes that gate itself.
    if (m_renaming || !session.document() || session.isProjectBusy() || session.isImporting())
        return;
    m_renaming = true;
    m_editor->setText(m_layerName);
    m_editor->show();
    m_editor->setFocus(Qt::OtherFocusReason);
    m_editor->selectAll();
}

void LayerCell::endRenaming(bool keeping)
{
    if (!m_renaming)
        return;
    EditorSession &session = m_list.session();
    const QString typed = m_editor->text();
    m_renaming = false;
    m_editor->hide();
    // Cleared first: the rename's signal would reopen the editor.
    if (session.renamingLayerID() == m_layerID)
        session.setRenamingLayerID(std::nullopt);
    if (keeping)
        session.renameLayer(m_layerID, typed);
    // The name the layer ended up with, marked as shown.
    if (session.document()) {
        const int index = indexOf(session.document()->layers, m_layerID);
        if (index >= 0) {
            const ImageLayer &layer = session.document()->layers[index];
            m_layerName = layer.name;
            m_name->setText((layer.maskSourceID ? QStringLiteral("↳ ") : QString()) + layer.name);
        }
    }
    m_list.focusList();
}

bool LayerCell::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_editor || !m_renaming)
        return QWidget::eventFilter(watched, event);
    if (event->type() == QEvent::FocusOut) {
        // A menu borrows focus and gives it back: no leaving.
        const Qt::FocusReason reason = static_cast<QFocusEvent *>(event)->reason();
        if (reason != Qt::MenuBarFocusReason && reason != Qt::PopupFocusReason)
            endRenaming(true);
        return false;
    }
    if (event->type() != QEvent::KeyPress)
        return false;
    const int key = static_cast<QKeyEvent *>(event)->key();
    if (key == Qt::Key_Return || key == Qt::Key_Enter)
        endRenaming(true);
    else if (key == Qt::Key_Escape)
        endRenaming(false);
    else
        return false;
    return true;
}
