#include "Document/BrushStroke.h"
#include "Document/ContentFill.h"
#include "SelectionFixtures.h"
#include "SessionRecord.h"
#include <QSemaphore>
#include <QThreadPool>
#include <QtConcurrent>

// Content-Aware Fill through the session; Subject Removal is 9.7's.
namespace {
const std::array<uchar, 4> red{255, 0, 0, 255}, blue{51, 153, 204, 255};

QImage picture(int width, int height, const std::function<std::array<uchar, 4>(int, int)> &colour)
{
    QImage image = BrushRaster::context(width, height, false);
    for (int y = 0; y < height; ++y) {
        uchar *row = image.scanLine(y);
        for (int x = 0; x < width; ++x)
            std::copy_n(colour(x, y).data(), 4, row + x * 4);
    }
    return image;
}

std::vector<int> pixel(const QImage &image, int x, int y)
{
    const uchar *at = image.constScanLine(y) + x * 4;
    return {at[0], at[1], at[2], at[3]};
}

bool within(const std::vector<int> &pixel, const std::array<uchar, 4> &expected, int tolerance)
{
    for (size_t i = 0; i < 4; ++i) {
        if (std::abs(pixel[i] - expected[i]) > tolerance)
            return false;
    }
    return true;
}

// Swift's fixture: a blue field with a red object, selected.
std::unique_ptr<EditorSession> fixture()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(64, 48, true);
    const QImage image = picture(64, 48, [](int x, int y) { return x >= 20 && x < 32 && y >= 16 && y < 26 ? red : blue; });
    session->insert(ImportedImage(image, image, "Object"));
    session->applySelection(rectPath(QRectF(20, 16, 12, 10)), SelectionMode::replace, "Select");
    return session;
}

bool committed(EditorSession &session)
{
    bool done = false;
    session.commitFilter([&] { done = true; });
    return QTest::qWaitFor([&] { return done; }, 20000);
}

bool prepared(EditorSession &session)
{
    return QTest::qWaitFor([&] { return session.filterEdit().has_value() && !session.filterEdit().value().preparing; }, 20000);
}

QImage active(const EditorSession &session)
{
    return session.activeLayer().value().asset.value().image();
}
}

class SmartEditTests : public QObject {
    Q_OBJECT
private slots:
    void fillContinuesRepeatingTexture();
    void fillReconstructsBackgroundInsideSelectionAndUndoes();
    void cancelAndNoSourceLeaveOriginalUntouched();
    void theFillGrowsTheLayerOverASelectionPastItsEdge();
    void aSolidPlacedOrDisabledMaskFollowsSwift();
    void growthKeepsTheLayersTurn();
    void commitsThatWaitForThePreviewEachResume();
    void aSoftEdgeBlendsTheFillInByItsCoverage();
    void theFillRefusesWhatSwiftRefuses();
    void aLayerChangedMeanwhileTakesNoResult();
    void filterCallsAnnounceAndRefuseInSilence();
    void theCanvasCounterMovesWhereSwiftsDoes();
};

void SmartEditTests::fillContinuesRepeatingTexture()
{
    EditorSession session;
    session.createDocument(80, 64, true);
    // Stripes four wide, a red block over them.
    const QImage image = picture(80, 64, [](int x, int y) {
        if (x >= 32 && x < 44 && y >= 26 && y < 36)
            return red;
        const uchar value = (x / 4) % 2 == 0 ? 51 : 204;
        return std::array<uchar, 4>{value, value, value, 255};
    });
    session.insert(ImportedImage(image, image, "Stripes"));
    session.applySelection(rectPath(QRectF(32, 26, 12, 10)), SelectionMode::replace, "Select");
    session.beginFilter(FilterKind::contentAwareFill);
    QVERIFY(committed(session));
    const QImage result = active(session);
    int matching = 0;
    for (int y = 26; y < 36; ++y) {
        for (int x = 32; x < 44; ++x)
            matching += std::abs(pixel(result, x, y)[0] - ((x / 4) % 2 == 0 ? 51 : 204)) <= 1;
    }
    QVERIFY2(matching >= 114, qPrintable(QStringLiteral("Matched %1 of 120 texture pixels").arg(matching)));
}

