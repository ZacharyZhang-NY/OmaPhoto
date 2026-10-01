#include "UI/ProjectTabs.h"
#include <QFontMetricsF>
#include <QToolButton>
#include <QtTest>

// Swift 1.2.4's steady tab strip: fixed widths, a kept offset.
namespace {
QToolButton &select(ProjectTabButton &button)
{
    return *button.findChild<QToolButton *>(QStringLiteral("selectTab"));
}

// Swift's projectTabLabelWidth, from the font Swift names.
int label(const QString &title, bool active)
{
    QFont font;
    font.setPixelSize(12);
    font.setWeight(active ? QFont::DemiBold : QFont::Medium);
    return int(std::min(155.0, std::max(35.0, std::ceil(QFontMetricsF(font).horizontalAdvance(title)))));
}
}

class ProjectTabStripTests : public QObject {
    Q_OBJECT
private slots:
    void eachTabIsAsWideAsItsTitle();
    void aTitleMovesOnlyTheTabsAfterIt();
};

void ProjectTabStripTests::eachTabIsAsWideAsItsTitle()
{
    ProjectWorkspace workspace;
    workspace.current().session.setProjectPath(QStringLiteral("/p/Ab.comp"));
    workspace.newCanvas();
    workspace.current().session.setProjectPath(QStringLiteral("/p/A title long enough to pass the pill's widest width.comp"));
    workspace.newCanvas();
    ProjectTabStrip strip(workspace);
    const QList<ProjectTabButton *> buttons = strip.buttons();
    QCOMPARE(buttons.size(), 3);
    // Short: 35 at least. Long: 155, elided. Front: semibold.
    QCOMPARE(select(*buttons[0]).width(), 35 + 8);
    QCOMPARE(select(*buttons[1]).width(), 155 + 8);
    QVERIFY(select(*buttons[1]).text().endsWith(QChar(0x2026)));
    QCOMPARE(select(*buttons[2]).width(), label(QStringLiteral("Untitled 3"), true) + 8);
    QCOMPARE(select(*buttons[2]).text(), QString("Untitled 3"));
    // Swift's pill: label, 11 before, 8 after, 16, 5.
    buttons[2]->adjustSize();
    QCOMPARE(buttons[2]->width(), label(QStringLiteral("Untitled 3"), true) + 40);
    // The dot: 10 for the title, its character's own width.
    workspace.current().session.createDocument(8, 8);
    workspace.current().session.addBlankLayer();
    QVERIFY(workspace.current().session.isModified());
    const QFontMetrics metrics(select(*buttons[2]).font());
    QCOMPARE(select(*buttons[2]).text(), QString("● Untitled 3"));
    QCOMPARE(select(*buttons[2]).width(), label(QStringLiteral("Untitled 3"), true) + 10 + 8 + metrics.horizontalAdvance(QStringLiteral("● ")) - 10);
    // An unsaved long title elides within Swift's width less 10.
    workspace.select(workspace.tabs()[1]->id);
    workspace.current().session.createDocument(8, 8);
    workspace.current().session.addBlankLayer();
    const QString long_ = workspace.current().title();
    const QFontMetrics bold(select(*buttons[1]).font());
    QCOMPARE(select(*buttons[1]).text(), QStringLiteral("● ") + bold.elidedText(long_, Qt::ElideRight, 145));
    // Chosen, a tab's weight changes its width.
    workspace.select(workspace.tabs()[0]->id);
    QCOMPARE(select(*buttons[2]).width(), label(QStringLiteral("Untitled 3"), false) + 10 + 8 + QFontMetrics(select(*buttons[2]).font()).horizontalAdvance(QStringLiteral("● ")) - 10);
}

void ProjectTabStripTests::aTitleMovesOnlyTheTabsAfterIt()
{
    ProjectWorkspace workspace;
    workspace.newCanvas();
    workspace.newCanvas();
    ProjectTabStrip strip(workspace);
    strip.resize(1000, 34);
    strip.show();
    QVERIFY(QTest::qWaitForWindowExposed(&strip));
    const QList<ProjectTabButton *> buttons = strip.buttons();
    // Swift's leading label: a short title's ink starts left.
    workspace.tabs()[0]->session.setProjectPath(QStringLiteral("/p/I.comp"));
    QToolButton &title = select(*buttons[0]);
    QTRY_COMPARE(title.text(), QString("I"));
    const QImage drawn = title.grab().toImage();
    const QColor ground = drawn.pixelColor(title.width() - 3, 2);
    int left = drawn.width();
    for (int y = 0; y < drawn.height(); ++y) {
        for (int x = 0; x < drawn.width(); ++x) {
            if (drawn.pixelColor(x, y) != ground)
                left = std::min(left, x);
        }
    }
    QVERIFY2(left <= 3 && title.width() == 43, qPrintable(QString::number(left)));
    QTRY_COMPARE(buttons[0]->width(), 35 + 40);
    QTRY_COMPARE(buttons[1]->x(), 75 + 6);
    const int first = buttons[0]->x(), second = buttons[1]->x(), third = buttons[2]->x();
    QCOMPARE(first, 0);
    workspace.tabs()[1]->session.setProjectPath(QStringLiteral("/p/A much longer title here.comp"));
    QTRY_VERIFY(buttons[2]->x() > third);
    QCOMPARE(buttons[0]->x(), first);
    QCOMPARE(buttons[1]->x(), second);
    // Tabs sit six apart.
    QCOMPARE(buttons[1]->x() - (buttons[0]->x() + buttons[0]->width()), 6);
}

QTEST_MAIN(ProjectTabStripTests)
#include "ProjectTabStripTests.moc"
