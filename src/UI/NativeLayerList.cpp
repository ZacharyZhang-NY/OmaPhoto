#include "UI/NativeLayerList.h"
#include <QEvent>
#include <QKeyEvent>
#include <QScrollBar>
#include <QVBoxLayout>

NativeLayerList::NativeLayerList(EditorSession &session, QWidget *parent)
    : QScrollArea(parent), m_session(session), m_column(new LayerColumn(this))
{
    setObjectName(QStringLiteral("layersList"));
    setAccessibleName(QStringLiteral("Layers"));
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    setAcceptDrops(true);
    viewport()->setMouseTracking(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(false);
    viewport()->setAutoFillBackground(false);
    auto *rows = new QVBoxLayout(m_column);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(2);
    rows->addStretch(1);
    setWidget(m_column);
    m_column->setAutoFillBackground(false);
    connect(&m_session, &EditorSession::changed, this, &NativeLayerList::update);
    m_edgeScroll.setInterval(50);
    connect(&m_edgeScroll, &QTimer::timeout, this, [this] { autoscroll(m_dragPoint, *m_dragData, m_dragActions); });
    update();
}

void NativeLayerList::update()
{
    const std::vector<LayerHierarchy::Entry> entries = m_session.layerRows();
    const std::vector<ImageLayer> &layers = m_session.document() ? m_session.document()->layers : std::vector<ImageLayer>();
    std::vector<ImageLayer> next;
    m_details.clear();
    for (const LayerHierarchy::Entry &entry : entries) {
        const int index = indexOf(layers, entry.layer.id);
        if (index < 0)
            continue;
        next.push_back(layers[index]);
        m_details.insert_or_assign(entry.layer.id, entry);
    }
    m_editingEnabled = m_session.canEditLayers();
    // The rows are made anew when the ids change.
    bool same = next.size() == m_rows.size();
    for (size_t row = 0; same && row < next.size(); ++row)
        same = next[row].id == m_rows[row].id;
    m_rows = std::move(next);
    if (!same) {
        for (LayerCell *cell : m_cells)
            delete cell;
        m_cells.clear();
        auto *rows = static_cast<QVBoxLayout *>(m_column->layout());
        for (size_t row = 0; row < m_rows.size(); ++row) {
            auto *cell = new LayerCell(*this);
            rows->insertWidget(int(row), cell);
            // A layout shows late children from the event loop.
            cell->show();
            m_cells.push_back(cell);
        }
        rows->activate();
    }
    for (size_t row = 0; row < m_rows.size(); ++row) {
        const LayerHierarchy::Entry &entry = m_details.at(m_rows[row].id);
        m_cells[row]->configure(m_rows[row], m_editingEnabled, entry.depth, entry.visible);
        m_cells[row]->updateTarget();
    }
    // Heights change at once, as Swift's noteHeightOfRows.
    m_column->layout()->activate();
    // A rename, asked from anywhere, is typed in the row.
    if (const std::optional<QUuid> renaming = m_session.renamingLayerID()) {
        const int row = indexOf(m_rows, renaming);
        if (row >= 0) {
            ensureWidgetVisible(m_cells[row]);
            m_cells[row]->beginRenaming();
        }
    }
}

int NativeLayerList::rowAt(QPoint listPoint) const
{
    const QPoint inColumn = m_column->mapFrom(viewport(), viewport()->mapFrom(this, listPoint));
    for (size_t row = 0; row < m_cells.size(); ++row) {
        if (m_cells[row]->geometry().contains(inColumn))
            return int(row);
    }
    return -1;
}

bool NativeLayerList::isClippingZone(const LayerCell &cell, QPoint cellPoint)
{
    return cellPoint.y() >= cell.height() - 10;
}

bool NativeLayerList::clickRow(LayerCell &cell, Qt::KeyboardModifiers modifiers, QPoint cellPoint)
{
    const int row = indexOf(m_rows, cell.layerID());
    if (row < 0)
        return false;
    const QUuid id = m_rows[row].id;
    focusList();
    // Swift's mouseDown lets go of a chosen effect first.
    m_session.dropEffectSelection();
    if (modifiers.testFlag(Qt::AltModifier) && !modifiers.testFlag(Qt::ControlModifier) && isClippingZone(cell, cellPoint)) {
        m_session.toggleClippingMask(id);
        // The cursor says what the next click does, as Swift's.
        m_hover = cell.mapTo(this, cellPoint);
        viewport()->setCursor(cursorFor(*m_hover, modifiers));
        return true;
    }
    if (modifiers.testFlag(Qt::ControlModifier)) {
        m_session.extendSelection(id);
        return false;
    }
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        // From the active row to this one, both included.
        const int anchor = indexOf(m_rows, m_session.activeLayerID());
        QSet<QUuid> ids;
        for (int each = std::min(anchor < 0 ? row : anchor, row); each <= std::max(anchor, row); ++each)
            ids.insert(m_rows[each].id);
        m_session.selectLayers(ids, id);
        return false;
    }
    // A click on a lone layer's name targets its pixels.
    const bool targetsMask = m_session.isMaskSelected() && m_session.selectedLayerIDs() == QSet<QUuid>{id} && !cell.isOnControl(cellPoint);
    if (targetsMask) {
        m_session.commitTransform();
        m_session.selectLayerTarget(id, false);
        return false;
    }
    // Selected already, a press keeps the selection: 4.7 drags it.
    if (m_session.selectedLayerIDs().contains(id))
        return false;
    m_session.selectLayers({id}, id);
    return false;
}