void SmartEditTests::fillReconstructsBackgroundInsideSelectionAndUndoes()
{
    const auto session = fixture();
    const ImageIdentity original = session->activeLayer().value().asset.value().identity();
    const int count = session->history.undoCount();
    QVERIFY(session->canContentAwareFill());
    session->beginFilter(FilterKind::contentAwareFill);
    QVERIFY(session->filterEdit().has_value() && session->filterEdit().value().preparing);
    QVERIFY(!session->filterEdit().value().grownTransform.has_value());
    QVERIFY(committed(*session));
    QVERIFY(!session->filterEdit().has_value() && !session->isProjectBusy());
    QCOMPARE(session->history.undoCount(), count + 1);
    QCOMPARE(session->history.undoName(), QString("Content-Aware Fill"));
    const QImage image = active(*session);
    QCOMPARE(image.size(), QSize(64, 48));
    for (int y = 0; y < 48; ++y) {
        for (int x = 0; x < 64; ++x)
            QVERIFY2(within(pixel(image, x, y), blue, 1), qPrintable(QStringLiteral("pixel %1,%2").arg(x).arg(y)));
    }
    QCOMPARE(session->activeLayer().value().name, QString("Object"));
    session->undo();
    QCOMPARE(session->activeLayer().value().asset.value().identity(), original);
}

void SmartEditTests::cancelAndNoSourceLeaveOriginalUntouched()
{
    const auto session = fixture();
    const ImageIdentity original = session->activeLayer().value().asset.value().identity();
    session->beginFilter(FilterKind::contentAwareFill);
    session->cancelFilter();
    QVERIFY(!session->filterEdit().has_value());
    QCOMPARE(session->activeLayer().value().asset.value().identity(), original);
    // Everything selected: no pixel to copy from.
    session->selectAll();
    session->beginFilter(FilterKind::contentAwareFill);
    QVERIFY(prepared(*session));
    QCOMPARE(session->filterEdit().value().previewError,
             std::optional(QString("Not enough unselected, opaque image pixels to synthesize a fill. Use a smaller selection with some surrounding image.")));
    QVERIFY(!session->filterEdit().value().previewImage(session->activeLayerID().value()).has_value());
    QVERIFY(committed(*session));
    QVERIFY(session->filterEdit().has_value());
    QCOMPARE(session->activeLayer().value().asset.value().identity(), original);
    session->cancelFilter();
    session->deselect();
    QVERIFY(!session->canContentAwareFill());
}

void SmartEditTests::theFillGrowsTheLayerOverASelectionPastItsEdge()
{
    EditorSession session;
    session.createDocument(60, 40, true);
    const QImage image = picture(20, 20, [](int, int) { return blue; });
    session.insert(ImportedImage(image, image, "Blue"), QPointF(20, 20));
    const QUuid id = session.activeLayerID().value();
    QCOMPARE(session.activeLayer().value().transform.origin, QPointF(10, 10));
    // White edges, a black block inside: the pixels must carry.
    QImage drawn = BrushRaster::context(20, 20, true);
    drawn.fill(Qt::white);
    for (int y = 5; y < 15; ++y)
        std::fill_n(drawn.scanLine(y) + 5, 10, uchar(0));
    rewrite(session, [&](ProjectSnapshot &snapshot) { setMask(snapshot, id, LayerMask::assetFrom(drawn)); });
    session.selectLayer(id);
    // Past the top left corner: 30 by 25 at (0,5).
    session.applySelection(rectPath(QRectF(-10, 5, 30, 10)), SelectionMode::replace, "Select");
    session.beginFilter(FilterKind::contentAwareFill);
    const FilterEdit &edit = session.filterEdit().value();
    QCOMPARE(edit.grownTransform, std::optional(LayerTransform{.origin = {0, 5}, .size = {30, 25}}));
    QCOMPARE(edit.grownImage.value().size(), QSize(30, 25));
    QCOMPARE(pixel(edit.grownImage.value(), 5, 3)[3], 0);
    QVERIFY(within(pixel(edit.grownImage.value(), 15, 10), blue, 0));
    // The preview sits on the grown grid, the mask stays.
    const ImageLayer layer = session.activeLayer().value();
    QCOMPARE(session.displayedTransform(layer), layer.transform);
    QVERIFY(prepared(session));
    QCOMPARE(session.displayedTransform(layer), edit.grownTransform.value());
    QCOMPARE(session.displayedMaskPlacement(layer), std::optional(layer.transform));
    QVERIFY(within(pixel(edit.previewImage(id).value(), 5, 3), blue, 1));
    QVERIFY(!edit.previewImage(QUuid::createUuid()).has_value());
    session.updateFilter(FilterSettings(), false);
    QCOMPARE(session.displayedTransform(layer), layer.transform);
    QVERIFY(!session.displayedMaskPlacement(layer).has_value());
    session.updateFilter(FilterSettings(), true);
    QVERIFY(committed(session));
    const ImageLayer grown = session.activeLayer().value();
    QCOMPARE(grown.transform, (LayerTransform{.origin = {0, 5}, .size = {30, 25}}));
    QCOMPARE(grown.asset.value().size(), QSize(30, 25));
    QVERIFY(within(pixel(grown.asset.value().image(), 5, 3), blue, 1));
    QCOMPARE(pixel(grown.asset.value().image(), 5, 22)[3], 0);
    // The mask covers the new grid, white past the edge.
    const QImage mask = grown.mask.value().asset.image();
    QCOMPARE(mask.size(), QSize(30, 25));
    QCOMPARE(int(mask.constScanLine(3)[5]), 255);
    QCOMPARE(int(mask.constScanLine(13)[18]), 0);
    QCOMPARE(int(mask.constScanLine(7)[12]), 255);
    QVERIFY(!grown.mask.value().placement.has_value());
    session.undo();
    QCOMPARE(session.activeLayer().value().transform.size, QSizeF(20, 20));
}

