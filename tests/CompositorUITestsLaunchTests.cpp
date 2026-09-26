#include "UI/ProjectWorkspaceView.h"
#include <QtTest>

class CompositorUITestsLaunchTests : public QObject {
    Q_OBJECT
private slots:
    void testLaunch();
    void testWindowKeepsItsMinimumSize();
};

void CompositorUITestsLaunchTests::testLaunch()
{
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    // Swift titles the window by its project.
    QCOMPARE(window.windowTitle(), QStringLiteral("Untitled[*]"));
    QCOMPARE(window.size(), QSize(1180, 780));
    // The welcome is up: Swift's launch test waits for it.
    QVERIFY(window.findChild<QWidget *>("createCanvas")->isVisible());
    QCoreApplication::processEvents();
    QVERIFY(window.grab().save(QStringLiteral("LaunchScreen.png")));
}

void CompositorUITestsLaunchTests::testWindowKeepsItsMinimumSize()
{
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    window.resize(100, 100);
    QCOMPARE(window.size(), QSize(800, 520));
}

QTEST_MAIN(CompositorUITestsLaunchTests)
#include "CompositorUITestsLaunchTests.moc"
