#include "CanvasFixtures.h"
#include "ContentView.h"
#include "UI/CanvasRulers.h"
#include "UI/LayersPanel.h"
#include <QNativeGestureEvent>

// Swift 1.1.7 on the canvas: grid, guides, rulers, drags.
namespace {
// A 400 by 300 document filling the canvas at 100%.
struct GuideCanvas : Shown {
    GuideCanvas() : Shown(QSize(400, 300), QSize(400, 300))
    {
        settle();
        session.zoom(1);
        session.setShowsTransformControls(false);
        QImage image(100, 60, QImage::Format_RGBA8888_Premultiplied);
        image.fill(Qt::red);
        session.insert(ImportedImage(image, image, QStringLiteral("Red")));
        session.selectTool(NavigationTool::move);
        canvas->synchronizeDisplay();
    }
    QPointF at(double x, double y) const { return session.viewport.viewPoint(QPointF(x, y), QSizeF(400, 300)); }
    QColor pixel(QPointF point) { return canvas->grab().toImage().pixelColor(point.toPoint()); }
    void press(QPointF point) { QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, point.toPoint()); }
    void drag(QPointF point) { ::drag(*canvas, point); }
    void release(QPointF point) { QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, point.toPoint()); }
};

void hover(QWidget &widget, QPointF at)
{
    QMouseEvent event(QEvent::MouseMove, at, at, widget.mapToGlobal(at), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
}

struct Painted : QObject {
    int &count;
    explicit Painted(int &count) : count(count) {}
    bool eventFilter(QObject *, QEvent *event) override
    {
        count += event->type() == QEvent::Paint;
        return false;
    }
};

// A hairline on a whole point spans two rows.
bool cyan(const QColor &colour)
{
    return colour.green() - colour.red() > 80 && colour.blue() - colour.red() > 80;
}
}

class GuideCanvasTests : public QObject {
    Q_OBJECT
private slots:
    void moveToolDragsAGuideAndRulerDropDeletesIt();
    void rulerStepUsesNicePixelIntervals();
    void guidesAndTheGridDrawOverTheCanvas();
    void theCursorKeysAndLostReleasesEndADrag();
    void theRulersFrameTheCanvasAndDragOutGuides();
};

void GuideCanvasTests::moveToolDragsAGuideAndRulerDropDeletesIt()
{
    GuideCanvas shown;
    const CanvasGuide guide{QUuid::createUuid(), CanvasGuide::Axis::vertical, 40};
    shown.session.addGuide(guide);
    shown.session.setShowsRulers(true);
    shown.press(shown.at(40, 20));
    shown.drag(shown.at(70, 20));
    QCOMPARE(shown.session.guideDrag().value().position, 70.0);
    shown.release(shown.at(70, 20));
    QCOMPARE(shown.session.document().value().guides.front().position, 70.0);
    QVERIFY(!shown.session.guideDrag());
    QCOMPARE(shown.session.history.undoName(), QString("Move Guide"));
    // The layer under the guide never moved.
    QCOMPARE(shown.session.activeLayer().value().origin(), QPointF(150, 120));
    shown.press(shown.at(70, 20));
    shown.release(shown.at(-10, 20));
    QVERIFY2(shown.session.document().value().guides.empty(), "dropping on the ruler deletes the guide");
    // So does the top ruler, for a horizontal guide.
    shown.session.addGuide({QUuid::createUuid(), CanvasGuide::Axis::horizontal, 200});
    shown.press(shown.at(300, 200));
    shown.release(shown.at(300, -5));
    QVERIFY(shown.session.document().value().guides.empty());
    shown.session.undo();
    shown.session.undo();
    // Without rulers a drop out there deletes nothing.
    shown.session.undo();
    shown.session.setShowsRulers(false);
    shown.press(shown.at(70, 20));
    shown.release(shown.at(-10, 20));
    QCOMPARE(shown.session.document().value().guides.front().position, 70.0);
}

