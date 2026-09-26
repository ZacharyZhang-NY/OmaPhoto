#include "Document/BrushStroke.h"
#include "Rendering/AdjustmentSurface.h"
#include "Rendering/EditorCanvas.h"
#include "RenderFixtures.h"
#include "UI/LevelsSheet.h"
#include <QtTest>

// Widgets at device pixel ratio 2: two coordinate systems.
namespace {
struct Probe : QWidget {
    std::function<void(QPainter &)> body;
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        if (body)
            body(painter);
    }
};

// A probe at an offset inside a window, both shown.
struct Framed {
    QWidget window;
    Probe child;
    Framed()
    {
        window.resize(300, 200);
        child.setParent(&window);
        child.setGeometry(57, 42, 200, 100);
        window.show();
        if (!QTest::qWaitForWindowExposed(&window))
            throw std::runtime_error("the window never showed");
    }
};
}

class HighDpiCanvasTests : public QObject {
    Q_OBJECT
private slots:
    void theScreenScalesByTwo();
    void theVisibleRectIsTheWidgetInItsOwnPoints();
    void theSurfaceCoversAnOffsetWidgetInDevicePixels();
    void theCanvasDrawsDevicePixels();
    void levelsEyedroppersDrawDevicePixels();
};

void HighDpiCanvasTests::theScreenScalesByTwo()
{
    QWidget widget;
    QCOMPARE(widget.devicePixelRatio(), 2.0);
}

void HighDpiCanvasTests::theVisibleRectIsTheWidgetInItsOwnPoints()
{
    Framed framed;
    std::vector<QRectF> seen;
    framed.child.body = [&](QPainter &painter) {
        seen.push_back(BrushRaster::visibleRect(painter));
        painter.translate(10, 5);
        seen.push_back(BrushRaster::visibleRect(painter));
        painter.setClipRect(QRectF(10, 10, 50, 20));
        seen.push_back(BrushRaster::visibleRect(painter));
    };
    // Through a grab and through the window's own store alike.
    framed.child.grab();
    framed.child.repaint();
    QCOMPARE(int(seen.size()), 6);
    for (size_t pass = 0; pass < 6; pass += 3) {
        QCOMPARE(seen[pass], QRectF(0, 0, 200, 100));
        QCOMPARE(seen[pass + 1], QRectF(-10, -5, 200, 100));
        QCOMPARE(seen[pass + 2], QRectF(10, 10, 50, 20));
    }
}

void HighDpiCanvasTests::theSurfaceCoversAnOffsetWidgetInDevicePixels()
{
    Framed framed;
    QSize allocated;
    framed.child.body = [&](QPainter &painter) {
        AdjustmentSurface::draw(painter, [&](QPainter &surface) {
            allocated = QSize(surface.device()->width(), surface.device()->height());
            surface.fillRect(QRectF(0, 0, 200, 100), Qt::red);
        });
    };
    QImage shot = framed.child.grab().toImage().convertToFormat(QImage::Format_ARGB32);
    QCOMPARE(shot.size(), QSize(400, 200));
    QCOMPARE(allocated, QSize(400, 200));
    for (const QPoint &point : {QPoint(0, 0), QPoint(399, 0), QPoint(0, 199), QPoint(399, 199), QPoint(200, 100)})
        QCOMPARE(shot.pixel(point), qRgb(255, 0, 0));
    // Through the window the child keeps its store offset.
    const QImage whole = framed.window.grab().toImage().convertToFormat(QImage::Format_ARGB32);
    QCOMPARE(whole.size(), QSize(600, 400));
    for (const QPoint &point : {QPoint(114, 84), QPoint(513, 84), QPoint(114, 283), QPoint(513, 283), QPoint(314, 184)})
        QCOMPARE(whole.pixel(point), qRgb(255, 0, 0));
    QVERIFY(whole.pixel(113, 84) != qRgb(255, 0, 0));
    QVERIFY(whole.pixel(514, 283) != qRgb(255, 0, 0));
    // A clip bounds the surface, in device pixels too.
    framed.child.body = [&](QPainter &painter) {
        painter.fillRect(QRectF(0, 0, 200, 100), Qt::black);
        painter.setClipRect(QRectF(10, 10, 50, 20));
        AdjustmentSurface::draw(painter, [&](QPainter &surface) {
            allocated = QSize(surface.device()->width(), surface.device()->height());
            surface.fillRect(QRectF(0, 0, 200, 100), Qt::red);
        });
    };
    shot = framed.child.grab().toImage().convertToFormat(QImage::Format_ARGB32);
    QCOMPARE(allocated, QSize(100, 40));
    QCOMPARE(shot.pixel(20, 20), qRgb(255, 0, 0));
    QCOMPARE(shot.pixel(119, 59), qRgb(255, 0, 0));
    QCOMPARE(shot.pixel(10, 10), qRgb(0, 0, 0));
    QCOMPARE(shot.pixel(120, 60), qRgb(0, 0, 0));
}

