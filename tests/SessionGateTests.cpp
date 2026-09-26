#include "Document/EditorSession.h"
#include <QSignalSpy>
#include <QtTest>

// Who may start a project operation, and when waiters run.
class SessionGateTests : public QObject {
    Q_OBJECT
private slots:
    void fileRequestsWaitForEveryGate_data();
    void fileRequestsWaitForEveryGate();
    void aWaiterLooksAgainWhenItsTurnComes();
    void aNewCanvasFreesWaitingFileRequests();
    void waitersEndWithTheirSession();
    void theBusyIndicatorWaitsAQuarterSecond();
};

void SessionGateTests::fileRequestsWaitForEveryGate_data()
{
    QTest::addColumn<int>("gate");
    QTest::newRow("a busy project") << 0;
    QTest::newRow("an import under way") << 1;
    QTest::newRow("the new canvas sheet") << 2;
    QTest::newRow("the importer sheet") << 3;
    QTest::newRow("a layer being renamed") << 4;
    QTest::newRow("an import error on show") << 5;
    QTest::newRow("an adjustment being edited") << 6;
}

void SessionGateTests::fileRequestsWaitForEveryGate()
{
    QFETCH(int, gate);
    EditorSession session;
    const auto set = [&](bool closed) {
        switch (gate) {
        case 0: session.setIsProjectBusy(closed); break;
        case 1: session.setIsImporting(closed); break;
        case 2: session.setShowsNewDocument(closed); break;
        case 3: session.setShowsImporter(closed); break;
        case 4: session.setRenamingLayerID(closed ? std::optional(QUuid::createUuid()) : std::nullopt); break;
        case 5: session.setImportError(closed ? std::optional(QString("failed")) : std::nullopt); break;
        default: session.setAdjustmentEditingID(closed ? std::optional(QUuid::createUuid()) : std::nullopt); break;
        }
    };
    int ran = 0;
    session.waitForFileRequest([&] { ++ran; });
    QCOMPARE(ran, 1);
    QVERIFY(session.canStartProjectOperation());
    set(true);
    QVERIFY(!session.canStartProjectOperation());
    session.waitForFileRequest([&] { ++ran; });
    QTest::qWait(20);
    QCOMPARE(ran, 1);
    // Another gate opening and closing frees nobody.
    session.setShowsImporter(gate != 3);
    session.setShowsImporter(gate == 3);
    QTest::qWait(20);
    QCOMPARE(ran, 1);
    set(false);
    QVERIFY(QTest::qWaitFor([&] { return ran == 2; }, 5'000));
}

void SessionGateTests::aWaiterLooksAgainWhenItsTurnComes()
{
    EditorSession session;
    QSignalSpy changes(&session, &EditorSession::changed);
    session.setIsProjectBusy(true);
    QStringList ran;
    qsizetype heard = 0;
    // The first to run takes the project for itself.
    session.waitForProjectAccess([&] {
        ran << "first";
        heard = changes.count();
        session.setIsProjectBusy(true);
    });
    session.waitForProjectAccess([&] { ran << "second"; });
    session.waitForFileRequest([&] { ran << "file"; });
    const qsizetype before = changes.count();
    session.setIsProjectBusy(false);
    // Waiters run once the change is made and announced.
    QVERIFY(ran.isEmpty());
    QTest::qWait(50);
    QCOMPARE(ran, QStringList{"first"});
    QCOMPARE(heard, before + 1);
    session.setIsProjectBusy(false);
    QVERIFY(QTest::qWaitFor([&] { return ran.size() == 3; }, 5'000));
    QCOMPARE(ran, (QStringList{"first", "second", "file"}));
    // Busy said again frees nobody and keeps the line.
    ran.clear();
    session.setIsProjectBusy(true);
    session.waitForProjectAccess([&] { ran << "a"; });
    session.setIsProjectBusy(true);
    session.waitForProjectAccess([&] { ran << "b"; });
    QTest::qWait(50);
    QVERIFY(ran.isEmpty());
    session.setIsProjectBusy(false);
    QVERIFY(QTest::qWaitFor([&] { return ran.size() == 2; }, 5'000));
    QCOMPARE(ran, (QStringList{"a", "b"}));
}

void SessionGateTests::aNewCanvasFreesWaitingFileRequests()
{
    // The sheet closes, or a rename ends, inside createDocument.
    for (const bool renaming : {false, true}) {
        EditorSession session;
        if (renaming)
            session.setRenamingLayerID(QUuid::createUuid());
        else
            session.setShowsNewDocument(true);
        bool ran = false;
        session.waitForFileRequest([&] { ran = true; });
        QTest::qWait(20);
        QVERIFY(!ran);
        session.createDocument(16, 16);
        QVERIFY(QTest::qWaitFor([&] { return ran; }, 5'000));
    }
}

void SessionGateTests::waitersEndWithTheirSession()
{
    QStringList ran;
    {
        EditorSession session;
        session.setIsProjectBusy(true);
        session.waitForProjectAccess([&] { ran << "project"; });
        session.waitForFileRequest([&] { ran << "file"; });
        // Both are posted, and the session ends before they run.
        session.setIsProjectBusy(false);
    }
    QTest::qWait(50);
    QCOMPARE(ran, QStringList());
}

void SessionGateTests::theBusyIndicatorWaitsAQuarterSecond()
{
    QCOMPARE(EditorSession::busyIndicatorDelay, 250);
    EditorSession session;
    QSignalSpy changes(&session, &EditorSession::changed);
    QElapsedTimer clock;
    clock.start();
    session.setIsProjectBusy(true);
    QVERIFY(!session.showsBusy());
    // A quick operation never dims the controls.
    QTest::qWait(60);
    QVERIFY(!session.showsBusy() || clock.elapsed() >= 250);
    session.setIsProjectBusy(false);
    QTest::qWait(400);
    QVERIFY(!session.showsBusy());
    // A long one does, by itself and never early.
    qint64 shownAfter = -1;
    connect(&session, &EditorSession::changed, this, [&] {
        if (session.showsBusy() && shownAfter < 0)
            shownAfter = clock.nsecsElapsed();
    });
    clock.restart();
    session.setIsProjectBusy(true);
    const qsizetype before = changes.count();
    // Busy said again on the way starts no new wait.
    QTest::qSleep(100);
    const QTimer *timer = session.findChild<QTimer *>();
    QVERIFY(timer != nullptr && timer->isActive());
    QCOMPARE(timer->timerType(), Qt::PreciseTimer);
    session.setIsProjectBusy(true);
    QVERIFY2(timer->remainingTime() <= 150, qPrintable(QString::number(timer->remainingTime())));
    QVERIFY(QTest::qWaitFor([&] { return session.showsBusy(); }, 10'000));
    QVERIFY2(shownAfter >= 250'000'000, qPrintable(QString::number(shownAfter)));
    QCOMPARE(changes.count(), before + 2);
    // Busy again keeps it on and starts no second wait.
    session.setIsProjectBusy(true);
    QVERIFY(session.showsBusy());
    QTest::qWait(400);
    QCOMPARE(changes.count(), before + 3);
    session.setIsProjectBusy(false);
    QVERIFY(!session.showsBusy());
}

QTEST_GUILESS_MAIN(SessionGateTests)
#include "SessionGateTests.moc"
