#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include "SelectionFixtures.h"
#include "SessionFixtures.h"
#include <QPainter>
#include <QtTest>

// Remove Background in the session: a preview, then a mask.
namespace {
// A red disc on white, a subject the model finds.
std::unique_ptr<EditorSession> withDisc()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(400, 300);
    QImage image = BrushRaster::context(400, 300, false);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(217, 26, 26));
    painter.drawEllipse(QPointF(200, 150), 80, 80);
    painter.end();
    session->insert(ImportedImage(image, image, QStringLiteral("Disc")));
    return session;
}

bool settled(const EditorSession &session)
{
    return QTest::qWaitFor([&] { return session.filterEdit() && !session.filterEdit().value().preparing; }, 20000);
}

bool committed(EditorSession &session)
{
    bool done = false;
    session.commitFilter([&done] { done = true; });
    return QTest::qWaitFor([&done] { return done; }, 20000);
}

int at(const QImage &mask, int x, int y)
{
    return mask.constScanLine(y)[x];
}

QImage maskOf(const EditorSession &session, QUuid id)
{
    return layerWith(session, id).mask.value().asset.image();
}

QImage gray(int value)
{
    QImage image(400, 300, QImage::Format_Grayscale8);
    image.fill(value);
    return image;
}
}

class RemoveBackgroundTests : public QObject {
    Q_OBJECT
private slots:
    void thePreviewClearsTheBackgroundAndTheCommitMasksIt();
    void anExistingMaskAndASelectionAreKept();
    void aPlacedMaskGivesWayToOneInTheLayersGrid();
    void aMaskOfAnotherGridIsReplaced();
    void noSubjectCommitsNothing();
};

void RemoveBackgroundTests::thePreviewClearsTheBackgroundAndTheCommitMasksIt()
{
    const auto session = withDisc();
    const QUuid id = session->activeLayerID().value();
    const ImageLayer before = layerWith(*session, id);
    session->beginFilter(FilterKind::removeBackground);
    QVERIFY(settled(*session));
    // Previewed whole: the white round the disc goes clear.
    const FilterEdit &edit = session->filterEdit().value();
    QVERIFY(edit.previewSource.size() == QSize(400, 300) && !edit.previewError);
    const QImage preview = edit.previewImage(id).value();
    QVERIFY(preview.pixelColor(0, 0).alpha() == 0 && preview.pixelColor(200, 150) == before.asset->image().pixelColor(200, 150));
    const int steps = session->history.undoCount();
    QVERIFY(committed(*session));
    // The pixels stay; a mask hides the background instead.
    const ImageLayer after = layerWith(*session, id);
    QVERIFY(after.asset->identity() == before.asset->identity() && after.transform == before.transform);
    const LayerMask &mask = after.mask.value();
    QVERIFY(mask.isEnabled && !mask.placement && mask.isLinked);
    QVERIFY(mask.asset.image().format() == QImage::Format_Grayscale8 && mask.asset.size() == QSize(400, 300));
    QVERIFY(at(mask.asset.image(), 200, 150) == 255 && at(mask.asset.image(), 0, 0) == 0);
    // One step; the mask chosen, panel closed, project free.
    QVERIFY(session->history.undoCount() == steps + 1 && session->history.undoName() == "Remove Background");
    QVERIFY(session->isMaskSelected() && !session->filterEdit() && !session->isProjectBusy());
    session->undo();
    QVERIFY(!layerWith(*session, id).mask);
}

