#include "ScrubFixtures.h"
#include "UI/LayerAppearanceControls.h"
#include "UI/NavigationToolHeader.h"
#include "UI/TransformInspector.h"

// Swift's scrubbable Move bar, zoom unit and layer opacity.
class ScrubbableTransformTests : public QObject {
    Q_OBJECT
private slots:
    void theTransformLabelsScrubWholeNumbersOverTyping();
    void scaleScrubsDownToATenth();
    void theZoomUnitScrubsThePercentage();
    void theLayerOpacityScrubIsOneStep();
    void anOpacityScrubEndsWithItsLayer();
    void aTransformScrubEndsWithItsLayer();
};

void ScrubbableTransformTests::theTransformLabelsScrubWholeNumbersOverTyping()
{
    EditorSession session;
    session.createDocument(200, 100);
    session.insert(pixels(40, 20));
    session.selectTool(NavigationTool::move);
    session.setLocksTransformRatio(false);
    TransformInspector bar(session);
    bar.show();
    bar.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&bar));
    const QPointF origin = session.activeLayer().value().transform.origin;
    const int base = session.history.undoCount();
    const auto shown = [&] { return session.activeLayer().value().transform; };
    // Each drag applies when let go, one step.
    drag(scrubbed(bar, "X"), 7);
    QVERIFY(!session.transformEdit().has_value());
    QCOMPARE(shown().origin.x(), std::round(origin.x()) + 7);
    QCOMPARE(session.history.undoCount(), base + 1);
    QCOMPARE(session.history.undoName(), QString("Transform Layer"));
    // A scrub writes over what is typed in its field.
    QLineEdit &width = find<QLineEdit>(bar, "transformW");
    width.setFocus();
    width.selectAll();
    QTest::keyClicks(&width, QStringLiteral("3"));
    drag(scrubbed(bar, "W"), 10);
    QCOMPARE(shown().size, QSizeF(13, 20));
    QCOMPARE(width.text(), QString("13"));
    drag(scrubbed(bar, "W"), -100);
    QCOMPARE(shown().size.width(), 1.0);
    drag(scrubbed(bar, "H"), 40000);
    QCOMPARE(shown().size.height(), 30000.0);
    drag(scrubbed(bar, "°"), 30);
    QCOMPARE(shown().rotation, 30.0);
    drag(scrubbed(bar, "°"), -900);
    QCOMPARE(shown().rotation, -0.0);
    // Unfocused, the field shows what the session keeps.
    QCOMPARE(find<QLineEdit>(bar, "transform°").text(), QString("0"));
    QCOMPARE(width.text(), QString("1"));
    drag(scrubbed(bar, "Y"), -90000);
    QCOMPARE(shown().origin.y(), -30000.0);
    // A fractional start lands on a whole number.
    QLineEdit &x = find<QLineEdit>(bar, "transformX");
    x.setFocus();
    x.selectAll();
    QTest::keyClicks(&x, QStringLiteral("10.4"));
    QVERIFY(session.transformEdit().value().fromFields);
    QCOMPARE(session.transformEdit().value().draft.origin.x(), 10.4);
    drag(scrubbed(bar, "X"), 3);
    QCOMPARE(shown().origin.x(), 13.0);
    QCOMPARE(x.text(), QString("13"));
    // Every range reaches its ends exactly.
    drag(scrubbed(bar, "X"), -90000);
    QCOMPARE(shown().origin.x(), -30000.0);
    drag(scrubbed(bar, "X"), 90000);
    QCOMPARE(shown().origin.x(), 30000.0);
    drag(scrubbed(bar, "Y"), 90000);
    QCOMPARE(shown().origin.y(), 30000.0);
    drag(scrubbed(bar, "W"), 90000);
    QCOMPARE(shown().size.width(), 30000.0);
    drag(scrubbed(bar, "H"), -90000);
    QCOMPARE(shown().size.height(), 1.0);
    // 360 reaches the setter, whose remainder leaves none.
    drag(scrubbed(bar, "°"), 9000);
    QCOMPARE(shown().rotation, 0.0);
    drag(scrubbed(bar, "°"), 359);
    QCOMPARE(shown().rotation, 359.0);
    // Scale snaps whole percents too.
    const auto restart = [&] {
        while (session.history.undoCount() > base)
            session.undo();
    };
    restart();
    drag(scrubbed(bar, "Scale"), 5);
    QCOMPARE(shown().size, QSizeF(42, 21));
    restart();
    drag(scrubbed(bar, "Scale"), 90000);
    QCOMPARE(shown().size, QSizeF(12000, 6000));
    QCOMPARE(find<QLineEdit>(bar, "transformScale").text(), QString("30000"));
    // While distorted the numbers rest, labels included.
    session.cancelTransform();
    session.beginTransform();
    session.beginDistort();
    QVERIFY(!scrubbed(bar, "X").isEnabled());
}

// Scale's floor is a tenth of a percent.
void ScrubbableTransformTests::scaleScrubsDownToATenth()
{
    EditorSession session;
    session.createDocument(4000, 2000);
    session.insert(pixels(2000, 1000));
    session.selectTool(NavigationTool::move);
    TransformInspector bar(session);
    bar.show();
    drag(scrubbed(bar, "Scale"), -90000);
    QCOMPARE(session.activeLayer().value().transform.size, QSizeF(2, 1));
    QCOMPARE(find<QLineEdit>(bar, "transformScale").text(), QString("0.10"));
}

