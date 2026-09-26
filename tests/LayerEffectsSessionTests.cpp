#include "Document/EditorSession.h"
#include "SessionFixtures.h"
#include "SessionRecord.h"
#include <QSignalSpy>
#include <QtTest>

// Swift's LayerEffects session extension, driven as its panel drives it.
namespace {
ImportedImage filled(int width, int height, const QColor &colour)
{
    QImage pixels(width, height, QImage::Format_RGBA8888_Premultiplied);
    pixels.fill(colour);
    return ImportedImage(pixels, pixels, QStringLiteral("Filled"));
}

// Two pixel layers, bottom then top, the top one active.
struct Two {
    EditorSession session;
    QUuid bottom, top;
    Two()
    {
        session.createDocument(20, 10);
        session.insert(filled(20, 10, Qt::blue));
        bottom = session.activeLayerID().value();
        session.insert(filled(8, 4, Qt::red));
        top = session.activeLayerID().value();
        session.setBackgroundColor(PaletteColor{0.2, 0.4, 0.6});
    }
    std::optional<LayerEffects> effects(QUuid id) const { return layerWith(session, id).effects; }
};

const LayerEffectSelection strokeOf(QUuid id)
{
    return {id, LayerEffectKind::stroke};
}
}

class LayerEffectsSessionTests : public QObject {
    Q_OBJECT
private slots:
    void addingAnEffectTakesItsDefaultsAndOpensItsPanel();
    void onlyPixelLayersTakeEffects();
    void cancelPutsBackThisPanelsEffectAlone();
    void setEffectsIsOneStepOnAValidChange();
    void panelEditsStayWithTheirLayer();
    void copyingAnEffectClosesTheTargetsPanelFirst();
    void togglingAndRemovingAnEffect();
    void choosingLayersDropsTheChosenEffect();
    void thePickerEditsTheOpenEffect();
    void everyChangeIsAnnouncedLast();
};

void LayerEffectsSessionTests::addingAnEffectTakesItsDefaultsAndOpensItsPanel()
{
    Two two;
    EditorSession &session = two.session;
    QVERIFY(session.canEditEffects() && session.activeEffects().isEmpty());
    session.addEffect(LayerEffectKind::stroke);
    // A stroke takes the background; its panel opens on it.
    const StrokeEffect stroke = two.effects(two.top).value().stroke.value();
    QVERIFY((stroke == StrokeEffect{.red = 0.2, .green = 0.4, .blue = 0.6}));
    QCOMPARE(session.history.undoName(), QString("Add Stroke"));
    QVERIFY(session.effectsEditing() == strokeOf(two.top) && session.effectSelection() == strokeOf(two.top));
    QVERIFY(session.selectedEffect() == strokeOf(two.top) && session.effectsEditingOriginal() == LayerEffects());
    QVERIFY(session.editingEffects() == two.effects(two.top).value() && session.activeEffects() == two.effects(two.top).value());
    // The same panel again does nothing; another closes it, Cancel.
    const int steps = session.history.undoCount();
    session.addEffect(LayerEffectKind::stroke);
    QCOMPARE(session.history.undoCount(), steps);
    session.addEffect(LayerEffectKind::shadow);
    const LayerEffects added = two.effects(two.top).value();
    QVERIFY(!added.stroke && added.shadow == ShadowEffect());
    QCOMPARE(session.history.undoName(), QString("Add Drop Shadow"));
    QVERIFY(session.effectsEditing() == (LayerEffectSelection{two.top, LayerEffectKind::shadow}) && session.effectsEditingOriginal() == LayerEffects());
    session.addEffect(LayerEffectKind::colorOverlay);
    QVERIFY((two.effects(two.top).value().colorOverlay == ColorOverlayEffect{.red = 0.2, .green = 0.4, .blue = 0.6}));
    session.finishEffectsEditing(true);
    session.addEffect(LayerEffectKind::innerShadow);
    QVERIFY(two.effects(two.top).value().innerShadow == InnerShadowEffect());
    session.finishEffectsEditing(true);
    // One already there opens its panel with no step.
    LayerEffects present = two.effects(two.top).value();
    present.shadow = ShadowEffect{.distance = 33};
    session.setEffects(present);
    session.addEffect(LayerEffectKind::shadow);
    QVERIFY(two.effects(two.top).value().shadow->distance == 33);
    session.finishEffectsEditing(true);
    const int kept = session.history.undoCount();
    session.addEffect(LayerEffectKind::colorOverlay);
    QVERIFY(session.history.undoCount() == kept && session.effectsEditingOriginal() == two.effects(two.top));
    QVERIFY(session.effectsEditing() == (LayerEffectSelection{two.top, LayerEffectKind::colorOverlay}));
}

