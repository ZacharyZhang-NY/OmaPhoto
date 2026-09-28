#include "MenuFixtures.h"
#include "SelectionCanvasFixtures.h"
#include "UI/CameraRawControls.h"
#include "UI/FilterSheet.h"
#include <QLabel>
#include <QToolButton>

// Camera Raw's tools on the canvas, and its docked panel.
namespace {
struct Raw : Canvas {
    Raw()
    {
        QImage image(400, 300, QImage::Format_RGBA8888_Premultiplied);
        image.fill(QColor(200, 120, 60));
        for (int y = 0; y < 300; ++y)
            for (int x = 200; x < 400; ++x)
                image.setPixelColor(x, y, QColor(60, 180, 90));
        session.insert(ImportedImage(image, image, QStringLiteral("Two")));
        session.beginFilter(FilterKind::cameraRaw);
        canvas->synchronizeDisplay();
    }
    const CameraRawPanel &panel() const { return session.filterEdit().value().rawPanel; }
    const CameraRawSettings &raw() const { return session.filterEdit().value().settings.cameraRaw; }
    void arm(const std::function<void(CameraRawPanel &)> &change)
    {
        CameraRawPanel copy = panel();
        change(copy);
        session.setCameraRawPanel(copy);
        canvas->synchronizeDisplay();
    }
};

QWidget *topLevel(const QString &name)
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() == name && widget->isVisible())
            return widget;
    }
    return nullptr;
}
}

class CameraRawCanvasTests : public QObject {
    Q_OBJECT
private slots:
    void aHoverReadsThePixelAndLeavingClearsIt();
    void eachEyedropperSamplesAndThePressGoesNoFurther();
    void aGuideIsDrawnByDragging();
    void aTargetedDragEndsOnReleaseOrWhenTheReleaseIsLost();
    void samplingToolsShowTheEyedropperAndSpacePans();
    void thePanelDocksInTheWindowAndCancelsLikeAPanel();
};

void CameraRawCanvasTests::aHoverReadsThePixelAndLeavingClearsIt()
{
    Raw raw;
    raw.hover(QPointF(50, 50));
    QCOMPARE(raw.panel().readout, std::optional(std::array{200, 120, 60}));
    raw.hover(QPointF(300, 50));
    QCOMPARE(raw.panel().readout, std::optional(std::array{60, 180, 90}));
    // A drag reads nothing: Swift's mouseMoved alone reads.
    raw.move(QPointF(50, 50));
    QCOMPARE(raw.panel().readout, std::optional(std::array{60, 180, 90}));
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(raw.canvas, &leave);
    QVERIFY(!raw.panel().readout);
    // Another filter reads nothing.
    raw.session.cancelFilter();
    raw.session.beginFilter(FilterKind::gaussianBlur);
    raw.hover(QPointF(50, 50));
    QVERIFY(!raw.session.filterEdit().value().rawPanel.readout);
}

void CameraRawCanvasTests::eachEyedropperSamplesAndThePressGoesNoFurther()
{
    Raw raw;
    raw.arm([](CameraRawPanel &panel) { panel.samplesWhiteBalance = true; });
    raw.click(QPointF(50, 50));
    QVERIFY(raw.raw().temperature != 0 && !raw.session.lassoDraft() && !raw.session.selection());
    raw.arm([](CameraRawPanel &panel) {
        panel.samplesWhiteBalance = false;
        panel.samplesPointColor = true;
    });
    raw.click(QPointF(300, 50));
    QCOMPARE(raw.raw().mixer.points.size(), size_t(1));
    QVERIFY(std::abs(raw.raw().mixer.points[0].hue - 135) < 1);
    raw.arm([](CameraRawPanel &panel) {
        panel.samplesPointColor = false;
        panel.samplesDefringe = true;
    });
    raw.click(QPointF(300, 50));
    QCOMPARE(raw.raw().optics.greenAmount, 50.0);
    QVERIFY(!raw.session.selection());
}

void CameraRawCanvasTests::aGuideIsDrawnByDragging()
{
    Raw raw;
    raw.arm([](CameraRawPanel &panel) { panel.drawingGeometryGuide = true; });
    raw.press(QPointF(40, 30));
    QVERIFY(raw.panel().guideDraft);
    raw.move(QPointF(360, 60));
    raw.release(QPointF(360, 60));
    QVERIFY(!raw.panel().guideDraft);
    QCOMPARE(raw.raw().geometry.guides.size(), size_t(1));
    const CameraRawGeometryGuide guide = raw.raw().geometry.guides[0];
    QVERIFY(std::abs(guide.startX - 0.1) < 1e-6 && std::abs(guide.startY - 0.1) < 1e-6);
    QVERIFY(std::abs(guide.endX - 0.9) < 1e-6 && std::abs(guide.endY - 0.2) < 1e-6);
    QVERIFY(raw.raw().geometry.upright == CameraRawUprightMode::guided);
    // A lost release: the next move without the button commits.
    raw.press(QPointF(100, 30));
    raw.move(QPointF(100, 270));
    raw.hover(QPointF(120, 270));
    QVERIFY(!raw.panel().guideDraft);
    QCOMPARE(raw.raw().geometry.guides.size(), size_t(2));
}

void CameraRawCanvasTests::aTargetedDragEndsOnReleaseOrWhenTheReleaseIsLost()
{
    Raw raw;
    raw.arm([](CameraRawPanel &panel) { panel.targetsCurve = true; });
    raw.press(QPointF(50, 150));
    QVERIFY(raw.panel().drag);
    raw.move(QPointF(50, 50));
    QCOMPARE(raw.raw().curve.lights, 35.0);
    raw.release(QPointF(50, 50));
    QVERIFY(!raw.panel().drag);
    // A move after the release changes nothing.
    raw.move(QPointF(50, 0));
    QCOMPARE(raw.raw().curve.lights, 35.0);
    raw.press(QPointF(50, 150));
    raw.hover(QPointF(50, 100));
    QVERIFY(!raw.panel().drag);
    QCOMPARE(raw.raw().curve.lights, 35.0);
}

