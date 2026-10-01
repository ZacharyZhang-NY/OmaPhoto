#pragma once
#include "Document/EditorSession.h"
#include "Document/LayerGroups.h"
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <map>
#include <memory>

class NativeLayerList;
class QMenu;

// A layer's eye: a press toggles, a drag swipes rows.
class EyeSwipeButton : public QToolButton {
    Q_OBJECT
public:
    explicit EyeSwipeButton(NativeLayerList &list, QWidget *parent);
    ~EyeSwipeButton() override;
    QUuid layerID;

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    // The release may land in a popup: watched application-wide.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void endSwipe();

    NativeLayerList &m_list;
    std::optional<bool> m_swipe;
};

// A thumbnail: a click targets it; Shift toggles a mask.
class LayerThumbnailButton : public QToolButton {
    Q_OBJECT
public:
    LayerThumbnailButton(NativeLayerList &list, bool maskTarget, QWidget *parent);
    QUuid layerID;
    const bool isMaskTarget;
    // An accent ring while targeted; white while shown alone.
    void setTargeted(bool targeted, bool alone = false);
    bool isTargeted() const { return m_targeted; }

    ~LayerThumbnailButton() override;

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    // The release may land in a popup: watched application-wide.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void endAltMaskPress();

    NativeLayerList &m_list;
    bool m_targeted = false;
    bool m_alone = false;
    // Alt on a mask: chosen on release, or dragged.
    std::optional<QPoint> m_altMaskPress;
};

// An effect under its layer: its eye, name and choice.
class LayerEffectRow : public QWidget {
    Q_OBJECT
public:
    LayerEffectRow(NativeLayerList &list, QUuid layerID, LayerEffectKind kind, QWidget *parent);
    const QUuid layerID;
    const LayerEffectKind kind;
    // Shown or hidden, editable, stepped in with its layer.
    void configure(bool enabled, bool editable, int indent);
    void select(bool editing);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    // Alt makes a press on the eye the row's.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void press(QMouseEvent *event, bool twice);
    void relayout();

    NativeLayerList &m_list;
    QToolButton *const m_eye;
    QLabel *const m_label;
    int m_indent = 0;
    // Swift's copyDown: an Alt press, chosen on release or dragged.
    std::optional<QPoint> m_copyDown;
};

// What a row's canvas-framed thumbnail shows; redrawn only on change.
struct ThumbnailKey {
    std::optional<ImageIdentity> image;
    LayerTransform transform;
    QSizeF canvas;
    // A folder's icon takes the palette's ink.
    QColor ink;
    bool editableText = false;
    friend bool operator==(const ThumbnailKey &, const ThumbnailKey &) = default;
};

// One row: eye, disclosure, thumbnails, link, name and size.
class LayerCell : public QWidget {
    Q_OBJECT
public:
    explicit LayerCell(NativeLayerList &list);
    // Swift's row height, and 24 more for each effect.
    static constexpr int rowHeight = 52;
    static constexpr int effectHeight = 24;

    void configure(const ImageLayer &layer, bool enabled, int depth, bool visible);
    void updateTarget();
    void beginRenaming();
    bool isOnControl(QPoint point) const;
    bool isRenaming() const { return m_renaming; }
    bool isGroup() const { return m_isGroup; }
    QUuid layerID() const { return m_layerID; }
    LayerThumbnailButton &thumbnail() { return *m_thumbnail; }
    LayerThumbnailButton &maskThumbnail() { return *m_maskThumbnail; }
    // A left press becomes a drag past the distance.
    void armDrag(QPoint cellPoint, Qt::KeyboardModifiers modifiers);
    void disarmDrag() { m_press = std::nullopt; }
    void moveDrag(QPoint cellPoint, Qt::MouseButtons buttons);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void relayout();
    void endRenaming(bool keeping);

    NativeLayerList &m_list;
    EyeSwipeButton *const m_eye;
    QToolButton *const m_disclosure;
    LayerThumbnailButton *const m_thumbnail;
    LayerThumbnailButton *const m_maskThumbnail;
    QToolButton *const m_link;
    QLabel *const m_disabledMaskMark;
    QLabel *const m_name;
    QLabel *const m_dimensions;
    QLineEdit *const m_editor;
    QGraphicsOpacityEffect *const m_fade;
    QUuid m_layerID;
    QString m_layerName;
    bool m_renaming = false;
    bool m_hasMask = false;
    bool m_linkable = false;
    bool m_isGroup = false;
    bool m_editableText = false;
    bool m_isAdjustment = false;
    // Swift's isEditable: Invert's row renames instead.
    bool m_editableAdjustment = false;
    int m_indent = 0;
    std::optional<QPoint> m_press;
    // Alt at the press makes the drag a copy.
    Qt::KeyboardModifiers m_pressModifiers;
    QSize m_thumbnailSize{36, 36};
    QSize m_maskSize{30, 30};
    std::optional<ThumbnailKey> m_thumbnailKey;
    std::optional<ThumbnailKey> m_maskThumbnailKey;
    std::vector<LayerEffectRow *> m_effectButtons;
};