void LayerEffectsSessionTests::onlyPixelLayersTakeEffects()
{
    Two two;
    EditorSession &session = two.session;
    session.addBlankLayer();
    QVERIFY(!session.canEditEffects());
    session.addEffect(LayerEffectKind::stroke);
    QVERIFY(!session.activeLayer().value().effects && !session.effectsEditing());
    LayerEffects effects;
    effects.shadow = ShadowEffect();
    session.setEffects(effects);
    QVERIFY(!session.activeLayer().value().effects);
    session.selectLayer(two.top);
    session.groupSelectedLayers();
    QVERIFY(!session.canEditEffects());
    session.setEffects(effects, session.activeLayerID());
    QVERIFY(!session.activeLayer().value().effects);
    session.selectLayer(two.top);
    session.setIsProjectBusy(true);
    QVERIFY(!session.canEditEffects());
    session.setEffects(effects);
    QVERIFY(!two.effects(two.top));
    session.setIsProjectBusy(false);
    session.setEffects(effects);
    QVERIFY(two.effects(two.top) == effects);
}

void LayerEffectsSessionTests::cancelPutsBackThisPanelsEffectAlone()
{
    Two two;
    EditorSession &session = two.session;
    LayerEffects start;
    start.stroke = StrokeEffect{.size = 3};
    start.shadow = ShadowEffect{.distance = 5};
    session.setEffects(start);
    session.selectEffect(LayerEffectKind::stroke, two.top, true);
    QVERIFY(session.effectsEditingOriginal() == start);
    session.changeEffects([](LayerEffects &effects) { effects.stroke->size = 9; });
    QCOMPARE(session.history.undoName(), QString("Edit Stroke"));
    // Another effect changed meanwhile keeps its change.
    LayerEffects other = two.effects(two.top).value();
    other.shadow->distance = 30;
    session.setEffects(other);
    session.finishEffectsEditing(false);
    const LayerEffects after = two.effects(two.top).value();
    QVERIFY(after.stroke->size == 3 && after.shadow->distance == 30);
    QCOMPARE(session.history.undoName(), QString("Cancel Stroke"));
    QVERIFY(!session.effectsEditing() && !session.effectsEditingOriginal());
    // The chosen effect stays chosen while its layer shows it.
    QVERIFY(session.effectSelection() == strokeOf(two.top));
    // A new effect cancelled goes again; OK keeps a change.
    session.addEffect(LayerEffectKind::innerShadow);
    session.finishEffectsEditing(false);
    QVERIFY(!two.effects(two.top).value().innerShadow);
    QVERIFY(!session.effectSelection());
    session.selectEffect(LayerEffectKind::shadow, two.top, true);
    session.changeEffects([](LayerEffects &effects) { effects.shadow->blur = 2; });
    const int steps = session.history.undoCount();
    session.finishEffectsEditing(true);
    QVERIFY(session.history.undoCount() == steps && two.effects(two.top).value().shadow->blur == 2);
    // Nothing open, nothing to finish.
    session.finishEffectsEditing(false);
    QCOMPARE(session.history.undoCount(), steps);
    // Opening another panel cancels the open one's changes.
    session.selectEffect(LayerEffectKind::stroke, two.top, true);
    session.changeEffects([](LayerEffects &effects) { effects.stroke->size = 40; });
    session.selectEffect(LayerEffectKind::shadow, two.top, true);
    QVERIFY(two.effects(two.top).value().stroke->size == 3 && session.history.undoName() == QString("Cancel Stroke"));
    session.finishEffectsEditing(true);
    // An overlay's Cancel puts back the overlay alone.
    session.addEffect(LayerEffectKind::colorOverlay);
    session.finishEffectsEditing(true);
    session.selectEffect(LayerEffectKind::colorOverlay, two.top, true);
    session.changeEffects([](LayerEffects &effects) { effects.colorOverlay->opacity = 0.25; });
    session.finishEffectsEditing(false);
    QCOMPARE(two.effects(two.top).value().colorOverlay->opacity, 1.0);
    // Its layer's effects all gone, Cancel has nothing to restore.
    session.selectEffect(LayerEffectKind::shadow, two.top, true);
    session.setEffects(LayerEffects(), two.top);
    session.finishEffectsEditing(false);
    QVERIFY(!two.effects(two.top) && !session.effectsEditing() && !session.effectSelection());
    // Choosing an effect takes the target off the mask.
    LayerEffects stroke;
    stroke.stroke = StrokeEffect();
    session.setEffects(stroke, two.top);
    session.addLayerMask(true);
    QVERIFY(session.isMaskSelected());
    session.selectEffect(LayerEffectKind::stroke, two.top);
    QVERIFY(!session.isMaskSelected() && session.effectSelection() == strokeOf(two.top));
}