void ScrubbableTransformTests::theZoomUnitScrubsThePercentage()
{
    EditorSession session;
    session.createDocument(800, 600);
    session.selectTool(NavigationTool::zoom);
    NavigationToolHeader bar(session);
    bar.show();
    session.zoom(1);
    drag(scrubbed(bar, "%"), 25.5);
    QVERIFY(qAbs(session.viewport.zoom() - 1.255) < 1e-9);
    QCOMPARE(find<QLineEdit>(bar, "zoomPercentage").text(), QString("125.5"));
    drag(scrubbed(bar, "%"), -900);
    QVERIFY(qAbs(session.viewport.zoom() - 0.001) < 1e-9);
    drag(scrubbed(bar, "%"), 9000);
    QVERIFY(qAbs(session.viewport.zoom() - 32) < 1e-9);
}

void ScrubbableTransformTests::theLayerOpacityScrubIsOneStep()
{
    EditorSession session;
    LayerAppearanceControls controls(session);
    session.createDocument(4, 4);
    session.insert(pixels(2, 2));
    controls.show();
    QLabel &opacity = scrubbed(controls, "Opacity");
    // One drag is one opacity edit, open until the release.
    drag(opacity, -30, false);
    QVERIFY(qAbs(session.activeLayer().value().opacity - 0.7) < 1e-12);
    QVERIFY(!session.canUndo());
    send(opacity, QEvent::MouseMove, QPoint(-18, 2), Qt::NoButton, Qt::LeftButton);
    QVERIFY(qAbs(session.activeLayer().value().opacity - 0.8) < 1e-12);
    send(opacity, QEvent::MouseButtonRelease, QPoint(-18, 2), Qt::LeftButton, Qt::NoButton);
    QVERIFY(session.canUndo());
    QCOMPARE(find<QLineEdit>(controls, "opacityPercent").text(), QString("80"));
    // One undo takes the whole drag back; the layer stays.
    session.undo();
    QCOMPARE(session.activeLayer().value().opacity, 1.0);
    QCOMPARE(session.document().value().layers.size(), size_t(1));
    drag(opacity, -30.5);
    QVERIFY(qAbs(session.activeLayer().value().opacity - 0.695) < 1e-12);
    drag(opacity, -500);
    QCOMPARE(session.activeLayer().value().opacity, 0.0);
    drag(opacity, 500);
    QCOMPARE(session.activeLayer().value().opacity, 1.0);
}

void ScrubbableTransformTests::anOpacityScrubEndsWithItsLayer()
{
    EditorSession session;
    LayerAppearanceControls controls(session);
    session.createDocument(4, 4);
    session.insert(pixels(2, 2));
    const QUuid lower = session.activeLayerID().value();
    session.setLayerOpacity(0.3);
    session.insert(pixels(2, 2));
    const QUuid upper = session.activeLayerID().value();
    controls.show();
    QLabel &opacity = scrubbed(controls, "Opacity");
    drag(opacity, -10, false);
    QVERIFY(qAbs(layerWith(session, upper).opacity - 0.9) < 1e-12);
    // Another layer ends the drag; held moves change nothing.
    session.selectLayer(lower);
    send(opacity, QEvent::MouseMove, QPointF(-18, 2), Qt::NoButton, Qt::LeftButton);
    send(opacity, QEvent::MouseButtonRelease, QPointF(-18, 2), Qt::LeftButton, Qt::NoButton);
    QCOMPARE(layerWith(session, lower).opacity, 0.3);
    QVERIFY(qAbs(layerWith(session, upper).opacity - 0.9) < 1e-12);
    QVERIFY(session.canUndo());
    session.undo();
    QCOMPARE(layerWith(session, upper).opacity, 1.0);
    QCOMPARE(layerWith(session, lower).opacity, 0.3);
    // A fresh press on the new layer scrubs it.
    session.selectLayer(lower);
    drag(opacity, 20);
    QVERIFY(qAbs(layerWith(session, lower).opacity - 0.5) < 1e-12);
}

void ScrubbableTransformTests::aTransformScrubEndsWithItsLayer()
{
    EditorSession session;
    session.createDocument(200, 100);
    session.insert(pixels(40, 20));
    const QUuid first = session.activeLayerID().value();
    session.insert(pixels(40, 20));
    const QUuid second = session.activeLayerID().value();
    session.selectTool(NavigationTool::move);
    TransformInspector bar(session);
    bar.show();
    const double x = layerWith(session, second).transform.origin.x();
    QLabel &label = scrubbed(bar, "X");
    drag(label, 10, false);
    QCOMPARE(session.transformEdit().value().draft.origin.x(), std::round(x) + 10);
    session.commitTransform();
    // Another layer ends the drag; held moves open no edit.
    session.selectLayer(first);
    send(label, QEvent::MouseMove, QPointF(22, 2), Qt::NoButton, Qt::LeftButton);
    send(label, QEvent::MouseButtonRelease, QPointF(22, 2), Qt::LeftButton, Qt::NoButton);
    QVERIFY(!session.transformEdit().has_value());
    QCOMPARE(layerWith(session, first).transform.origin.x(), x);
    QCOMPARE(layerWith(session, second).transform.origin.x(), std::round(x) + 10);
    // A fresh press scrubs the new layer, applied on release.
    drag(label, 5);
    QVERIFY(!session.transformEdit().has_value());
    QCOMPARE(layerWith(session, first).transform.origin.x(), std::round(x) + 5);
}

QTEST_MAIN(ScrubbableTransformTests)
#include "ScrubbableTransformTests.moc"