void SmartEditTests::aSolidPlacedOrDisabledMaskFollowsSwift()
{
    EditorSession session;
    session.createDocument(60, 40, true);
    const QImage image = picture(20, 20, [](int, int) { return blue; });
    session.insert(ImportedImage(image, image, "Blue"), QPointF(20, 20));
    const QUuid id = session.activeLayerID().value();
    const auto fill = [&] {
        session.selectLayerTarget(id, false);
        session.applySelection(rectPath(QRectF(25, 15, 20, 10)), SelectionMode::replace, "Select");
        session.beginFilter(FilterKind::contentAwareFill);
        return committed(session);
    };
    // A solid mask is left as it is.
    session.addMask(true);
    QVERIFY(fill());
    QCOMPARE(session.activeLayer().value().asset.value().size(), QSize(35, 20));
    QCOMPARE(session.activeLayer().value().mask.value().asset.size(), QSize(1, 1));
    session.undo();
    // Empty selection: nothing to fill.
    QImage white = BrushRaster::context(20, 20, true);
    white.fill(Qt::white);
    rewrite(session, [&](ProjectSnapshot &snapshot) {
        setMask(snapshot, id, LayerMask::assetFrom(white));
        record(snapshot, id).maskPlacement = LayerTransform{.origin = {40, 0}, .size = {20, 20}};
        record(snapshot, id).maskLinked = false;
    });
    QVERIFY(fill());
    QCOMPARE(session.activeLayer().value().mask.value().asset.size(), QSize(20, 20));
    QCOMPARE(session.activeLayer().value().mask.value().placement, std::optional(LayerTransform{.origin = {40, 0}, .size = {20, 20}}));
    session.undo();
    rewrite(session, [&](ProjectSnapshot &snapshot) {
        setMask(snapshot, id, LayerMask::assetFrom(white));
        record(snapshot, id).maskPlacement = std::nullopt;
        record(snapshot, id).maskLinked = std::nullopt;
        record(snapshot, id).maskEnabled = false;
    });
    QVERIFY(fill());
    QCOMPARE(session.activeLayer().value().mask.value().asset.size(), QSize(35, 20));
    QVERIFY(!session.activeLayer().value().mask.value().isEnabled);
}

