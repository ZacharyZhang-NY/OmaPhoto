#include "Document/EditorSession.h"
#include "Rendering/RasterSnapshot.h"
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

// The session cases of Swift's ImageImportTests, and the queue.
namespace {
const QString unreadable = QStringLiteral("The image could not be read. It may be damaged or unavailable.");
const QString unsupported = QStringLiteral("Choose a JPEG, PNG, HEIC, or TIFF image.");
const QString tooLarge = QStringLiteral("This import exceeds the current 100-megapixel document budget or 30,000-pixel side limit.");

// A 64 by 32 PNG, as Swift's fixture is.
QUrl fixture(const QTemporaryDir &folder, const QString &name = QStringLiteral("fixture.png"))
{
    QImage image(64, 32, QImage::Format_RGBA8888);
    image.fill(Qt::red);
    if (!image.save(folder.filePath(name), "PNG"))
        throw std::runtime_error("cannot write the fixture");
    return QUrl::fromLocalFile(folder.filePath(name));
}

// Runs the event loop until the request is in.
bool imported(EditorSession &session, const QList<QUrl> &urls, std::optional<QPointF> point = std::nullopt)
{
    bool done = false;
    session.importImages(urls, point, [&] { done = true; });
    return QTest::qWaitFor([&] { return done && !session.isImporting(); }, 10'000);
}

// The thread that logs each finished decode.
QThread *decodingThread = nullptr;
QtMessageHandler forward = nullptr;

void noteDecodingThread(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (message.startsWith("imported ") && !message.startsWith("imported a request"))
        decodingThread = QThread::currentThread();
    forward(type, context, message);
}

// Ends a session between two events of the loop.
class Reaper : public QObject {
public:
    std::unique_ptr<EditorSession> *victim = nullptr;

    bool event(QEvent *event) override
    {
        if (event->type() != QEvent::User)
            return QObject::event(event);
        victim->reset();
        return true;
    }
};

// Pixels the canvas holds without the memory for them.
ImportedImage hollow(int width, int height)
{
    return ImportedImage(std::make_shared<const RasterSnapshot>(width, height, QImage(), QRectF(), std::vector<BrushPatch>()), QImage(), "Hollow");
}
}

class ImageImportSessionTests : public QObject {
    Q_OBJECT
private slots:
    void importPlacementAndPartialFailure();
    void dropPositionUsesDocumentCoordinates();
    void queuedImportsAreNotLost();
    void aRequestIsOneUndoStep();
    void anEmptyRequestIsDoneAtOnce();
    void importsWaitWhileTheProjectIsBusy();
    void importingCommitsAnOpenTransform();
    void decodingRunsOffTheCallersThread();
    void aCallbackSeesItsRequestSettled();
    void aSessionMayEndWhileItsImportRuns();
    void aUrlThatIsNoFileIsUnsupported();
    void thePixelBudgetCountsWhatTheCanvasHolds();
};

void ImageImportSessionTests::importPlacementAndPartialFailure()
{
    QTemporaryDir folder;
    const QUrl url = fixture(folder);
    EditorSession session;
    QVERIFY(imported(session, {url, url}));
    QCOMPARE(session.document().value().size(), QSizeF(64, 32));
    QCOMPARE(int(session.document().value().layers.size()), 2);
    QCOMPARE(session.activeLayerID(), std::optional(session.document().value().layers.back().id));
    QCOMPARE(session.document().value().layers.back().name, QString("fixture"));
    session.createDocument(128, 128);
    const QUrl missing = QUrl::fromLocalFile(url.toLocalFile() + ".missing");
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("^cannot import .*fixture\\.png\\.missing: .*"));
    QVERIFY(imported(session, {url, missing}));
    QCOMPARE(session.document().value().size(), QSizeF(128, 128));
    QCOMPARE(int(session.document().value().layers.size()), 1);
    QCOMPARE(session.document().value().layers.front().transform.origin, QPointF(32, 48));
    QCOMPARE(session.importError(), std::optional("fixture.png.missing: " + unreadable));
    QVERIFY(!session.isImporting());
}

void ImageImportSessionTests::dropPositionUsesDocumentCoordinates()
{
    QTemporaryDir folder;
    const QUrl url = fixture(folder);
    EditorSession session;
    session.createDocument(1000, 800);
    const QSizeF size = session.document().value().size();
    session.viewport.resize(QSizeF(700, 500), 2, size);
    session.zoom(2.5);
    session.viewport.translate(QSizeF(70, -35));
    const QPointF location = session.viewport.viewPoint(QPointF(300, 250), size);
    const QPointF dropPoint = session.viewport.documentPoint(location, size);
    QVERIFY(imported(session, {url}, dropPoint));
    QCOMPARE(session.document().value().layers.front().transform.origin, QPointF(268, 234));
    QCOMPARE(session.document().value().size(), QSizeF(1000, 800));
}