void GuideCanvasTests::rulerStepUsesNicePixelIntervals()
{
    QCOMPARE(CanvasRulerView::majorStep(1), 100.0);
    QCOMPARE(CanvasRulerView::majorStep(8), 10.0);
    QCOMPARE(CanvasRulerView::majorStep(0.7), 100.0);
    QCOMPARE(CanvasRulerView::majorStep(0.0001), 50'000.0);
    QCOMPARE(CanvasRulerView::label(0), QString("0"));
    QCOMPARE(CanvasRulerView::label(250), QString("250"));
    QCOMPARE(CanvasRulerView::label(-0.4), QString("0"));
    QCOMPARE(CanvasRulerView::label(-99.6), QString("-100"));
}

void GuideCanvasTests::guidesAndTheGridDrawOverTheCanvas()
{
    GuideCanvas shown;
    // A shown flag or a new guide repaints the canvas.
    QTest::qWait(50);
    int paints = 0;
    Painted counter(paints);
    shown.canvas->installEventFilter(&counter);
    shown.session.setShowsGrid(true);
    shown.canvas->synchronizeDisplay();
    QTRY_VERIFY(paints > 0);
    shown.session.setShowsGrid(false);
    shown.canvas->synchronizeDisplay();
    paints = 0;
    shown.session.addGuide({QUuid::createUuid(), CanvasGuide::Axis::vertical, 390});
    shown.canvas->synchronizeDisplay();
    QTRY_VERIFY(paints > 0);
    shown.canvas->removeEventFilter(&counter);
    // A guide spans the view in cyan; hidden, it goes.
    shown.session.addGuide({QUuid::createUuid(), CanvasGuide::Axis::horizontal, 20});
    shown.canvas->synchronizeDisplay();
    const QPointF line = shown.at(10, 20);
    QVERIFY2(cyan(shown.pixel(line)), qPrintable(shown.pixel(line).name()));
    QVERIFY(cyan(shown.pixel(line + QPointF(350, 0))));
    shown.session.addGuide({QUuid::createUuid(), CanvasGuide::Axis::vertical, 300});
    QVERIFY(cyan(shown.pixel(shown.at(300, 290))));
    // A hairline: two columns at most show it at 100%.
    const QImage hair = shown.canvas->grab().toImage();
    int columns = 0;
    for (int x = int(shown.at(300, 0).x()) - 5; x <= int(shown.at(300, 0).x()) + 5; ++x) {
        const QColor seen = hair.pixelColor(x, int(shown.at(0, 290).y()));
        columns += seen.green() - seen.red() > 30;
    }
    QVERIFY2(columns >= 1 && columns <= 2, qPrintable(QString::number(columns)));
    shown.session.setShowsGuides(false);
    QVERIFY(!cyan(shown.pixel(line)));
    // The grid's majors every 64 pixels, subdivisions dotted between.
    const QColor plain = shown.pixel(shown.at(64.5, 10.5));
    shown.session.setShowsGrid(true);
    const QColor major = shown.pixel(shown.at(64, 10) + QPointF(0.2, 0));
    QVERIFY2(major != plain, qPrintable(major.name() + plain.name()));
    // The major ink: gray 0.7 at 45%, over two columns.
    const QImage majors = shown.canvas->grab().toImage();
    const int column = int(shown.at(64, 0).x()), rowAt = int(shown.at(0, 10).y());
    const double spread = majors.pixelColor(column - 1, rowAt).red() + majors.pixelColor(column, rowAt).red() - 2.0 * plain.red();
    QVERIFY2(std::abs(spread - 0.45 * (0.7 * 255 - plain.red())) <= 4, qPrintable(QString::number(spread)));
    // The subdivision at 32 is dotted: some rows inked.
    const QImage grid = shown.canvas->grab().toImage();
    int inked = 0;
    for (int y = 0; y < 30; ++y)
        inked += grid.pixelColor(shown.at(32, y).toPoint()) != grid.pixelColor(shown.at(36, y).toPoint());
    QVERIFY2(inked > 3 && inked < 27, qPrintable(QString::number(inked)));
    // At 25% the subdivisions are closer than four points: none.
    shown.session.zoom(0.25);
    shown.canvas->synchronizeDisplay();
    const QImage quarter = shown.canvas->grab().toImage();
    QCOMPARE(quarter.pixelColor(shown.at(8, 150).toPoint()), quarter.pixelColor(shown.at(12, 150).toPoint()));
    // The major at 64 shows on a column it straddles.
    const int major64 = int(std::floor(shown.at(64, 150).x()));
    const QColor between = quarter.pixelColor(shown.at(40, 150).toPoint());
    QVERIFY(quarter.pixelColor(major64, 150) != between || quarter.pixelColor(major64 - 1, 150) != between);
}

void GuideCanvasTests::theCursorKeysAndLostReleasesEndADrag()
{
    GuideCanvas shown;
    shown.session.addGuide({QUuid::createUuid(), CanvasGuide::Axis::vertical, 40});
    shown.session.addGuide({QUuid::createUuid(), CanvasGuide::Axis::horizontal, 250});
    hover(*shown.canvas, shown.at(41, 20));
    QCOMPARE(shown.canvas->cursor().shape(), Qt::SizeHorCursor);
    hover(*shown.canvas, shown.at(60, 251));
    QCOMPARE(shown.canvas->cursor().shape(), Qt::SizeVerCursor);
    shown.session.setLocksGuides(true);
    hover(*shown.canvas, shown.at(60, 251));
    QVERIFY(shown.canvas->cursor().shape() != Qt::SizeVerCursor);
    shown.session.setLocksGuides(false);
    // Escape puts the guide back; the cursor lets go.
    shown.press(shown.at(40, 20));
    shown.drag(shown.at(90, 20));
    QCOMPARE(shown.canvas->cursor().shape(), Qt::SizeHorCursor);
    // A guide drag takes Escape before a transform edit.
    QTest::keyClick(shown.canvas, Qt::Key_Escape);
    shown.release(shown.at(90, 20));
    shown.session.beginTransform();
    QVERIFY(shown.session.transformEdit());
    shown.press(shown.at(40, 20));
    QVERIFY(shown.session.guideDrag());
    QTest::keyClick(shown.canvas, Qt::Key_Escape);
    QVERIFY(!shown.session.guideDrag() && shown.session.transformEdit());
    shown.release(shown.at(40, 20));
    shown.session.cancelTransform();
    shown.press(shown.at(40, 20));
    shown.drag(shown.at(90, 20));
    // A wheel or pinch rests while a guide is dragged.
    const CanvasViewport before = shown.session.viewport;
    QWheelEvent wheel(shown.at(90, 20), shown.canvas->mapToGlobal(shown.at(90, 20)), QPoint(0, 30), QPoint(0, 0), Qt::LeftButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(shown.canvas, &wheel);
    QNativeGestureEvent pinch(Qt::ZoomNativeGesture, QPointingDevice::primaryPointingDevice(), 2, shown.at(90, 20), shown.at(90, 20),
                              shown.canvas->mapToGlobal(shown.at(90, 20)), 0.5, {}, {});
    QApplication::sendEvent(shown.canvas, &pinch);
    QCOMPARE(shown.session.viewport, before);
    QTest::keyClick(shown.canvas, Qt::Key_Escape);
    QVERIFY(!shown.session.guideDrag());
    QCOMPARE(shown.session.document().value().guides.front().position, 40.0);
    hover(*shown.canvas, shown.at(200, 150));
    QVERIFY(shown.canvas->cursor().shape() != Qt::SizeHorCursor);
    shown.release(shown.at(90, 20));
    QCOMPARE(shown.session.document().value().guides.front().position, 40.0);
    // A lost release: a buttonless move lands it.
    shown.press(shown.at(40, 20));
    shown.drag(shown.at(95, 20));
    hover(*shown.canvas, shown.at(95, 20));
    QVERIFY(!shown.session.guideDrag());
    QCOMPARE(shown.session.document().value().guides.front().position, 95.0);
    // The drag's cursor let go with it.
    hover(*shown.canvas, shown.at(300, 50));
    QVERIFY(shown.canvas->cursor().shape() != Qt::SizeHorCursor);
    // So does a lost focus.
    shown.press(shown.at(95, 20));
    shown.drag(shown.at(120, 20));
    shown.canvas->clearFocus();
    QVERIFY(!shown.session.guideDrag());
    QCOMPARE(shown.session.document().value().guides.front().position, 120.0);
    // A rename in progress leaves the guides alone.
    shown.session.setRenamingLayerID(shown.session.activeLayerID());
    shown.press(shown.at(120, 20));
    QVERIFY(!shown.session.guideDrag());
    // No drag began: a wheel still pans.
    const CanvasViewport resting = shown.session.viewport;
    QApplication::sendEvent(shown.canvas, &wheel);
    QVERIFY(!(shown.session.viewport == resting));
    shown.release(shown.at(120, 20));
    shown.session.setRenamingLayerID(std::nullopt);
    // Away from guides a press moves the layer, as before.
    shown.press(shown.at(200, 150));
    shown.drag(shown.at(230, 150));
    shown.release(shown.at(230, 150));
    QCOMPARE(shown.session.activeLayer().value().origin(), QPointF(180, 120));
}

void GuideCanvasTests::theRulersFrameTheCanvasAndDragOutGuides()
{
    EditorSession session;
    ContentView view(session);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    const QList<CanvasRulerView *> rulers = view.findChildren<CanvasRulerView *>();
    QCOMPARE(rulers.size(), 2);
    CanvasRulerView *top = rulers[0]->accessibleName() == "Horizontal ruler" ? rulers[0] : rulers[1];
    CanvasRulerView *side = top == rulers[0] ? rulers[1] : rulers[0];
    QCOMPARE(side->accessibleName(), QString("Vertical ruler"));
    session.setShowsRulers(true);
    QVERIFY(!top->isVisible());
    session.createDocument(400, 300);
    QTRY_VERIFY(top->isVisible() && side->isVisible());
    QVERIFY(view.findChild<CanvasRulerCorner *>()->isVisible());
    QVERIFY(top->height() == CanvasRuler::thickness && side->width() == CanvasRuler::thickness);
    QVERIFY(top->cursor().shape() == Qt::SizeVerCursor && side->cursor().shape() == Qt::SizeHorCursor);
    CanvasView &canvas = *view.findChild<CanvasView *>();
    // The top ruler sits above the canvas, the side left.
    QCOMPARE(top->mapTo(&view, QPoint(0, top->height())).y(), canvas.mapTo(&view, QPoint(0, 0)).y());
    QCOMPARE(side->mapTo(&view, QPoint(side->width(), 0)).x(), canvas.mapTo(&view, QPoint(0, 0)).x());
    QTRY_COMPARE(session.viewport.viewSize, QSizeF(canvas.size()));
    session.zoom(1);
    // The corner's diagonal, and rulers that follow the view.
    const QImage corner = view.findChild<CanvasRulerCorner *>()->grab().toImage();
    QVERIFY(corner.pixelColor(9, 9).lightness() > corner.pixelColor(3, 3).lightness() + 10);
    int paints = 0;
    Painted counter(paints);
    top->installEventFilter(&counter);
    session.zoom(2);
    QTRY_VERIFY(paints > 0);
    top->removeEventFilter(&counter);
    session.zoom(1);
    // A press on a ruler hands the canvas the keys.
    view.layersPanel().setFocus();
    QTRY_VERIFY(!canvas.hasFocus());
    QTest::mousePress(side, Qt::LeftButton, Qt::NoModifier, QPoint(9, 100));
    QVERIFY(canvas.hasFocus());
    QTest::mouseRelease(side, Qt::LeftButton, Qt::NoModifier, QPoint(9, 100));
    // A drag from the top ruler makes a guide.
    const QPointF target = session.viewport.viewPoint(QPointF(0, 150), QSizeF(400, 300));
    QTest::mousePress(top, Qt::LeftButton, Qt::NoModifier, QPoint(50, 9));
    QVERIFY(session.guideDrag() && session.guideDrag().value().axis == CanvasGuide::Axis::horizontal);
    const QPoint inRuler = top->mapFromGlobal(canvas.mapToGlobal(target.toPoint()));
    drag(*top, QPointF(inRuler));
    QCOMPARE(session.guideDrag().value().position, 150.0);
    QTest::mouseRelease(top, Qt::LeftButton, Qt::NoModifier, inRuler);
    QCOMPARE(session.document().value().guides.size(), size_t(1));
    QCOMPARE(session.document().value().guides.front().position, 150.0);
    QCOMPARE(session.history.undoName(), QString("New Guide"));
    // Dropped back on the ruler, no guide is made.
    QTest::mousePress(side, Qt::LeftButton, Qt::NoModifier, QPoint(9, 100));
    QTest::mouseRelease(side, Qt::LeftButton, Qt::NoModifier, QPoint(9, 100));
    QCOMPARE(session.document().value().guides.size(), size_t(1));
    // Locked guides take no drag; rulers follow the flag.
    session.setLocksGuides(true);
    QTest::mousePress(side, Qt::LeftButton, Qt::NoModifier, QPoint(9, 100));
    QVERIFY(!session.guideDrag());
    QTest::mouseRelease(side, Qt::LeftButton, Qt::NoModifier, QPoint(9, 100));
    session.setShowsRulers(false);
    QVERIFY(!top->isVisible() && !side->isVisible());
    // The ruler ticks: a labelled major every 100 pixels.
    session.setShowsRulers(true);
    const QImage strip = top->grab().toImage();
    const int zero = top->mapFromGlobal(canvas.mapToGlobal(session.viewport.viewPoint(QPointF(0, 0), QSizeF(400, 300)).toPoint())).x();
    const int row = CanvasRuler::thickness - 2;
    // A half-point hairline lands on one of two columns.
    QVERIFY(std::max(strip.pixelColor(zero, row).lightness(), strip.pixelColor(zero - 1, row).lightness()) > 120);
    // Majors reach eight points up, halves five, minors three.
    const auto lit = [&](int x, int y) { return std::max(strip.pixelColor(x, y).lightness(), strip.pixelColor(x - 1, y).lightness()) > 120; };
    QVERIFY(lit(zero + 50, CanvasRuler::thickness - 5) && !lit(zero + 50, CanvasRuler::thickness - 6));
    // A label two points past the major, in gray 0.78.
    int brightest = 0;
    for (int y = 0; y < 10; ++y) {
        for (int x = zero + 2; x < zero + 12; ++x)
            brightest = std::max(brightest, strip.pixelColor(x, y).lightness());
    }
    // Gray 0.78 at partial coverage stays well below white.
    QVERIFY2(brightest > 150 && brightest < 180, qPrintable(QString::number(brightest)));
    int between = 0;
    for (int y = 0; y < 8; ++y) {
        for (int x = zero + 30; x < zero + 45; ++x)
            between = std::max(between, strip.pixelColor(x, y).lightness());
    }
    QVERIFY(between < 80);
    QVERIFY(lit(zero, CanvasRuler::thickness - 7) && lit(zero + 10, row) && !lit(zero + 10, CanvasRuler::thickness - 5));
    QVERIFY(strip.pixelColor(zero + 5, CanvasRuler::thickness - 2).lightness() < 80);
}

QTEST_MAIN(GuideCanvasTests)
#include "GuideCanvasTests.moc"