void SmartEditTests::growthKeepsTheLayersTurn()
{
    EditorSession session;
    session.createDocument(60, 40, true);
    // A red block at pixel (2,2) on a turned layer.
    const QImage image = picture(20, 20, [](int x, int y) { return x >= 2 && x < 5 && y >= 2 && y < 5 ? red : blue; });
    session.insert(ImportedImage(image, image, "Turned"), QPointF(20, 20));
    const QUuid id = session.activeLayerID().value();
    rewrite(session, [&](ProjectSnapshot &snapshot) { record(snapshot, id).transform.rotation = 90; });
    session.selectLayer(id);
    const LayerTransform before = session.activeLayer().value().transform;
    const QPointF redAt = BrushRaster::pixelToDocument(before, 20, 20).map(QPointF(3.5, 3.5));
    session.applySelection(rectPath(QRectF(25, 15, 60, 10)), SelectionMode::replace, "Select");
    session.beginFilter(FilterKind::contentAwareFill);
    const FilterEdit &edit = session.filterEdit().value();
    QCOMPARE(edit.grownTransform.value().rotation, 90.0);
    QVERIFY(committed(session));
    const ImageLayer grown = session.activeLayer().value();
    QCOMPARE(grown.transform.rotation, 90.0);
    QVERIFY(grown.transform.size.width() > 20 || grown.transform.size.height() > 20);
    // The block sits where it sat on the document.
    const QSize size = grown.asset.value().size();
    const QPointF pixel = BrushRaster::pixelToDocument(grown.transform, size.width(), size.height()).inverted().map(redAt);
    QVERIFY(within(::pixel(grown.asset.value().image(), int(std::floor(pixel.x())), int(std::floor(pixel.y()))), red, 0));
}

void SmartEditTests::commitsThatWaitForThePreviewEachResume()
{
    const auto session = fixture();
    const int count = session->history.undoCount();
    // Two wait; the first commits, the second finds it committing.
    session->beginFilter(FilterKind::contentAwareFill);
    bool first = false, second = false;
    session->commitFilter([&] { first = true; });
    session->commitFilter([&] { second = true; });
    QTRY_VERIFY(first && second);
    QCOMPARE(session->history.undoCount(), count + 1);
    QVERIFY(!session->filterEdit().has_value());
    session->undo();
    // A waiter whose edit was cancelled returns without applying.
    session->beginFilter(FilterKind::contentAwareFill);
    bool third = false;
    session->commitFilter([&] { third = true; });
    session->cancelFilter();
    session->beginFilter(FilterKind::contentAwareFill);
    QTRY_VERIFY(third);
    QVERIFY(session->filterEdit().has_value() && !session->filterEdit().value().committing);
    QCOMPARE(session->history.undoCount(), count);
    QVERIFY(prepared(*session));
    QVERIFY(session->filterEdit().has_value());
    session->cancelFilter();
    // Done but undelivered: it holds the next job, and commits.
    session->beginFilter(FilterKind::contentAwareFill);
    QThreadPool::globalInstance()->waitForDone();
    QVERIFY(session->filterEdit().value().preparing);
    session->updateFilter(FilterSettings(), true);
    QVERIFY(session->filterEdit().value().pending.has_value());
    bool fourth = false;
    session->commitFilter([&] { fourth = true; });
    QTRY_VERIFY(fourth);
    QCOMPARE(session->history.undoCount(), count + 1);
    QVERIFY(!session->filterEdit().has_value());
    session->undo();
    // Preview off while the job waits: it still runs.
    QThreadPool &pool = *QThreadPool::globalInstance();
    const int threads = pool.maxThreadCount();
    pool.setMaxThreadCount(1);
    QSemaphore gate;
    const QFuture<void> held = QtConcurrent::run([&] { gate.acquire(); });
    session->beginFilter(FilterKind::contentAwareFill);
    session->cancelFilter();
    session->beginFilter(FilterKind::contentAwareFill);
    QVERIFY(session->filterEdit().value().pending.has_value());
    session->updateFilter(FilterSettings(), false);
    QVERIFY(session->filterEdit().value().pending.has_value() && session->filterEdit().value().preparing);
    gate.release();
    pool.setMaxThreadCount(threads);
    QVERIFY(prepared(*session));
    QVERIFY(!session->filterEdit().value().previewError.has_value());
    QVERIFY(!session->filterEdit().value().previewImage(session->activeLayerID().value()).has_value());
    bool fifth = false;
    session->commitFilter([&] { fifth = true; });
    QTRY_VERIFY(fifth);
    QCOMPARE(session->history.undoCount(), count + 1);
}

