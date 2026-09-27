#include "Document/EditorSession.h"
#include "Document/ToolDefaults.h"
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

// Swift's ToolDefaults: toggles that outlive the session and the run.
namespace {
struct Toggle {
    QString key;
    bool fallback;
    std::function<bool(const EditorSession &)> read;
    std::function<void(EditorSession &, bool)> write;
};

std::vector<Toggle> toggles()
{
    return {
        {"autoSelect", false, &EditorSession::transformAutoSelect, &EditorSession::setTransformAutoSelect},
        {"transformControls", true, &EditorSession::showsTransformControls, &EditorSession::setShowsTransformControls},
        {"pixelGrid", true, &EditorSession::showsPixelGrid, &EditorSession::setShowsPixelGrid},
        {"grid", false, &EditorSession::showsGrid, &EditorSession::setShowsGrid},
        {"guides", true, &EditorSession::showsGuides, &EditorSession::setShowsGuides},
        {"rulers", false, &EditorSession::showsRulers, &EditorSession::setShowsRulers},
        {"snap", true, &EditorSession::snapEnabled, &EditorSession::setSnapEnabled},
        {"snapGuides", true, &EditorSession::snapToGuides, &EditorSession::setSnapToGuides},
        {"snapGrid", false, &EditorSession::snapToGrid, &EditorSession::setSnapToGrid},
        {"snapLayers", true, &EditorSession::snapToLayers, &EditorSession::setSnapToLayers},
        {"snapBounds", true, &EditorSession::snapToDocumentBounds, &EditorSession::setSnapToDocumentBounds},
        {"lockGuides", false, &EditorSession::locksGuides, &EditorSession::setLocksGuides},
    };
}
}

class ToolDefaultsTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void untilTheAppTurnsThemOnTheDefaultsHold();
    void everyToggleOutlivesItsSession();
    void showingAGuideRemembersGuidesShown();
    void aStrangeValueIsIgnoredAndSaid();

private:
    QTemporaryDir m_folder;
};

void ToolDefaultsTests::initTestCase()
{
    // Settings land in a folder of the test's own.
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, m_folder.path());
}

void ToolDefaultsTests::untilTheAppTurnsThemOnTheDefaultsHold()
{
    QSettings().setValue("tool.grid", true);
    QVERIFY(!ToolDefaults::boolean("grid", false));
    ToolDefaults::set(false, "rulers");
    QVERIFY(!QSettings().contains("tool.rulers"));
    EditorSession session;
    QVERIFY(!session.showsGrid());
    session.setShowsRulers(true);
    QVERIFY(!QSettings().contains("tool.rulers"));
    QSettings().remove("tool.grid");
}

void ToolDefaultsTests::everyToggleOutlivesItsSession()
{
    ToolDefaults::enable();
    // An unset toggle is no strange value.
    QTest::failOnWarning(QRegularExpression("ignoring the tool setting"));
    for (const Toggle &toggle : toggles()) {
        QVERIFY(!QSettings().contains("tool." + toggle.key));
        QCOMPARE(toggle.read(EditorSession()), toggle.fallback);
        {
            EditorSession session;
            toggle.write(session, !toggle.fallback);
        }
        QCOMPARE(QSettings().value("tool." + toggle.key).toBool(), !toggle.fallback);
        QVERIFY2(toggle.read(EditorSession()) == !toggle.fallback, qPrintable(toggle.key));
        // The next run reads text, as the file keeps it.
        QSettings().setValue("tool." + toggle.key, !toggle.fallback ? "true" : "false");
        QCOMPARE(ToolDefaults::boolean(toggle.key, toggle.fallback), !toggle.fallback);
        QSettings().remove("tool." + toggle.key);
    }
}

void ToolDefaultsTests::showingAGuideRemembersGuidesShown()
{
    ToolDefaults::enable();
    EditorSession session;
    session.createDocument(100, 100);
    for (const bool adding : {true, false}) {
        session.setShowsGuides(false);
        QCOMPARE(QSettings().value("tool.guides").toString(), QString("false"));
        if (adding)
            session.addGuide(CanvasGuide{QUuid::createUuid(), CanvasGuide::Axis::vertical, 40});
        else
            session.beginGuideCreation(CanvasGuide::Axis::horizontal, 20);
        QVERIFY(session.showsGuides());
        QCOMPARE(QSettings().value("tool.guides").toString(), QString("true"));
        QVERIFY(EditorSession().showsGuides());
    }
    QSettings().remove("tool.guides");
}

void ToolDefaultsTests::aStrangeValueIsIgnoredAndSaid()
{
    ToolDefaults::enable();
    QSettings().setValue("tool.grid", "maybe");
    QTest::ignoreMessage(QtWarningMsg, "ignoring the tool setting grid of maybe");
    QVERIFY(!ToolDefaults::boolean("grid", false));
    QTest::ignoreMessage(QtWarningMsg, "ignoring the tool setting grid of maybe");
    QVERIFY(ToolDefaults::boolean("grid", true));
    QSettings().setValue("tool.grid", 1);
    QTest::ignoreMessage(QtWarningMsg, "ignoring the tool setting grid of 1");
    QVERIFY(!ToolDefaults::boolean("grid", false));
    QSettings().remove("tool.grid");
}

QTEST_GUILESS_MAIN(ToolDefaultsTests)
#include "ToolDefaultsTests.moc"
