#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include "IO/ProjectStore.h"
#include "Rendering/RasterSnapshot.h"
#include "SelectionFixtures.h"
#include <QSignalSpy>
#include <QThreadPool>
#include <QtTest>

// Filters in the session: every kind's begin, update and commit.
namespace {
std::unique_ptr<EditorSession> filled(int width, int height, QColor colour = Qt::red)
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(width, height);
    QImage image = BrushRaster::context(width, height, false);
    image.fill(colour);
    session->insert(ImportedImage(image, image, QStringLiteral("Filled")));
    return session;
}

bool committed(EditorSession &session)
{
    bool done = false;
    session.commitFilter([&done] { done = true; });
    return QTest::qWaitFor([&done] { return done; }, 20000);
}

// The newest preview has landed and nothing waits.
bool settled(const EditorSession &session)
{
    return QTest::qWaitFor([&] { return session.filterEdit() && !session.filterEdit().value().preparing; }, 20000);
}

const FilterSettings &settings(const EditorSession &session)
{
    return session.filterEdit().value().settings;
}
}

class FilterSessionTests : public QObject {
    Q_OBJECT
private slots:
    void beginOpensEveryKindWhereColorsCanBeAdjusted();
    void gradientMapStartsFromThePalette();
    void updatesNormalizeGrowAndDropWhatPreviewOffHides();
    void theLastCommitIsWhereTheNextStarts();
    void nothingToChangeClosesWithoutAStep();
    void blurCommitsAreTrimmedAndOthersKeepTheirPlace();
    void aRunningPreviewDropsAtTheCommit();
    void anAutomaticKindRendersOncePerSettings();
    void aCoveringMaskStaysPutUnderABlur();
    void theGradientMapPickerEditsTheOpenMap();
    void onlyTheFillGrowsOverTheSelection();
    void aGrowthPastTheLimitsIsAnError();
};

void FilterSessionTests::beginOpensEveryKindWhereColorsCanBeAdjusted()
{
    const std::unique_ptr<EditorSession> session = filled(8, 8);
    for (const FilterKind kind : {FilterKind::gaussianBlur, FilterKind::motionBlur, FilterKind::addNoise, FilterKind::lensCorrection,
                                  FilterKind::curves, FilterKind::exposure, FilterKind::gradientMap, FilterKind::grain}) {
        session->beginFilter(kind);
        QVERIFY2(session->filterEdit() && session->filterEdit().value().kind == kind, qPrintable(rawValue(kind)));
        QVERIFY(settled(*session) && session->filterEdit().value().previewImage(session->activeLayerID().value()));
        session->cancelFilter();
    }
    // The fill needs a selection: without one nothing opens.
    session->beginFilter(FilterKind::contentAwareFill);
    QVERIFY(!session->filterEdit());
    // Under Hue/Saturation, and on a blank layer, nothing opens.
    session->beginHueSaturation();
    session->beginFilter(FilterKind::gaussianBlur);
    QVERIFY(!session->filterEdit());
    session->cancelHueSaturation();
    session->addBlankLayer();
    session->beginFilter(FilterKind::exposure);
    QVERIFY(!session->filterEdit());
    session->undo();
    // A transform edit lands first; a lasso draft goes.
    session->selectTool(NavigationTool::move);
    session->beginTransform();
    LayerTransform moved = session->activeLayer().value().transform;
    moved.origin = QPointF(1, 0);
    session->previewTransform(moved);
    session->beginFilter(FilterKind::exposure);
    QVERIFY(!session->transformEdit());
    QCOMPARE(session->filterEdit().value().mapping.map(QPointF(0, 0)), QPointF(1, 0));
    session->cancelFilter();
    session->selectTool(NavigationTool::lasso);
    session->beginLasso(QPointF(0, 0), SelectionMode::replace);
    session->beginFilter(FilterKind::exposure);
    QVERIFY(!session->lassoDraft() && session->filterEdit());
    session->cancelFilter();
}

void FilterSessionTests::gradientMapStartsFromThePalette()
{
    const std::unique_ptr<EditorSession> session = filled(4, 4);
    session->setForegroundColor(PaletteColor{0.2, 0.4, 0.6});
    session->setBackgroundColor(PaletteColor{0.9, 0.8, 0.7});
    session->beginFilter(FilterKind::gradientMap);
    QCOMPARE(settings(*session).gradientMap.shadows, AdjustmentColor(0.2, 0.4, 0.6));
    QCOMPARE(settings(*session).gradientMap.highlights, AdjustmentColor(0.9, 0.8, 0.7));
    session->cancelFilter();
    // Other kinds start from the remembered settings.
    session->beginFilter(FilterKind::exposure);
    QVERIFY(settings(*session) == FilterSettings());
    session->cancelFilter();
}

