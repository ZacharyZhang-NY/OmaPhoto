#include "BrushCanvasFixtures.h"
#include "UI/GradientControls.h"
#include "UI/LassoControls.h"
#include "Rendering/TransformOverlay.h"
#include <numbers>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>

// Swift's gradient on the canvas: drag, ends, keys, bar.
namespace {
void gradient(Canvas &shown)
{
    shown.session.addBlankLayer();
    shown.session.selectTool(NavigationTool::gradient);
    shown.canvas->synchronizeDisplay();
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

class GradientCanvasTests : public QObject {
    Q_OBJECT
private slots:
    void aDragDrawsAPendingGradientAndItsLine();
    void endsAreGrabbedWithinTenPoints();
    void shiftKeepsTheLineToFortyFiveDegrees();
    void escapeCancelsAndReturnApplies();
    void theOverlayDrawsTheLineAsSwift();
    void aFoldersPendingMaskClipsAtOnce();
    void theBarSetsTheGradient();
    void theSwatchShowsTheColoursOverChecks();
};

void GradientCanvasTests::aDragDrawsAPendingGradientAndItsLine()
{
    Canvas shown;
    gradient(shown);
    const QColor ground = shownAt(shown, 200, 100);
    shown.drag(QPointF(50, 150), QPointF(350, 150));
    const std::optional<GradientEdit> &edit = shown.session.gradientEdit();
    QVERIFY(edit.has_value());
    QCOMPARE(edit.value().start, QPointF(50, 150));
    QCOMPARE(edit.value().end, QPointF(350, 150));
    QCOMPARE(shown.canvas->synchronizeDisplay(), false);
    // Black at the start, fading; the line and ends above.
    QCOMPARE(shownAt(shown, 40, 100), QColor(0, 0, 0));
    const QColor mid = shownAt(shown, 200, 100);
    QVERIFY2(std::abs(mid.red() - ground.red() / 2) <= 3, qPrintable(mid.name() + " " + ground.name()));
    // Each end: its colour, a white ring, a black rim.
    QCOMPARE(shownAt(shown, 50, 150), QColor(0, 0, 0));
    QCOMPARE(shownAt(shown, 50, 145), QColor(255, 255, 255));
    const QImage view = shown.canvas->grab().toImage();
    QVERIFY(std::min({view.pixelColor(50, 143).red(), view.pixelColor(50, 144).red()}) < 110);
    QCOMPARE(shownAt(shown, 350, 150), QColor(191, 191, 191));
    // The white line over the black one lightens its row.
    QVERIFY(shownAt(shown, 200, 150).red() > mid.red() + 40);
    // A click without a line leaves nothing pending.
    shown.session.cancelGradient();
    shown.click(QPointF(100, 100));
    QVERIFY(!shown.session.gradientEdit());
}

void GradientCanvasTests::endsAreGrabbedWithinTenPoints()
{
    Canvas shown;
    gradient(shown);
    shown.drag(QPointF(50, 150), QPointF(350, 150));
    // Nine points from the end grabs it; the start stays.
    shown.drag(QPointF(359, 150), QPointF(300, 100));
    QCOMPARE(shown.session.gradientEdit().value().start, QPointF(50, 150));
    QCOMPARE(shown.session.gradientEdit().value().end, QPointF(300, 100));
    shown.drag(QPointF(52, 157), QPointF(80, 200));
    QCOMPARE(shown.session.gradientEdit().value().start, QPointF(80, 200));
    QCOMPARE(shown.session.gradientEdit().value().end, QPointF(300, 100));
    // Eleven points from either end starts a new line there.
    shown.drag(QPointF(80, 211), QPointF(150, 250));
    QCOMPARE(shown.session.gradientEdit().value().start, QPointF(80, 211));
    shown.drag(QPointF(161, 250), QPointF(250, 250));
    shown.drag(QPointF(311, 100), QPointF(390, 20));
    QCOMPARE(shown.session.gradientEdit().value().start, QPointF(311, 100));
    QCOMPARE(shown.session.gradientEdit().value().end, QPointF(390, 20));
    // A release lets go: hovering moves nothing.
    shown.hover(QPointF(200, 200));
    QCOMPARE(shown.session.gradientEdit().value().end, QPointF(390, 20));
    // Losing focus drops a drag under way.
    shown.canvas->setFocus();
    QTRY_VERIFY(shown.canvas->hasFocus());
    shown.press(QPointF(390, 20));
    QWidget other(&shown.window);
    other.setFocusPolicy(Qt::StrongFocus);
    other.show();
    other.setFocus();
    QTRY_VERIFY(!shown.canvas->hasFocus());
    shown.move(QPointF(200, 200));
    QCOMPARE(shown.session.gradientEdit().value().end, QPointF(390, 20));
    shown.release(QPointF(200, 200));
}

void GradientCanvasTests::shiftKeepsTheLineToFortyFiveDegrees()
{
    Canvas shown;
    gradient(shown);
    shown.press(QPointF(100, 100));
    shown.move(QPointF(200, 130), Qt::ShiftModifier);
    const QPointF end = shown.session.gradientEdit().value().end;
    QCOMPARE(end.y(), 100.0);
    QCOMPARE(end.x(), 100 + std::hypot(100.0, 30.0));
    shown.move(QPointF(180, 170), Qt::ShiftModifier);
    const QPointF diagonal = shown.session.gradientEdit().value().end;
    QVERIFY(std::abs((diagonal.x() - 100) - (diagonal.y() - 100)) < 1e-9);
    shown.release(QPointF(180, 170), Qt::ShiftModifier);
    // The start snaps around the end the same way.
    shown.press(QPointF(100, 100));
    shown.move(QPointF(100 + 3, 250), Qt::ShiftModifier);
    const QPointF start = shown.session.gradientEdit().value().start, fixed = shown.session.gradientEdit().value().end;
    QVERIFY(start.x() < fixed.x() && std::abs(std::abs(start.x() - fixed.x()) - std::abs(start.y() - fixed.y())) < 1e-9);
    shown.release(QPointF(103, 250));
}

void GradientCanvasTests::escapeCancelsAndReturnApplies()
{
    Canvas shown;
    gradient(shown);
    shown.canvas->setFocus();
    QTRY_VERIFY(shown.canvas->hasFocus());
    shown.drag(QPointF(50, 150), QPointF(350, 150));
    QTest::keyClick(shown.canvas, Qt::Key_Escape);
    QVERIFY(!shown.session.gradientEdit());
    const int count = shown.session.history.undoCount();
    shown.drag(QPointF(50, 150), QPointF(350, 150));
    QTest::keyClick(shown.canvas, Qt::Key_Return);
    QTRY_COMPARE(shown.session.history.undoCount(), count + 1);
    QVERIFY(!shown.session.gradientEdit());
    shown.drag(QPointF(50, 150), QPointF(350, 150));
    QTest::keyClick(shown.canvas, Qt::Key_Enter);
    QTRY_COMPARE(shown.session.history.undoCount(), count + 2);
}

void GradientCanvasTests::theOverlayDrawsTheLineAsSwift()
{
    Canvas shown;
    QImage image = BrushRaster::context(60, 40, false);
    image.fill(QColor(90, 90, 90));
    shown.session.insert(ImportedImage(image, image, QStringLiteral("Block")));
    shown.session.selectTool(NavigationTool::gradient);
    const TransformOverlay overlay(shown.session);
    const auto drawn = [&] {
        QImage layer(400, 300, QImage::Format_ARGB32_Premultiplied);
        layer.fill(Qt::transparent);
        QPainter painter(&layer);
        overlay.draw(painter, shown.canvas->palette());
        return layer;
    };
    // A press alone has no line to draw.
    shown.session.beginGradient(QPointF(100, 150));
    QVERIFY(!overlay.gradientLine());
    shown.session.moveGradient(std::nullopt, QPointF(300, 150));
    // A three-point shade under the one-point white line.
    QVERIFY(qAlpha(drawn().pixel(200, 148)) > 40);
    QCOMPARE(qAlpha(drawn().pixel(200, 146)), 0);
    // The radial's dashed rim, at the line's length.
    GradientSettings radial = shown.session.gradientSettings();
    radial.shape = GradientShape::radial;
    shown.session.setGradientSettings(radial);
    shown.session.moveGradient(std::nullopt, QPointF(160, 150));
    const QImage rim = drawn();
    int dashes = 0;
    for (int step = 0; step < 160; ++step) {
        const double angle = (100 + step) * std::numbers::pi / 180;
        dashes += qAlpha(rim.pixel(int(100 + 60 * std::cos(angle)), int(150 + 60 * std::sin(angle)))) > 0;
    }
    QVERIFY2(dashes > 40 && dashes < 150, qPrintable(QString::number(dashes)));
    // Switched to Move, not yet applied: the line, no handles.
    shown.session.selectTool(NavigationTool::move);
    QVERIFY(shown.session.gradientEdit() && overlay.geometry());
    QCOMPARE(qAlpha(drawn().pixel(int(overlay.geometry().value().handles[0].x()), int(overlay.geometry().value().handles[0].y()))), 0);
    QTRY_VERIFY(!shown.session.gradientEdit());
}

void GradientCanvasTests::aFoldersPendingMaskClipsAtOnce()
{
    Canvas shown;
    QImage image = BrushRaster::context(400, 300, false);
    image.fill(QColor(200, 30, 30));
    shown.session.insert(ImportedImage(image, image, QStringLiteral("Red")));
    shown.session.groupSelectedLayers();
    const QUuid folder = shown.session.activeLayerID().value();
    QVERIFY(shown.session.document().value().layers[size_t(indexOf(shown.session.document().value().layers, folder))].isGroup);
    shown.session.addLayerMask(true);
    shown.session.selectLayerTarget(folder, true);
    shown.session.selectTool(NavigationTool::gradient);
    GradientSettings settings = shown.session.gradientSettings();
    settings.style = GradientStyle::foregroundToBackground;
    shown.session.setGradientSettings(settings);
    shown.canvas->synchronizeDisplay();
    QCOMPARE(shownAt(shown, 20, 100), QColor(200, 30, 30));
    // Black to white across the folder's mask, still pending.
    shown.drag(QPointF(0, 150), QPointF(400, 150));
    QVERIFY(shown.session.gradientEdit().value().raster->isMask);
    // Over gray checks, red minus green measures the red shown.
    const auto red = [&](int x) {
        const QColor seen = shownAt(shown, x, 100);
        return seen.red() - seen.green();
    };
    const int mid = red(200);
    QVERIFY2(red(20) < 20 && red(380) > 150 && std::abs(mid - 85) <= 4, qPrintable(QString::number(mid)));
    // The step lands as it looked.
    shown.session.commitGradient();
    QTRY_VERIFY(!shown.session.gradientEdit());
    shown.canvas->synchronizeDisplay();
    QVERIFY(std::abs(red(200) - mid) <= 2);
}

void GradientCanvasTests::theBarSetsTheGradient()
{
    EditorSession session;
    session.createDocument(40, 20, true);
    session.selectTool(NavigationTool::gradient);
    GradientControls bar(session);
    bar.show();
    QCOMPARE(bar.title->text(), QString("Gradient"));
    QStringList labels;
    for (const QLabel *label : bar.findChildren<QLabel *>())
        labels << label->text();
    labels.sort();
    QCOMPARE(labels, (QStringList{"%", "Gradient", "Mask", "Opacity"}));
    auto &linear = find<QToolButton>(bar, "gradientLinear"), &radial = find<QToolButton>(bar, "gradientRadial");
    QCOMPARE(radial.toolTip(), QString("Linear runs along the line; Radial spreads out from the start point"));
    QVERIFY(linear.isChecked());
    radial.click();
    QCOMPARE(session.gradientSettings().shape, GradientShape::radial);
    QVERIFY(radial.isChecked() && !linear.isChecked());
    linear.click();
    QCOMPARE(session.gradientSettings().shape, GradientShape::linear);
    QVERIFY(linear.isChecked() && !radial.isChecked());
    auto &style = find<QComboBox>(bar, "gradientStyle");
    QCOMPARE(style.count(), 2);
    QCOMPARE(style.itemText(0), QString("Foreground to Background"));
    QCOMPARE(style.itemText(1), QString("Foreground to Transparent"));
    QCOMPARE(style.currentIndex(), 1);
    style.setCurrentIndex(0);
    emit style.activated(0);
    QCOMPARE(session.gradientSettings().style, GradientStyle::foregroundToBackground);
    style.setCurrentIndex(1);
    emit style.activated(1);
    QCOMPARE(session.gradientSettings().style, GradientStyle::foregroundToTransparent);
    auto &reverse = find<QCheckBox>(bar, "gradientReverse");
    reverse.click();
    QVERIFY(session.gradientSettings().reversed && reverse.isChecked());
    reverse.click();
    QVERIFY(!session.gradientSettings().reversed);
    // The slider runs from 1% to 100% in thousandths.
    auto &slider = find<QSlider>(bar, "gradientOpacitySlider");
    QCOMPARE(slider.width(), 100);
    slider.setValue(0);
    QCOMPARE(session.gradientSettings().opacity, 0.01);
    slider.setValue(5000);
    QCOMPARE(session.gradientSettings().opacity, 1.0);
    slider.setValue(333);
    QCOMPARE(session.gradientSettings().opacity, 0.333);
    auto &field = find<SelectionAmountField>(bar, "gradientOpacity");
    QCOMPARE(field.text(), QString("33"));
    QCOMPARE(field.width(), 42);
    QCOMPARE(field.toolTip(), QString("Press 1–9 for 10–90%, 0 for 100%"));
    field.setFocus();
    field.selectAll();
    QTest::keyClicks(&field, "70");
    QTest::keyClick(&field, Qt::Key_Return);
    QCOMPARE(session.gradientSettings().opacity, 0.7);
    // The session's changes show without writing back.
    session.setGradientSettings(GradientSettings{GradientShape::radial, GradientStyle::foregroundToBackground, true, 0.12345});
    QVERIFY(radial.isChecked() && style.currentIndex() == 0 && reverse.isChecked());
    QCOMPARE(slider.value(), 123);
    QCOMPARE(field.text(), QString("12"));
    QCOMPARE(session.gradientSettings().opacity, 0.12345);
    session.setGradientSettings(GradientSettings{GradientShape::linear, GradientStyle::foregroundToTransparent, false, 0.5});
    QVERIFY(linear.isChecked() && style.currentIndex() == 1 && !reverse.isChecked());
    QCOMPARE(slider.value(), 500);
    // Apply and Cancel while pending, at the far end.
    auto &apply = find<QPushButton>(bar, "gradientApply"), &cancel = find<QPushButton>(bar, "gradientCancel");
    QVERIFY(!apply.isVisible() && !cancel.isVisible());
    session.beginGradient(QPointF(1, 1));
    session.moveGradient(std::nullopt, QPointF(30, 1));
    QVERIFY(apply.isVisible() && cancel.isVisible());
    bar.resize(bar.sizeHint().width() + 300, bar.height());
    QTRY_VERIFY(cancel.x() - field.geometry().right() > 250 && apply.x() > cancel.x());
    const int count = session.history.undoCount();
    cancel.click();
    QVERIFY(!session.gradientEdit());
    QCOMPARE(session.history.undoCount(), count);
    session.beginGradient(QPointF(1, 1));
    session.moveGradient(std::nullopt, QPointF(30, 1));
    apply.click();
    QTRY_COMPARE(session.history.undoCount(), count + 1);
    // The edit ends from the event loop, after its step.
    QTRY_VERIFY(!session.gradientEdit());
    // The note, in secondary text, on a mask.
    auto &note = find<QLabel>(bar, "gradientMaskNote");
    QVERIFY(!note.isVisible());
    QCOMPARE(note.foregroundRole(), QPalette::PlaceholderText);
    session.addLayerMask(true);
    QVERIFY(note.isVisible());
    session.setIsProjectBusy(true);
    QTRY_VERIFY(!bar.isEnabled());
    session.setIsProjectBusy(false);
    QVERIFY(bar.isEnabled());
    EditorSession empty;
    const GradientControls idle(empty);
    QVERIFY(!idle.isEnabled());
}

void GradientCanvasTests::theSwatchShowsTheColoursOverChecks()
{
    EditorSession session;
    session.createDocument(40, 20, true);
    session.selectTool(NavigationTool::gradient);
    session.setForegroundColor(PaletteColor{1, 0, 0});
    GradientControls bar(session);
    QWidget &swatch = find<QWidget>(bar, "gradientSwatch");
    PaintSpy spy(swatch);
    bar.show();
    QTRY_VERIFY(spy.count > 0);
    QCOMPARE(swatch.size(), QSize(56, 18));
    const auto drawn = [&] {
        QImage image(56, 18, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        swatch.render(&image, QPoint(), QRegion(), QWidget::DrawChildren);
        return image;
    };
    const QImage shown = drawn();
    // Red at the start, fading to clear over the checks.
    QVERIFY(shown.pixelColor(1, 9).red() > 240 && shown.pixelColor(1, 9).green() < 12);
    QVERIFY(std::abs(shown.pixelColor(28, 10).green() - 130) <= 4);
    QVERIFY(std::abs(shown.pixelColor(50, 6).green() - 230) <= 4);
    for (const int y : {2, 6, 10, 14, 16})
        QCOMPARE(shown.pixelColor(50, y).green() < 210, y != 6 && y != 14);
    // A half-black rim, its corners rounded and smooth.
    QVERIFY(shown.pixelColor(28, 0).green() < shown.pixelColor(28, 10).green() - 40);
    QVERIFY(qAlpha(shown.pixel(0, 0)) < 40);
    QVERIFY(qAlpha(shown.pixel(1, 0)) > 40 && qAlpha(shown.pixel(1, 0)) < 240);
    // Every change repaints it.
    QApplication::processEvents();
    spy.count = 0;
    session.setForegroundColor(PaletteColor{0, 0, 1});
    QTRY_VERIFY(spy.count > 0);
    QVERIFY(drawn().pixelColor(1, 9).blue() > 240);
}

QTEST_MAIN(GradientCanvasTests)
#include "GradientCanvasTests.moc"