void LayerEffectsSessionTests::setEffectsIsOneStepOnAValidChange()
{
    Two two;
    EditorSession &session = two.session;
    LayerEffects effects;
    effects.stroke = StrokeEffect();
    const int steps = session.history.undoCount();
    session.setEffects(effects, two.bottom, QStringLiteral("Named"));
    QVERIFY(session.history.undoCount() == steps + 1 && session.history.undoName() == QString("Named"));
    QVERIFY(two.effects(two.bottom) == effects && !two.effects(two.top));
    // The same again, an invalid one, a missing layer: nothing.
    session.setEffects(effects, two.bottom);
    LayerEffects wild = effects;
    wild.stroke->size = 501;
    session.setEffects(wild, two.bottom);
    session.setEffects(effects, QUuid::createUuid());
    QCOMPARE(session.history.undoCount(), steps + 1);
    // Empty is none at all.
    session.setEffects(LayerEffects(), two.bottom);
    QVERIFY(!two.effects(two.bottom) && session.history.undoName() == QString("Layer Effects"));
    // The same effects again leave an opacity drag alone.
    session.beginOpacityEdit();
    session.setLayerOpacity(0.5);
    session.setEffects(LayerEffects());
    QVERIFY(!session.canUndo());
    // A real change ends the drag first, its own step.
    session.setEffects(effects);
    QVERIFY(session.canUndo() && session.history.undoName() == QString("Layer Effects"));
    session.undo();
    QVERIFY(!two.effects(two.top) && session.activeLayer().value().opacity == 0.5);
}

void LayerEffectsSessionTests::panelEditsStayWithTheirLayer()
{
    Two two;
    EditorSession &session = two.session;
    session.addEffect(LayerEffectKind::stroke);
    session.selectLayer(two.bottom);
    session.changeEffects([](LayerEffects &effects) { effects.stroke->size = 12; });
    QVERIFY(two.effects(two.top).value().stroke->size == 12 && !two.effects(two.bottom));
    // Once its effect is gone, edits have nowhere to go.
    LayerEffects without = two.effects(two.top).value();
    without.stroke.reset();
    without.shadow = ShadowEffect();
    session.setEffects(without, two.top);
    const int steps = session.history.undoCount();
    session.changeEffects([](LayerEffects &effects) { effects.shadow->blur = 1; });
    QCOMPARE(session.history.undoCount(), steps);
    session.finishEffectsEditing(true);
    session.changeEffects([](LayerEffects &effects) { effects.shadow->blur = 1; });
    QCOMPARE(session.history.undoCount(), steps);
}

