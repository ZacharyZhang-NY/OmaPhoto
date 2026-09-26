#include "Rendering/RasterSnapshot.h"
#include "SessionFixtures.h"
#include "SessionRecord.h"
#include <QAbstractButton>
#include <QApplication>
#include <QMessageBox>
#include <QSemaphore>
#include <QThreadPool>
#include <QTimer>
#include <QtTest>

// Deleting a base others clip to: the alert's three answers.
namespace {
struct Asked {
    int times = 0;
    QMessageBox::Icon icon = QMessageBox::NoIcon;
    QString text;
    QString details;
    QStringList buttons;
};

// Clicks `answer` on the alert once it is up.
void answer(Asked &asked, const QString &reply)
{
    auto *timer = new QTimer(qApp);
    QObject::connect(timer, &QTimer::timeout, qApp, [&asked, reply, timer] {
        auto *alert = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!alert)
            return;
        timer->deleteLater();
        asked.times += 1;
        asked.icon = alert->icon();
        asked.text = alert->text();
        asked.details = alert->informativeText();
        for (const QAbstractButton *button : alert->buttons())
            asked.buttons << button->text();
        for (QAbstractButton *button : alert->buttons()) {
            if (button->text() == reply)
                button->click();
        }
    });
    timer->start(0);
}

QImage solid(QColor colour, int alpha)
{
    QImage image(2, 2, QImage::Format_RGBA8888_Premultiplied);
    colour.setAlpha(alpha);
    image.fill(colour);
    return image;
}

// Blue, opaque, clipped to a half-clear red base.
std::unique_ptr<EditorSession> clipped(QUuid &base, QUuid &top)
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(2, 2);
    session->insert(ImportedImage(solid(Qt::red, 128), QImage(), "Base"));
    base = session->activeLayerID().value();
    session->insert(ImportedImage(solid(Qt::blue, 255), QImage(), "Top"));
    top = session->activeLayerID().value();
    session->toggleClippingMask(top);
    return session;
}

int alphaOf(const EditorSession &session, QUuid id)
{
    return layerWith(session, id).asset.value().image().pixelColor(0, 0).alpha();
}
}

class LiveMaskDeleteTests : public QObject {
    Q_OBJECT
private slots:
    void aLayerNothingDependsOnGoesUnasked();
    void removingLinksDeletesAndRevealsThePixels();
    void cancelChangesNothing();
    void bakingKeepsTheLookInTheDependentsPixels();
    void theBakeQueuesForAWorkerAndTheCallerGoesOn();
    void severalLayersAreAskedAboutOnce();
    void aBakeThatFailsDeletesNothingAndSaysWhy();
};

void LiveMaskDeleteTests::aLayerNothingDependsOnGoesUnasked()
{
    QUuid base, top;
    const std::unique_ptr<EditorSession> session = clipped(base, top);
    // The clipped layer supplies no mask: no question.
    QVERIFY(!session->deleteWithLiveMaskChoice({top}));
    QCOMPARE(int(session->document().value().layers.size()), 2);
    session->deleteLayer(top);
    QCOMPARE(int(session->document().value().layers.size()), 1);
    QCOMPARE(session->history.undoName(), QString("Delete Layer"));
    // Without a canvas nothing depends on anything.
    EditorSession empty;
    QVERIFY(!empty.deleteWithLiveMaskChoice({top}));
}

void LiveMaskDeleteTests::removingLinksDeletesAndRevealsThePixels()
{
    QUuid base, top;
    const std::unique_ptr<EditorSession> session = clipped(base, top);
    const ImageIdentity pixels = layerWith(*session, top).asset.value().identity();
    const int count = session->history.undoCount();
    Asked asked;
    answer(asked, "Remove Links and Delete");
    session->deleteLayer(base);
    QCOMPARE(asked.times, 1);
    QCOMPARE(asked.icon, QMessageBox::Warning);
    QCOMPARE(asked.text, QString("This layer supplies a live mask"));
    QCOMPARE(asked.details, QString("Bake keeps the current masked appearance in the dependent layers’ pixels. "
                                    "Remove Links reveals their pixels. You can undo either choice."));
    QCOMPARE(asked.buttons, (QStringList{"Bake and Delete", "Cancel", "Remove Links and Delete"}));
    QCOMPARE(int(session->document().value().layers.size()), 1);
    QCOMPARE(layerWith(*session, top).maskSourceID, std::nullopt);
    QVERIFY(layerWith(*session, top).asset.value().identity() == pixels);
    QVERIFY(!session->isProjectBusy());
    QCOMPARE(session->history.undoCount(), count + 1);
    session->undo();
    QCOMPARE(layerWith(*session, top).maskSourceID, std::optional(base));
}

