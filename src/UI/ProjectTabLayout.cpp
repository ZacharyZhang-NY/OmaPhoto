#include "UI/ProjectTabLayout.h"
#include <algorithm>

double ProjectTabOverflow::contentWidth() const
{
    // A pill always has a tab after it.
    return visible.empty() ? 0 : visible.back().x + visible.back().width;
}

QString projectTabOverflowLabel(int hiddenCount)
{
    return hiddenCount == 1 ? QStringLiteral("1 more tab") : QStringLiteral("%1 more tabs").arg(hiddenCount);
}

ProjectTabOverflow projectTabOverflow(const std::vector<QUuid> &order, const std::map<QUuid, double> &widths, QUuid selectedID,
                                      double availableWidth, const std::function<double(int)> &pillWidth)
{
    if (order.empty())
        return {};
    const auto span = [&](auto first, auto last) {
        double total = 0;
        for (auto id = first; id != last; ++id)
            total += widths.at(*id);
        // Never asked of no tabs.
        return total + projectTabSpacing * double(std::distance(first, last) - 1);
    };
    const auto place = [&](const std::vector<QUuid> &ids, double x) {
        std::vector<ProjectTabSlot> placed;
        for (const QUuid &id : ids) {
            placed.push_back({id, x, widths.at(id)});
            x += widths.at(id) + projectTabSpacing;
        }
        return placed;
    };
    // Not yet measured, or everything fits: no pill.
    if (availableWidth <= 0 || span(order.begin(), order.end()) <= availableWidth)
        return {place(order, 0), {}, std::nullopt};
    qsizetype shown = qsizetype(order.size());
    while (shown > 1) {
        const qsizetype hidden = qsizetype(order.size()) - shown;
        if (pillWidth(int(hidden)) + projectTabSpacing + span(order.end() - shown, order.end()) <= availableWidth)
            break;
        shown -= 1;
    }
    std::vector<QUuid> visibleIDs(order.end() - shown, order.end()), hiddenIDs(order.begin(), order.end() - shown);
    if (const auto selected = std::ranges::find(hiddenIDs, selectedID); selected != hiddenIDs.end()) {
        hiddenIDs.erase(selected);
        hiddenIDs.push_back(visibleIDs[0]);
        visibleIDs[0] = selectedID;
        // A wider selected tab: the tabs after it make room.
        while (visibleIDs.size() > 1 && pillWidth(int(hiddenIDs.size())) + projectTabSpacing + span(visibleIDs.begin(), visibleIDs.end()) > availableWidth) {
            hiddenIDs.push_back(visibleIDs[1]);
            visibleIDs.erase(visibleIDs.begin() + 1);
        }
    }
    const double pill = pillWidth(int(hiddenIDs.size()));
    return {place(visibleIDs, pill + projectTabSpacing), hiddenIDs, ProjectTabPillSlot{0, pill}};
}
