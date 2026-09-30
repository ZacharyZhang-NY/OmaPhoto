#include "AcceptanceFixtures.h"
#include "Document/ColorPalette.h"
#include "Document/Guides.h"
#include "Document/ShapeTool.h"

// The acceptance run, part one: layers, selections, painting, arranging.
class AcceptanceTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void layersFoldersMasksAndClipping();
    void selections();
    void paintingTools();
    void transformGuidesAndGrid();
};

void AcceptanceTests::layersFoldersMasksAndClipping()
{
    App app;
    QVERIFY(scene(1200, 800).save(app.path("photo.png")));
    app.session().createDocument(1200, 800);
    app.importFile(app.path("photo.png"));
    const QUuid photo = app.session().activeLayerID().value();
    // A painted layer, clipped to the photo, multiplied at 60%.
    app.action("newBlankLayer").trigger();
    const QUuid paint = app.session().activeLayerID().value();
    QVERIFY(paint != photo);
    app.session().selectTool(NavigationTool::brush);
    app.session().setBrushSettings({.diameter = 120});
    app.session().setForegroundColor({1, 0.8, 0});
    app.drag(QPointF(100, 400), QPointF(1100, 400));
    app.settle();
    QCOMPARE(pixel(app.session(), paint, 600, 400), QColor(255, 204, 0));
    QCOMPARE(pixel(app.session(), paint, 600, 100).alpha(), 0);
    app.action("clippingMask").trigger();
    app.session().setLayerBlendMode(LayerBlendMode::multiply);
    app.session().setLayerOpacity(0.6);
    // A mask on the photo; both wrapped in a folder.
    app.session().selectLayer(photo);
    app.session().addLayerMask();
    app.session().selectLayers({photo, paint}, paint);
    app.action("groupLayers").trigger();
    const std::vector<ImageLayer> &layers = app.session().document().value().layers;
    QCOMPARE(int(layers.size()), 3);
    const auto find = [&layers](const std::function<bool(const ImageLayer &)> &match) {
        const auto found = std::find_if(layers.begin(), layers.end(), match);
        if (found == layers.end())
            throw std::runtime_error("no such layer");
        return *found;
    };
    const ImageLayer folder = find([](const ImageLayer &layer) { return layer.isGroup; });
    const ImageLayer photoLayer = find([photo](const ImageLayer &layer) { return layer.id == photo; });
    const ImageLayer paintLayer = find([paint](const ImageLayer &layer) { return layer.id == paint; });
    QVERIFY(photoLayer.mask);
    QCOMPARE(photoLayer.parentID, std::optional<QUuid>(folder.id));
    QCOMPARE(paintLayer.parentID, std::optional<QUuid>(folder.id));
    QCOMPARE(paintLayer.maskSourceID, std::optional<QUuid>(photo));
    QCOMPARE(paintLayer.blendMode, LayerBlendMode::multiply);
    QCOMPARE(paintLayer.opacity, 0.6);
    app.shot(QStringLiteral("01-layers"));
    // Undo walks the steps back one at a time.
    app.action("undo").trigger();
    QCOMPARE(int(app.session().document().value().layers.size()), 2);
}

void AcceptanceTests::selections()
{
    App app;
    QVERIFY(scene(1200, 800).save(app.path("photo.png")));
    app.session().createDocument(1200, 800);
    app.importFile(app.path("photo.png"));
    // A marquee dragged over the canvas.
    app.session().selectTool(NavigationTool::marquee);
    app.drag(QPointF(100, 100), QPointF(500, 300));
    const QRectF marquee = app.session().selection().value().path.boundingRect();
    QVERIFY2(qAbs(marquee.left() - 100) <= 2 && qAbs(marquee.right() - 500) <= 2, qPrintable(QString::number(marquee.width())));
    // The wand takes the ground's flat green.
    app.session().selectTool(NavigationTool::wand);
    app.press(QPointF(600, 750));
    app.release(QPointF(600, 750));
    app.settle();
    const QRectF ground = app.session().selection().value().path.boundingRect();
    QVERIFY2(ground.top() >= 598 && ground.top() <= 602 && ground.width() >= 1198, qPrintable(QString::number(ground.top())));
    // Select Subject finds the red disc.
    app.action("subject").trigger();
    app.settle();
    const QRectF subject = app.session().selection().value().path.boundingRect();
    QVERIFY2(qAbs(subject.center().x() - 600) < 12 && qAbs(subject.width() - 400) < 40, qPrintable(QString::number(subject.width())));
    app.shot(QStringLiteral("02-selections"));
    app.action("deselect").trigger();
    QVERIFY(!app.session().selection());
}