void RemoveBackgroundTests::anExistingMaskAndASelectionAreKept()
{
    const auto session = withDisc();
    const QUuid id = session->activeLayerID().value();
    rewrite(*session, [&](ProjectSnapshot &snapshot) {
        setMask(snapshot, id, LayerMask::assetFrom(gray(128)));
        record(snapshot, id).maskEnabled = false;
    });
    session->selectLayer(id);
    // The left half selected: only it changes.
    session->applySelection(rectPath(QRectF(0, 0, 200, 300)), SelectionMode::replace, QStringLiteral("Select"));
    session->beginFilter(FilterKind::removeBackground);
    QVERIFY(settled(*session) && committed(*session));
    const QImage mask = maskOf(*session, id);
    // Inside, both masks hide; outside, the old mask stays.
    QVERIFY(at(mask, 190, 150) == 128 && at(mask, 10, 10) == 0);
    QVERIFY(at(mask, 210, 150) == 128 && at(mask, 390, 10) == 128);
    // A disabled mask is multiplied in, then shown, as Swift's.
    QVERIFY(layerWith(*session, id).mask.value().isEnabled);
    // Without a selection, a white mask stands in for none.
    session->undo();
    rewrite(*session, [&](ProjectSnapshot &snapshot) { record(snapshot, id).maskFile = std::nullopt; });
    session->selectLayer(id);
    session->applySelection(rectPath(QRectF(0, 0, 200, 300)), SelectionMode::replace, QStringLiteral("Select"));
    session->beginFilter(FilterKind::removeBackground);
    QVERIFY(settled(*session) && committed(*session));
    const QImage fresh = maskOf(*session, id);
    QVERIFY(at(fresh, 190, 150) == 255 && at(fresh, 10, 10) == 0 && at(fresh, 390, 10) == 255 && at(fresh, 210, 150) == 255);
}

void RemoveBackgroundTests::aPlacedMaskGivesWayToOneInTheLayersGrid()
{
    const auto session = withDisc();
    const QUuid id = session->activeLayerID().value();
    LayerTransform placed = layerWith(*session, id).transform;
    placed.origin += QPointF(50, 40);
    rewrite(*session, [&](ProjectSnapshot &snapshot) {
        setMask(snapshot, id, LayerMask::assetFrom(gray(64)));
        record(snapshot, id).maskPlacement = placed;
        record(snapshot, id).maskLinked = false;
    });
    session->selectLayer(id);
    session->beginFilter(FilterKind::removeBackground);
    QVERIFY(settled(*session) && committed(*session));
    // The placed mask's pixels are not kept, nor its placement.
    const LayerMask mask = layerWith(*session, id).mask.value();
    QVERIFY(!mask.placement && !mask.isLinked && mask.isEnabled);
    QVERIFY(at(mask.asset.image(), 200, 150) == 255 && at(mask.asset.image(), 0, 0) == 0);
}

void RemoveBackgroundTests::aMaskOfAnotherGridIsReplaced()
{
    const auto session = withDisc();
    const QUuid id = session->activeLayerID().value();
    // A solid mask one pixel square covers the layer.
    QImage solid(1, 1, QImage::Format_Grayscale8);
    solid.fill(0);
    rewrite(*session, [&](ProjectSnapshot &snapshot) { setMask(snapshot, id, LayerMask::assetFrom(solid)); });
    session->selectLayer(id);
    session->beginFilter(FilterKind::removeBackground);
    QVERIFY(settled(*session) && committed(*session));
    const QImage mask = maskOf(*session, id);
    QVERIFY(mask.size() == QSize(400, 300) && at(mask, 200, 150) == 255 && at(mask, 0, 0) == 0);
}

void RemoveBackgroundTests::noSubjectCommitsNothing()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(64, 48);
    QImage flat = BrushRaster::context(64, 48, false);
    flat.fill(QColor(90, 140, 200));
    session->insert(ImportedImage(flat, flat, QStringLiteral("Flat")));
    const QUuid id = session->activeLayerID().value();
    session->beginFilter(FilterKind::removeBackground);
    QVERIFY(settled(*session));
    QCOMPARE(session->filterEdit().value().previewError.value(),
             QString("No foreground subject was detected in this layer. Try an image with a more distinct subject."));
    const int steps = session->history.undoCount();
    // OK does nothing; the panel stays for Cancel.
    QVERIFY(committed(*session));
    QVERIFY(!layerWith(*session, id).mask && session->history.undoCount() == steps && session->filterEdit());
    session->cancelFilter();
    QVERIFY(!session->filterEdit());
}

QTEST_GUILESS_MAIN(RemoveBackgroundTests)
#include "RemoveBackgroundTests.moc"
