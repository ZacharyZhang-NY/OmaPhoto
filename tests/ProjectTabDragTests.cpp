#include "UI/ProjectTabs.h"
#include <QMenu>
#include <QPropertyAnimation>
#include <QtTest>

// Swift 1.3's strip: tabs drag to reorder; others gather.
namespace {
QToolButton &select(ProjectTabButton &button)
{
    return *button.findChild<QToolButton *>(QStringLiteral("selectTab"));
}

QStringList titles(const ProjectWorkspace &workspace)
{
    QStringList result;
    for (const std::shared_ptr<ProjectTab> &tab : workspace.tabs())
        result << tab->title();
    return result;
}

ProjectTabButton &button(ProjectTabStrip &strip, const QString &title)
{
    for (ProjectTabButton *button : strip.buttons()) {
        if (button->tab->title() == title)
            return *button;
    }
    throw std::runtime_error("no such tab");
}

// Three tabs, Untitled to Untitled 3, the last in front.
struct Strip {
    ProjectWorkspace workspace;
    ProjectTabStrip strip{workspace};
    Strip(int tabs = 3, int width = 1000)
    {
        for (int index = 1; index < tabs; ++index)
            workspace.newCanvas();
        strip.resize(width, 34);
        strip.show();
        if (!QTest::qWaitForWindowExposed(&strip))
            throw std::runtime_error("the strip never showed");
    }
    // QTest sends to a widget, not to the press's grab.
    QToolButton *pressed = nullptr;
    // Presses a tab; returns the point, in the strip.
    QPoint press(const QString &title)
    {
        ProjectTabButton &tab = button(strip, title);
        const QPoint point = tab.mapTo(&strip, QPoint(20, 14));
        pressed = &select(tab);
        QTest::mousePress(pressed, Qt::LeftButton, {}, pressed->mapFrom(&strip, point));
        return point;
    }
    void move(QPoint point) { QTest::mouseMove(pressed, pressed->mapFrom(&strip, point)); }
    // The release goes to the pressed title wherever it is.
    void release(const QString &title, QPoint point)
    {
        QToolButton &title_ = select(button(strip, title));
        QTest::mouseRelease(&title_, Qt::LeftButton, {}, title_.mapFrom(&strip, point));
    }
};

// Every slide finished: tabs rest at their slots.
bool settled(const ProjectTabStrip &strip)
{
    return strip.findChildren<QPropertyAnimation *>().isEmpty();
}
}

class ProjectTabDragTests : public QObject {
    Q_OBJECT
private slots:
    void aDraggedTabFollowsThePointerAndLandsInTheGap();
    void aSmallOrUpwardMoveIsAClick();
    void aBusyProjectHoldsTheOrder();
    void aTabClosedMidDragEndsIt();
    void aLostReleaseGivesWayToTheNextDrag();
    void tabsThatDoNotFitGatherInAMenu();
    void theMenuSwitchesAndPinsTheChosenTab();
    void aBusyProjectOpensNoMenu();
};

