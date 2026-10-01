#include "UI/ProjectTabs.h"
#include <QPropertyAnimation>
#include <algorithm>
#include <cmath>

ProjectTabStrip::ProjectTabStrip(ProjectWorkspace &workspace, QWidget *parent)
    : QWidget(parent), m_workspace(workspace), m_pill(new OverflowTabsPill(workspace, this))
{
    setObjectName(QStringLiteral("projectTabs"));
    setAccessibleName(QStringLiteral("Project tabs"));
    setFixedHeight(34);
    // The toolbar's free width; the toolbar shows through.
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(&m_workspace, &ProjectWorkspace::changed, this, &ProjectTabStrip::synchronize);
    synchronize();
}

// Every tab in a row, as if unmeasured.
QSize ProjectTabStrip::sizeHint() const
{
    std::vector<QUuid> order;
    for (const std::shared_ptr<ProjectTab> &tab : m_workspace.tabs())
        order.push_back(tab->id);
    return {int(std::ceil(projectTabOverflow(order, widths(), m_workspace.selectedID(), 0, &OverflowTabsPill::pillWidth).contentWidth())), 34};
}

QList<ProjectTabButton *> ProjectTabStrip::buttons() const
{
    return QList<ProjectTabButton *>(m_buttons.begin(), m_buttons.end());
}

void ProjectTabStrip::synchronize()
{
    // Buttons follow the tabs: kept where the tab stays.
    std::vector<ProjectTabButton *> kept;
    for (const std::shared_ptr<ProjectTab> &tab : m_workspace.tabs()) {
        const auto found = std::ranges::find(m_buttons, tab, &ProjectTabButton::tab);
        ProjectTabButton *button = nullptr;
        if (found == m_buttons.end()) {
            const QUuid id = tab->id;
            button = new ProjectTabButton(m_workspace, tab, [this, id](TabDragPhase phase, double translation) { handleReorder(id, phase, translation); }, this);
            // A title or dot changes the widths.
            connect(&tab->session, &EditorSession::changed, button, [this] { layOut(); });
        } else {
            button = *found;
            m_buttons.erase(found);
        }
        button->synchronize();
        kept.push_back(button);
    }
    for (ProjectTabButton *gone : m_buttons)
        delete gone;
    m_buttons = std::move(kept);
    // A drag whose tab closed under it is over.
    if (m_reorder && !m_workspace.tab(m_reorder->id))
        m_reorder.reset();
    layOut();
}

std::map<QUuid, double> ProjectTabStrip::widths() const
{
    std::map<QUuid, double> result;
    for (const ProjectTabButton *button : m_buttons)
        result.insert({button->tab->id, button->sizeHint().width()});
    return result;
}

ProjectTabOverflow ProjectTabStrip::overflow() const
{
    std::vector<QUuid> order;
    for (const std::shared_ptr<ProjectTab> &tab : m_workspace.tabs())
        order.push_back(tab->id);
    return projectTabOverflow(order, widths(), m_workspace.selectedID(), width(), &OverflowTabsPill::pillWidth);
}

void ProjectTabStrip::layOut(bool animated)
{
    const ProjectTabOverflow layout = overflow();
    m_pill->setVisible(layout.pill.has_value());
    if (layout.pill) {
        m_pill->setHiddenIDs(layout.hiddenIDs);
        m_pill->setGeometry(int(layout.pill->x), 3, int(layout.pill->width), 28);
    }
    for (ProjectTabButton *button : m_buttons) {
        const auto slot = std::ranges::find(layout.visible, button->tab->id, &ProjectTabSlot::id);
        button->setVisible(slot != layout.visible.end());
        if (slot == layout.visible.end())
            continue;
        button->resize(int(slot->width), 28);
        const QPoint target(int(std::round(renderX(*slot, layout.contentWidth()))), 3);
        // The dragged tab follows the pointer; the others ease aside.
        auto *slide = button->findChild<QPropertyAnimation *>(QString(), Qt::FindDirectChildrenOnly);
        const bool eases = animated && !(m_reorder && m_reorder->id == button->tab->id);
        if (slide && (!eases || slide->endValue() != QVariant(target)))
            delete std::exchange(slide, nullptr);
        if (!eases)
            button->move(target);
        else if (!slide && button->pos() != target) {
            slide = new QPropertyAnimation(button, "pos", button);
            slide->setDuration(150);
            slide->setEasingCurve(QEasingCurve::OutQuad);
            slide->setEndValue(target);
            slide->start(QAbstractAnimation::DeleteWhenStopped);
        }
    }
    updateGeometry();
}

