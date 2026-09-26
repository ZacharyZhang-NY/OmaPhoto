#include "Document/BrushStroke.h"
#include "SelectionFixtures.h"
#include <QPainter>

// Select ▸ Subject: U²-Net's subject becomes the selection.
namespace {
// A red disc on white, or flat white.
std::unique_ptr<EditorSession> withImage(bool disc)
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(400, 300);
    QImage image = BrushRaster::context(400, 300, false);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(217, 26, 26));
    if (disc)
        painter.drawEllipse(QPointF(200, 150), 80, 80);
    painter.end();
    session->insert(ImportedImage(image, image, QStringLiteral("Picture")));
    return session;
}

bool selected(EditorSession &session, SelectionMode mode)
{
    bool done = false;
    session.selectSubject(mode, [&done] { done = true; });
    return QTest::qWaitFor([&done] { return done; }, 20000);
}
}

class SelectSubjectTests : public QObject {
    Q_OBJECT
private slots:
    void theSubjectBecomesTheSelection();
    void theModeCombinesWithTheSelection();
    void aFlatPictureSelectsNothingAndSaysSo();
    void aRefusalAnswersAndChangesNothing();
    void anotherDocumentTakesNoOutline();
};

void SelectSubjectTests::theSubjectBecomesTheSelection()
{
    const auto session = withImage(true);
    QVERIFY(session->canSelectSubject());
    bool done = false;
    session->selectSubject(SelectionMode::replace, [&done] { done = true; });
    QVERIFY(session->isProjectBusy());
    QVERIFY(!session->canSelectSubject());
    QVERIFY(QTest::qWaitFor([&done] { return done; }, 20000));
    QVERIFY(!session->isProjectBusy());
    QCOMPARE(session->history.undoName(), QString("Select Subject"));
    QCOMPARE(coverage(*session, 200, 150), 255);
    QCOMPARE(coverage(*session, 270, 150), 255);
    QCOMPARE(coverage(*session, 295, 150), 0);
    QCOMPARE(coverage(*session, 10, 10), 0);
    session->undo();
    QVERIFY(!session->selection());
}

void SelectSubjectTests::theModeCombinesWithTheSelection()
{
    const auto session = withImage(true);
    session->applySelection(rectPath(QRectF(10, 10, 30, 30)), SelectionMode::replace, "Select");
    QVERIFY(selected(*session, SelectionMode::add));
    QCOMPARE(coverage(*session, 20, 20), 255);
    QCOMPARE(coverage(*session, 200, 150), 255);
    QVERIFY(selected(*session, SelectionMode::subtract));
    QCOMPARE(coverage(*session, 20, 20), 255);
    QCOMPARE(coverage(*session, 200, 150), 0);
    QVERIFY(selected(*session, SelectionMode::replace));
    QCOMPARE(coverage(*session, 20, 20), 0);
    QCOMPARE(coverage(*session, 200, 150), 255);
}

void SelectSubjectTests::aFlatPictureSelectsNothingAndSaysSo()
{
    const auto session = withImage(false);
    QVERIFY(selected(*session, SelectionMode::replace));
    QVERIFY(!session->selection());
    QVERIFY(session->brushError().value().contains("subject"));
    QVERIFY(!session->isProjectBusy());
}

void SelectSubjectTests::aRefusalAnswersAndChangesNothing()
{
    EditorSession empty;
    QVERIFY(!empty.canSelectSubject());
    QSignalSpy quiet(&empty, &EditorSession::changed);
    QVERIFY(selected(empty, SelectionMode::replace));
    QCOMPARE(int(quiet.count()), 0);
    const auto session = withImage(true);
    session->setIsProjectBusy(true);
    QVERIFY(!session->canSelectSubject());
    QSignalSpy changes(session.get(), &EditorSession::changed);
    QVERIFY(selected(*session, SelectionMode::replace));
    QCOMPARE(int(changes.count()), 0);
    QVERIFY(!session->selection());
}

void SelectSubjectTests::anotherDocumentTakesNoOutline()
{
    const auto session = withImage(true);
    EditorSession other;
    other.createDocument(400, 300);
    bool done = false;
    session->selectSubject(SelectionMode::replace, [&done] { done = true; });
    session->installProject(other.projectSnapshot().value(), QString());
    QVERIFY(QTest::qWaitFor([&done] { return done; }, 20000));
    QVERIFY(!session->selection());
    QCOMPARE(session->history.undoName(), QString());
    QVERIFY(!session->isProjectBusy());
    // A closed project takes none either.
    const auto closing = withImage(true);
    done = false;
    closing->selectSubject(SelectionMode::replace, [&done] { done = true; });
    closing->clearProject();
    QVERIFY(QTest::qWaitFor([&done] { return done; }, 20000));
    QVERIFY(!closing->document());
    QVERIFY(!closing->brushError());
    QVERIFY(!closing->isProjectBusy());
}

QTEST_GUILESS_MAIN(SelectSubjectTests)
#include "SelectSubjectTests.moc"