void ProjectTabDragTests::aDraggedTabFollowsThePointerAndLandsInTheGap()
{
    Strip fixture;
    ProjectTabButton &first = button(fixture.strip, "Untitled"), &second = button(fixture.strip, "Untitled 2"),
                     &third = button(fixture.strip, "Untitled 3");
    QCOMPARE(second.x(), first.width() + 6);
    const QPoint start = fixture.press("Untitled");
    // Two points stay a press; three drag, and select.
    fixture.move(start + QPoint(2, 0));
    QCOMPARE(fixture.workspace.selectedID(), fixture.workspace.tabs()[2]->id);
    fixture.move(start + QPoint(3, 0));
    QCOMPARE(fixture.workspace.selectedID(), fixture.workspace.tabs()[0]->id);
    fixture.move(start + QPoint(40, 0));
    QCOMPARE(fixture.workspace.selectedID(), fixture.workspace.tabs()[0]->id);
    QCOMPARE(first.x(), 40);
    // Chosen, it is set semibold: widths read now.
    const int w1 = first.width(), w2 = second.width(), w3 = third.width();
    // Nearest the second's compacted place still: nothing slides.
    QTRY_VERIFY(settled(fixture.strip));
    QCOMPARE(second.x(), w1 + 6);
    // Past the middle of the gap: the second slides left.
    fixture.move(start + QPoint(w2 / 2 + 10, 0));
    const auto *slide = second.findChild<QPropertyAnimation *>();
    QVERIFY(slide && slide->duration() == 150 && slide->easingCurve() == QEasingCurve::OutQuad);
    QCOMPARE(slide->endValue().toPoint(), QPoint(0, 3));
    QVERIFY(!first.findChild<QPropertyAnimation *>() && !third.findChild<QPropertyAnimation *>());
    QTRY_VERIFY(settled(fixture.strip));
    // The dragged tab lies over the one it crosses.
    QVERIFY(first.x() < second.x() + second.width());
    QWidget *hit = fixture.strip.childAt(first.x() + 2, 14);
    QVERIFY(hit == &first || first.isAncestorOf(hit));
    QCOMPARE(second.x(), 0);
    QCOMPARE(third.x(), w1 + 6 + w2 + 6);
    // Far right: held at the row's end, the others compacted.
    fixture.move(start + QPoint(5000, 0));
    QTRY_VERIFY(settled(fixture.strip));
    const int end = w1 + w2 + w3 + 12;
    QCOMPARE(first.x(), end - w1);
    QCOMPARE(third.x(), w2 + 6);
    // Far left: held at the row's start.
    fixture.move(start + QPoint(-5000, 0));
    QCOMPARE(first.x(), 0);
    fixture.move(start + QPoint(5000, 0));
    QVERIFY(first.isVisible() && first.y() == 3);
    fixture.release("Untitled", start + QPoint(5000, 0));
    QCOMPARE(titles(fixture.workspace), (QStringList{"Untitled 2", "Untitled 3", "Untitled"}));
    QCOMPARE(fixture.workspace.selectedID(), first.tab->id);
    QTRY_VERIFY(settled(fixture.strip));
    QCOMPARE(second.x(), 0);
    QCOMPARE(third.x(), second.width() + 6);
    QCOMPARE(first.x(), third.x() + third.width() + 6);
    // The front tab, dragged one place left.
    const QPoint back = fixture.press("Untitled");
    fixture.move(back - QPoint(third.width() / 2 + 10, 0));
    fixture.release("Untitled", back - QPoint(third.width() / 2 + 10, 0));
    QCOMPARE(titles(fixture.workspace), (QStringList{"Untitled 2", "Untitled", "Untitled 3"}));
    // The first, dragged one place right.
    const QPoint right = fixture.press("Untitled 2");
    fixture.move(right + QPoint(first.width() / 2 + 10, 0));
    fixture.release("Untitled 2", right + QPoint(first.width() / 2 + 10, 0));
    QCOMPARE(titles(fixture.workspace), (QStringList{"Untitled", "Untitled 2", "Untitled 3"}));
    // Let go where it began: the order stays.
    const QPoint still = fixture.press("Untitled 3");
    fixture.move(still + QPoint(5, 0));
    fixture.release("Untitled 3", still + QPoint(5, 0));
    QCOMPARE(titles(fixture.workspace), (QStringList{"Untitled", "Untitled 2", "Untitled 3"}));
    QTRY_VERIFY(settled(fixture.strip));
    QCOMPARE(third.x(), second.width() + first.width() + 12);
}