void CameraRawCanvasTests::samplingToolsShowTheEyedropperAndSpacePans()
{
    Raw raw;
    const QCursor eyedropper = CanvasView::eyedropperCursor(raw.canvas->devicePixelRatio());
    raw.hover(QPointF(50, 50));
    QVERIFY(!raw.shows(eyedropper));
    for (const auto &change : std::vector<std::function<void(CameraRawPanel &)>>{
             [](CameraRawPanel &panel) { panel.samplesWhiteBalance = true; }, [](CameraRawPanel &panel) { panel.samplesPointColor = true; },
             [](CameraRawPanel &panel) { panel.samplesDefringe = true; }, [](CameraRawPanel &panel) { panel.drawingGeometryGuide = true; }}) {
        raw.arm([](CameraRawPanel &panel) { panel = CameraRawPanel(); });
        raw.hover(QPointF(50, 50));
        QVERIFY(!raw.shows(eyedropper));
        raw.arm(change);
        raw.hover(QPointF(52, 50));
        QVERIFY(raw.shows(eyedropper));
    }
    // With Space held a press pans and samples nothing.
    raw.arm([](CameraRawPanel &panel) {
        panel = CameraRawPanel();
        panel.samplesWhiteBalance = true;
    });
    raw.canvas->setFocus();
    QTest::keyPress(raw.canvas, Qt::Key_Space);
    raw.press(QPointF(50, 50));
    raw.release(QPointF(50, 50));
    QTest::keyRelease(raw.canvas, Qt::Key_Space);
    QCOMPARE(raw.raw().temperature, 0.0);
}

void CameraRawCanvasTests::thePanelDocksInTheWindowAndCancelsLikeAPanel()
{
    Bar bar;
    bar.window.resize(1200, 800);
    bar.window.show();
    QVERIFY(QTest::qWaitForWindowActive(&bar.window));
    bar.session().createDocument(8, 8, true);
    QImage image(8, 8, QImage::Format_RGBA8888_Premultiplied);
    image.fill(QColor(200, 120, 60));
    bar.session().insert(ImportedImage(image, image, QStringLiteral("Warm")));
    // A floating filter panel, moved, then closed.
    bar.session().beginFilter(FilterKind::gaussianBlur);
    QWidget *floating = topLevel(QStringLiteral("filterPanel"));
    QVERIFY(floating);
    floating->move(40, 60);
    const QPoint parked = floating->pos();
    bar.session().cancelFilter();
    // Camera Raw fills the window's right slot instead.
    bar.session().beginFilter(FilterKind::cameraRaw);
    auto *dock = bar.window.findChild<QWidget *>(QStringLiteral("panelDock"));
    QVERIFY(dock && dock->isVisible() && !topLevel(QStringLiteral("filterPanel")));
    QCOMPARE(dock->width(), 440);
    auto *docked = dock->findChild<QWidget *>(QStringLiteral("filterPanel"));
    QVERIFY(docked && !docked->isWindow() && docked->findChild<CameraRawControls *>());
    QCOMPARE(docked->findChild<QLabel *>(QStringLiteral("dockedTitle"))->text(), QString("Camera Raw Filter"));
    QTRY_VERIFY(docked->height() > 600);
    // Shown, it takes the keys, Swift's makeKeyAndOrderFront.
    QVERIFY(docked->hasFocus());
    // At the right edge; a taller window, a taller panel.
    QCOMPARE(dock->mapTo(&bar.window, dock->rect().topRight()).x(), bar.window.centralWidget()->mapTo(&bar.window, bar.window.centralWidget()->rect().topRight()).x());
    const int height = docked->height();
    bar.window.resize(1200, 900);
    QTRY_COMPARE(docked->height(), height + 100);
    // A sample hands the keys back to the docked panel.
    auto *canvas = bar.window.findChild<CanvasView *>();
    CameraRawPanel panel = bar.session().filterEdit().value().rawPanel;
    panel.samplesWhiteBalance = true;
    bar.session().setCameraRawPanel(panel);
    canvas->setFocus();
    const QPointF at = bar.session().viewport.viewPoint(QPointF(4, 4), QSizeF(8, 8));
    QTest::mouseClick(canvas, Qt::LeftButton, {}, at.toPoint());
    QVERIFY(docked->isAncestorOf(QApplication::focusWidget()) || QApplication::focusWidget() == docked);
    // Its close button cancels; Escape inside it too.
    docked->findChild<QToolButton *>(QStringLiteral("dockedClose"))->click();
    QVERIFY(!bar.session().filterEdit() && !dock->isVisible());
    bar.session().beginFilter(FilterKind::cameraRaw);
    QVERIFY(dock->isVisible());
    docked->setFocus();
    QTest::keyClick(docked, Qt::Key_Escape);
    QVERIFY(!bar.session().filterEdit() && !dock->isVisible());
    // The floating filters reopen where they were left.
    bar.session().beginFilter(FilterKind::gaussianBlur);
    QCOMPARE(topLevel(QStringLiteral("filterPanel"))->pos(), parked);
    QVERIFY(!dock->isVisible());
    bar.session().cancelFilter();
}

QTEST_MAIN(CameraRawCanvasTests)
#include "CameraRawCanvasTests.moc"