void LiveMaskDeleteTests::cancelChangesNothing()
{
    QUuid base, top;
    const std::unique_ptr<EditorSession> session = clipped(base, top);
    const std::optional<CanvasDocument> before = session->document();
    const int count = session->history.undoCount();
    Asked asked;
    answer(asked, "Cancel");
    session->deleteLayer(base);
    QCOMPARE(asked.times, 1);
    QCOMPARE(session->document(), before);
    QCOMPARE(session->history.undoCount(), count);
    QVERIFY(!session->isProjectBusy());
}

void LiveMaskDeleteTests::bakingKeepsTheLookInTheDependentsPixels()
{
    QUuid base, top;
    const std::unique_ptr<EditorSession> session = clipped(base, top);
    const ImageIdentity pixels = layerWith(*session, top).asset.value().identity();
    QCOMPARE(alphaOf(*session, top), 255);
    const int count = session->history.undoCount();
    Asked asked;
    QStringList seen;
    bool freeTooSoon = false;
    connect(session.get(), &EditorSession::changed, this, [&] {
        seen = described(*session);
        // Free again only once the layers are gone.
        freeTooSoon = freeTooSoon || (asked.times == 1 && !session->isProjectBusy() && session->document().value().layers.size() == 2);
    });
    answer(asked, "Bake and Delete");
    QTest::ignoreMessage(QtInfoMsg, "baking 1 live masks before a deletion");
    session->deleteLayer(base);
    QCOMPARE(asked.times, 1);
    QCOMPARE(seen, described(*session));
    QVERIFY(seen.contains("flags 1000"));
    // The bake runs on a worker; the project is busy.
    QVERIFY(session->isProjectBusy());
    QCOMPARE(int(session->document().value().layers.size()), 2);
    QVERIFY(QTest::qWaitFor([&] { return !session->isProjectBusy(); }, 10'000));
    // The last signal sees the deletion and a free project.
    QCOMPARE(seen, described(*session));
    QVERIFY(!freeTooSoon);
    QVERIFY(seen.contains("flags 0000"));
    QCOMPARE(int(session->document().value().layers.size()), 1);
    QCOMPARE(layerWith(*session, top).maskSourceID, std::nullopt);
    QVERIFY(layerWith(*session, top).asset.value().identity() != pixels);
    QCOMPARE(layerWith(*session, top).asset.value().name, QString("Top"));
    // Half the base's alpha is in the pixels now.
    QVERIFY2(std::abs(alphaOf(*session, top) - 128) <= 1, qPrintable(QString::number(alphaOf(*session, top))));
    QCOMPARE(layerWith(*session, top).asset.value().image().pixelColor(0, 0).blue(), 255);
    QCOMPARE(session->brushError(), std::nullopt);
    QCOMPARE(session->history.undoCount(), count + 1);
    QCOMPARE(session->history.undoName(), QString("Delete Layer"));
    // Undo brings back the base, the link, the old pixels.
    session->undo();
    QCOMPARE(int(session->document().value().layers.size()), 2);
    QCOMPARE(layerWith(*session, top).maskSourceID, std::optional(base));
    QVERIFY(layerWith(*session, top).asset.value().identity() == pixels);
}

void LiveMaskDeleteTests::theBakeQueuesForAWorkerAndTheCallerGoesOn()
{
    // A painted base: its pixels flatten on first use.
    const auto raster = std::make_shared<const RasterSnapshot>(2, 2, solid(Qt::red, 128), QRectF(0, 0, 2, 2), std::vector<BrushPatch>{});
    EditorSession session;
    session.createDocument(2, 2);
    session.insert(ImportedImage(raster, QImage(), "Base"));
    const QUuid base = session.activeLayerID().value();
    session.insert(ImportedImage(solid(Qt::blue, 255), QImage(), "Top"));
    const QUuid top = session.activeLayerID().value();
    session.toggleClippingMask(top);
    // Every worker is held: the bake has to queue.
    QThreadPool *pool = QThreadPool::globalInstance();
    const int workers = pool->maxThreadCount();
    QSemaphore held, gate;
    // A failed check must not strand the held workers.
    const auto letGo = qScopeGuard([&] {
        gate.release(workers);
        pool->waitForDone();
    });
    for (int index = 0; index < workers; ++index) {
        pool->start([&] {
            held.release();
            gate.acquire();
        });
    }
    QVERIFY(held.tryAcquire(workers, 10'000));
    Asked asked;
    answer(asked, "Bake and Delete");
    QTest::ignoreMessage(QtInfoMsg, "baking 1 live masks before a deletion");
    session.deleteLayer(base);
    // Back at once: busy, nothing flattened, nothing deleted.
    QCOMPARE(asked.times, 1);
    QVERIFY(session.isProjectBusy());
    QVERIFY(!raster->hasMaterializedPixels());
    gate.release(workers);
    // No event runs in this wait: a worker flattens.
    QElapsedTimer clock;
    clock.start();
    while (!raster->hasMaterializedPixels() && clock.elapsed() < 10'000)
        QTest::qSleep(5);
    QVERIFY(raster->hasMaterializedPixels());
    QVERIFY(session.isProjectBusy());
    QCOMPARE(int(session.document().value().layers.size()), 2);
    QVERIFY(QTest::qWaitFor([&] { return !session.isProjectBusy(); }, 10'000));
    QCOMPARE(int(session.document().value().layers.size()), 1);
    QVERIFY2(std::abs(alphaOf(session, top) - 128) <= 1, qPrintable(QString::number(alphaOf(session, top))));
}

void LiveMaskDeleteTests::severalLayersAreAskedAboutOnce()
{
    QUuid base, top;
    const std::unique_ptr<EditorSession> session = clipped(base, top);
    session->selectLayer(std::nullopt);
    session->insert(ImportedImage(solid(Qt::green, 255), QImage(), "Other"));
    const QUuid other = session->activeLayerID().value();
    session->selectLayers({base, other}, other);
    Asked asked;
    answer(asked, "Remove Links and Delete");
    session->deleteSelectedLayers();
    QCOMPARE(asked.times, 1);
    QCOMPARE(asked.text, QString("These layers supply live masks"));
    QCOMPARE(int(session->document().value().layers.size()), 1);
    QCOMPARE(session->history.undoName(), QString("Delete Layers"));
    QCOMPARE(layerWith(*session, top).maskSourceID, std::nullopt);
    // A folder that holds the base counts as supplying it.
    session->undo();
    session->selectLayers({base}, base);
    session->groupSelectedLayers();
    const QUuid folder = session->activeLayerID().value();
    QVERIFY(session->linkMask(base, top));
    Asked again;
    answer(again, "Cancel");
    session->deleteLayer(folder);
    QCOMPARE(again.times, 1);
    QCOMPARE(again.text, QString("This layer supplies a live mask"));
    QCOMPARE(int(session->document().value().layers.size()), 4);
    // A folder holding base and dependent leaves nobody behind.
    QVERIFY(session->placeLayer(top, folder));
    QVERIFY(session->linkMask(base, top));
    QVERIFY(!session->deleteWithLiveMaskChoice({folder}));
    session->deleteLayer(folder);
    QCOMPARE(int(session->document().value().layers.size()), 1);
}

void LiveMaskDeleteTests::aBakeThatFailsDeletesNothingAndSaysWhy()
{
    EditorSession session;
    session.createDocument(2, 2);
    session.insert(ImportedImage(solid(Qt::red, 128), QImage(), "Base"));
    const QUuid base = session.activeLayerID().value();
    // An asset without pixels: no surface can hold it.
    session.insert(ImportedImage(QImage(), QImage(), "Void"));
    const QUuid hollow = session.activeLayerID().value();
    QVERIFY(session.linkMask(base, hollow));
    const std::optional<CanvasDocument> before = session.document();
    Asked asked;
    answer(asked, "Bake and Delete");
    QTest::ignoreMessage(QtInfoMsg, "baking 1 live masks before a deletion");
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("^cannot bake the live masks: .*"));
    session.deleteLayer(base);
    QVERIFY(QTest::qWaitFor([&] { return !session.isProjectBusy(); }, 30'000));
    QCOMPARE(session.document(), before);
    QCOMPARE(session.brushError(), std::optional(QString("The canvas could not be rendered. Try a smaller canvas.")));
    session.setBrushError(std::nullopt);
    QCOMPARE(session.brushError(), std::nullopt);
}

QTEST_MAIN(LiveMaskDeleteTests)
#include "LiveMaskDeleteTests.moc"
