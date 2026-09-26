#include "CanvasFixtures.h"
#include "Rendering/TransformOverlay.h"

// The Move tool's box: its handles, hits, cursors and drawing.
namespace {
bool near(QPointF a, QPointF b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y()) < 0.0001;
}
}

class TransformOverlayTests : public QObject {
    Q_OBJECT
private slots:
    void hoverRegionsMatchRotatedEdgesCornersAndRotationHandle();
    void documentMappingAndRotatedHitTesting();
    void resizeCursorsTurnWithTheBox();
    void theBoxShowsForAMovableLayerAndHidesWithTheControls();
    void handlesAndGuidesArePaintedWhereTheyBelong();
    void anotherLayersHandlesRepaintOnlyTheirRects();
};

namespace {
// Every paint's region, to see how much a change repaints.
struct PaintSpy : QObject {
    std::vector<QRect> rects;
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::Paint)
            rects.push_back(static_cast<QPaintEvent *>(event)->region().boundingRect());
        return false;
    }
};
}

void TransformOverlayTests::hoverRegionsMatchRotatedEdgesCornersAndRotationHandle()
{
    CanvasViewport viewport;
    const QSizeF size(1000, 800);
    viewport.resize(QSizeF(1000, 800), 1, size);
    const LayerTransform transform{.origin = {100, 100}, .size = {400, 300}, .rotation = 37};
    const TransformOverlayGeometry geometry(transform, viewport, size);
    for (const auto &[start, end, expected] : {std::tuple(0, 2, 1), std::tuple(2, 4, 3), std::tuple(4, 6, 5), std::tuple(6, 0, 7)}) {
        const QPointF a = geometry.handles[size_t(start)], b = geometry.handles[size_t(end)];
        const QPointF point(a.x() * 0.75 + b.x() * 0.25, a.y() * 0.75 + b.y() * 0.25);
        const TransformDrag::Mode hit = geometry.hit(point).value();
        QVERIFY(hit.kind == TransformDrag::Kind::resize);
        QCOMPARE(hit.index, expected);
    }
    for (const int index : {0, 2, 4, 6}) {
        const TransformDrag::Mode hit = geometry.hit(geometry.handles[size_t(index)]).value();
        QVERIFY(hit.kind == TransformDrag::Kind::resize);
        QCOMPARE(hit.index, index);
    }
    QVERIFY(geometry.hit(geometry.rotationHandle).value().kind == TransformDrag::Kind::rotate);
    QVERIFY(!geometry.hit(viewport.viewPoint(transform.center(), size)).has_value());
    // The grip stands 28 points off the top edge, turned.
    const double radians = transform.radians();
    QVERIFY(near(geometry.rotationHandle, geometry.handles[1] + QPointF(std::sin(radians) * 28, -std::cos(radians) * 28)));
}

void TransformOverlayTests::documentMappingAndRotatedHitTesting()
{
    CanvasViewport viewport;
    const QSizeF size(1000, 800);
    viewport.resize(QSizeF(900, 600), 2, size);
    viewport.setZoom(1.5, QPointF(300, 200), size);
    viewport.translate(QSizeF(57, -30));
    const LayerTransform transform{.origin = {100, 200}, .size = {100, 50}, .rotation = 90};
    const TransformOverlayGeometry geometry(transform, viewport, size);
    QVERIFY(near(viewport.documentPoint(geometry.handles[4], size), transform.point(QPointF(1, 1))));
    QVERIFY(geometry.hit(geometry.rotationHandle).value().kind == TransformDrag::Kind::rotate);
    for (size_t index = 0; index < geometry.handles.size(); ++index) {
        const TransformDrag::Mode hit = geometry.hit(geometry.handles[index]).value();
        QVERIFY(hit.kind == TransformDrag::Kind::resize);
        QCOMPARE(hit.index, int(index));
    }
    QVERIFY(!geometry.hit(viewport.viewPoint(transform.center(), size)).has_value());
}

