#include "BrushCanvasFixtures.h"
#include "UI/LassoControls.h"
#include "UI/ShapeControls.h"
#include <QSlider>
#include <QToolButton>

// The Shape tool on the canvas: draft, keys, drawing, bar.
namespace {
const QColor redShown(255, 0, 0);

// A blank active layer, the Shape tool, red paint.
QUuid shapes(Canvas &shown)
{
    shown.session.addBlankLayer();
    shown.session.selectTool(NavigationTool::shape);
    shown.session.setForegroundColor(PaletteColor{1, 0, 0});
    shown.canvas->synchronizeDisplay();
    return shown.session.activeLayerID().value();
}

QColor shownAt(Canvas &shown, int x, int y)
{
    return shown.canvas->grab().toImage().pixelColor(x, y);
}

template <typename Widget> Widget &find(QWidget &parent, const char *name)
{
    auto *found = parent.findChild<Widget *>(QString::fromLatin1(name));
    if (!found)
        throw std::runtime_error(name);
    return *found;
}
}

class ShapeCanvasTests : public QObject {
    Q_OBJECT
private slots:
    void aDragShowsTheDraftThenMakesALayer();
    void theDraftSitsJustAboveTheActiveLayer();
    void shiftSquaresAndAltGrowsFromTheCentre();
    void escapeAndLostFocusCancel();
    void shiftUStepsTheKinds();
    void aLineDraftHasItsWidthAndRoundCaps();
    void aDraftRepaintsTheView();
    void aDraftOutlivingItsProjectMakesNothing();
    void aPixelWideDraftIsItsOutline();
    void aDraftFollowsItsLayerInTheStack();
    void aDraftFollowsANewTarget();
    void aRoundedDraftScalesItsCorners();
    void aFolderMaskClipsTheDraft();
    void theMoveToolShowsRoundCornersRedrawn();
    void theBarSetsTheShape();
};

void ShapeCanvasTests::aDragShowsTheDraftThenMakesALayer()
{
    Canvas shown;
    shapes(shown);
    shown.press(QPointF(50, 50));
    shown.move(QPointF(150, 120));
    QCOMPARE(shown.session.shapeDraft().value().rect, QRectF(50, 50, 100, 70));
    QCOMPARE(shownAt(shown, 100, 80), redShown);
    QVERIFY(shownAt(shown, 40, 40) != redShown && shownAt(shown, 151, 80) != redShown);
    QCOMPARE(shown.session.document().value().layers.size(), size_t(1));
    shown.release(QPointF(150, 120));
    QVERIFY(!shown.session.shapeDraft());
    const ImageLayer made = shown.session.activeLayer().value();
    QCOMPARE(made.name, QString("Rectangle 1"));
    QCOMPARE(made.transform.origin, QPointF(50, 50));
    QCOMPARE(made.transform.size, QSizeF(100, 70));
    shown.canvas->synchronizeDisplay();
    QCOMPARE(shownAt(shown, 100, 80), redShown);
    // Space pans instead of drawing.
    shown.canvas->setFocus();
    QTRY_VERIFY(shown.canvas->hasFocus());
    QTest::keyPress(shown.canvas, Qt::Key_Space);
    shown.press(QPointF(200, 200));
    QVERIFY(!shown.session.shapeDraft());
    shown.release(QPointF(200, 200));
    QTest::keyRelease(shown.canvas, Qt::Key_Space);
}

void ShapeCanvasTests::theDraftSitsJustAboveTheActiveLayer()
{
    Canvas shown;
    // Green below and active; blue over the right half.
    shown.session.insert(filled(400, 300, qRgba(0, 255, 0, 255), QStringLiteral("Green")), QPointF(200, 150));
    const QUuid below = shown.session.activeLayerID().value();
    shown.session.insert(filled(200, 300, qRgba(0, 0, 255, 255), QStringLiteral("Blue")), QPointF(300, 150));
    shown.session.selectLayer(below);
    shown.session.selectTool(NavigationTool::shape);
    shown.session.setForegroundColor(PaletteColor{1, 0, 0});
    shown.press(QPointF(100, 100));
    shown.move(QPointF(300, 200));
    QCOMPARE(shownAt(shown, 150, 150), redShown);
    QCOMPARE(shownAt(shown, 250, 150), QColor(0, 0, 255));
    // A hidden active layer shows no draft.
    shown.session.toggleLayerVisibility(below);
    shown.move(QPointF(301, 200));
    QVERIFY(shownAt(shown, 150, 150) != redShown && shownAt(shown, 150, 150) != QColor(0, 255, 0));
    shown.release(QPointF(301, 200));
}