void FilterSessionTests::updatesNormalizeGrowAndDropWhatPreviewOffHides()
{
    const std::unique_ptr<EditorSession> session = filled(20, 20);
    session->beginFilter(FilterKind::gaussianBlur);
    QVERIFY(settled(*session));
    QCOMPARE(session->filterEdit().value().grownMargin, 5.0);
    // A radius past range stops at 250; the grid grows.
    session->updateFilter(FilterSettings{.radius = 1000}, true);
    QCOMPARE(settings(*session).radius, 250.0);
    QCOMPARE(session->filterEdit().value().grownMargin, 752.0);
    // The last preview stays up while the grown grid renders.
    QVERIFY(session->filterEdit().value().preparedPreview);
    QVERIFY(settled(*session) && session->filterEdit().value().preparedPreview);
    // Off: the preview and the queue go at once.
    const int revision = session->brushRevision();
    session->updateFilter(FilterSettings{.radius = 2}, true);
    session->updateFilter(FilterSettings{.radius = 3}, true);
    QVERIFY(session->filterEdit().value().pending);
    session->updateFilter(FilterSettings{.radius = 3}, false);
    QVERIFY(!session->filterEdit().value().pending && !session->filterEdit().value().preparedPreview);
    QVERIFY(!session->filterEdit().value().previewImage(session->activeLayerID().value()));
    QVERIFY(session->brushRevision() > revision);
    // The run under way lands unseen.
    QVERIFY(settled(*session));
    QVERIFY(!session->filterEdit().value().preparedPreview);
    session->cancelFilter();
}

void FilterSessionTests::theLastCommitIsWhereTheNextStarts()
{
    const std::unique_ptr<EditorSession> session = filled(8, 8);
    session->beginFilter(FilterKind::gaussianBlur);
    session->updateFilter(FilterSettings{.radius = 3, .angle = 30}, false);
    QVERIFY(committed(*session));
    QVERIFY(session->filterSettings().radius == 3 && session->filterSettings().angle == 30);
    session->beginFilter(FilterKind::motionBlur);
    QVERIFY(settings(*session).radius == 3 && settings(*session).angle == 30);
    // A cancel remembers nothing.
    session->updateFilter(FilterSettings{.angle = 60}, false);
    session->cancelFilter();
    QCOMPARE(session->filterSettings().angle, 30.0);
}

void FilterSessionTests::nothingToChangeClosesWithoutAStep()
{
    const std::unique_ptr<EditorSession> session = filled(4, 4);
    const int steps = session->history.undoCount();
    for (const FilterKind kind : {FilterKind::lensCorrection, FilterKind::exposure, FilterKind::grain}) {
        session->beginFilter(kind);
        FilterSettings still = settings(*session);
        still.grain.amount = 0;
        session->updateFilter(still, false);
        QVERIFY(committed(*session));
        QVERIFY2(!session->filterEdit() && session->history.undoCount() == steps, qPrintable(rawValue(kind)));
        QVERIFY(!session->isProjectBusy());
    }
    QVERIFY(session->filterSettings() == FilterSettings());
    // A change is a step, named for the filter.
    session->beginFilter(FilterKind::lensCorrection);
    session->updateFilter(FilterSettings{.distortion = 50}, false);
    QVERIFY(committed(*session));
    QCOMPARE(session->history.undoCount(), steps + 1);
    QCOMPARE(session->history.undoName(), QString("Lens Correction"));
}

void FilterSessionTests::blurCommitsAreTrimmedAndOthersKeepTheirPlace()
{
    const std::unique_ptr<EditorSession> session = filled(10, 10);
    session->beginFilter(FilterKind::motionBlur);
    session->updateFilter(FilterSettings{.distance = 8}, false);
    QVERIFY(committed(*session));
    // A level streak spreads sideways alone.
    const ImageLayer blurred = session->activeLayer().value();
    QVERIFY(blurred.transform.origin.x() < 0 && blurred.transform.origin.y() == 0);
    QCOMPARE(blurred.transform.size.height(), 10.0);
    QCOMPARE(blurred.asset.value().size(), blurred.transform.size.toSize());
    QCOMPARE(blurred.asset.value().name, QString("Motion Blur"));
    session->beginFilter(FilterKind::exposure);
    FilterSettings brighter;
    brighter.exposure.exposure = 1;
    session->updateFilter(brighter, false);
    QVERIFY(committed(*session));
    QCOMPARE(session->activeLayer().value().transform, blurred.transform);
    QCOMPARE(session->activeLayer().value().asset.value().size(), blurred.asset.value().size());
}