void ImageImportSessionTests::queuedImportsAreNotLost()
{
    QTemporaryDir folder;
    const QUrl url = fixture(folder);
    EditorSession session;
    int finished = 0;
    QList<int> order;
    // Each request reports its layers and its undo steps.
    const auto report = [&](int request) {
        order << request << int(session.document().value().layers.size()) << session.history.undoCount();
        ++finished;
    };
    session.importImages({url, url}, QPointF(999, 999), [&] { report(1); });
    session.importImages({url}, std::nullopt, [&] { report(2); });
    QVERIFY(session.isImporting());
    QVERIFY(QTest::qWaitFor([&] { return finished == 2 && !session.isImporting(); }, 10'000));
    // First in, first done, each step recorded before its report.
    QCOMPARE(order, (QList<int>{1, 2, 1, 2, 3, 2}));
    QCOMPARE(int(session.document().value().layers.size()), 3);
    QCOMPARE(session.document().value().size(), QSizeF(64, 32));
    // No canvas yet: both ignore the drop point.
    QCOMPARE(session.document().value().layers[0].transform.origin, QPointF(0, 0));
    QCOMPARE(session.document().value().layers[1].transform.origin, QPointF(0, 0));
    QVERIFY(!session.isImporting());
    QCOMPARE(session.importError(), std::nullopt);
    // One step for each request.
    QCOMPARE(session.history.undoCount(), 2);
}

void ImageImportSessionTests::aRequestIsOneUndoStep()
{
    QTemporaryDir folder;
    const QUrl url = fixture(folder), other = fixture(folder, "other.png");
    EditorSession session;
    session.createDocument(200, 100);
    const std::optional<CanvasDocument> before = session.document();
    const int count = session.history.undoCount();
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression("^imported .*fixture\\.png.* 64 x 32$"));
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression("^imported .*other\\.png.* 64 x 32$"));
    QTest::ignoreMessage(QtInfoMsg, "imported a request of 2 files");
    QVERIFY(imported(session, {url, other}, QPointF(100, 50)));
    QCOMPARE(session.history.undoCount(), count + 1);
    QCOMPARE(session.history.undoName(), QString("Import Images"));
    QCOMPARE(int(session.document().value().layers.size()), 2);
    QCOMPARE(session.document().value().layers[0].name, QString("fixture"));
    QCOMPARE(session.document().value().layers[1].name, QString("other"));
    // Both land centred on the drop point.
    QCOMPARE(session.document().value().layers[0].transform.origin, QPointF(68, 34));
    QCOMPARE(session.document().value().layers[1].transform.origin, QPointF(68, 34));
    session.undo();
    QCOMPARE(session.document(), before);
}