// Where a drag lands: above, into a folder, or last.
struct LayerDropTarget {
    int row;
    bool onRow;
    bool atBottom;
    bool copying;
    friend bool operator==(const LayerDropTarget &, const LayerDropTarget &) = default;
};

// The column of rows; it paints the drop indicator.
class LayerColumn : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;
    std::optional<QRect> indicator;
    bool indicatorFills = false;

protected:
    void paintEvent(QPaintEvent *event) override;
};

// The layer rows, top down, following the session's hierarchy.
class NativeLayerList : public QScrollArea {
    Q_OBJECT
public:
    explicit NativeLayerList(EditorSession &session, QWidget *parent = nullptr);
    ~NativeLayerList() override;
    // A dragged mask carries its layer's id under this type.
    static const QString maskType;
    // A row drag names its list, as Swift checks tables.
    static const QString sourceType;
    // A dragged effect: its layer's id and kind, colon apart.
    static const QString effectType;
    QString dragToken() const { return m_dragToken; }

    EditorSession &session() const { return m_session; }
    void update();
    std::vector<LayerCell *> cells() const { return m_cells; }
    // The row under a point of this list, or -1.
    int rowAt(QPoint listPoint) const;
    // A row pressed; modifiers extend or clip; true when clipped.
    bool clickRow(LayerCell &cell, Qt::KeyboardModifiers modifiers, QPoint cellPoint);
    // Photoshop's strip along the bottom of a row.
    static bool isClippingZone(const LayerCell &cell, QPoint cellPoint);
    void focusList();
    // Swift's contextMenu(for:) and menu(for:): entries, then the press's routing.
    std::unique_ptr<QMenu> contextMenu(QUuid id);
    std::unique_ptr<QMenu> menuFor(LayerCell &cell, QPoint cellPoint);
    // What a drag of this row carries: its selection.
    std::unique_ptr<QMimeData> dragData(const LayerCell &cell) const;
    void startDrag(LayerCell &cell, Qt::KeyboardModifiers modifiers);
    void startMaskDrag(LayerThumbnailButton &thumbnail);
    void startEffectDrag(LayerEffectRow &row);
    // Where a drop lands; Alt offers a copy alone.
    std::optional<LayerDropTarget> dropTarget(const QMimeData &data, Qt::DropActions offered, QPoint listPoint) const;
    bool acceptDrop(const QMimeData &data, Qt::DropActions offered, QPoint listPoint);
    // Whether a drag held at an edge still scrolls.
    bool autoscrolling() const { return m_edgeScroll.isActive(); }
    // The cursor Alt asks for here: clip, duplicate, arrow.
    QCursor cursorFor(QPoint listPoint, Qt::KeyboardModifiers modifiers) const;
    static QCursor clippingCursor(bool releasing, double ratio);
    // Alt over a mask: the duplicate pointer, an eye behind.
    static QCursor showMaskCursor(double ratio);

protected:
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void pressKey(QKeyEvent *event);
    // Below every row: Swift's table lets go of everything.
    void mousePressEvent(QMouseEvent *event) override;
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool viewportEvent(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    std::vector<QUuid> draggedLayers(const QMimeData &data) const;
    std::optional<LayerEffectSelection> draggedEffect(const QMimeData &data) const;
    void showIndicator(const std::optional<LayerDropTarget> &target);
    void refreshCursor();
    // A drag held at an edge scrolls until it ends.
    void autoscroll(QPoint inViewport, const QMimeData &data, Qt::DropActions offered);

    EditorSession &m_session;
    LayerColumn *const m_column;
    std::vector<LayerCell *> m_cells;
    std::vector<ImageLayer> m_rows;
    std::map<QUuid, LayerHierarchy::Entry> m_details;
    bool m_editingEnabled = false;
    const QString m_dragToken = QUuid::createUuid().toString();
    // Where the pointer last hovered, in the list's coordinates.
    std::optional<QPoint> m_hover;
    QTimer m_edgeScroll;
    QPoint m_dragPoint;
    const QMimeData *m_dragData = nullptr;
    Qt::DropActions m_dragActions;
};

// The size on the canvas and, once scaled, how much.
QString sizeLabel(const ImageLayer &layer);