void SmartEditTests::aSoftEdgeBlendsTheFillInByItsCoverage()
{
    const auto session = fixture();
    // Half a pixel in: the left column is half filled.
    session->applySelection(rectPath(QRectF(20.5, 16, 11.5, 10)), SelectionMode::replace, "Select");
    session->beginFilter(FilterKind::contentAwareFill);
    QVERIFY(committed(*session));
    const QImage image = active(*session);
    QVERIFY(within(pixel(image, 25, 20), blue, 1));
    QVERIFY(within(pixel(image, 20, 20), {153, 77, 102, 255}, 2));
    QVERIFY(within(pixel(image, 19, 20), blue, 1));
}

void SmartEditTests::theFillRefusesWhatSwiftRefuses()
{
    const auto session = fixture();
    const QImage image = active(*session);
    QVERIFY_THROWS_EXCEPTION(ContentFillError, ContentFill::run(FilterJob{FilterKind::contentAwareFill, image, FilterSettings(), 1, std::nullopt, QTransform()}));
    QCOMPARE(QString::fromUtf8(ContentFillError(ContentFillError::Kind::noSource).what()),
             QString("Not enough unselected, opaque image pixels to synthesize a fill. Use a smaller selection with some surrounding image."));
    QCOMPARE(rawValue(FilterKind::contentAwareFill), QString("Content-Aware Fill"));
    QVERIFY(isAutomatic(FilterKind::contentAwareFill));
    // A mask target, a hidden layer, a busy project: no.
    const QUuid id = session->activeLayerID().value();
    session->addMask(true);
    session->applySelection(rectPath(QRectF(20, 16, 12, 10)), SelectionMode::replace, "Select");
    session->selectLayerTarget(id, true);
    QVERIFY(!session->canContentAwareFill());
    session->selectLayerTarget(id, false);
    QVERIFY(session->canContentAwareFill());
    // An empty selection has nothing to fill.
    session->applySelection(rectPath(QRectF(0, 0, 64, 48)), SelectionMode::subtract, "Subtract");
    QVERIFY(session->selection().value().isEmpty() && !session->canContentAwareFill());
    session->applySelection(rectPath(QRectF(20, 16, 12, 10)), SelectionMode::replace, "Select");
    QVERIFY(session->canContentAwareFill());
    session->toggleLayerVisibility(id);
    QVERIFY(!session->canContentAwareFill());
    session->toggleLayerVisibility(id);
    session->setIsProjectBusy(true);
    QVERIFY(!session->canContentAwareFill());
    session->setIsProjectBusy(false);
    // A blank layer and a folder have no pixels.
    session->addBlankLayer();
    QVERIFY(!session->canContentAwareFill());
    session->groupSelectedLayers();
    QVERIFY(session->activeLayer().value().isGroup && !session->canContentAwareFill());
    session->selectLayer(id);
    QVERIFY(session->canContentAwareFill());
    // Beginning drops an open outline and commits a transform.
    session->selectTool(NavigationTool::lasso);
    session->setLassoKind(LassoKind::polygonal);
    session->beginLasso(QPointF(1, 1), SelectionMode::add);
    QVERIFY(session->lassoDraft().has_value());
    session->beginFilter(FilterKind::contentAwareFill);
    QVERIFY(session->filterEdit().has_value() && !session->lassoDraft().has_value());
    session->cancelFilter();
    session->selectTool(NavigationTool::move);
    session->beginTransform();
    session->previewTransform(LayerTransform{.origin = {1, 0}, .size = {64, 48}});
    QVERIFY(session->transformEdit().has_value());
    session->beginFilter(FilterKind::contentAwareFill);
    QVERIFY(session->filterEdit().has_value() && !session->transformEdit().has_value());
    QCOMPARE(session->activeLayer().value().transform.origin, QPointF(1, 0));
    session->cancelFilter();
    // Open edit: the layers close, a second begin is refused.
    session->beginFilter(FilterKind::contentAwareFill);
    QVERIFY(session->filterEdit().has_value() && !session->canEditLayers() && !session->canContentAwareFill());
    session->beginFilter(FilterKind::contentAwareFill);
    QVERIFY(prepared(*session));
    session->cancelFilter();
    QVERIFY(session->canEditLayers());
}