void TransformOverlayTests::resizeCursorsTurnWithTheBox()
{
    CanvasViewport viewport;
    const QSizeF size(400, 300);
    viewport.resize(QSizeF(400, 300), 1, size);
    const TransformOverlayGeometry upright({.origin = {100, 100}, .size = {100, 50}}, viewport, size);
    QCOMPARE(upright.resizeCursor(3), Qt::SizeHorCursor);
    QCOMPARE(upright.resizeCursor(7), Qt::SizeHorCursor);
    QCOMPARE(upright.resizeCursor(1), Qt::SizeVerCursor);
    QCOMPARE(upright.resizeCursor(5), Qt::SizeVerCursor);
    QCOMPARE(upright.resizeCursor(0), Qt::SizeFDiagCursor);
    QCOMPARE(upright.resizeCursor(4), Qt::SizeFDiagCursor);
    QCOMPARE(upright.resizeCursor(2), Qt::SizeBDiagCursor);
    QCOMPARE(upright.resizeCursor(6), Qt::SizeBDiagCursor);
    // Ten points reach a handle; more do not.
    QCOMPARE(upright.hit(upright.handles[0] + QPointF(-9.5, 0)).value().index, 0);
    QVERIFY(!upright.hit(upright.handles[0] + QPointF(-10.5, 0)).has_value());
    QVERIFY(!upright.hit(upright.handles[0] + QPointF(-10.5, 0.5)).has_value());
    // A quarter turn swaps the edges' cursors.
    const TransformOverlayGeometry turned({.origin = {100, 100}, .size = {100, 50}, .rotation = 90}, viewport, size);
    QCOMPARE(turned.resizeCursor(3), Qt::SizeVerCursor);
    QCOMPARE(turned.resizeCursor(1), Qt::SizeHorCursor);
    QCOMPARE(turned.resizeCursor(0), Qt::SizeBDiagCursor);
}

void TransformOverlayTests::theBoxShowsForAMovableLayerAndHidesWithTheControls()
{
    Shown shown;
    EditorSession &session = shown.session;
    const TransformOverlay overlay(session);
    QVERIFY(!overlay.geometry().has_value());
    session.addBlankLayer();
    QVERIFY(!session.canTransform());
    QVERIFY(!overlay.geometry().has_value());
    session.insert(filled(20, 20, qRgba(255, 0, 0, 255), "Red"));
    const QUuid red = session.activeLayerID().value();
    QVERIFY(session.canTransform());
    QVERIFY(overlay.geometry().has_value());
    QCOMPARE(overlay.geometry().value(), TransformOverlayGeometry(layerWith(session, red).transform, session.viewport, shown.documentSize()));
    session.selectTool(NavigationTool::hand);
    QVERIFY(!overlay.geometry().has_value());
    session.selectTool(NavigationTool::move);
    // Hidden controls: only a Ctrl+T transform shows its box.
    session.setShowsTransformControls(false);
    QVERIFY(!overlay.geometry().has_value());
    session.beginTransform(false);
    QVERIFY(!overlay.geometry().has_value());
    session.cancelTransform();
    session.beginTransform(true);
    QVERIFY(overlay.geometry().has_value());
    session.cancelTransform();
    session.setShowsTransformControls(true);
    session.toggleLayerVisibility(red);
    QVERIFY(!overlay.geometry().has_value());
    session.toggleLayerVisibility(red);
    // A selection or a folder: the box around them all.
    session.insert(filled(10, 10, qRgba(0, 0, 255, 255), "Blue"), QPointF(85, 85));
    const QUuid blue = session.activeLayerID().value();
    session.selectLayers({red, blue}, red);
    QCOMPARE(overlay.geometry().value(), TransformOverlayGeometry(session.groupTransformBox().value(), session.viewport, shown.documentSize()));
    session.beginTransform();
    LayerTransform draft = session.transformEdit().value().draft;
    draft.origin += QPointF(3, 0);
    session.previewTransform(draft);
    QCOMPARE(overlay.geometry().value(), TransformOverlayGeometry(draft, session.viewport, shown.documentSize()));
    session.cancelTransform();
    session.addGroup();
    const QUuid folder = session.activeLayerID().value();
    QVERIFY(session.placeLayer(blue, folder));
    session.selectLayer(folder);
    QCOMPARE(overlay.geometry().value(), TransformOverlayGeometry(session.groupTransformBox().value(), session.viewport, shown.documentSize()));
}

