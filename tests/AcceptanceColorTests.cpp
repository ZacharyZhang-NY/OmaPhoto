#include "AcceptanceFixtures.h"
#include "Document/HueSaturation.h"
#include "Document/LayerEffects.h"

// The acceptance run, part two: adjustments, Camera Raw, effects, filters.
class AcceptanceColorTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void adjustmentsAsEditsAndLayers();
    void cameraRawDocked();
    void layerEffects();
    void filters();
};

namespace {
// The photo, alone in a document its size.
QUuid photo(App &app)
{
    if (!scene(1200, 800).save(app.path("photo.png")))
        throw std::runtime_error("the photo could not be written");
    app.session().createDocument(1200, 800);
    app.importFile(app.path("photo.png"));
    return app.session().activeLayerID().value();
}
}

void AcceptanceColorTests::adjustmentsAsEditsAndLayers()
{
    App app;
    const QUuid layer = photo(app);
    QCOMPARE(app.shown(600, 400), QColor(220, 30, 30));
    // Hue/Saturation as an edit: the red disc turns green.
    app.action("hueSaturation").trigger();
    QVERIFY(app.session().hueSaturation());
    app.session().updateHueSaturation(HueSaturationSettings(120), true);
    bool done = false;
    app.session().commitHueSaturation([&done] { done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 30000);
    const QColor green = pixel(app.session(), layer, 600, 400);
    QVERIFY2(green.green() > 200 && green.red() < 60, qPrintable(green.name()));
    // Invert as a layer: the pixels stay, the view inverts.
    app.action("newInvertAdjustment").trigger();
    QVERIFY(app.session().activeLayer().value().adjustment);
    QCOMPARE(pixel(app.session(), layer, 600, 400), green);
    const QColor inverted = app.shown(600, 400);
    QVERIFY2(qAbs(inverted.red() - (255 - green.red())) <= 1 && qAbs(inverted.green() - (255 - green.green())) <= 1, qPrintable(inverted.name()));
    // Curves as a layer opens its editor in the panel.
    app.action("newCurvesAdjustment").trigger();
    QTRY_VERIFY_WITH_TIMEOUT(app.session().filterEdit(), 30000);
    QCOMPARE(app.session().filterEdit().value().kind, FilterKind::curves);
    app.shot(QStringLiteral("05-adjustments"));
    app.session().cancelFilter();
    QCOMPARE(int(app.session().document().value().layers.size()), 3);
}

void AcceptanceColorTests::cameraRawDocked()
{
    App app;
    const QUuid layer = photo(app);
    app.action("cameraRawFilter").trigger();
    QVERIFY(app.session().filterEdit());
    QWidget *const dock = app.window.findChild<QWidget *>(QStringLiteral("panelDock"));
    QVERIFY(dock && dock->isVisible());
    FilterSettings settings = app.session().filterSettings();
    settings.cameraRaw.exposure = 1;
    app.session().updateFilter(settings, true);
    QTRY_VERIFY_WITH_TIMEOUT(app.session().filterEdit().value().previewImage(layer), 30000);
    app.shot(QStringLiteral("06-camera-raw"));
    bool done = false;
    app.session().commitFilter([&done] { done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 30000);
    // A stop brighter: the ground's green lifts.
    const QColor ground = pixel(app.session(), layer, 600, 700);
    QVERIFY2(ground.green() > 170, qPrintable(ground.name()));
}

void AcceptanceColorTests::layerEffects()
{
    App app;
    app.session().createDocument(1200, 800);
    app.session().selectTool(NavigationTool::shape);
    app.session().setForegroundColor({0.2, 0.5, 0.9});
    app.drag(QPointF(300, 200), QPointF(900, 600));
    const QUuid shape = app.session().activeLayerID().value();
    // A red stroke and a drop shadow.
    app.session().addEffect(LayerEffectKind::stroke);
    app.session().changeEffects([](LayerEffects &effects) {
        effects.stroke.value().size = 12;
        effects.stroke.value().red = 1;
        effects.stroke.value().green = 0;
        effects.stroke.value().blue = 0;
    });
    app.session().finishEffectsEditing(true);
    app.session().addEffect(LayerEffectKind::shadow);
    QVERIFY(app.session().effectsEditing());
    app.shot(QStringLiteral("07-effects"));
    app.session().finishEffectsEditing(true);
    const LayerEffects effects = app.session().activeLayer().value().effects.value();
    QVERIFY(effects.stroke && effects.shadow);
    QCOMPARE(app.session().activeLayerID(), std::optional<QUuid>(shape));
    // The stroke rings the shape outside its edge.
    QCOMPARE(app.shown(300 - 6, 400), QColor(255, 0, 0));
    QCOMPARE(app.shown(600, 400), QColor(0x33, 0x80, 0xe6));
}

void AcceptanceColorTests::filters()
{
    App app;
    const QUuid layer = photo(app);
    // Dither: black and white dots by default.
    app.action("dither").trigger();
    QTRY_VERIFY_WITH_TIMEOUT(app.session().filterEdit() && app.session().filterEdit().value().previewImage(layer), 30000);
    app.shot(QStringLiteral("08-filters-dither"));
    bool done = false;
    app.session().commitFilter([&done] { done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 30000);
    for (const QPoint at : {QPoint(100, 100), QPoint(600, 400), QPoint(900, 700)}) {
        const QColor dot = pixel(app.session(), layer, at.x(), at.y());
        QVERIFY2(dot == QColor(Qt::black) || dot == QColor(Qt::white), qPrintable(dot.name()));
    }
    // Remove Background masks the photo round its subject.
    app.action("undo").trigger();
    app.action("removeBackground").trigger();
    QVERIFY(app.session().filterEdit());
    done = false;
    app.session().commitFilter([&done] { done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 60000);
    app.settle();
    QVERIFY(app.session().activeLayer().value().mask);
    QCOMPARE(app.shown(600, 400).alpha(), 255);
    QCOMPARE(app.shown(40, 40).alpha(), 0);
    app.shot(QStringLiteral("09-remove-background"));
}

QTEST_MAIN(AcceptanceColorTests)
#include "AcceptanceColorTests.moc"