void SmartEditTests::aLayerChangedMeanwhileTakesNoResult()
{
    const auto session = fixture();
    const QUuid id = session->activeLayerID().value();
    const ImageIdentity original = session->activeLayer().value().asset.value().identity();
    session->beginFilter(FilterKind::contentAwareFill);
    QVERIFY(prepared(*session));
    // Moved meanwhile: the result has no layer to land on.
    rewrite(*session, [&](ProjectSnapshot &snapshot) { record(snapshot, id).transform.origin = QPointF(1, 1); });
    QVERIFY(session->filterEdit().has_value());
    QVERIFY(committed(*session));
    QVERIFY(!session->filterEdit().has_value() && !session->isProjectBusy());
    QCOMPARE(session->history.undoCount(), 0);
    QCOMPARE(session->activeLayer().value().asset.value().identity(), original);
    QCOMPARE(session->activeLayer().value().transform.origin, QPointF(1, 1));
    // Other pixels meanwhile: the same.
    session->applySelection(rectPath(QRectF(21, 17, 12, 10)), SelectionMode::replace, "Select");
    session->beginFilter(FilterKind::contentAwareFill);
    QVERIFY(prepared(*session));
    const QImage other = picture(64, 48, [](int, int) { return red; });
    rewrite(*session, [&](ProjectSnapshot &snapshot) { snapshot.images.insert_or_assign(id, ImportedImage(other, other, "Red")); });
    QVERIFY(committed(*session));
    QVERIFY(!session->filterEdit().has_value());
    QCOMPARE(session->history.undoCount(), 0);
    QVERIFY(within(pixel(active(*session), 25, 20), red, 0));
}

void SmartEditTests::filterCallsAnnounceAndRefuseInSilence()
{
    const auto session = fixture();
    QSignalSpy changes(session.get(), &EditorSession::changed);
    const auto announced = [&](const std::function<void()> &change) {
        const qsizetype before = changes.count();
        change();
        return changes.count() > before;
    };
    QVERIFY(!announced([&] { session->updateFilter(FilterSettings(), false); }));
    QVERIFY(!announced([&] { session->cancelFilter(); }));
    QVERIFY(!announced([&] { session->commitFilter(); }));
    QVERIFY(announced([&] { session->beginFilter(FilterKind::contentAwareFill); }));
    QVERIFY(!announced([&] { session->beginFilter(FilterKind::contentAwareFill); }));
    QVERIFY(announced([&] { session->updateFilter(FilterSettings(), false); }));
    QVERIFY(announced([&] { session->cancelFilter(); }));
    QVERIFY(!announced([&] { session->cancelFilter(); }));
    // The last signal of a change sees what it leaves.
    QStringList seen;
    connect(session.get(), &EditorSession::changed, session.get(), [&] { seen = described(*session); });
    session->beginFilter(FilterKind::contentAwareFill);
    QCOMPARE(seen, described(*session));
    QVERIFY(prepared(*session));
    QCOMPARE(seen, described(*session));
    session->updateFilter(FilterSettings(), false);
    QCOMPARE(seen, described(*session));
    QVERIFY(committed(*session));
    QCOMPARE(seen, described(*session));
    QVERIFY(!session->filterEdit().has_value());
}

void SmartEditTests::theCanvasCounterMovesWhereSwiftsDoes()
{
    const auto session = fixture();
    const int revision = session->brushRevision();
    session->beginFilter(FilterKind::contentAwareFill);
    QCOMPARE(session->brushRevision(), revision);
    // Preview off while the fill prepares; then the delivery.
    session->updateFilter(FilterSettings(), false);
    QCOMPARE(session->brushRevision(), revision + 1);
    QVERIFY(prepared(*session));
    QCOMPARE(session->brushRevision(), revision + 2);
    // A made fill shows again at once.
    session->updateFilter(FilterSettings(), true);
    QCOMPARE(session->brushRevision(), revision + 3);
    session->cancelFilter();
    QCOMPARE(session->brushRevision(), revision + 4);
    session->beginFilter(FilterKind::contentAwareFill);
    QVERIFY(prepared(*session));
    QVERIFY(committed(*session));
    QCOMPARE(session->brushRevision(), revision + 6);
    // A refused commit leaves it.
    session->selectAll();
    session->beginFilter(FilterKind::contentAwareFill);
    QVERIFY(prepared(*session) && session->filterEdit().value().previewError.has_value());
    const int failed = session->brushRevision();
    QVERIFY(committed(*session));
    QCOMPARE(session->brushRevision(), failed);
}

QTEST_GUILESS_MAIN(SmartEditTests)
#include "SmartEditTests.moc"