void ShapeCanvasTests::shiftSquaresAndAltGrowsFromTheCentre()
{
    Canvas shown;
    shapes(shown);
    shown.press(QPointF(100, 100));
    shown.move(QPointF(150, 120), Qt::ShiftModifier);
    QCOMPARE(shown.session.shapeDraft().value().rect, QRectF(100, 100, 50, 50));
    shown.move(QPointF(150, 120), Qt::AltModifier);
    QCOMPARE(shown.session.shapeDraft().value().rect, QRectF(50, 80, 100, 40));
    shown.move(QPointF(150, 120), Qt::ShiftModifier | Qt::AltModifier);
    QCOMPARE(shown.session.shapeDraft().value().rect, QRectF(50, 50, 100, 100));
    shown.release(QPointF(150, 120), Qt::ShiftModifier | Qt::AltModifier);
    QCOMPARE(shown.session.activeLayer().value().transform.size, QSizeF(100, 100));
    QCOMPARE(shown.session.activeLayer().value().transform.origin, QPointF(50, 50));
}

void ShapeCanvasTests::escapeAndLostFocusCancel()
{
    Canvas shown;
    shapes(shown);
    shown.canvas->setFocus();
    QTRY_VERIFY(shown.canvas->hasFocus());
    shown.press(QPointF(100, 100));
    shown.move(QPointF(150, 150));
    QTest::keyClick(shown.canvas, Qt::Key_Escape);
    QVERIFY(!shown.session.shapeDraft());
    QVERIFY(shownAt(shown, 120, 120) != redShown);
    shown.move(QPointF(160, 160));
    shown.release(QPointF(160, 160));
    QCOMPARE(shown.session.document().value().layers.size(), size_t(1));
    shown.press(QPointF(100, 100));
    shown.move(QPointF(150, 150));
    QWidget other(&shown.window);
    other.setFocusPolicy(Qt::StrongFocus);
    other.show();
    other.setFocus();
    QTRY_VERIFY(!shown.canvas->hasFocus());
    QVERIFY(!shown.session.shapeDraft());
    shown.release(QPointF(150, 150));
    QCOMPARE(shown.session.document().value().layers.size(), size_t(1));
}

void ShapeCanvasTests::shiftUStepsTheKinds()
{
    Canvas shown;
    shapes(shown);
    shown.canvas->setFocus();
    QTRY_VERIFY(shown.canvas->hasFocus());
    QTest::keyClick(shown.canvas, Qt::Key_U, Qt::ShiftModifier);
    QCOMPARE(shown.session.shapeKind(), ShapeKind::ellipse);
    QTest::keyClick(shown.canvas, Qt::Key_U, Qt::ShiftModifier);
    QCOMPARE(shown.session.shapeKind(), ShapeKind::line);
    QTest::keyClick(shown.canvas, Qt::Key_U);
    QCOMPARE(shown.session.shapeKind(), ShapeKind::line);
    // From another tool, Shift-U only chooses the Shape.
    shown.session.selectTool(NavigationTool::brush);
    QTest::keyClick(shown.canvas, Qt::Key_U, Qt::ShiftModifier);
    QCOMPARE(shown.session.tool(), NavigationTool::shape);
    QCOMPARE(shown.session.shapeKind(), ShapeKind::line);
    shown.session.selectTool(NavigationTool::brush);
    QTest::keyClick(shown.canvas, Qt::Key_U);
    QCOMPARE(shown.session.tool(), NavigationTool::shape);
}