void TransformOverlayTests::handlesAndGuidesArePaintedWhereTheyBelong()
{
    Shown shown;
    CanvasView &canvas = *shown.canvas;
    EditorSession &session = shown.session;
    shown.settle();
    session.zoom(1);
    session.insert(filled(20, 20, qRgba(255, 0, 0, 255), "Red"));
    QVERIFY(canvas.synchronizeDisplay());
    QCOMPARE(session.viewport.viewPoint(QPointF(0, 0), shown.documentSize()), QPointF(150, 100));
    const TransformOverlay overlay(session);
    QCOMPARE(overlay.drawnRect(canvas.rect()), QRect(185, 107, 30, 58));
    // White squares on the handles, a disc on the grip.
    QImage shot = canvas.grab().toImage();
    QCOMPARE(shot.pixelColor(190, 140), QColor(Qt::white));
    QCOMPARE(shot.pixelColor(210, 160), QColor(Qt::white));
    QCOMPARE(shot.pixelColor(200, 112), QColor(Qt::white));
    // The accent outline runs between the handles, over the red.
    QVERIFY(shot.pixelColor(190, 145) != QColor(Qt::red));
    QVERIFY(shot.pixelColor(190, 145).blue() > shot.pixelColor(190, 145).red());
    session.setShowsTransformControls(false);
    QVERIFY(overlay.drawnRect(canvas.rect()).isEmpty());
    shot = canvas.grab().toImage();
    QVERIFY(shot.pixelColor(190, 140) != QColor(Qt::white));
    QVERIFY(shot.pixelColor(200, 112) != QColor(Qt::white));
    QCOMPARE(shot.pixelColor(190, 145), QColor(Qt::red));
    // A guide: an accent line across the document.
    session.snapGuides = {{50}, {}};
    QVERIFY(!canvas.synchronizeDisplay());
    QCOMPARE(overlay.drawnRect(canvas.rect()), canvas.rect());
    shot = canvas.grab().toImage();
    QCOMPARE(shot.pixelColor(200, 190), canvas.palette().color(QPalette::Highlight));
    QCOMPARE(shot.pixelColor(200, 101), canvas.palette().color(QPalette::Highlight));
    QVERIFY(shot.pixelColor(230, 190) != canvas.palette().color(QPalette::Highlight));
    session.snapGuides = {{}, {30}};
    canvas.synchronizeDisplay();
    shot = canvas.grab().toImage();
    QCOMPARE(shot.pixelColor(170, 130), canvas.palette().color(QPalette::Highlight));
    QVERIFY(shot.pixelColor(200, 190) != canvas.palette().color(QPalette::Highlight));
}

void TransformOverlayTests::anotherLayersHandlesRepaintOnlyTheirRects()
{
    Shown shown;
    CanvasView &canvas = *shown.canvas;
    EditorSession &session = shown.session;
    shown.settle();
    session.zoom(1);
    session.insert(filled(20, 20, qRgba(255, 0, 0, 255), "Red"), QPointF(20, 20));
    const QUuid red = session.activeLayerID().value();
    session.insert(filled(10, 10, qRgba(0, 0, 255, 255), "Blue"), QPointF(80, 80));
    canvas.synchronizeDisplay();
    QCoreApplication::processEvents();
    PaintSpy spy;
    canvas.installEventFilter(&spy);
    // The old box and the new one, nothing more.
    session.selectLayer(red);
    QVERIFY(!canvas.synchronizeDisplay());
    QCoreApplication::processEvents();
    QCOMPARE(int(spy.rects.size()), 1);
    QCOMPARE(spy.rects[0], QRect(155, 77, 85, 113));
    canvas.removeEventFilter(&spy);
}

QTEST_MAIN(TransformOverlayTests)
#include "TransformOverlayTests.moc"
