#include "UI/JPEGExportSheet.h"
#include "Document/EditorSession.h"
#include "UI/KeyboardShortcuts.h"
#include <QHBoxLayout>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollBar>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QtTest>

// Swift 1.3.5's JPEGPreview: fitted or zoomed, dragged, its keys.
class JPEGPreviewTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void fitAndStepsFollowSwift();
    void itDrawsFittedAndBelowOneThroughAnAverage();
    void fromOneUpEachPixelShowsAsItIs();
    void theUpdatingPlateSitsInTheMiddle();
    void zoomKeepsTheMiddleAndDragsMoveIt();
    void theSheetsButtonsAndKeysZoomIt();
};

namespace {
QImage noise(int width, int height, quint32 seed)
{
    QImage image(width, height, QImage::Format_RGBA8888_Premultiplied);
    QRandomGenerator random(seed);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            image.setPixelColor(x, y, QColor::fromRgb(random.bounded(256), random.bounded(256), random.bounded(256)));
    return image;
}

QImage drawn(JPEGPreview &preview)
{
    return preview.viewport()->grab().toImage().convertToFormat(QImage::Format_RGB32);
}

// The part of the view at `rect` equals `reference`.
bool shows(JPEGPreview &preview, QRect rect, const QImage &reference)
{
    return drawn(preview).copy(rect) == reference.convertToFormat(QImage::Format_RGB32);
}

// A move with `held` down.
void dragTo(QWidget *widget, QPoint at, Qt::MouseButtons held = Qt::LeftButton)
{
    QMouseEvent event(QEvent::MouseMove, at, widget->mapToGlobal(at), Qt::NoButton, held, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

// Counts a widget's repaints.
struct Paints : QObject {
    int count = 0;
    explicit Paints(QWidget *widget) { widget->installEventFilter(this); }
    bool eventFilter(QObject *, QEvent *event) override
    {
        count += event->type() == QEvent::Paint;
        return false;
    }
};

struct Shown {
    JPEGPreview preview;
    explicit Shown(QSize pixels, std::function<void()> zoomed = [] {}) : preview(pixels, std::move(zoomed), nullptr)
    {
        preview.show();
        if (!QTest::qWaitForWindowExposed(&preview))
            throw std::runtime_error("the preview never showed");
    }
    // The zoom, then the scroll bars it brings.
    void zoom(std::optional<double> zoom)
    {
        preview.setZoom(zoom);
        QCoreApplication::processEvents();
    }
};
}

void JPEGPreviewTests::fitAndStepsFollowSwift()
{
    QCOMPARE(JPEGPreview::frame, QSize(560, 330));
    QCOMPARE(JPEGPreview::fitZoom(1200, 800, QSizeF(560, 330), 1), 330.0 / 800);
    QCOMPARE(JPEGPreview::fitZoom(1200, 800, QSizeF(560, 330), 2), 330.0 / 400);
    QCOMPARE(JPEGPreview::fitZoom(100, 400, QSizeF(560, 330), 0.5), 330.0 / 400);
    QCOMPARE(JPEGPreview::step(0.4125, 1), std::optional(0.5));
    QCOMPARE(JPEGPreview::step(0.4125, -1), std::optional(0.25));
    // A step a hair away counts as that step.
    QCOMPARE(JPEGPreview::step(0.9995, 1), std::optional(2.0));
    QCOMPARE(JPEGPreview::step(1.0005, -1), std::optional(0.5));
    QCOMPARE(JPEGPreview::step(0.998, 1), std::optional(1.0));
    QCOMPARE(JPEGPreview::step(1.002, -1), std::optional(1.0));
    QCOMPARE(JPEGPreview::step(8, 1), std::nullopt);
    QCOMPARE(JPEGPreview::step(0.25, -1), std::nullopt);
    QCOMPARE(JPEGPreview::step(20, -1), std::optional(8.0));
}

void JPEGPreviewTests::itDrawsFittedAndBelowOneThroughAnAverage()
{
    Shown shown(QSize(1100, 660));
    JPEGPreview &preview = shown.preview;
    QVERIFY(preview.size() == QSize(560, 330) && preview.viewport()->cursor().shape() == Qt::ArrowCursor);
    QCOMPARE(preview.toolTip(), QString("Drag or scroll to move around; double-click switches between Fit and 100%"));
    QVERIFY(preview.horizontalScrollBar()->maximum() == 0 && preview.verticalScrollBar()->maximum() == 0);
    QVERIFY(!preview.horizontalScrollBar()->isVisible() && preview.horizontalScrollBar()->singleStep() == 20 && preview.verticalScrollBar()->singleStep() == 20);
    // Nothing yet: the dark gray.
    QCOMPARE(drawn(preview).pixelColor(280, 165), QColor(31, 31, 31));
    Paints paints(preview.viewport());
    const QImage first = noise(1100, 660, 1);
    preview.setImage(first, false);
    QTRY_VERIFY(paints.count > 0);
    // Fitted, 550 by 330 in the middle, averaged down.
    QVERIFY(shows(preview, QRect(5, 0, 550, 330), first.scaled(550, 330, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)));
    QCOMPARE(drawn(preview).pixelColor(4, 165), QColor(31, 31, 31));
    // A new image of the same size is averaged anew.
    const QImage second = noise(1100, 660, 2);
    preview.setImage(second, false);
    QVERIFY(shows(preview, QRect(5, 0, 550, 330), second.scaled(550, 330, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)));
    // A quarter, then Fit again: each size averaged for itself.
    QCoreApplication::processEvents();
    paints.count = 0;
    shown.zoom(0.25);
    QTRY_VERIFY(paints.count > 0);
    QVERIFY(shows(preview, QRect(143, 83, 275, 165), second.scaled(275, 165, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)));
    QVERIFY(drawn(preview).pixelColor(141, 165) == QColor(31, 31, 31) && drawn(preview).pixelColor(280, 81) == QColor(31, 31, 31));
    QCOMPARE(preview.horizontalScrollBar()->maximum(), 0);
    shown.zoom(std::nullopt);
    QVERIFY(shows(preview, QRect(5, 0, 550, 330), second.scaled(550, 330, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)));
}

void JPEGPreviewTests::fromOneUpEachPixelShowsAsItIs()
{
    Shown shown(QSize(1100, 660));
    JPEGPreview &preview = shown.preview;
    const QImage image = noise(1100, 660, 3);
    preview.setImage(image, false);
    shown.zoom(2);
    // Bars come once laid out; ranges follow the view.
    QScrollBar &across = *preview.horizontalScrollBar(), &down = *preview.verticalScrollBar();
    const QSize view = preview.viewport()->size();
    QVERIFY(across.isVisible() && down.isVisible() && view.width() < 560 && view.height() < 330);
    QVERIFY(across.maximum() == 2200 - view.width() && down.maximum() == 1320 - view.height());
    QVERIFY(across.pageStep() == view.width() && down.pageStep() == view.height());
    // Twice, nearest: each JPEG pixel a 2 by 2 block.
    across.setValue(100);
    down.setValue(60);
    QVERIFY(shows(preview, QRect(0, 0, 200, 100), image.copy(50, 30, 100, 50).scaled(200, 100)));
    // A smaller decode still spans the export's pixels.
    preview.setImage(image.scaled(550, 330), false);
    shown.zoom(1);
    QCOMPARE(down.maximum(), 660 - preview.viewport()->height());
    // A size between pixels reaches its last one.
    Shown odd(QSize(1201, 1201));
    odd.preview.setImage(noise(1201, 1201, 4), false);
    odd.zoom(0.5);
    QCOMPARE(odd.preview.horizontalScrollBar()->maximum(), int(std::ceil(600.5 - odd.preview.viewport()->width())));
}

void JPEGPreviewTests::theUpdatingPlateSitsInTheMiddle()
{
    Shown shown(QSize(1100, 660));
    shown.preview.setImage(QImage(), true);
    const QImage image = drawn(shown.preview);
    const QColor window = shown.preview.palette().color(QPalette::Window);
    const int plate = qRound(window.red() * 0.85 + 31 * 0.15);
    // 64 square, no rim: its edges sit on whole pixels.
    QVERIFY(std::abs(image.pixelColor(248, 165).red() - plate) <= 1 && std::abs(image.pixelColor(311, 165).red() - plate) <= 1);
    QVERIFY(image.pixelColor(247, 165) == QColor(31, 31, 31) && image.pixelColor(312, 165) == QColor(31, 31, 31));
    QVERIFY(image.pixelColor(280, 132) == QColor(31, 31, 31) && std::abs(image.pixelColor(280, 133).red() - plate) <= 1);
    // Its corners round by 8, antialiased.
    QCOMPARE(image.pixelColor(248, 133), QColor(31, 31, 31));
    const int corner = image.pixelColor(250, 135).red();
    QVERIFY(corner > 40 && corner < plate - 10);
}

void JPEGPreviewTests::zoomKeepsTheMiddleAndDragsMoveIt()
{
    int heard = 0;
    Shown shown(QSize(1200, 800), [&heard] { ++heard; });
    JPEGPreview &preview = shown.preview;
    preview.setImage(noise(1200, 800, 5), false);
    // From Fit, the middle of the image.
    Paints paints(preview.viewport());
    preview.setZoom(1);
    QVERIFY(heard == 1 && paints.count == 0);
    QTRY_VERIFY(paints.count > 0);
    QScrollBar &across = *preview.horizontalScrollBar(), &down = *preview.verticalScrollBar();
    QVERIFY(across.value() == 320 && down.value() == 235);
    QCoreApplication::processEvents();
    // Zooming keeps the point in the middle where it was.
    const QSize view = preview.viewport()->size();
    across.setValue(0);
    down.setValue(0);
    shown.zoom(2);
    QVERIFY(across.value() == int(std::lround(view.width() / 2.0)) && down.value() == int(std::lround(view.height() / 2.0)));
    shown.zoom(2);
    QCOMPARE(heard, 2);
    // Smaller than the view, its middle is the image's middle.
    shown.zoom(0.25);
    preview.setZoom(0.5);
    QVERIFY(across.value() == 20 && down.value() == 35);
    // A half-pixel landing rounds, as Swift scrolls to it.
    Shown odd(QSize(1201, 801));
    odd.preview.setZoom(1);
    QVERIFY(odd.preview.horizontalScrollBar()->value() == 321 && odd.preview.verticalScrollBar()->value() == 236);
    shown.zoom(2);
    // A drag moves the image with the pointer.
    across.setValue(500);
    down.setValue(300);
    QTest::mousePress(preview.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
    QCOMPARE(preview.viewport()->cursor().shape(), Qt::OpenHandCursor);
    dragTo(preview.viewport(), QPoint(60, 80));
    QVERIFY(across.value() == 540 && down.value() == 320 && preview.viewport()->cursor().shape() == Qt::ClosedHandCursor);
    QTest::mouseRelease(preview.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(60, 80));
    QCOMPARE(preview.viewport()->cursor().shape(), Qt::OpenHandCursor);
    dragTo(preview.viewport(), QPoint(20, 20));
    QCOMPARE(across.value(), 540);
    // Another button drags nothing; a new zoom ends a drag.
    QTest::mousePress(preview.viewport(), Qt::RightButton, Qt::NoModifier, QPoint(100, 100));
    dragTo(preview.viewport(), QPoint(60, 80), Qt::RightButton);
    QTest::mouseRelease(preview.viewport(), Qt::RightButton, Qt::NoModifier, QPoint(60, 80));
    QCOMPARE(across.value(), 540);
    QTest::mousePress(preview.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
    shown.zoom(4);
    const int zoomed = across.value();
    dragTo(preview.viewport(), QPoint(60, 80));
    QVERIFY(across.value() == zoomed && preview.viewport()->cursor().shape() == Qt::OpenHandCursor);
    QTest::mouseRelease(preview.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(60, 80));
    // Double-click: 100% to Fit and back; Fit drags nothing.
    QTest::mouseDClick(preview.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
    QVERIFY(!preview.zoom() && preview.viewport()->cursor().shape() == Qt::ArrowCursor);
    QTest::mousePress(preview.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
    dragTo(preview.viewport(), QPoint(60, 80));
    QCOMPARE(preview.viewport()->cursor().shape(), Qt::ArrowCursor);
    QTest::mouseRelease(preview.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(60, 80));
    QTest::mouseDClick(preview.viewport(), Qt::RightButton, Qt::NoModifier, QPoint(50, 50));
    QVERIFY(!preview.zoom());
    QTest::mouseDClick(preview.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
    QCOMPARE(preview.zoom(), std::optional(1.0));
}

void JPEGPreviewTests::theSheetsButtonsAndKeysZoomIt()
{
    QSettings().remove(JPEGExportSheet::qualityKey);
    EditorSession session;
    QImage red(1200, 800, QImage::Format_RGBA8888_Premultiplied);
    red.fill(Qt::red);
    JPEGExportSheet sheet({red, 72}, session, [](std::optional<QByteArray>) {});
    sheet.show();
    QVERIFY(QTest::qWaitForWindowActive(&sheet));
    auto &preview = *sheet.findChild<JPEGPreview *>();
    auto &fit = *sheet.findChild<QPushButton *>(QStringLiteral("jpegFit"));
    auto &in = *sheet.findChild<QPushButton *>(QStringLiteral("jpegZoomIn"));
    auto &out = *sheet.findChild<QPushButton *>(QStringLiteral("jpegZoomOut"));
    // Title row 8 above the preview; bottom row 12 apart.
    auto *top = qobject_cast<QVBoxLayout *>(sheet.layout()->itemAt(0)->layout());
    auto *title = qobject_cast<QHBoxLayout *>(top->itemAt(0)->layout());
    QVERIFY(top->spacing() == 8 && title->spacing() == 8 && title->itemAt(2)->widget() == &fit && title->itemAt(4)->widget() == &out);
    auto *bottom = qobject_cast<QHBoxLayout *>(sheet.layout()->itemAt(3)->layout());
    QVERIFY(bottom->spacing() == 12 && bottom->itemAt(0)->widget()->objectName() == QString("jpegSize") && bottom->itemAt(1)->spacerItem());
    QVERIFY(!fit.isEnabled() && in.isEnabled() && out.isEnabled() && in.accessibleName() == QString("Zoom in"));
    QCOMPARE(fit.toolTip(), QString("Show the whole image (Ctrl+0)"));
    QCOMPARE(in.toolTip(), QString("Zoom in (Ctrl+=), now 41%. At 100% each pixel of the JPEG is one pixel of the screen, as on the canvas"));
    QCOMPARE(out.toolTip(), QString("Zoom out (Ctrl+-), now 41%"));
    // The magnifiers in the button ink, redrawn with the theme.
    const QImage before = in.icon().pixmap(16).toImage();
    QPalette palette = sheet.palette();
    palette.setColor(QPalette::ButtonText, Qt::red);
    sheet.setPalette(palette);
    QVERIFY(in.icon().pixmap(16).toImage() != before && out.icon().pixmap(16).toImage() != in.icon().pixmap(16).toImage());
    out.click();
    QVERIFY(preview.zoom() == std::optional(0.25) && !out.isEnabled() && fit.isEnabled());
    QCOMPARE(in.toolTip(), QString("Zoom in (Ctrl+=), now 25%. At 100% each pixel of the JPEG is one pixel of the screen, as on the canvas"));
    in.click();
    QCOMPARE(preview.zoom(), std::optional(0.5));
    fit.click();
    QVERIFY(!preview.zoom());
    // The View menu's keys, blocked by the sheet, zoom it.
    QTest::keyClick(&sheet, Qt::Key_1, Qt::ControlModifier);
    QCOMPARE(preview.zoom(), std::optional(1.0));
    for (int step = 0; step < 4; ++step)
        QTest::keyClick(&sheet, Qt::Key_Equal, Qt::ControlModifier);
    QVERIFY(preview.zoom() == std::optional(8.0) && !in.isEnabled());
    QTest::keyClick(&sheet, Qt::Key_Minus, Qt::ControlModifier);
    QCOMPARE(preview.zoom(), std::optional(4.0));
    QTest::keyClick(&sheet, Qt::Key_0, Qt::ControlModifier);
    QVERIFY(!preview.zoom());
    // Remapped, the new key zooms and the tooltip says it.
    QSettings().remove(QLatin1String(ShortcutSettings::storageKey));
    QVERIFY(ShortcutSettings::shared().save({{QStringLiteral("Menus:Zoom In"), ShortcutChord(QStringLiteral("k"), 1)}}));
    QVERIFY(in.toolTip().startsWith(QStringLiteral("Zoom in (Ctrl+K), now 41%")));
    QTest::keyClick(&sheet, Qt::Key_Equal, Qt::ControlModifier);
    QVERIFY(!preview.zoom());
    QTest::keyClick(&sheet, Qt::Key_K, Qt::ControlModifier);
    QCOMPARE(preview.zoom(), std::optional(0.5));
    QSettings().remove(QLatin1String(ShortcutSettings::storageKey));
    ShortcutSettings::shared().reload();
}

QTEST_MAIN(JPEGPreviewTests)
#include "JPEGPreviewTests.moc"