void ShapeCanvasTests::aLineDraftHasItsWidthAndRoundCaps()
{
    Canvas shown;
    shapes(shown);
    shown.session.setShapeKind(ShapeKind::line);
    shown.session.setShapeLineWidth(10);
    shown.press(QPointF(100, 150));
    shown.move(QPointF(300, 150));
    QCOMPARE(shownAt(shown, 200, 154), redShown);
    QVERIFY(shownAt(shown, 200, 157) != redShown);
    // The round cap reaches past the start.
    QCOMPARE(shownAt(shown, 96, 150), redShown);
    QVERIFY(shownAt(shown, 95, 145) != redShown);
    // Twice the zoom, twice the width on screen.
    shown.session.zoom(2, QPointF(100, 150));
    shown.canvas->synchronizeDisplay();
    const QPointF middle = shown.session.viewport.viewPoint(QPointF(150, 150), shown.documentSize());
    QCOMPARE(shownAt(shown, int(middle.x()), int(middle.y()) + 8), redShown);
    QVERIFY(shownAt(shown, int(middle.x()), int(middle.y()) + 12) != redShown);
    // Half the zoom, half the width on screen.
    shown.session.zoom(0.5);
    shown.canvas->synchronizeDisplay();
    const QPointF half = shown.session.viewport.viewPoint(QPointF(150, 150), shown.documentSize());
    QCOMPARE(shownAt(shown, int(half.x()), int(half.y()) + 1), redShown);
    QVERIFY(shownAt(shown, int(half.x()), int(half.y()) + 4) != redShown);
    // A quarter the zoom: still a whole point wide.
    shown.session.setShapeLineWidth(1);
    shown.session.zoom(0.25);
    shown.canvas->synchronizeDisplay();
    const QPointF centre = shown.session.viewport.viewPoint(QPointF(200, 150), shown.documentSize());
    QCOMPARE(centre.y(), 150.0);
    QVERIFY2(shownAt(shown, int(centre.x()), 150).red() > 140, qPrintable(shownAt(shown, int(centre.x()), 150).name()));
    shown.release(QPointF(300, 150));
}

void ShapeCanvasTests::aRoundedDraftScalesItsCorners()
{
    Canvas shown;
    shapes(shown);
    shown.session.setShapeCornerRadius(20);
    shown.session.zoom(0.5);
    const auto view = [&](double x, double y) { return shown.session.viewport.viewPoint(QPointF(x, y), shown.documentSize()); };
    shown.press(view(60, 60));
    shown.move(view(150, 120));
    // 20 pixels are 10 points: four points in fills.
    const QPoint corner = view(60, 60).toPoint();
    QCOMPARE(shownAt(shown, corner.x() + 4, corner.y() + 4), redShown);
    QVERIFY(shownAt(shown, corner.x(), corner.y()) != redShown);
    // The box keeps its height at this zoom.
    QVERIFY(shownAt(shown, view(100, 125).toPoint().x(), view(100, 125).toPoint().y()) != redShown);
    shown.release(view(150, 120));
}

void ShapeCanvasTests::aDraftRepaintsTheView()
{
    Canvas shown;
    shapes(shown);
    shown.press(QPointF(100, 100));
    shown.move(QPointF(150, 150));
    QApplication::processEvents();
    PaintSpy spy(*shown.canvas);
    shown.move(QPointF(160, 170));
    QTRY_VERIFY(spy.count > 0);
    // As Swift's observation redraws it: the whole view.
    QCOMPARE(spy.painted, shown.canvas->rect());
    // Nothing new, nothing repainted.
    QApplication::processEvents();
    spy.count = 0;
    shown.canvas->synchronizeDisplay();
    QApplication::processEvents();
    QCOMPARE(spy.count, 0);
    // A new width or colour repaints the draft too.
    shown.session.setShapeLineWidth(30);
    shown.canvas->synchronizeDisplay();
    QTRY_VERIFY(spy.count > 0);
    QApplication::processEvents();
    spy.count = 0;
    shown.session.setForegroundColor(PaletteColor{0, 0, 1});
    shown.canvas->synchronizeDisplay();
    QTRY_VERIFY(spy.count > 0);
    QCOMPARE(shownAt(shown, 130, 130), QColor(0, 0, 255));
    shown.release(QPointF(160, 170));
    // An ellipse's rim is smooth: part paint, part ground.
    shown.session.setShapeKind(ShapeKind::ellipse);
    shown.press(QPointF(250, 50));
    shown.move(QPointF(290, 90));
    const QImage view = shown.canvas->grab().toImage();
    int partial = 0;
    for (int x = 245; x < 260; ++x)
        partial += view.pixelColor(x, 60).green() > 5 && view.pixelColor(x, 60).green() < 70;
    QVERIFY(partial > 0);
    shown.release(QPointF(290, 90));
}