void AcceptanceTests::paintingTools()
{
    App app;
    app.session().createDocument(1200, 800, true);
    const QUuid base = app.session().activeLayerID().value();
    // A gradient dragged across the empty layer.
    app.session().selectTool(NavigationTool::gradient);
    app.session().setForegroundColor({0.1, 0.2, 0.6});
    app.drag(QPointF(0, 400), QPointF(1200, 400));
    QTest::keyClick(&app.canvas(), Qt::Key_Return);
    app.settle();
    QCOMPARE(pixel(app.session(), base, 2, 400).blue(), 153);
    // The default style fades the foreground to clear.
    QVERIFY(qAbs(pixel(app.session(), base, 600, 400).alpha() - 128) <= 2);
    // A rectangle, then a brush stroke on the base.
    app.session().selectTool(NavigationTool::shape);
    app.session().setShapeKind(ShapeKind::rectangle);
    app.session().setForegroundColor({0.9, 0.3, 0.1});
    app.drag(QPointF(200, 200), QPointF(500, 500));
    const QUuid shape = app.session().activeLayerID().value();
    QVERIFY(shape != base);
    QVERIFY(app.session().activeLayer().value().liveShape());
    app.session().selectLayer(base);
    app.session().selectTool(NavigationTool::brush);
    app.session().setBrushSettings({.diameter = 60, .hardness = 0.5});
    app.session().setForegroundColor(PaletteColor::white());
    app.drag(QPointF(700, 200), QPointF(1000, 600));
    app.settle();
    QCOMPARE(pixel(app.session(), base, 850, 400), QColor(Qt::white));
    // Text typed on the canvas, its first word coloured.
    app.session().selectTool(NavigationTool::type);
    app.session().setForegroundColor(PaletteColor::black());
    app.press(QPointF(600, 650));
    app.release(QPointF(600, 650));
    QTRY_VERIFY(app.session().textDraft());
    QTest::keyClicks(&app.canvas(), QStringLiteral("Hello OmaPhoto"));
    QCOMPARE(app.session().textDraft().value().style.content, QStringLiteral("Hello OmaPhoto"));
    QTest::keyClick(&app.canvas(), Qt::Key_Home);
    for (int letter = 0; letter < 5; ++letter)
        QTest::keyClick(&app.canvas(), Qt::Key_Right, Qt::ShiftModifier);
    app.session().setPaletteColor({0.9, 0.1, 0.1}, false);
    app.shot(QStringLiteral("03-painting"));
    QTest::keyClick(&app.canvas(), Qt::Key_Return, Qt::ControlModifier);
    QTRY_VERIFY(!app.session().textDraft());
    const LayerText text = app.session().activeLayer().value().liveText().value();
    QCOMPARE(text.style.content, QStringLiteral("Hello OmaPhoto"));
    // One run: the first word red over black text.
    QCOMPARE(text.style.colorRuns.value(), (std::vector<LayerTextColorRun>{{.location = 0, .length = 5, .red = 0.9, .green = 0.1, .blue = 0.1}}));
    QCOMPARE(int(app.session().document().value().layers.size()), 3);
}

void AcceptanceTests::transformGuidesAndGrid()
{
    App app;
    QVERIFY(scene(1200, 800).save(app.path("photo.png")));
    app.session().createDocument(1600, 1000);
    app.importFile(app.path("photo.png"));
    // The Move tool drags the layer; Transform scales it.
    app.session().selectTool(NavigationTool::move);
    const QPointF origin = app.session().activeLayer().value().transform.origin;
    app.drag(QPointF(800, 500), QPointF(900, 560));
    // View points round to whole pixels on a reduced canvas.
    const QPointF moved = app.session().activeLayer().value().transform.origin - origin;
    QVERIFY2(qAbs(moved.x() - 100) <= 3 && qAbs(moved.y() - 60) <= 3, qPrintable(QString::number(moved.x())));
    app.action("transformLayer").trigger();
    QVERIFY(app.session().transformEdit());
    LayerTransform half = app.session().activeLayer().value().transform;
    half.size = half.size / 2;
    app.session().previewTransform(half);
    QTest::keyClick(&app.canvas(), Qt::Key_Return);
    QCOMPARE(app.session().activeLayer().value().transform.size, QSizeF(600, 400));
    // Rulers, a guide and the grid.
    app.session().setShowsRulers(true);
    app.session().setShowsGrid(true);
    app.session().addGuide({.id = QUuid::createUuid(), .axis = CanvasGuide::Axis::horizontal, .position = 500});
    QCOMPARE(int(app.session().document().value().guides.size()), 1);
    app.session().selectTool(NavigationTool::move);
    app.shot(QStringLiteral("04-transform-guides-grid"));
}

QTEST_MAIN(AcceptanceTests)
#include "AcceptanceTests.moc"
