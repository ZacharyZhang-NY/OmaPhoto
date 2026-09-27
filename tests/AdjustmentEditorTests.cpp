#include "Document/EditorSession.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectStore+Json.h"
#include "SessionFixtures.h"
#include "SessionRecord.h"
#include <QSignalSpy>
#include <QtTest>

// Swift's AdjustmentEditorTests: the colour editors write layer settings.
namespace {
ImportedImage filled(int width, int height, const QColor &colour)
{
    QImage pixels(width, height, QImage::Format_RGBA8888_Premultiplied);
    pixels.fill(colour);
    return ImportedImage(pixels, pixels, QStringLiteral("Fixture"));
}

// Swift's await: true once the call's callback has run.
bool awaited(const std::function<void(std::function<void()>)> &call)
{
    bool done = false;
    call([&done] { done = true; });
    return QTest::qWaitFor([&done] { return done; }, 10'000);
}

bool opened(EditorSession &session, QUuid id)
{
    return awaited([&](std::function<void()> done) { session.beginAdjustmentEditing(id, std::move(done)); });
}

// The pixels the open editor reads, premultiplied RGBA.
std::vector<int> input(const EditorSession &session)
{
    const QImage image = session.levels().value().original.image().convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    std::vector<int> result;
    for (int y = 0; y < image.height(); ++y)
        result.insert(result.end(), image.constScanLine(y), image.constScanLine(y) + image.width() * 4);
    return result;
}
}

class AdjustmentEditorTests : public QObject {
    Q_OBJECT
private slots:
    void sharedEditorsKeepPixelsDynamicAndSupportCancel_data();
    void sharedEditorsKeepPixelsDynamicAndSupportCancel();
    void theEditorReadsOnlyWhatLiesBeneath();
    void theEditorOpensOnlyForItsRequest();
    void aFailedRenderEndsTheRequest();
    void anEditBegunMeanwhileKeepsItsEditor();
    void editingClosesLayerEditsAndFileRequests();
    void closingDropsTheHueTools();
    void aLayerUndoneMeanwhileStillCloses();
    void aGradientMapPickerOutlivesItsEditor();
    void everyStepIsAnnouncedAndItsLastSignalSeesIt();
};

void AdjustmentEditorTests::sharedEditorsKeepPixelsDynamicAndSupportCancel_data()
{
    QTest::addColumn<AdjustmentKind>("kind");
    // Invert has no settings, so no editor: held apart.
    for (const AdjustmentKind kind : allAdjustmentKinds)
        if (isEditable(kind))
            QTest::newRow(qPrintable(rawValue(kind))) << kind;
}

void AdjustmentEditorTests::sharedEditorsKeepPixelsDynamicAndSupportCancel()
{
    QFETCH(AdjustmentKind, kind);
    EditorSession session;
    session.createDocument(2, 2);
    const ImportedImage asset = filled(2, 2, Qt::white);
    session.insert(asset);
    const QUuid baseID = session.activeLayerID().value();
    session.addAdjustment(kind);
    const QUuid id = session.adjustmentEditingID().value();
    // As made: a Gradient Map's palette, Grain's own pattern.
    const LayerAdjustment created = session.activeLayer().value().adjustment.value();
    QVERIFY(opened(session, id));
    QVERIFY(session.adjustmentOriginal().value().kind == kind);
    QVERIFY(!session.showsBusy());
    switch (kind) {
    case AdjustmentKind::levels: {
        QTRY_VERIFY(session.levels().value().histogramReady);
        QCOMPARE(session.levels().value().histogram[0][255], 4.0);
        LevelsSettings settings = session.levels().value().settings;
        settings.ranges[0].outputWhite = 0;
        session.updateLevels(settings, true);
        QVERIFY(session.activeLayer().value().adjustment.value().levels == settings);
        session.updateLevels(settings, false);
        QVERIFY(session.activeLayer().value().adjustment.value() == LayerAdjustment{kind});
        QVERIFY(awaited([&](std::function<void()> done) { session.commitLevels(std::move(done)); }));
        break;
    }
    case AdjustmentKind::curves: {
        QVERIFY(session.filterEdit().value().kind == FilterKind::curves);
        FilterSettings settings = session.filterEdit().value().settings;
        settings.curves.channels[0] = {{0, 0}, {255, 0}};
        session.updateFilter(settings, true);
        QVERIFY(session.activeLayer().value().adjustment.value().curves == settings.curves);
        QVERIFY(awaited([&](std::function<void()> done) { session.commitFilter(std::move(done)); }));
        break;
    }
    case AdjustmentKind::exposure:
    case AdjustmentKind::gradientMap:
    case AdjustmentKind::grain:
    case AdjustmentKind::blackWhite:
    case AdjustmentKind::colorBalance: {
        QVERIFY(session.filterEdit().value().kind == filterKind(kind));
        FilterSettings settings = session.filterEdit().value().settings;
        if (kind == AdjustmentKind::exposure)
            settings.exposure.exposure = 1;
        else if (kind == AdjustmentKind::gradientMap)
            settings.gradientMap.reversed = true;
        else if (kind == AdjustmentKind::blackWhite)
            settings.blackWhite.reds = 100;
        else if (kind == AdjustmentKind::colorBalance)
            settings.colorBalance.midCyanRed = 50;
        else
            settings.grain.amount = 70;
        session.updateFilter(settings, true);
        const LayerAdjustment live = session.activeLayer().value().adjustment.value();
        QVERIFY(live.exposure() == settings.exposure && live.gradientMap() == settings.gradientMap && live.grain() == settings.grain
                && live.blackWhite() == settings.blackWhite && live.colorBalance() == settings.colorBalance);
        QVERIFY(awaited([&](std::function<void()> done) { session.commitFilter(std::move(done)); }));
        break;
    }
    case AdjustmentKind::hsv: {
        HueSaturationSettings settings = session.hueSaturation().value().settings;
        settings.range = ColorRange::reds;
        settings.setHue(80);
        settings.setBand(settings.band().centered(25));
        settings.invertRange = true;
        session.updateHueSaturation(settings, true);
        QVERIFY(session.activeLayer().value().adjustment.value().resolvedHSV() == settings);
        QVERIFY(awaited([&](std::function<void()> done) { session.commitHueSaturation(std::move(done)); }));
        break;
    }
    case AdjustmentKind::invert:
        QFAIL("Invert has no editor");
    }
    const LayerAdjustment saved = session.activeLayer().value().adjustment.value();
    QVERIFY(!session.adjustmentEditingID() && saved != created);
    QVERIFY(layerWith(session, baseID).asset.value().identity() == asset.identity());
    QVERIFY(!session.activeLayer().value().asset);
    QVERIFY(ManifestJson::adjustment(ManifestJson::encoded(saved)) == saved);
    session.setAdjustmentEditingID(id);
    QVERIFY(opened(session, id));
    switch (kind) {
    case AdjustmentKind::levels:
        QVERIFY(session.levels().value().settings == saved.levels);
        session.updateLevels(LevelsSettings(), true);
        session.cancelLevels();
        break;
    case AdjustmentKind::curves:
    case AdjustmentKind::exposure:
    case AdjustmentKind::gradientMap:
    case AdjustmentKind::grain:
    case AdjustmentKind::blackWhite:
    case AdjustmentKind::colorBalance: {
        const FilterSettings reopened = session.filterEdit().value().settings;
        QVERIFY(reopened.curves == saved.curves && reopened.exposure == saved.exposure() && reopened.gradientMap == saved.gradientMap()
                && reopened.grain == saved.grain() && reopened.blackWhite == saved.blackWhite() && reopened.colorBalance == saved.colorBalance());
        session.updateFilter(FilterSettings(), true);
        session.cancelFilter();
        break;
    }
    case AdjustmentKind::hsv:
        QVERIFY(session.hueSaturation().value().settings == saved.resolvedHSV());
        session.updateHueSaturation(HueSaturationSettings(), true);
        session.cancelHueSaturation();
        break;
    case AdjustmentKind::invert:
        QFAIL("Invert has no editor");
    }
    QVERIFY(session.activeLayer().value().adjustment.value() == saved);
    QVERIFY(!session.adjustmentEditingID());
    session.undo();
    QVERIFY(session.activeLayer().value().adjustment.value() == created);
    session.redo();
    QVERIFY(session.activeLayer().value().adjustment.value() == saved);
}

void AdjustmentEditorTests::theEditorReadsOnlyWhatLiesBeneath()
{
    // Red, then a folder: green, the adjustment, then blue.
    EditorSession session;
    session.createDocument(2, 1);
    session.insert(filled(2, 1, Qt::red));
    session.insert(filled(1, 1, Qt::green), QPointF(0.5, 0.5));
    const QUuid green = session.activeLayerID().value();
    session.groupSelectedLayers();
    session.selectLayer(green);
    session.addAdjustment(AdjustmentKind::levels);
    const QUuid id = session.activeLayerID().value();
    session.setAdjustmentEditingID(std::nullopt);
    // Its own settings would turn the input black.
    LayerAdjustment black{AdjustmentKind::levels};
    black.levels.ranges[0].outputWhite = 0;
    session.updateAdjustment(id, black);
    session.insert(filled(2, 1, Qt::blue));
    QCOMPARE(layerWith(session, id).parentID, layerWith(session, green).parentID);
    session.setAdjustmentEditingID(id);
    QVERIFY(opened(session, id));
    QVERIFY((input(session) == std::vector<int>{0, 255, 0, 255, 255, 0, 0, 255}));
    // The editors preview nothing: the layer's settings are the preview.
    LevelsSettings settings;
    settings.ranges[0].outputWhite = 100;
    session.updateLevels(settings, true);
    QTest::qWait(50);
    QVERIFY(!session.levels().value().preparedPreview && !session.levels().value().pending);
    session.finishAdjustmentEditing(false);
    QVERIFY(layerWith(session, id).adjustment.value() == black);
}

void AdjustmentEditorTests::theEditorOpensOnlyForItsRequest()
{
    EditorSession session;
    session.createDocument(2, 2);
    session.insert(filled(2, 2, Qt::white));
    const QUuid pixels = session.activeLayerID().value();
    session.addAdjustment(AdjustmentKind::levels);
    const QUuid id = session.adjustmentEditingID().value();
    // Another request, a missing layer, a layer without settings.
    for (const std::optional<QUuid> request : {std::optional<QUuid>(), std::optional(QUuid::createUuid()), std::optional(pixels)}) {
        session.setAdjustmentEditingID(request);
        QVERIFY(opened(session, request.value_or(id)));
        QVERIFY(!session.levels() && !session.adjustmentOriginal() && session.adjustmentEditingID() == request);
    }
    // A pixel edit already open refuses it; the request stays.
    session.selectLayer(pixels);
    for (int edit = 0; edit < 3; ++edit) {
        if (edit == 0)
            session.beginLevels();
        else if (edit == 1)
            session.beginHueSaturation();
        else
            session.beginFilter(FilterKind::curves);
        QVERIFY(session.levels() || session.hueSaturation() || session.filterEdit());
        session.setAdjustmentEditingID(id);
        QVERIFY(opened(session, id));
        QVERIFY(!session.adjustmentOriginal() && session.adjustmentEditingID() == id);
        session.cancelLevels();
        session.cancelHueSaturation();
        session.cancelFilter();
    }
    // A request changed while it renders opens nothing.
    session.setAdjustmentEditingID(id);
    bool dropped = false;
    session.beginAdjustmentEditing(id, [&dropped] { dropped = true; });
    session.setAdjustmentEditingID(std::nullopt);
    QTRY_VERIFY(dropped);
    QVERIFY(!session.levels() && !session.adjustmentOriginal());
    // Two renders: the first lands, the other finds it open.
    session.setAdjustmentEditingID(id);
    bool first = false, second = false;
    session.beginAdjustmentEditing(id, [&first] { first = true; });
    session.beginAdjustmentEditing(id, [&second] { second = true; });
    QTRY_VERIFY(first && second);
    const QUuid edit = session.levels().value().id;
    QVERIFY(opened(session, id));
    QCOMPARE(session.levels().value().id, edit);
    session.cancelLevels();
    QVERIFY(!session.levels() && !session.adjustmentEditingID() && !session.adjustmentOriginal());
    // One transaction opened, so one close ends it.
    QVERIFY(session.canUndo() && session.history.undoName() == QString("New Levels Adjustment"));
}

void AdjustmentEditorTests::aFailedRenderEndsTheRequest()
{
    // Past the export's hundred million pixels: the render refuses.
    EditorSession session;
    session.createDocument(10'001, 10'000);
    session.addAdjustment(AdjustmentKind::curves);
    const int steps = session.history.undoCount();
    int ran = 0;
    session.waitForFileRequest([&ran] { ++ran; });
    QVERIFY(opened(session, session.adjustmentEditingID().value()));
    QCOMPARE(session.brushError(), std::optional(QString::fromUtf8(ExportError(ExportError::Kind::tooLarge).what())));
    QTRY_COMPARE(ran, 1);
    QVERIFY(!session.adjustmentEditingID() && !session.adjustmentOriginal() && !session.filterEdit());
    QVERIFY(session.canEditLayers() && session.canUndo() && session.history.undoCount() == steps);
}

void AdjustmentEditorTests::anEditBegunMeanwhileKeepsItsEditor()
{
    EditorSession session;
    session.createDocument(2, 2);
    session.insert(filled(2, 2, Qt::white));
    const QUuid pixels = session.activeLayerID().value();
    session.addAdjustment(AdjustmentKind::levels);
    const QUuid id = session.adjustmentEditingID().value();
    int ran = 0;
    session.waitForFileRequest([&ran] { ++ran; });
    bool landed = false;
    session.beginAdjustmentEditing(id, [&landed] { landed = true; });
    // Hue/Saturation opens on the pixels while the render runs.
    session.selectLayer(pixels);
    session.beginHueSaturation();
    QCOMPARE(session.hueSaturation().value().layerID, pixels);
    std::optional<QUuid> last = id;
    connect(&session, &EditorSession::changed, this, [&] { last = session.adjustmentEditingID(); });
    QTRY_VERIFY(landed);
    QCOMPARE(session.hueSaturation().value().layerID, pixels);
    QVERIFY(!session.levels() && !session.adjustmentOriginal() && !session.adjustmentEditingID() && !session.brushError());
    // Its signal saw the request gone; waiting requests go on.
    QVERIFY(!last);
    QTRY_COMPARE(ran, 1);
    session.cancelHueSaturation();
    QVERIFY(session.canEditLayers());
}

void AdjustmentEditorTests::editingClosesLayerEditsAndFileRequests()
{
    std::vector<bool> open;
    EditorSession session;
    session.createDocument(2, 2);
    session.insert(filled(2, 2, Qt::white));
    session.addAdjustment(AdjustmentKind::exposure);
    const QUuid id = session.adjustmentEditingID().value();
    QVERIFY(!session.canEditLayers() && !session.canStartProjectOperation());
    int ran = 0;
    session.waitForFileRequest([&ran] { ++ran; });
    QVERIFY(opened(session, id));
    QCOMPARE(ran, 0);
    QVERIFY(!session.canUndo());
    // The last signal of the close sees it closed.
    connect(&session, &EditorSession::changed, this, [&] { open.push_back(session.adjustmentEditingID() || session.filterEdit()); });
    const int focus = session.canvasFocusRequest(), revision = session.brushRevision();
    QVERIFY(session.finishAdjustmentEditing(true));
    QVERIFY(!open.empty() && !open.back());
    QVERIFY(session.canvasFocusRequest() == focus + 1 && session.brushRevision() > revision);
    QVERIFY(session.canEditLayers() && session.canStartProjectOperation());
    QTRY_COMPARE(ran, 1);
    // OK writes the settings out, as Swift does: a step.
    QCOMPARE(session.history.undoName(), QString("Edit Exposure Adjustment"));
    QVERIFY(layerWith(session, id).adjustment.value().exposureSettings == ExposureSettings());
    // Cancel puts the original back: no step.
    const int steps = session.history.undoCount();
    session.setAdjustmentEditingID(id);
    QVERIFY(opened(session, id));
    session.cancelFilter();
    QVERIFY(session.history.undoCount() == steps && !session.adjustmentEditingID());
    QVERIFY(!session.finishAdjustmentEditing(true) && !session.previewAdjustmentEditing(true));
}

void AdjustmentEditorTests::closingDropsTheHueTools()
{
    EditorSession session;
    session.createDocument(2, 2);
    session.insert(filled(2, 2, Qt::red));
    const QUuid pixels = session.activeLayerID().value();
    session.addAdjustment(AdjustmentKind::hsv);
    QVERIFY(opened(session, session.adjustmentEditingID().value()));
    session.setHueSampleMode(HueSampleMode::add);
    session.setHueTargeting(true);
    QVERIFY(session.beginHueTargeting(QPointF(1, 1)));
    QVERIFY(session.finishAdjustmentEditing(true));
    QVERIFY(!session.hueSampleMode() && !session.hueTargeting());
    // The drag went too: a new edit's drag changes nothing.
    session.selectLayer(pixels);
    session.beginHueSaturation();
    session.dragHueTargeting(40, false);
    QVERIFY(session.hueSaturation().value().settings.isIdentity());
}

void AdjustmentEditorTests::aLayerUndoneMeanwhileStillCloses()
{
    EditorSession session;
    session.createDocument(2, 2);
    session.insert(filled(2, 2, Qt::white));
    session.addAdjustment(AdjustmentKind::levels);
    const QUuid id = session.adjustmentEditingID().value();
    bool landed = false;
    session.beginAdjustmentEditing(id, [&landed] { landed = true; });
    // History stays open while it renders, as in Swift.
    session.undo();
    QVERIFY(!session.document().value().layers.empty() && indexOf(session.document().value().layers, id) < 0);
    QTRY_VERIFY(landed);
    QVERIFY(session.levels() && session.adjustmentOriginal());
    // Nothing to write back; the canvas still redraws once.
    const int revision = session.brushRevision();
    session.cancelLevels();
    QVERIFY(!session.levels() && !session.adjustmentEditingID() && session.brushRevision() == revision + 1);
    QVERIFY(session.canUndo() && session.history.redoName() == QString("New Levels Adjustment"));
}

void AdjustmentEditorTests::aGradientMapPickerOutlivesItsEditor()
{
    EditorSession session;
    session.createDocument(2, 2);
    session.insert(filled(2, 2, Qt::gray));
    const QUuid pixels = session.activeLayerID().value();
    session.addAdjustment(AdjustmentKind::gradientMap);
    const QUuid id = session.activeLayerID().value();
    for (const bool commit : {true, false}) {
        const LayerAdjustment before = layerWith(session, id).adjustment.value();
        QVERIFY(before.gradientMap().highlights != AdjustmentColor(1, 0, 0));
        session.setAdjustmentEditingID(id);
        QVERIFY(opened(session, id));
        session.openGradientMapColorPicker(true);
        session.setColorPickerHSB(PickerHSB(PaletteColor{1, 0, 0}));
        session.previewGradientMapColor();
        QVERIFY(layerWith(session, id).adjustment.value().gradientMap().highlights == AdjustmentColor(1, 0, 0));
        // Finished directly, as Swift's tests do: the picker stays open.
        QVERIFY(session.finishAdjustmentEditing(commit));
        QVERIFY(session.colorPicker().has_value());
        const LayerAdjustment kept = layerWith(session, id).adjustment.value();
        QVERIFY(kept.gradientMap().highlights == (commit ? AdjustmentColor(1, 0, 0) : before.gradientMap().highlights));
        if (commit) {
            // With no filter open, it just closes.
            session.closeColorPicker(true);
            QVERIFY(!session.colorPicker() && !session.filterEdit());
        } else {
            // A filter opened meanwhile takes nothing from it.
            session.selectLayer(pixels);
            session.beginFilter(FilterKind::curves);
            const FilterSettings curves = session.filterEdit().value().settings;
            session.closeColorPicker(true);
            QVERIFY(!session.colorPicker() && session.filterEdit().value().settings == curves);
            session.cancelFilter();
        }
        QVERIFY(layerWith(session, id).adjustment.value() == kept);
        // The next round starts from white again.
        LayerAdjustment white = kept;
        white.setGradientMap(GradientMapSettings{});
        session.updateAdjustment(id, white);
        session.selectLayer(id);
    }
}

void AdjustmentEditorTests::everyStepIsAnnouncedAndItsLastSignalSeesIt()
{
    QStringList seen;
    EditorSession session;
    connect(&session, &EditorSession::changed, this, [&] { seen = described(session); });
    // Empty when the last signal saw the state left behind.
    const auto stale = [&](const std::function<void()> &change) {
        seen.clear();
        change();
        const QStringList left = described(session);
        return seen == left ? QString() : seen.join("; ") + " != " + left.join("; ");
    };
    session.createDocument(2, 2);
    session.insert(filled(2, 2, Qt::gray));
    QCOMPARE(stale([&] { session.addAdjustment(AdjustmentKind::curves); }), QString());
    const QUuid id = session.adjustmentEditingID().value();
    QCOMPARE(stale([&] { QVERIFY(opened(session, id)); }), QString());
    QVERIFY(session.adjustmentOriginal() && session.filterEdit());
    FilterSettings settings = session.filterEdit().value().settings;
    settings.curves.channels[0] = {{0, 0}, {255, 128}};
    QCOMPARE(stale([&] { session.updateFilter(settings, true); }), QString());
    QCOMPARE(stale([&] { session.updateFilter(settings, false); }), QString());
    // Settings the layer refuses still change the editor, announced.
    FilterSettings single = settings;
    single.curves.channels[0] = {{0, 0}};
    QCOMPARE(stale([&] { session.updateFilter(single, true); }), QString());
    QCOMPARE(stale([&] { session.finishAdjustmentEditing(true); }), QString());
    for (const AdjustmentKind kind : {AdjustmentKind::levels, AdjustmentKind::hsv}) {
        session.addAdjustment(kind);
        QVERIFY(opened(session, session.adjustmentEditingID().value()));
        QTest::qWait(20);
        if (kind == AdjustmentKind::levels) {
            LevelsSettings reversed;
            reversed.ranges[0].black = 200;
            reversed.ranges[0].white = 100;
            QCOMPARE(stale([&] { session.updateLevels(reversed, true); }), QString());
            QCOMPARE(stale([&] { session.cancelLevels(); }), QString());
        } else {
            QCOMPARE(stale([&] { session.updateHueSaturation(HueSaturationSettings(1000), true); }), QString());
            QCOMPARE(stale([&] { session.cancelHueSaturation(); }), QString());
        }
    }
    QCOMPARE(stale([&] { session.setAdjustmentEditingID(QUuid::createUuid()); }), QString());
    // Refusals stay silent: another request, nothing open.
    QSignalSpy changed(&session, &EditorSession::changed);
    QVERIFY(opened(session, id));
    QVERIFY(!session.finishAdjustmentEditing(true) && !session.previewAdjustmentEditing(true));
    QCOMPARE(changed.count(), 0);
    // A failed render's last signal sees the request end.
    EditorSession huge;
    connect(&huge, &EditorSession::changed, this, [&] { seen = described(huge); });
    huge.createDocument(10'001, 10'000);
    huge.addAdjustment(AdjustmentKind::levels);
    seen.clear();
    QVERIFY(opened(huge, huge.adjustmentEditingID().value()));
    QVERIFY(!huge.adjustmentEditingID() && huge.brushError());
    QCOMPARE(seen, described(huge));
}

QTEST_GUILESS_MAIN(AdjustmentEditorTests)
#include "AdjustmentEditorTests.moc"
