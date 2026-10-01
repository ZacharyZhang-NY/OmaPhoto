#pragma once
#include <QString>
#include <QUuid>
#include <functional>
#include <map>
#include <optional>
#include <vector>

// Between pills, and after the overflow pill.
inline constexpr double projectTabSpacing = 6;

// A tab's place in the strip.
struct ProjectTabSlot {
    QUuid id;
    double x;
    double width;
    bool operator==(const ProjectTabSlot &) const = default;
};

// The overflow pill's place: it is no tab.
struct ProjectTabPillSlot {
    double x;
    double width;
    bool operator==(const ProjectTabPillSlot &) const = default;
};

// What the strip draws: tabs that fit, the rest.
struct ProjectTabOverflow {
    std::vector<ProjectTabSlot> visible;
    std::vector<QUuid> hiddenIDs;
    std::optional<ProjectTabPillSlot> pill;
    double contentWidth() const;
    bool operator==(const ProjectTabOverflow &) const = default;
};

// "N more tabs", singular for one.
QString projectTabOverflowLabel(int hiddenCount);

// Drops tabs from the front until the rest fit.
ProjectTabOverflow projectTabOverflow(const std::vector<QUuid> &order, const std::map<QUuid, double> &widths, QUuid selectedID,
                                      double availableWidth, const std::function<double(int)> &pillWidth);