void ShapeCanvasTests::aDraftOutlivingItsProjectMakesNothing()
{
    Canvas shown;
    shapes(shown);
    shown.press(QPointF(100, 100));
    shown.move(QPointF(150, 150));
    shown.session.clearProject();
    shown.move(QPointF(160, 160));
    QCOMPARE(shown.session.shapeDraft().value().rect, QRectF(100, 100, 50, 50));
    shown.release(QPointF(160, 160));
    QVERIFY(!shown.session.shapeDraft() && !shown.session.document());
}

void ShapeCanvasTests::aPixelWideDraftIsItsOutline()
{
    Canvas shown;
    shapes(shown);
    shown.session.setShapeKind(ShapeKind::line);
    shown.session.setShapeLineWidth(1);
    shown.press(QPointF(100, 100));
    shown.move(QPointF(300, 200));
    const QImage one = shown.canvas->grab().toImage();
    shown.session.setShapeLineWidth(1.001);
    shown.canvas->synchronizeDisplay();
    const QImage wider = shown.canvas->grab().toImage();
    // Qt thins a one-pixel pen; an outline does not.
    int apart = 0;
    for (int y = 95; y < 205; ++y) {
        for (int x = 95; x < 305; ++x)
            apart = std::max(apart, std::abs(one.pixelColor(x, y).red() - wider.pixelColor(x, y).red()));
    }
    QVERIFY2(apart <= 3, qPrintable(QString::number(apart)));
    shown.release(QPointF(300, 200));
}

void ShapeCanvasTests::aDraftFollowsItsLayerInTheStack()
{
    Canvas shown;
    const QUuid blank = shapes(shown);
    shown.session.addBlankLayer();
    const QUuid other = shown.session.activeLayerID().value();
    shown.session.insert(filled(400, 300, qRgba(0, 0, 255, 255), QStringLiteral("Blue")), QPointF(200, 150));
    shown.session.selectLayer(blank);
    shown.session.selectTool(NavigationTool::shape);
    shown.canvas->synchronizeDisplay();
    shown.press(QPointF(100, 100));
    shown.move(QPointF(300, 200));
    QCOMPARE(shownAt(shown, 200, 150), QColor(0, 0, 255));
    // Another blank layer draws nothing: hiding it changes nothing.
    shown.session.toggleLayerVisibility(other);
    QVERIFY(!shown.canvas->synchronizeDisplay());
    // Past the hidden blank layer, nothing shows otherwise.
    shown.session.moveActiveLayer(1);
    QVERIFY(!shown.canvas->synchronizeDisplay());
    // Raised over the blue mid-drag, as Ctrl+] does: redrawn.
    shown.session.moveActiveLayer(1);
    QVERIFY(shown.canvas->synchronizeDisplay());
    QCOMPARE(shownAt(shown, 200, 150), redShown);
    shown.session.toggleLayerVisibility(blank);
    QVERIFY(shown.canvas->synchronizeDisplay());
    shown.release(QPointF(300, 200));
    // Without a draft, a blank layer moves nothing on screen.
    shown.session.toggleLayerVisibility(blank);
    shown.canvas->synchronizeDisplay();
    shown.session.selectLayer(blank);
    shown.session.moveActiveLayer(-1);
    QVERIFY(!shown.canvas->synchronizeDisplay());
}

