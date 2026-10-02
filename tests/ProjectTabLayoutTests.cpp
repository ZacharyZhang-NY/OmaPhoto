#include "UI/ProjectTabLayout.h"
#include <QtTest>

// projectTabOverflow is geometry: made-up ids and widths.
namespace {
const QUuid a = QUuid::createUuid(), b = QUuid::createUuid(), c = QUuid::createUuid(), d = QUuid::createUuid(), e = QUuid::createUuid();

// Every tab 100 wide.
std::map<QUuid, double> widths(const std::vector<QUuid> &ids)
{
    std::map<QUuid, double> result;
    for (const QUuid &id : ids)
        result.insert({id, 100});
    return result;
}

const std::function<double(int)> pill = [](int) { return 80.0; };

std::vector<QUuid> ids(const ProjectTabOverflow &layout)
{
    std::vector<QUuid> result;
    for (const ProjectTabSlot &slot : layout.visible)
        result.push_back(slot.id);
    return result;
}

std::vector<double> xs(const ProjectTabOverflow &layout)
{
    std::vector<double> result;
    for (const ProjectTabSlot &slot : layout.visible)
        result.push_back(slot.x);
    return result;
}
}

class ProjectTabLayoutTests : public QObject {
    Q_OBJECT
private slots:
    void everythingFitsShowsAllTabsWithNoPill();
    void overflowHidesTheLeftmostTabsAndPillsThem();
    void selectedTabInTheHiddenSetIsPinnedRightOfThePill();
    void overflowLabelIsSingularForOneTab();
    void unmeasuredWidthShowsEverythingRatherThanGuessing();
    void aWiderSelectedTabStillFits();
    void thePillsWidthFollowsTheHiddenCount();
};

void ProjectTabLayoutTests::everythingFitsShowsAllTabsWithNoPill()
{
    const std::vector<QUuid> order{a, b, c};
    const ProjectTabOverflow result = projectTabOverflow(order, widths(order), c, 400, pill);
    QVERIFY(result.hiddenIDs.empty());
    QVERIFY(!result.pill);
    QVERIFY(ids(result) == order);
    QVERIFY((xs(result) == std::vector<double>{0, 106, 212}));
    QCOMPARE(result.contentWidth(), 312.0);
    // Exactly the room it needs still fits.
    QVERIFY(!projectTabOverflow(order, widths(order), c, 312, pill).pill);
    QVERIFY(projectTabOverflow(order, widths(order), c, 311.5, pill).pill);
    QVERIFY(projectTabOverflow({}, {}, c, 400, pill) == ProjectTabOverflow());
    QCOMPARE(ProjectTabOverflow().contentWidth(), 0.0);
}

void ProjectTabLayoutTests::overflowHidesTheLeftmostTabsAndPillsThem()
{
    const std::vector<QUuid> order{a, b, c, d, e};
    // The pill and two tabs: 292; a third needs 398.
    const ProjectTabOverflow result = projectTabOverflow(order, widths(order), e, 300, pill);
    QVERIFY((result.hiddenIDs == std::vector<QUuid>{a, b, c}));
    QVERIFY((ids(result) == std::vector<QUuid>{d, e}));
    QVERIFY((result.pill.value() == ProjectTabPillSlot{0, 80}));
    QCOMPARE(result.visible.front().x, 86.0);
    QCOMPARE(result.visible.front().width, 100.0);
    QCOMPARE(result.contentWidth(), 292.0);
    // 292 exactly still holds two; a third at 398.
    QCOMPARE(projectTabOverflow(order, widths(order), e, 292, pill).visible.size(), size_t(2));
    QCOMPARE(projectTabOverflow(order, widths(order), e, 397, pill).visible.size(), size_t(2));
    QCOMPARE(projectTabOverflow(order, widths(order), e, 398, pill).visible.size(), size_t(3));
    // Too narrow for even one: one tab stays.
    QVERIFY((ids(projectTabOverflow(order, widths(order), e, 50, pill)) == std::vector<QUuid>{e}));
}