void FilterSessionTests::aRunningPreviewDropsAtTheCommit()
{
    const std::unique_ptr<EditorSession> session = filled(3000, 2000);
    session->beginFilter(FilterKind::exposure);
    QVERIFY(settled(*session));
    FilterSettings one, two;
    one.exposure.exposure = 1;
    two.exposure.exposure = 2;
    session->updateFilter(one, true);
    session->updateFilter(two, true);
    QVERIFY(session->filterEdit().value().pending);
    const int revision = session->brushRevision();
    bool done = false;
    session->commitFilter([&done] { done = true; });
    QVERIFY(!session->filterEdit().value().pending);
    QTRY_VERIFY_WITH_TIMEOUT(done, 20000);
    // One redraw, the commit's: the preview under way landed nowhere.
    QCOMPARE(session->brushRevision(), revision + 1);
    QVERIFY(session->filterSettings() == two);
}

void FilterSessionTests::anAutomaticKindRendersOncePerSettings()
{
    const std::unique_ptr<EditorSession> session = filled(16, 16);
    session->applySelection(rectPath(QRectF(6, 6, 4, 4)), SelectionMode::replace, "Select");
    session->beginFilter(FilterKind::contentAwareFill);
    QVERIFY(settled(*session));
    const auto made = [&session] { return session->filterEdit().value().preparedPreview.value().cacheKey(); };
    const qint64 first = made();
    // The same settings keep the fill, Preview off too.
    const int revision = session->brushRevision();
    session->updateFilter(settings(*session), false);
    QVERIFY(!session->filterEdit().value().preparing && made() == first && session->brushRevision() > revision);
    // Other settings make it once more; the commit takes it.
    const FilterSettings other{.radius = 5};
    session->updateFilter(other, true);
    QVERIFY(session->filterEdit().value().preparing);
    QVERIFY(settled(*session) && made() != first);
    const qint64 second = made();
    session->updateFilter(other, true);
    QVERIFY(!session->filterEdit().value().preparing && made() == second);
    QVERIFY(committed(*session));
    QCOMPARE(session->activeLayer().value().asset.value().image().cacheKey(), second);
    // A fill landing for older settings is made again.
    session->undo();
    session->beginFilter(FilterKind::contentAwareFill);
    QVERIFY(settled(*session));
    session->updateFilter(FilterSettings{.radius = 6}, true);
    session->updateFilter(FilterSettings{.radius = 7}, false);
    QVERIFY(settled(*session));
    const qint64 stale = made();
    QVERIFY(committed(*session));
    QVERIFY(session->activeLayer().value().asset.value().image().cacheKey() != stale);
}

void FilterSessionTests::aCoveringMaskStaysPutUnderABlur()
{
    // Paint the left quarter; the mask reveals the half.
    auto session = std::make_unique<EditorSession>();
    session->createDocument(40, 20);
    QImage image = BrushRaster::context(40, 20, false);
    QPainter(&image).fillRect(0, 0, 10, 20, Qt::red);
    session->insert(ImportedImage(image, image, QStringLiteral("Quarter")));
    const QUuid id = session->activeLayerID().value();
    session->applySelection(rectPath(QRectF(0, 0, 20, 20)), SelectionMode::replace, "Select");
    session->addMask(false);
    session->selectLayerTarget(id, false);
    session->beginFilter(FilterKind::gaussianBlur);
    QVERIFY(committed(*session));
    // Trimmed round the quarter, the mask still reveals the half.
    const ImageLayer layer = session->activeLayer().value();
    const QImage mask = layer.mask.value().asset.image();
    QVERIFY(layer.transform.size.width() < 20 && mask.size() == layer.asset.value().size());
    const QTransform toMask = BrushRaster::pixelToDocument(layer.transform, mask.width(), mask.height()).inverted();
    // Swift's grown box would hide x from 5 to 10.
    for (const QPointF point : {QPointF(2.5, 10.5), QPointF(7.5, 10.5), QPointF(12.5, 10.5)}) {
        const QPointF pixel = toMask.map(point);
        QCOMPARE(int(mask.constScanLine(int(pixel.y()))[int(pixel.x())]), 255);
    }
}