void ShapeCanvasTests::aDraftFollowsANewTarget()
{
    Canvas shown;
    shown.session.insert(filled(400, 300, qRgba(0, 255, 0, 255), QStringLiteral("Green")), QPointF(200, 150));
    const QUuid below = shown.session.activeLayerID().value();
    shown.session.insert(filled(400, 300, qRgba(0, 0, 255, 255), QStringLiteral("Blue")), QPointF(200, 150));
    const QUuid above = shown.session.activeLayerID().value();
    shown.session.selectLayer(below);
    shown.session.renameLayer(below, QStringLiteral("Below"));
    shown.session.selectLayer(above);
    shown.session.selectTool(NavigationTool::shape);
    shown.session.setForegroundColor(PaletteColor{1, 0, 0});
    shown.canvas->synchronizeDisplay();
    shown.press(QPointF(100, 100));
    shown.move(QPointF(300, 200));
    QCOMPARE(shownAt(shown, 200, 150), redShown);
    QApplication::processEvents();
    PaintSpy spy(*shown.canvas);
    // Undo mid-drag makes the lower layer active: the draft sinks.
    shown.session.undo();
    QCOMPARE(shown.session.activeLayerID(), std::optional(below));
    shown.canvas->synchronizeDisplay();
    QTRY_VERIFY(spy.count > 0);
    QCOMPARE(shownAt(shown, 200, 150), QColor(0, 0, 255));
    shown.release(QPointF(300, 200));
}

void ShapeCanvasTests::aFolderMaskClipsTheDraft()
{
    Canvas shown;
    const QUuid child = shapes(shown);
    shown.session.groupSelectedLayers();
    const QUuid folder = shown.session.activeLayerID().value();
    QImage mask(400, 300, QImage::Format_Grayscale8);
    mask.fill(255);
    for (int y = 0; y < 300; ++y)
        std::fill_n(mask.scanLine(y), 200, uchar(0));
    rewrite(shown.session, [&](ProjectSnapshot &snapshot) { setMask(snapshot, folder, LayerMask::assetFrom(mask)); });
    shown.session.zoom(1);
    shown.session.selectLayer(child);
    shown.session.selectTool(NavigationTool::shape);
    shown.canvas->synchronizeDisplay();
    shown.press(QPointF(100, 100));
    shown.move(QPointF(300, 200));
    QVERIFY(shownAt(shown, 150, 150) != redShown);
    QCOMPARE(shownAt(shown, 250, 150), redShown);
    shown.release(QPointF(300, 200));
}

void ShapeCanvasTests::theMoveToolShowsRoundCornersRedrawn()
{
    Canvas shown;
    shapes(shown);
    shown.session.setShapeCornerRadius(20);
    shown.press(QPointF(100, 100));
    shown.move(QPointF(200, 160));
    shown.release(QPointF(200, 160));
    shown.session.selectTool(NavigationTool::move);
    shown.session.beginTransform();
    LayerTransform draft = shown.session.transformEdit().value().draft;
    draft.size = QSizeF(200, 120);
    shown.session.previewTransform(draft);
    shown.canvas->synchronizeDisplay();
    // Radius 20 covers (106, 106); a stretched 40 would not.
    QCOMPARE(shownAt(shown, 106, 106), redShown);
    QVERIFY(shownAt(shown, 101, 101) != redShown);
    QCOMPARE(shownAt(shown, 250, 200), redShown);
    shown.session.cancelTransform();
}