void ProjectTabLayoutTests::selectedTabInTheHiddenSetIsPinnedRightOfThePill()
{
    const std::vector<QUuid> order{a, b, c, d, e};
    // a takes the first slot; d, bumped, hides in order.
    const ProjectTabOverflow result = projectTabOverflow(order, widths(order), a, 300, pill);
    QVERIFY((ids(result) == std::vector<QUuid>{a, e}));
    QVERIFY((result.hiddenIDs == std::vector<QUuid>{b, c, d}));
    QCOMPARE(result.pill.value().width, 80.0);
    QVERIFY((xs(result) == std::vector<double>{86, 192}));
}

void ProjectTabLayoutTests::overflowLabelIsSingularForOneTab()
{
    QCOMPARE(projectTabOverflowLabel(1), QString("1 more tab"));
    QCOMPARE(projectTabOverflowLabel(2), QString("2 more tabs"));
    QCOMPARE(projectTabOverflowLabel(11), QString("11 more tabs"));
}

void ProjectTabLayoutTests::unmeasuredWidthShowsEverythingRatherThanGuessing()
{
    const std::vector<QUuid> order{a, b, c, d, e};
    const ProjectTabOverflow result = projectTabOverflow(order, widths(order), a, 0, pill);
    QVERIFY(result.hiddenIDs.empty());
    QVERIFY(!result.pill);
    QCOMPARE(result.visible.size(), size_t(5));
}

// A long-titled hidden tab still leaves a row that fits.
void ProjectTabLayoutTests::aWiderSelectedTabStillFits()
{
    std::vector<QUuid> order;
    std::map<QUuid, double> sizes;
    for (int index = 0; index < 8; ++index) {
        order.push_back(QUuid::createUuid());
        sizes.insert({order.back(), 80});
    }
    sizes[order[0]] = 190;
    const ProjectTabOverflow layout = projectTabOverflow(order, sizes, order[0], 500, [](int) { return 90.0; });
    QVERIFY2(layout.contentWidth() <= 500, qPrintable(QString::number(layout.contentWidth())));
    QCOMPARE(layout.visible.front().id, order[0]);
    // Pill 90, 190, two of 80; three would make 522.
    QVERIFY((ids(layout) == std::vector<QUuid>{order[0], order[6], order[7]}));
    QVERIFY((layout.hiddenIDs == std::vector<QUuid>{order[1], order[2], order[3], order[4], order[5]}));
    QCOMPARE(layout.contentWidth(), 90 + 6 + 190 + 6 + 80 + 6 + 80.0);
    // Wide enough, it stands alone after the pill.
    const std::vector<QUuid> five{a, b, c, d, e};
    std::map<QUuid, double> wide = widths(five);
    wide[a] = 290;
    const ProjectTabOverflow alone = projectTabOverflow(five, wide, a, 400, pill);
    QVERIFY((ids(alone) == std::vector<QUuid>{a}));
    QVERIFY((alone.hiddenIDs == std::vector<QUuid>{b, c, d, e}));
}

// The pill's width is asked by its hidden count.
void ProjectTabLayoutTests::thePillsWidthFollowsTheHiddenCount()
{
    const std::vector<QUuid> order{a, b, c, d, e};
    std::vector<int> asked;
    const ProjectTabOverflow result = projectTabOverflow(order, widths(order), e, 300, [&](int hidden) {
        asked.push_back(hidden);
        return 50.0 + hidden;
    });
    // Three hidden fits (265); the last ask places it.
    QVERIFY((asked == std::vector<int>{0, 1, 2, 3, 3}));
    QVERIFY((ids(result) == std::vector<QUuid>{d, e}));
    QCOMPARE(result.pill.value().width, 53.0);
    QCOMPARE(result.visible.front().x, 59.0);
}

QTEST_GUILESS_MAIN(ProjectTabLayoutTests)
#include "ProjectTabLayoutTests.moc"