void LayerEffectsSessionTests::copyingAnEffectClosesTheTargetsPanelFirst()
{
    Two two;
    EditorSession &session = two.session;
    LayerEffects source;
    source.shadow = ShadowEffect{.distance = 44};
    session.setEffects(source, two.top);
    QVERIFY(session.canCopyEffect(LayerEffectKind::shadow, two.top, two.bottom));
    QVERIFY(!session.canCopyEffect(LayerEffectKind::shadow, two.top, two.top) && !session.canCopyEffect(LayerEffectKind::stroke, two.top, two.bottom));
    QVERIFY(!session.canCopyEffect(LayerEffectKind::shadow, two.top, QUuid::createUuid()));
    // The target's own shadow is open: OK, then the copy.
    LayerEffects target;
    target.shadow = ShadowEffect{.distance = 2};
    target.stroke = StrokeEffect();
    session.setEffects(target, two.bottom);
    session.selectEffect(LayerEffectKind::shadow, two.bottom, true);
    session.changeEffects([](LayerEffects &effects) { effects.shadow->blur = 7; });
    session.copyEffect(LayerEffectKind::shadow, two.top, two.bottom);
    QCOMPARE(session.history.undoName(), QString("Copy Drop Shadow"));
    QVERIFY(!session.effectsEditing() && two.effects(two.bottom).value().shadow == source.shadow);
    QVERIFY(two.effects(two.bottom).value().stroke == StrokeEffect());
    QVERIFY(session.effectSelection() == (LayerEffectSelection{two.bottom, LayerEffectKind::shadow}) && session.activeLayerID() == two.bottom);
    // The open edit was kept: one undo brings it back.
    session.undo();
    QVERIFY(two.effects(two.bottom).value().shadow->blur == 7);
    session.redo();
    // No source, a blank or folder target, busy: no copy.
    QVERIFY(!session.canCopyEffect(LayerEffectKind::shadow, QUuid::createUuid(), two.bottom));
    session.addBlankLayer();
    QVERIFY(!session.canCopyEffect(LayerEffectKind::shadow, two.top, session.activeLayerID().value()));
    session.groupSelectedLayers();
    QVERIFY(!session.canCopyEffect(LayerEffectKind::shadow, two.top, session.activeLayerID().value()));
    session.setIsProjectBusy(true);
    QVERIFY(!session.canCopyEffect(LayerEffectKind::shadow, two.top, two.bottom));
}

void LayerEffectsSessionTests::togglingAndRemovingAnEffect()
{
    Two two;
    EditorSession &session = two.session;
    session.addEffect(LayerEffectKind::stroke);
    session.finishEffectsEditing(true);
    session.toggleEffect(LayerEffectKind::stroke, two.top);
    QVERIFY(two.effects(two.top).value().stroke->enabled == false && session.history.undoName() == QString("Hide Stroke"));
    session.toggleEffect(LayerEffectKind::stroke, two.top);
    QVERIFY(two.effects(two.top).value().stroke->enabled == true && session.history.undoName() == QString("Show Stroke"));
    // Toggling a missing effect, or none at all, does nothing.
    const int steps = session.history.undoCount();
    session.toggleEffect(LayerEffectKind::shadow, two.top);
    session.toggleEffect(LayerEffectKind::stroke, two.bottom);
    QCOMPARE(session.history.undoCount(), steps);
    // Delete takes the chosen effect first, panel and picker too.
    session.selectEffect(LayerEffectKind::stroke, two.top, true);
    session.openEffectColorPicker(LayerEffectKind::stroke);
    QVERIFY(session.colorPicker());
    session.deleteLayerOrMask();
    QVERIFY(!two.effects(two.top) && session.history.undoName() == QString("Remove Stroke"));
    QVERIFY(!session.effectsEditing() && !session.effectSelection() && !session.colorPicker());
    QCOMPARE(session.document().value().layers.size(), size_t(2));
    // With none chosen, Delete takes the layer; the key likewise.
    session.addEffect(LayerEffectKind::shadow);
    session.finishEffectsEditing(true);
    session.selectAll();
    const ImageIdentity pixels = layerWith(session, two.top).asset.value().identity();
    session.deleteKeyPressed();
    QVERIFY(!two.effects(two.top).value_or(LayerEffects()).shadow && session.history.undoName() == QString("Remove Drop Shadow"));
    QVERIFY(layerWith(session, two.top).asset.value().identity() == pixels && !session.isProjectBusy());
    session.deselect();
    session.deleteKeyPressed();
    QCOMPARE(session.document().value().layers.size(), size_t(1));
    session.removeSelectedEffect();
    QCOMPARE(session.document().value().layers.size(), size_t(1));
    // Busy, a chosen effect stays.
    session.setEffects(LayerEffects{.stroke = StrokeEffect()}, two.bottom);
    session.selectEffect(LayerEffectKind::stroke, two.bottom);
    session.setIsProjectBusy(true);
    session.removeSelectedEffect();
    QVERIFY(two.effects(two.bottom) && session.selectedEffect());
}