void HighDpiCanvasTests::theCanvasDrawsDevicePixels()
{
    EditorSession session;
    session.createDocument(2, 1);
    QImage edge = BrushRaster::context(2, 1, false);
    edge.setPixel(0, 0, qRgba(0, 0, 0, 255));
    edge.setPixel(1, 0, qRgba(255, 255, 255, 255));
    session.insert(ImportedImage(edge, edge, "Edge"), QPointF(1, 0.5));
    session.setShowsTransformControls(false);
    QWidget window;
    auto *canvas = new CanvasView(session, &window);
    QPalette dark = canvas->palette();
    dark.setColor(QPalette::Base, QColor(0x1b, 0x1b, 0x1b));
    canvas->setPalette(dark);
    window.resize(300, 200);
    canvas->setGeometry(0, 0, 40, 20);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_COMPARE(session.viewport.backingScale, 2.0);
    QCOMPARE(session.viewport.viewSize, QSizeF(40, 20));
    session.zoom(4);
    QCOMPARE(session.viewport.pointsPerPixel(), 2.0);
    const QImage shot = canvas->grab().toImage().convertToFormat(QImage::Format_ARGB32);
    QCOMPARE(shot.size(), QSize(80, 40));
    // The two document pixels span eight device pixels, hard-edged.
    QCOMPARE(qRed(shot.pixel(37, 19)), 0);
    QCOMPARE(qRed(shot.pixel(39, 19)), 0);
    QCOMPARE(qRed(shot.pixel(40, 19)), 255);
    QCOMPARE(qRed(shot.pixel(42, 19)), 255);
    QCOMPARE(qRed(shot.pixel(2, 2)), 27);
}

void HighDpiCanvasTests::levelsEyedroppersDrawDevicePixels()
{
    EditorSession session;
    session.createDocument(2, 1);
    QImage red(2, 1, QImage::Format_RGBA8888_Premultiplied);
    red.fill(Qt::red);
    session.insert(ImportedImage(red, red, QStringLiteral("Red")));
    session.beginLevels();
    LevelsSheet sheet(session);
    sheet.show();
    QVERIFY(QTest::qWaitForWindowExposed(&sheet));
    // Fourteen points: the glyph spans all 28 pixels.
    for (QPushButton *button : sheet.findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("Black")) {
            const QImage drawn = button->icon().pixmap(QSize(14, 14)).toImage();
            QCOMPARE(drawn.size(), QSize(28, 28));
            int right = 0;
            for (int y = 0; y < 28; ++y) {
                for (int x = 16; x < 28; ++x)
                    right += qAlpha(drawn.pixel(x, y)) > 0;
            }
            QVERIFY(right > 20);
        }
    }
}

int main(int argc, char **argv)
{
    // Every widget on this screen scales by two.
    qputenv("QT_SCALE_FACTOR", "2");
    QApplication app(argc, argv);
    HighDpiCanvasTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "HighDpiCanvasTests.moc"