void ShapeCanvasTests::theBarSetsTheShape()
{
    EditorSession session;
    session.createDocument(40, 20, true);
    session.selectTool(NavigationTool::shape);
    ShapeControls bar(session);
    bar.show();
    QCOMPARE(bar.title->text(), QString("Shape"));
    auto &rectangle = find<QToolButton>(bar, "shapeRectangle"), &ellipse = find<QToolButton>(bar, "shapeEllipse"),
         &line = find<QToolButton>(bar, "shapeLine");
    QCOMPARE(line.text(), QString("Line"));
    QCOMPARE(ellipse.toolTip(), QString("Shift-U (or Tab) steps through Rectangle, Ellipse and Line"));
    QVERIFY(rectangle.isChecked());
    QWidget &width = find<QWidget>(bar, "shapeWidthGroup"), &radius = find<QWidget>(bar, "shapeRadiusGroup");
    QVERIFY(!width.isVisible() && radius.isVisible());
    QCOMPARE(radius.toolTip(), QString("Round the rectangle's corners by this many pixels; 0 keeps them square"));
    // Choosing a kind drops the draft first.
    session.beginShape(QPointF(1, 1));
    line.click();
    QVERIFY(!session.shapeDraft());
    QCOMPARE(session.shapeKind(), ShapeKind::line);
    QVERIFY(line.isChecked() && !rectangle.isChecked());
    QVERIFY(width.isVisible() && !radius.isVisible());
    ellipse.click();
    QCOMPARE(session.shapeKind(), ShapeKind::ellipse);
    QVERIFY(!width.isVisible() && !radius.isVisible());
    rectangle.click();
    QCOMPARE(session.shapeKind(), ShapeKind::rectangle);
    // A line's width: slider to 100, field to 5000.
    session.setShapeKind(ShapeKind::line);
    auto &widthSlider = find<QSlider>(bar, "shapeWidthSlider");
    auto &widthField = find<SelectionAmountField>(bar, "shapeWidth");
    QVERIFY(widthSlider.minimum() == 1 && widthSlider.maximum() == 100 && widthSlider.width() == 100 && widthField.width() == 48);
    widthSlider.setValue(37);
    QCOMPARE(session.shapeLineWidth(), 37.0);
    QCOMPARE(widthField.text(), QString("37"));
    widthField.setFocus();
    widthField.selectAll();
    QTest::keyClicks(&widthField, "0");
    QCOMPARE(session.shapeLineWidth(), 1.0);
    widthField.selectAll();
    QTest::keyClicks(&widthField, "9999");
    QTest::keyClick(&widthField, Qt::Key_Return);
    QCOMPARE(session.shapeLineWidth(), 5000.0);
    QCOMPARE(widthSlider.value(), 100);
    QCOMPARE(widthField.text(), QString("5000"));
    session.setShapeLineWidth(4.6);
    QVERIFY(widthSlider.value() == 5 && widthField.text() == QString("5"));
    QCOMPARE(session.shapeLineWidth(), 4.6);
    // A rectangle's radius: slider to 200, field to 5000.
    session.setShapeKind(ShapeKind::rectangle);
    auto &radiusSlider = find<QSlider>(bar, "shapeRadiusSlider");
    auto &radiusField = find<SelectionAmountField>(bar, "shapeRadius");
    QVERIFY(radiusSlider.minimum() == 0 && radiusSlider.maximum() == 200 && radiusSlider.width() == 100 && radiusField.width() == 48);
    radiusSlider.setValue(150);
    QCOMPARE(session.shapeCornerRadius(), 150.0);
    radiusField.setFocus();
    radiusField.selectAll();
    QTest::keyClicks(&radiusField, "9999");
    QTest::keyClick(&radiusField, Qt::Key_Return);
    QCOMPARE(session.shapeCornerRadius(), 5000.0);
    QCOMPARE(session.shapeLineWidth(), 4.6);
    session.setShapeCornerRadius(700);
    QVERIFY(radiusSlider.value() == 200 && radiusField.text() == QString("700"));
    QCOMPARE(session.shapeCornerRadius(), 700.0);
    QStringList labels;
    for (const QLabel *label : bar.findChildren<QLabel *>())
        labels << label->text();
    labels.sort();
    QCOMPARE(labels, (QStringList{"Fill", "Radius", "Shape", "Width", "px", "px"}));
    // The row reads left to right, spaced as Swift's.
    QCOMPARE(radius.layout()->spacing(), 6);
    bar.resize(bar.sizeHint().width() + 300, bar.height());
    QTRY_VERIFY(radius.geometry().right() < bar.width() / 2 + 100 && rectangle.x() < line.x());
    session.setIsProjectBusy(true);
    QTRY_VERIFY(!bar.isEnabled());
    session.setIsProjectBusy(false);
    QVERIFY(bar.isEnabled());
    EditorSession empty;
    const ShapeControls idle(empty);
    QVERIFY(!idle.isEnabled());
}

QTEST_MAIN(ShapeCanvasTests)
#include "ShapeCanvasTests.moc"