void LayerEffectsSessionTests::choosingLayersDropsTheChosenEffect()
{
    for (int way = 0; way < 3; ++way) {
        Two two;
        EditorSession &session = two.session;
        session.addEffect(LayerEffectKind::stroke);
        session.finishEffectsEditing(true);
        QVERIFY(session.effectSelection());
        // Swift clears it first, even when the choice is refused.
        session.setIsProjectBusy(way == 2);
        if (way == 0)
            session.selectLayer(two.top);
        else if (way == 1)
            session.selectLayers({two.top}, two.top);
        else
            session.selectLayerTarget(two.top, false);
        QVERIFY2(!session.effectSelection(), qPrintable(QString::number(way)));
        QVERIFY(!session.selectedEffect() && two.effects(two.top));
    }
    // The chosen effect needs its layer active and holding it.
    Two two;
    LayerEffects stroke;
    stroke.stroke = StrokeEffect();
    two.session.setEffects(stroke, two.bottom);
    two.session.addEffect(LayerEffectKind::stroke);
    two.session.finishEffectsEditing(true);
    two.session.setActiveLayerID(two.bottom);
    QVERIFY(two.session.effectSelection() && !two.session.selectedEffect());
}

void LayerEffectsSessionTests::thePickerEditsTheOpenEffect()
{
    Two two;
    EditorSession &session = two.session;
    // Without an open panel no effect takes a picker.
    session.openEffectColorPicker(LayerEffectKind::stroke);
    QVERIFY(!session.colorPicker());
    session.addEffect(LayerEffectKind::stroke);
    session.openEffectColorPicker(LayerEffectKind::stroke);
    QCOMPARE(session.colorPicker().value().target.title(), QString("Color Picker (Stroke Color)"));
    QVERIFY(session.colorPicker().value().original == (PaletteColor{0.2, 0.4, 0.6}));
    // Its working colour previews on the layer, one step each.
    session.setColorPickerHSB(PickerHSB(PaletteColor{1, 0, 0}));
    session.previewEffectColor();
    QVERIFY(two.effects(two.top).value().color(LayerEffectKind::stroke) == (PaletteColor{1, 0, 0}));
    QCOMPARE(session.history.undoName(), QString("Edit Stroke"));
    // Cancel puts the original back; OK keeps the choice.
    session.closeColorPicker(false);
    QVERIFY(two.effects(two.top).value().color(LayerEffectKind::stroke) == (PaletteColor{0.2, 0.4, 0.6}));
    session.openEffectColorPicker(LayerEffectKind::stroke);
    session.setColorPickerHSB(PickerHSB(PaletteColor{0, 1, 0}));
    session.closeColorPicker(true);
    QVERIFY(two.effects(two.top).value().color(LayerEffectKind::stroke) == (PaletteColor{0, 1, 0}));
    // The panel's Cancel takes the picker with it and restores.
    session.openEffectColorPicker(LayerEffectKind::stroke);
    session.setColorPickerHSB(PickerHSB(PaletteColor{0, 0, 1}));
    session.previewEffectColor();
    session.finishEffectsEditing(false);
    QVERIFY(!session.colorPicker() && !two.effects(two.top));
    // Another picker's preview recolours no effect.
    session.setEffects(LayerEffects{.stroke = StrokeEffect{.red = 1}});
    session.addEffect(LayerEffectKind::shadow);
    session.openColorPicker(false);
    QVERIFY(session.colorPicker());
    const int steps = session.history.undoCount();
    session.previewEffectColor();
    QVERIFY(session.history.undoCount() == steps && two.effects(two.top).value().stroke->red == 1);
    session.openEffectColorPicker(LayerEffectKind::shadow);
    QVERIFY(session.colorPicker().value().target.kind == ColorPickerTarget::Kind::palette);
    session.closeColorPicker(false);
    // A palette picker outlives the panel's close.
    session.openColorPicker(false);
    session.finishEffectsEditing(true);
    QVERIFY(session.colorPicker().value().target.kind == ColorPickerTarget::Kind::palette);
    session.closeColorPicker(false);
    session.selectEffect(LayerEffectKind::shadow, two.top, true);
    // A kind the layer lacks starts black, writes nothing.
    session.openEffectColorPicker(LayerEffectKind::innerShadow);
    QVERIFY(session.colorPicker().value().original == PaletteColor::black());
    const int unchanged = session.history.undoCount();
    session.setColorPickerHSB(PickerHSB(PaletteColor{0, 1, 1}));
    session.closeColorPicker(true);
    QVERIFY(session.history.undoCount() == unchanged && !two.effects(two.top).value().innerShadow);
    // Busy, no picker opens.
    session.setIsProjectBusy(true);
    session.openEffectColorPicker(LayerEffectKind::shadow);
    QVERIFY(!session.colorPicker());
    session.setIsProjectBusy(false);
    // Opening another panel closes its effect picker, Cancel.
    session.openEffectColorPicker(LayerEffectKind::shadow);
    session.setColorPickerHSB(PickerHSB(PaletteColor{1, 1, 0}));
    session.previewEffectColor();
    session.addEffect(LayerEffectKind::stroke);
    QVERIFY(!session.colorPicker() && two.effects(two.top).value().shadow->color() == PaletteColor::black());
}