double ProjectTabStrip::renderX(const ProjectTabSlot &slot, double contentWidth) const
{
    if (!m_reorder)
        return slot.x;
    if (slot.id == m_reorder->id)
        return std::min(std::max(m_reorder->originX + m_reorder->translation, m_reorder->startX), std::max(m_reorder->startX, contentWidth - slot.width));
    const auto index = std::ranges::find(m_reorder->others, slot.id);
    if (index == m_reorder->others.end())
        return slot.x;
    const double x = m_reorder->compactedX.at(slot.id);
    return index - m_reorder->others.begin() >= m_reorder->targetIndex ? x + m_reorder->widths.at(m_reorder->id) + projectTabSpacing : x;
}

// The gap nearest the dragged tab: where it lands.
int ProjectTabStrip::Reorder::nearestSlot() const
{
    const double x = originX + translation;
    std::vector<double> gaps;
    for (const QUuid &other : others)
        gaps.push_back(compactedX.at(other));
    gaps.push_back(others.empty() ? startX : compactedX.at(others.back()) + widths.at(others.back()) + projectTabSpacing);
    return int(std::ranges::min_element(gaps, {}, [x](double slot) { return std::abs(slot - x); }) - gaps.begin());
}

void ProjectTabStrip::handleReorder(QUuid id, TabDragPhase phase, double translation)
{
    if (phase == TabDragPhase::changed) {
        // Another tab's drag lost its release, as Swift's timer finds.
        if (m_reorder && m_reorder->id != id)
            m_reorder.reset();
        if (!m_reorder) {
            if (!m_workspace.canSwitch() || std::abs(translation) < 3)
                return;
            // Dragging a tab selects it, as in Safari.
            m_workspace.select(id);
            m_reorder = makeReorder(id);
            // Above the tabs it crosses.
            for (ProjectTabButton *button : m_buttons) {
                if (button->tab->id == id)
                    button->raise();
            }
        }
        m_reorder->translation = translation;
        m_reorder->targetIndex = m_reorder->nearestSlot();
        layOut(true);
        return;
    }
    // Busy by the drop: the tabs go back.
    if (m_reorder && m_reorder->id == id && m_workspace.canSwitch())
        commitReorder(*m_reorder);
    m_reorder.reset();
    layOut(true);
}

ProjectTabStrip::Reorder ProjectTabStrip::makeReorder(QUuid id) const
{
    const ProjectTabOverflow layout = overflow();
    Reorder state{.id = id, .others = {}, .widths = widths(), .compactedX = {}, .startX = layout.visible.empty() ? 0 : layout.visible.front().x,
                  .originX = 0};
    double x = state.startX;
    state.originX = state.startX;
    for (const ProjectTabSlot &slot : layout.visible) {
        if (slot.id == id) {
            state.originX = slot.x;
            continue;
        }
        state.others.push_back(slot.id);
        state.compactedX.insert({slot.id, x});
        x += state.widths.at(slot.id) + projectTabSpacing;
    }
    return state;
}

// Only the tabs visible as the drag began ever move.
void ProjectTabStrip::commitReorder(const Reorder &state)
{
    std::vector<QUuid> order;
    for (const std::shared_ptr<ProjectTab> &tab : m_workspace.tabs())
        order.push_back(tab->id);
    // A tab closed mid-drag has no place.
    const auto position = [&order](QUuid id) -> std::optional<qsizetype> {
        const auto found = std::ranges::find(order, id);
        return found == order.end() ? std::nullopt : std::optional(found - order.begin());
    };
    const qsizetype from = position(state.id).value();
    qsizetype target = from;
    if (const auto neighbor = state.targetIndex < int(state.others.size()) ? position(state.others[size_t(state.targetIndex)]) : std::nullopt)
        target = *neighbor;
    else if (const auto last = state.others.empty() ? std::nullopt : position(state.others.back()))
        target = *last + 1;
    if (from < target)
        target -= 1;
    m_workspace.moveTab(state.id, int(target));
}

void ProjectTabStrip::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layOut();
}