void NativeLayerList::mousePressEvent(QMouseEvent *event)
{
    // The spacing between rows takes no press, as before.
    const QPoint inColumn = m_column->mapFrom(viewport(), event->position().toPoint());
    if (event->button() != Qt::LeftButton || (!m_cells.empty() && inColumn.y() <= m_cells.back()->geometry().bottom()))
        return;
    m_session.dropEffectSelection();
    if (!(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)))
        m_session.selectLayers({}, std::nullopt);
}

void NativeLayerList::focusList()
{
    setFocus(Qt::MouseFocusReason);
}

// Tab reaches the keys, not the focus chain, as Swift.
bool NativeLayerList::event(QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
            keyPressEvent(key);
            return true;
        }
    }
    return QScrollArea::event(event);
}

void NativeLayerList::keyPressEvent(QKeyEvent *event)
{
    const bool plain = !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    const int key = event->key();
    const bool enter = key == Qt::Key_Return || key == Qt::Key_Enter;
    const bool arrow = key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down;
    static const std::map<int, NavigationTool> tools{
        {Qt::Key_A, NavigationTool::idle}, {Qt::Key_V, NavigationTool::move}, {Qt::Key_H, NavigationTool::hand},
        {Qt::Key_Z, NavigationTool::zoom}, {Qt::Key_G, NavigationTool::gradient}, {Qt::Key_L, NavigationTool::lasso}, {Qt::Key_M, NavigationTool::marquee},
        {Qt::Key_W, NavigationTool::wand}, {Qt::Key_J, NavigationTool::spotHealing}, {Qt::Key_S, NavigationTool::cloneStamp},
        {Qt::Key_U, NavigationTool::shape}, {Qt::Key_R, NavigationTool::blur}, {Qt::Key_I, NavigationTool::eyedropper},
        {Qt::Key_C, NavigationTool::crop}};
    if (plain && (key == Qt::Key_Backspace || key == Qt::Key_Delete)) {
        m_session.deleteKeyPressed();
        return;
    }
    if (key == Qt::Key_Escape && m_session.transformEdit()) {
        m_session.cancelTransform();
    } else if (enter && m_session.transformEdit()) {
        m_session.commitTransform();
    } else if (plain && (key == Qt::Key_Tab || key == Qt::Key_Backtab)) {
        // Swift's list cycles on Shift-Tab as on Tab.
        m_session.cycleToolMode();
    } else if (plain && (key == Qt::Key_B || key == Qt::Key_E)) {
        m_session.selectTool(NavigationTool::brush);
        m_session.setBrushMode(key == Qt::Key_E ? BrushToolMode::erase : BrushToolMode::paint);
    } else if (plain && (key == Qt::Key_X || key == Qt::Key_D)) {
        if (key == Qt::Key_X)
            m_session.swapPaletteColors();
        else
            m_session.resetPaletteColors();
    } else if (plain && tools.contains(key)) {
        m_session.selectTool(tools.at(key));
    } else if (plain && event->text().size() == 1 && event->text().front().isDigit() && m_session.usesOpacityKeys()) {
        m_session.typeOpacityDigit(event->text().front().digitValue());
    } else if (plain && arrow && m_session.tool() == NavigationTool::move) {
        // Under Move the arrows nudge; an edit implies Move.
        const double step = event->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1;
        m_session.nudgeLayer(key == Qt::Key_Left ? -step : key == Qt::Key_Right ? step : 0, key == Qt::Key_Up ? -step : key == Qt::Key_Down ? step : 0);
    } else if (plain && (key == Qt::Key_Up || key == Qt::Key_Down)) {
        const int row = indexOf(m_rows, m_session.activeLayerID());
        const int next = row + (key == Qt::Key_Up ? -1 : 1);
        if (row < 0 || next < 0 || next >= int(m_rows.size()))
            return;
        // Shift ranges from the anchor; the row comes into view.
        if (event->modifiers().testFlag(Qt::ShiftModifier)) {
            const QSet<QUuid> selected = m_session.selectedLayerIDs();
            int first = row, last = row;
            while (first > 0 && selected.contains(m_rows[first - 1].id))
                --first;
            while (last + 1 < int(m_rows.size()) && selected.contains(m_rows[last + 1].id))
                ++last;
            const int anchor = row == last ? first : last;
            QSet<QUuid> range;
            for (int each = std::min(anchor, next); each <= std::max(anchor, next); ++each)
                range.insert(m_rows[each].id);
            m_session.selectLayers(range, m_rows[next].id);
        } else {
            m_session.selectLayers({m_rows[next].id}, m_rows[next].id);
        }
        ensureWidgetVisible(m_cells[next], 0, 0);
    } else {
        QScrollArea::keyPressEvent(event);
    }
}

// A new palette: icons redraw once the widgets have it.
void NativeLayerList::changeEvent(QEvent *event)
{
    QScrollArea::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        QMetaObject::invokeMethod(this, &NativeLayerList::update, Qt::QueuedConnection);
}