void LayerEffectsSessionTests::everyChangeIsAnnouncedLast()
{
    QStringList seen;
    Two two;
    EditorSession &session = two.session;
    connect(&session, &EditorSession::changed, this, [&] { seen = described(session); });
    const auto stale = [&](const std::function<void()> &change) {
        seen.clear();
        change();
        const QStringList left = described(session);
        return seen == left ? QString() : seen.join("; ") + " != " + left.join("; ");
    };
    QCOMPARE(stale([&] { session.addEffect(LayerEffectKind::stroke); }), QString());
    QCOMPARE(stale([&] { session.changeEffects([](LayerEffects &effects) { effects.stroke->size = 6; }); }), QString());
    QCOMPARE(stale([&] { session.openEffectColorPicker(LayerEffectKind::stroke); }), QString());
    QCOMPARE(stale([&] { session.setColorPickerHSB(PickerHSB(PaletteColor{1, 0, 0})); }), QString());
    QCOMPARE(stale([&] { session.previewEffectColor(); }), QString());
    QCOMPARE(stale([&] { session.finishEffectsEditing(false); }), QString());
    QCOMPARE(stale([&] { session.addEffect(LayerEffectKind::shadow); }), QString());
    QCOMPARE(stale([&] { session.finishEffectsEditing(true); }), QString());
    QCOMPARE(stale([&] { session.toggleEffect(LayerEffectKind::shadow, two.top); }), QString());
    QCOMPARE(stale([&] { session.selectEffect(LayerEffectKind::shadow, two.top); }), QString());
    QCOMPARE(stale([&] { session.copyEffect(LayerEffectKind::shadow, two.top, two.bottom); }), QString());
    QCOMPARE(stale([&] { session.removeSelectedEffect(); }), QString());
    QCOMPARE(stale([&] { session.selectEffect(LayerEffectKind::shadow, two.top); }), QString());
    QCOMPARE(stale([&] { session.selectLayer(two.bottom); }), QString());
    // The writers the window uses since 9.6d.
    QCOMPARE(stale([&] { session.selectEffect(LayerEffectKind::shadow, two.top); }), QString());
    QCOMPARE(stale([&] { session.dropEffectSelection(); }), QString());
    QCOMPARE(stale([&] { session.setEffectsEditing(strokeOf(two.top)); }), QString());
    QCOMPARE(stale([&] { session.setEffectsEditingOriginal(LayerEffects()); }), QString());
    QCOMPARE(stale([&] { session.setEffectsEditing(std::nullopt); }), QString());
    QCOMPARE(stale([&] { session.setEffectsEditingOriginal(std::nullopt); }), QString());
    // Refusals stay silent.
    QSignalSpy changed(&session, &EditorSession::changed);
    session.removeSelectedEffect();
    session.finishEffectsEditing(true);
    session.selectEffect(LayerEffectKind::colorOverlay, two.top);
    session.toggleEffect(LayerEffectKind::stroke, two.bottom);
    session.setEffects(LayerEffects(), two.bottom);
    session.previewEffectColor();
    session.dropEffectSelection();
    session.setEffectsEditing(std::nullopt);
    session.setEffectsEditingOriginal(std::nullopt);
    QCOMPARE(changed.count(), 0);
    session.beginLevels();
    changed.clear();
    session.selectLayer(two.top);
    QCOMPARE(changed.count(), 0);
}

QTEST_GUILESS_MAIN(LayerEffectsSessionTests)
#include "LayerEffectsSessionTests.moc"