void FilterSessionTests::theGradientMapPickerEditsTheOpenMap()
{
    // Swift's own test of it sits in ImageAdjustmentTests.
    const std::unique_ptr<EditorSession> session = filled(4, 4);
    // Only an open Gradient Map takes it.
    session->openGradientMapColorPicker(true);
    QVERIFY(!session->colorPicker());
    session->beginFilter(FilterKind::exposure);
    session->openGradientMapColorPicker(true);
    QVERIFY(!session->colorPicker());
    session->cancelFilter();
    session->beginFilter(FilterKind::gradientMap);
    // A busy project, a save for one, keeps it shut.
    session->setIsProjectBusy(true);
    session->openGradientMapColorPicker(true);
    QVERIFY(!session->colorPicker());
    session->setIsProjectBusy(false);
    // Each end opens on its own colour, from the palette.
    session->openGradientMapColorPicker(false);
    QCOMPARE(session->colorPicker().value().target.title(), QString("Color Picker (Gradient Map Shadows)"));
    QCOMPARE(session->colorPicker().value().original.hex(), QString("000000"));
    session->closeColorPicker(false);
    session->openGradientMapColorPicker(true);
    const ColorPickerState picker = session->colorPicker().value();
    QCOMPARE(picker.target.title(), QString("Color Picker (Gradient Map Highlights)"));
    QCOMPARE(picker.original.hex(), QString("FFFFFF"));
    // One picker at a time.
    session->openGradientMapColorPicker(false);
    QVERIFY(session->colorPicker().value().target.highlights);
    // The map follows the working colour; the same is silent.
    PickerHSB hsb = picker.hsb;
    hsb.setRGB(PaletteColor{1, 0, 0});
    session->setColorPickerHSB(hsb);
    session->previewGradientMapColor();
    QCOMPARE(settings(*session).gradientMap.highlights, AdjustmentColor(1, 0, 0));
    QSignalSpy changed(session.get(), &EditorSession::changed);
    session->previewGradientMapColor();
    QCOMPARE(changed.count(), 0);
    session->closeColorPicker(false);
    // A palette picker's colour stays out of the map.
    session->openColorPicker(false);
    hsb.setRGB(PaletteColor{0, 1, 0});
    session->setColorPickerHSB(hsb);
    const GradientMapSettings before = settings(*session).gradientMap;
    session->previewGradientMapColor();
    QVERIFY(settings(*session).gradientMap == before);
    session->closeColorPicker(false);
    // Committing, the map takes no picker.
    session->commitFilter();
    session->openGradientMapColorPicker(true);
    QVERIFY(!session->colorPicker());
    QTRY_VERIFY(!session->filterEdit());
    // Committing commits the picker's colour into the step.
    session->beginFilter(FilterKind::gradientMap);
    session->openGradientMapColorPicker(false);
    session->setColorPickerHSB(hsb);
    QVERIFY(committed(*session));
    QVERIFY(!session->colorPicker());
    QCOMPARE(session->filterSettings().gradientMap.shadows, AdjustmentColor(0, 1, 0));
}

void FilterSessionTests::onlyTheFillGrowsOverTheSelection()
{
    // A 10 by 10 layer centred in 20 by 20.
    auto session = std::make_unique<EditorSession>();
    session->createDocument(20, 20);
    QImage image = BrushRaster::context(10, 10, false);
    image.fill(Qt::red);
    session->insert(ImportedImage(image, image, QStringLiteral("Small")));
    session->selectAll();
    session->beginFilter(FilterKind::exposure);
    QVERIFY(!session->filterEdit().value().grownImage);
    session->cancelFilter();
    session->beginFilter(FilterKind::contentAwareFill);
    QCOMPARE(session->filterEdit().value().grownImage.value().size(), QSize(20, 20));
    session->cancelFilter();
}

void FilterSessionTests::aGrowthPastTheLimitsIsAnError()
{
    // A painted strip: its grid grows from tiles, cheaply.
    auto session = std::make_unique<EditorSession>();
    session->createDocument(29000, 1);
    QImage base = BrushRaster::context(2, 1, false);
    base.fill(Qt::red);
    session->insert(ImportedImage(std::make_shared<const RasterSnapshot>(29000, 1, base, QRectF(0, 0, 2, 1), std::vector<BrushPatch>{}), QImage(),
                                  QStringLiteral("Painted")));
    session->beginFilter(FilterKind::gaussianBlur);
    QCOMPARE(session->filterEdit().value().grownMargin, 5.0);
    // Radius 250 needs 30,504 pixels across: refused, the grid kept.
    session->updateFilter(FilterSettings{.radius = 250}, false);
    QCOMPARE(session->brushError().value(), QString::fromUtf8(ProjectError(ProjectError::Kind::tooLarge).what()));
    QVERIFY(session->filterEdit() && session->filterEdit().value().grownMargin == 5);
    QCOMPARE(settings(*session).radius, 250.0);
    session->cancelFilter();
}

QTEST_GUILESS_MAIN(FilterSessionTests)
#include "FilterSessionTests.moc"