void ProjectTabDragTests::aSmallOrUpwardMoveIsAClick()
{
    Strip fixture;
    const QPoint start = fixture.press("Untitled");
    // Three points up, two across: no drag, no selection yet.
    fixture.move(start + QPoint(2, -3));
    QCOMPARE(fixture.workspace.selectedID(), fixture.workspace.tabs()[2]->id);
    QCOMPARE(button(fixture.strip, "Untitled").x(), 0);
    // Its release lost, a move under another button drags nothing.
    const QPoint far = fixture.pressed->mapFrom(&fixture.strip, start + QPoint(200, 0));
    QMouseEvent other(QEvent::MouseMove, QPointF(far), QPointF(fixture.pressed->mapToGlobal(far)), Qt::NoButton, Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(fixture.pressed, &other);
    QCOMPARE(button(fixture.strip, "Untitled").x(), 0);
    fixture.release("Untitled", start + QPoint(2, -3));
    // The click selects; the order stays.
    QCOMPARE(fixture.workspace.selectedID(), fixture.workspace.tabs()[0]->id);
    QCOMPARE(titles(fixture.workspace), (QStringList{"Untitled", "Untitled 2", "Untitled 3"}));
    // A busy project starts no drag and selects nothing.
    fixture.workspace.current().session.setIsProjectBusy(true);
    const QPoint busy = fixture.press("Untitled");
    fixture.move(busy + QPoint(200, 0));
    QCOMPARE(button(fixture.strip, "Untitled").x(), 0);
    fixture.release("Untitled", busy + QPoint(200, 0));
    QCOMPARE(titles(fixture.workspace), (QStringList{"Untitled", "Untitled 2", "Untitled 3"}));
}

void ProjectTabDragTests::aBusyProjectHoldsTheOrder()
{
    Strip fixture;
    const QPoint start = fixture.press("Untitled");
    fixture.move(start + QPoint(5000, 0));
    QVERIFY(button(fixture.strip, "Untitled").x() > 100);
    // Busy by the drop: the tabs go back.
    fixture.workspace.current().session.setIsProjectBusy(true);
    fixture.release("Untitled", start + QPoint(5000, 0));
    QCOMPARE(titles(fixture.workspace), (QStringList{"Untitled", "Untitled 2", "Untitled 3"}));
    QTRY_VERIFY(settled(fixture.strip));
    QCOMPARE(button(fixture.strip, "Untitled").x(), 0);
    QCOMPARE(button(fixture.strip, "Untitled 2").x(), button(fixture.strip, "Untitled").width() + 6);
}

void ProjectTabDragTests::aTabClosedMidDragEndsIt()
{
    Strip fixture;
    const QPoint start = fixture.press("Untitled 2");
    fixture.move(start - QPoint(5000, 0));
    QTRY_VERIFY(settled(fixture.strip));
    QVERIFY(button(fixture.strip, "Untitled").x() > 0);
    fixture.workspace.removeTab(fixture.workspace.tabs()[1]->id);
    QCOMPARE(fixture.strip.buttons().size(), 2);
    QTRY_VERIFY(settled(fixture.strip));
    QCOMPARE(button(fixture.strip, "Untitled").x(), 0);
    QCOMPARE(button(fixture.strip, "Untitled 3").x(), button(fixture.strip, "Untitled").width() + 6);
    // Another drag works at once.
    const QPoint next = fixture.press("Untitled 3");
    fixture.move(next - QPoint(5000, 0));
    fixture.release("Untitled 3", next - QPoint(5000, 0));
    QCOMPARE(titles(fixture.workspace), (QStringList{"Untitled 3", "Untitled"}));
}

void ProjectTabDragTests::aLostReleaseGivesWayToTheNextDrag()
{
    Strip fixture;
    const QPoint first = fixture.press("Untitled");
    fixture.move(first + QPoint(5000, 0));
    // Its release never comes; another tab is dragged.
    const QPoint next = fixture.press("Untitled 3");
    fixture.move(next - QPoint(5000, 0));
    fixture.release("Untitled 3", next - QPoint(5000, 0));
    QCOMPARE(titles(fixture.workspace), (QStringList{"Untitled 3", "Untitled", "Untitled 2"}));
    QTRY_VERIFY(settled(fixture.strip));
    QCOMPARE(button(fixture.strip, "Untitled 3").x(), 0);
}

void ProjectTabDragTests::tabsThatDoNotFitGatherInAMenu()
{
    Strip fixture(12, 400);
    OverflowTabsPill &pill = *fixture.strip.pill();
    QVERIFY(pill.isVisible());
    QCOMPARE(pill.objectName(), QString("projectTabsOverflow"));
    QList<ProjectTabButton *> shown;
    for (ProjectTabButton *tab : fixture.strip.buttons()) {
        if (tab->isVisible())
            shown << tab;
    }
    const int hidden = 12 - int(shown.size());
    QVERIFY(hidden > 0 && shown.size() >= 2);
    QCOMPARE(pill.text(), QStringLiteral("%1 more tabs").arg(hidden));
    QCOMPARE(pill.toolTip(), pill.text());
    QCOMPARE(pill.accessibleName(), pill.text());
    QCOMPARE(pill.geometry(), QRect(0, 3, int(OverflowTabsPill::pillWidth(hidden)), 28));
    // The right-most tabs stay, the front one last.
    QCOMPARE(shown.last()->tab->id, fixture.workspace.selectedID());
    QCOMPARE(shown.first()->tab->id, fixture.workspace.tabs()[size_t(hidden)]->id);
    QCOMPARE(shown.first()->x(), pill.width() + 6);
    QVERIFY(shown.last()->geometry().right() < 400);
    // Swift's measure: the label, 11, 4, 10, 11.
    QFont font;
    font.setPixelSize(12);
    font.setWeight(QFont::Medium);
    QCOMPARE(OverflowTabsPill::pillWidth(3), std::ceil(QFontMetricsF(font).horizontalAdvance("3 more tabs")) + 36);
    // Wide enough, every tab shows and the pill goes.
    fixture.strip.resize(3000, 34);
    QVERIFY(!pill.isVisible());
    for (ProjectTabButton *tab : fixture.strip.buttons())
        QVERIFY(tab->isVisible());
    QCOMPARE(fixture.strip.buttons().first()->x(), 0);
}

void ProjectTabDragTests::theMenuSwitchesAndPinsTheChosenTab()
{
    Strip fixture(12, 400);
    OverflowTabsPill &pill = *fixture.strip.pill();
    fixture.workspace.tabs()[1]->session.createDocument(8, 8);
    QVERIFY(fixture.workspace.tabs()[1]->session.isModified());
    QTest::mousePress(&pill, Qt::LeftButton);
    auto *menu = pill.findChild<QMenu *>("projectTabsOverflowMenu");
    QVERIFY(menu && menu->isVisible());
    QStringList entries;
    for (const QAction *action : menu->actions())
        entries << action->text();
    const int hidden = int(entries.size());
    QCOMPARE(pill.text(), QStringLiteral("%1 more tabs").arg(hidden));
    QCOMPARE(entries.mid(0, 3), (QStringList{"Untitled", "• Untitled 2", "Untitled 3"}));
    QCOMPARE(menu->mapToGlobal(QPoint(0, 0)), pill.mapToGlobal(QPoint(0, pill.height() + 4)));
    menu->actions()[1]->trigger();
    menu->close();
    QTest::mouseRelease(&pill, Qt::LeftButton);
    const QUuid chosen = fixture.workspace.tabs()[1]->id;
    QCOMPARE(fixture.workspace.selectedID(), chosen);
    // Pinned after the pill; the row stays inside.
    ProjectTabButton &pinned = button(fixture.strip, "Untitled 2");
    QVERIFY(pinned.isVisible());
    QCOMPARE(pinned.x(), pill.width() + 6);
    for (ProjectTabButton *tab : fixture.strip.buttons())
        QVERIFY(!tab->isVisible() || tab->geometry().right() < 400);
    QTRY_VERIFY(!pill.findChild<QMenu *>("projectTabsOverflowMenu"));
}

void ProjectTabDragTests::aBusyProjectOpensNoMenu()
{
    Strip fixture(12, 400);
    OverflowTabsPill &pill = *fixture.strip.pill();
    const QImage free = pill.grab().toImage();
    fixture.workspace.current().session.setIsProjectBusy(true);
    pill.update();
    // The label dims; the capsule stays.
    const QImage busy = pill.grab().toImage();
    QVERIFY(busy != free);
    QCOMPARE(busy.pixelColor(pill.width() / 2, 1), free.pixelColor(pill.width() / 2, 1));
    QTest::mouseClick(&pill, Qt::LeftButton);
    QVERIFY(!pill.findChild<QMenu *>("projectTabsOverflowMenu"));
}

QTEST_MAIN(ProjectTabDragTests)
#include "ProjectTabDragTests.moc"