void ImageImportSessionTests::anEmptyRequestIsDoneAtOnce()
{
    EditorSession session;
    QSignalSpy changes(&session, &EditorSession::changed);
    bool done = false;
    session.importImages({}, std::nullopt, [&] { done = true; });
    QVERIFY(done && !session.isImporting());
    QCOMPARE(int(changes.count()), 0);
    session.importImages({});
    QVERIFY(!session.isImporting());
    // Files may come without anyone waiting for them.
    QTemporaryDir folder;
    session.importImages({fixture(folder)});
    QVERIFY(QTest::qWaitFor([&] { return !session.isImporting(); }, 10'000));
    QTest::qWait(50);
    QCOMPARE(int(session.document().value().layers.size()), 1);
}

void ImageImportSessionTests::importsWaitWhileTheProjectIsBusy()
{
    QTemporaryDir folder;
    const QUrl url = fixture(folder), other = fixture(folder, "other.png");
    EditorSession session;
    session.setIsProjectBusy(true);
    int done = 0;
    session.importImages({url}, std::nullopt, [&] { ++done; });
    session.importImages({other}, std::nullopt, [&] { ++done; });
    QTest::qWait(100);
    QVERIFY(done == 0 && !session.isImporting() && !session.document().has_value());
    session.setIsProjectBusy(false);
    QVERIFY(QTest::qWaitFor([&] { return done == 2; }, 10'000));
    // Those that waited go in as they came.
    QCOMPARE(int(session.document().value().layers.size()), 2);
    QCOMPARE(session.document().value().layers[0].name, QString("fixture"));
    QCOMPARE(session.document().value().layers[1].name, QString("other"));
}

void ImageImportSessionTests::importingCommitsAnOpenTransform()
{
    QTemporaryDir folder;
    const QUrl url = fixture(folder);
    EditorSession session;
    QVERIFY(imported(session, {url}));
    const QUuid first = session.activeLayerID().value();
    session.beginTransform();
    session.nudgeLayer(7, 0);
    QVERIFY(session.transformEdit().has_value());
    QVERIFY(imported(session, {url}));
    QVERIFY(!session.transformEdit().has_value());
    QCOMPARE(session.document().value().layers.front().id, first);
    QCOMPARE(session.document().value().layers.front().transform.origin, QPointF(7, 0));
}

void ImageImportSessionTests::decodingRunsOffTheCallersThread()
{
    QTemporaryDir folder;
    const QUrl url = fixture(folder);
    EditorSession session;
    decodingThread = nullptr;
    forward = qInstallMessageHandler(noteDecodingThread);
    const bool finished = imported(session, {url});
    qInstallMessageHandler(forward);
    QVERIFY(finished);
    // The importer logs from the thread that decoded.
    QVERIFY(decodingThread != nullptr);
    QVERIFY(decodingThread != QThread::currentThread());
    QCOMPARE(session.thread(), QThread::currentThread());
}

void ImageImportSessionTests::aCallbackSeesItsRequestSettled()
{
    QTemporaryDir folder;
    const QUrl url = fixture(folder);
    const QUrl missing = QUrl::fromLocalFile(folder.filePath("missing.png"));
    EditorSession session;
    QStringList seen;
    const auto note = [&](const QString &request) {
        seen << QStringLiteral("%1: importing %2, steps %3, error %4").arg(request).arg(int(session.isImporting()))
                    .arg(session.history.undoCount()).arg(session.importError().value_or("none"));
    };
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("^cannot import .*missing\\.png: .*"));
    session.importImages({url}, std::nullopt, [&] { note("first"); });
    session.importImages({missing}, std::nullopt, [&] { note("last"); });
    QVERIFY(QTest::qWaitFor([&] { return seen.size() == 2; }, 10'000));
    // The first goes on while the next request decodes.
    QCOMPARE(seen[0], QString("first: importing 1, steps 1, error none"));
    // The last finds the import over and its error shown.
    QCOMPARE(seen[1], "last: importing 0, steps 1, error missing.png: " + unreadable);
}

void ImageImportSessionTests::aSessionMayEndWhileItsImportRuns()
{
    QTemporaryDir folder;
    const QUrl url = fixture(folder);
    bool done = false;
    {
        EditorSession session;
        session.importImages({url, url}, std::nullopt, [&] { done = true; });
        QVERIFY(session.isImporting());
    }
    // The worker ends alone; nothing calls back into the dead.
    QTest::qWait(300);
    QVERIFY(!done);
    // Nor does a callback already posted when the session ends.
    auto session = std::make_unique<EditorSession>();
    Reaper reaper;
    reaper.victim = &session;
    bool began = false, reaped = false;
    connect(session.get(), &EditorSession::changed, &reaper, [&] {
        began = began || session->isImporting();
        // The drain is over; its callback waits in the queue.
        if (began && !session->isImporting() && !reaped) {
            reaped = true;
            QCoreApplication::postEvent(&reaper, new QEvent(QEvent::User), Qt::HighEventPriority);
        }
    });
    session->importImages({url}, std::nullopt, [&] { done = true; });
    QVERIFY(QTest::qWaitFor([&] { return session == nullptr; }, 10'000));
    QTest::qWait(100);
    QVERIFY(reaped && !done);
}

void ImageImportSessionTests::aUrlThatIsNoFileIsUnsupported()
{
    QTemporaryDir folder;
    EditorSession session;
    const QUrl gone = QUrl::fromLocalFile(folder.filePath("gone.png"));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("^cannot import .*gone\\.png: .*"));
    QVERIFY(imported(session, {QUrl("https://example.invalid/picture.png"), gone}));
    QVERIFY(!session.document().has_value());
    // Each failure on its own, a blank line between.
    QCOMPARE(session.importError(), std::optional("picture.png: " + unsupported + "\n\n" + "gone.png: " + unreadable));
    QCOMPARE(session.history.undoCount(), 0);
    // The next import's error names its own failure alone.
    session.setImportError(std::nullopt);
    const QUrl lost = QUrl::fromLocalFile(folder.filePath("lost.png"));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("^cannot import .*lost\\.png: .*"));
    QVERIFY(imported(session, {lost}));
    QCOMPARE(session.importError(), std::optional("lost.png: " + unreadable));
}

void ImageImportSessionTests::thePixelBudgetCountsWhatTheCanvasHolds()
{
    QTemporaryDir folder;
    const QUrl url = fixture(folder);
    EditorSession session;
    // 100,000,000 less these leaves 2,048: the fixture exactly.
    session.insert(hollow(10'000, 9'999));
    session.insert(hollow(7'952, 1));
    session.addBlankLayer();
    QVERIFY(imported(session, {url}));
    QCOMPARE(int(session.document().value().layers.size()), 4);
    QCOMPARE(session.importError(), std::nullopt);
    // Full now: the next pixel is one too many.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("^cannot import .*fixture\\.png: .*"));
    QVERIFY(imported(session, {url}));
    QCOMPARE(int(session.document().value().layers.size()), 4);
    QCOMPARE(session.importError(), std::optional("fixture.png: " + tooLarge));
    QVERIFY(!session.document().value().layers[0].asset.value().raster->hasMaterializedPixels());
}

QTEST_GUILESS_MAIN(ImageImportSessionTests)
#include "ImageImportSessionTests.moc"
