#include "IO/RecentProjects.h"
#include <QDir>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <cstdio>

// The list without a GUI application: no activation to follow.
class RecentProjectsCoreTests : public QObject {
    Q_OBJECT
private slots:
    void theListWorksWithoutAGuiApplication();
    void theListOutlivesItsProcess();
};

namespace {
// One step in a fresh process: its output.
QString step(const QString &what)
{
    QProcess child;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("RECENT_STEP"), what);
    child.setProcessEnvironment(environment);
    child.start(QCoreApplication::applicationFilePath(), {});
    if (!child.waitForFinished(30'000) || child.exitStatus() != QProcess::NormalExit || child.exitCode() != 0)
        throw std::runtime_error("the step failed: " + what.toStdString());
    return QString::fromUtf8(child.readAllStandardOutput());
}
}

void RecentProjectsCoreTests::theListWorksWithoutAGuiApplication()
{
    QVERIFY(!qobject_cast<QGuiApplication *>(QCoreApplication::instance()));
    QTemporaryDir root;
    RecentProjects::shared().clear();
    RecentProjects::shared().note(root.path());
    QCOMPARE(RecentProjects::shared().paths(), QStringList{QDir(root.path()).canonicalPath()});
    QCOMPARE(QSettings().value(QStringLiteral("recentProjects")).toStringList(), RecentProjects::shared().paths());
    RecentProjects::shared().clear();
    QVERIFY(RecentProjects::shared().paths().isEmpty());
}

void RecentProjectsCoreTests::theListOutlivesItsProcess()
{
    QTemporaryDir first, second;
    step(QStringLiteral("clear"));
    step(QStringLiteral("note:") + first.path());
    step(QStringLiteral("note:") + second.path());
    // A later launch reads what the earlier ones stored.
    QCOMPARE(step(QStringLiteral("read")), QDir(second.path()).canonicalPath() + "\n" + QDir(first.path()).canonicalPath() + "\n");
    step(QStringLiteral("clear"));
    QCOMPARE(step(QStringLiteral("read")), QString());
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    // A child run does one step, then exits.
    const QString what = qEnvironmentVariable("RECENT_STEP");
    if (what == QLatin1String("clear")) {
        RecentProjects::shared().clear();
    } else if (what.startsWith(QLatin1String("note:"))) {
        RecentProjects::shared().note(what.mid(5));
    } else if (what == QLatin1String("read")) {
        for (const QString &path : RecentProjects::shared().paths())
            std::printf("%s\n", qPrintable(path));
    } else if (!what.isEmpty()) {
        return 2;
    } else {
        RecentProjectsCoreTests tests;
        return QTest::qExec(&tests, argc, argv);
    }
    return 0;
}
#include "RecentProjectsCoreTests.moc"
