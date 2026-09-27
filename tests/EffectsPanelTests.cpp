#include "ContentView.h"
#include "MenuFixtures.h"
#include "Rendering/EditorCanvas.h"
#include "UI/EffectsSheet.h"
#include "UI/LayerIcons.h"
#include "UI/LayersPanel.h"
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>
#include <QWheelEvent>
#include <QtTest>
#include <cmath>

// Layer effects in the window: footer, panel, words.
namespace {
template <typename Widget> Widget &find(QWidget &root, const char *name)
{
    Widget *found = root.findChild<Widget *>(QString::fromLatin1(name));
    if (!found)
        throw std::runtime_error(std::string("no widget named ") + name);
    return *found;
}

// A white layer in a shown editor.
struct Editor {
    EditorSession session;
    ContentView view{session};
    QUuid layer;
    Editor()
    {
        session.createDocument(40, 30);
        QImage white(40, 30, QImage::Format_RGBA8888_Premultiplied);
        white.fill(Qt::white);
        session.insert(ImportedImage(white, white, QStringLiteral("White")));
        layer = session.activeLayerID().value();
        view.show();
    }
    // The panel's shown sheet; a closed panel's goes later.
    EffectsSheet *sheet()
    {
        for (EffectsSheet *each : view.findChildren<EffectsSheet *>()) {
            if (each->isVisible())
                return each;
        }
        return nullptr;
    }
    QDialog &panel() { return *qobject_cast<QDialog *>(sheet()->window()); }
    // The shown panel holds the keys, as users find it.
    bool keyed() { return QTest::qWaitFor([&] { return sheet() && QApplication::activeWindow() == &panel(); }); }
    LayerEffects effects() const { return session.activeLayer().value().effects.value(); }
};

// Types into a field and stays there.
void typing(QLineEdit &field, const QString &text)
{
    field.setFocus();
    field.selectAll();
    QTest::keyClicks(&field, text);
}

// Types into a field, then leaves it.
void type(QLineEdit &field, const QString &text)
{
    typing(field, text);
    field.clearFocus();
}

// Counts the paints a widget receives.
struct Painted : QObject {
    int &count;
    explicit Painted(int &count) : count(count) {}
    bool eventFilter(QObject *, QEvent *event) override
    {
        count += event->type() == QEvent::Paint;
        return false;
    }
};

bool inked(const QImage &image, int x, int y)
{
    return qAlpha(image.pixel(x, y)) > 128;
}
}

class EffectsPanelTests : public QObject {
    Q_OBJECT
private slots:
    void theFooterAddsEachEffectAndOpensItsPanel();
    void theSheetEditsItsEffect();
    void aWheelOnTheSliderReplacesTheTyping();
    void theSheetKeepsSwiftsMeasures();
    void eachKindHasSwiftsControls_data();
    void eachKindHasSwiftsControls();
    void aMissingEffectHidesItsControls();
    void cancelAndEscapePutTheEffectBack();
    void anEffectThatGoesTakesItsPanel();
    void deletingNamesAChosenEffect();
    void aCanvasPressLetsGoOfTheChosenEffect();
};

void EffectsPanelTests::theFooterAddsEachEffectAndOpensItsPanel()
{
    Editor editor;
    auto &button = find<QToolButton>(editor.view, "layerEffects");
    QCOMPARE(button.toolTip(), QString("Layer effects: stroke and drop shadow"));
    QCOMPARE(button.accessibleName(), QString("Layer effects"));
    QCOMPARE(button.popupMode(), QToolButton::InstantPopup);
    // Swift's sparkles, in the footer's ink: three four-pointed stars.
    const LayersPanel &panel = editor.view.layersPanel();
    const QImage icon = LayerIcons::pixmap(LayerIcon::sparkles, 16, panel.palette().color(QPalette::PlaceholderText), panel.devicePixelRatio()).toImage();
    QCOMPARE(button.icon().pixmap(QSize(16, 16), panel.devicePixelRatio()).toImage().convertToFormat(icon.format()), icon);
    const QImage glyph = LayerIcons::pixmap(LayerIcon::sparkles, 18, Qt::black, 1).toImage();
    QVERIFY(inked(glyph, 7, 10) && inked(glyph, 13, 4) && inked(glyph, 14, 13) && !inked(glyph, 2, 2) && !inked(glyph, 16, 9));
    QStringList names;
    for (const QAction *entry : button.menu()->actions())
        names << entry->text();
    QCOMPARE(names, QStringList({"Stroke…", "Drop Shadow…", "Color Overlay…", "Inner Shadow…", "Outer Glow…"}));
    QVERIFY(button.isEnabled());
    button.menu()->actions()[0]->trigger();
    QVERIFY((editor.session.effectsEditing() == LayerEffectSelection{editor.layer, LayerEffectKind::stroke}));
    QCOMPARE(editor.panel().objectName(), QString("effectsPanel"));
    QCOMPARE(editor.panel().windowTitle(), QString("Stroke"));
    // Another kind cancels this panel: the new stroke goes.
    button.menu()->actions()[1]->trigger();
    QCOMPARE(editor.panel().windowTitle(), QString("Drop Shadow"));
    QVERIFY(!editor.effects().stroke && editor.effects().shadow);
    editor.session.finishEffectsEditing(true);
    QTRY_VERIFY(!editor.sheet());
    // A layer without pixels takes none.
    editor.session.addBlankLayer();
    QVERIFY(!button.isEnabled());
}

void EffectsPanelTests::theSheetEditsItsEffect()
{
    Editor editor;
    editor.session.addEffect(LayerEffectKind::stroke);
    QVERIFY(editor.keyed());
    EffectsSheet &sheet = *editor.sheet();
    auto &size = find<QLineEdit>(sheet, "sizeField");
    auto &slider = find<QSlider>(sheet, "sizeSlider");
    QCOMPARE(size.text(), QString("4"));
    type(size, "12");
    QCOMPARE(editor.effects().stroke->size, 12.0);
    // The slider spans 0 to 20; typing reaches 500.
    QCOMPARE(slider.value(), 600);
    // No finite number changes nothing; the field goes back.
    type(size, "abc");
    QVERIFY(editor.effects().stroke->size == 12 && size.text() == "12");
    type(size, "inf");
    QVERIFY(editor.effects().stroke->size == 12 && size.text() == "12");
    // Up steps one, Shift ten; past the range, clamped.
    QTest::keyClick(&size, Qt::Key_Up);
    QCOMPARE(editor.effects().stroke->size, 13.0);
    QTest::keyClick(&size, Qt::Key_Up, Qt::ShiftModifier);
    QCOMPARE(editor.effects().stroke->size, 23.0);
    // Past its end the thumb rests there, writing nothing back.
    QCOMPARE(slider.value(), 1000);
    // A step replaces pending typing, even where it clamps.
    typing(size, "12");
    QTest::keyClick(&size, Qt::Key_Up);
    QVERIFY(editor.effects().stroke->size == 24 && size.text() == "24" && !size.isModified());
    size.clearFocus();
    QCOMPARE(editor.effects().stroke->size, 24.0);
    type(size, "900");
    QVERIFY(editor.effects().stroke->size == 500 && size.text() == "500");
    typing(size, "12");
    QTest::keyClick(&size, Qt::Key_Up);
    QVERIFY(editor.effects().stroke->size == 500 && size.text() == "500" && !size.isModified());
    size.clearFocus();
    QCOMPARE(editor.effects().stroke->size, 500.0);
    // The slider sets any value; the field rounds, ties even.
    slider.setValue(525);
    QVERIFY(editor.effects().stroke->size == 10.5 && size.text() == "10");
    // Leaving an untouched field keeps the exact value.
    size.setFocus();
    size.clearFocus();
    QCOMPARE(editor.effects().stroke->size, 10.5);
    // Typing waits while other changes arrive.
    auto &opacity = find<QLineEdit>(sheet, "opacityField");
    QCOMPARE(opacity.text(), QString("100"));
    typing(size, "3");
    find<QSlider>(sheet, "opacitySlider").setValue(500);
    QVERIFY(editor.effects().stroke->opacity == 0.5 && opacity.text() == "50" && size.text() == "3");
    size.clearFocus();
    QCOMPARE(editor.effects().stroke->size, 3.0);
    // Inside or Outside, as Swift's segments, following an undo.
    const QList<QToolButton *> position = find<QWidget>(sheet, "strokePosition").findChildren<QToolButton *>();
    QVERIFY(position.size() == 2 && position[0]->text() == "Outside" && position[1]->text() == "Inside" && position[0]->isChecked());
    position[1]->click();
    QVERIFY(editor.effects().stroke->inside && position[1]->isChecked());
    editor.session.undo();
    QVERIFY(!editor.effects().stroke->inside && position[0]->isChecked());
    position[1]->click();
    // The swatch opens the picker, whose colour shows at once.
    auto &swatch = find<QAbstractButton>(sheet, "effectColor");
    QVERIFY(swatch.toolTip() == "Stroke color" && swatch.accessibleName() == "Stroke color" && swatch.size() == QSize(36, 18));
    swatch.click();
    QVERIFY(editor.session.colorPicker().value().target.kind == ColorPickerTarget::Kind::effect);
    QTest::qWait(100);
    int paints = 0;
    Painted counter(paints);
    swatch.installEventFilter(&counter);
    editor.session.setColorPickerHSB(PickerHSB(PaletteColor{1, 0, 0}));
    QCOMPARE(editor.effects().stroke->color(), (PaletteColor{1, 0, 0}));
    QTRY_VERIFY(paints > 0);
    // A black rim, a white ring inside, then the colour.
    const QImage shot = swatch.grab().toImage();
    QVERIFY2(shot.pixelColor(0, 9) == QColor(Qt::black), qPrintable(shot.pixelColor(0, 9).name()));
    QVERIFY2(shot.pixelColor(1, 9) == QColor(Qt::white), qPrintable(shot.pixelColor(1, 9).name()));
    QCOMPARE(shot.pixelColor(18, 9), QColor(Qt::red));
    editor.session.closeColorPicker(true);
    // Return commits, then goes on to OK, keeping everything.
    typing(size, "7");
    QTest::keyClick(&size, Qt::Key_Return);
    QVERIFY(!editor.session.effectsEditing() && editor.effects().stroke->size == 7 && editor.effects().stroke->inside);
    QTRY_VERIFY(!editor.sheet());
}

void EffectsPanelTests::aWheelOnTheSliderReplacesTheTyping()
{
    Editor editor;
    editor.session.addEffect(LayerEffectKind::stroke);
    QVERIFY(editor.keyed());
    EffectsSheet &sheet = *editor.sheet();
    auto &size = find<QLineEdit>(sheet, "sizeField");
    auto &slider = find<QSlider>(sheet, "sizeSlider");
    // A wheel moves the slider; the field keeps the keys.
    typing(size, "12");
    QWheelEvent wheel(QPointF(10, 5), slider.mapToGlobal(QPointF(10, 5)), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(&slider, &wheel);
    const double wheeled = 20.0 * (200 - QApplication::wheelScrollLines()) / 1000;
    QCOMPARE(editor.effects().stroke->size, wheeled);
    QVERIFY(size.hasFocus() && size.text() == QString::number(std::lround(wheeled)) && !size.isModified());
    // Return then keeps the slider's value, going on to OK.
    QTest::keyClick(&size, Qt::Key_Return);
    QVERIFY(!editor.session.effectsEditing() && editor.effects().stroke->size == wheeled);
}

void EffectsPanelTests::theSheetKeepsSwiftsMeasures()
{
    Editor editor;
    editor.session.addEffect(LayerEffectKind::stroke);
    EffectsSheet &sheet = *editor.sheet();
    QTRY_VERIFY(sheet.isVisible());
    QCOMPARE(sheet.width(), 340);
    auto &slider = find<QSlider>(sheet, "sizeSlider");
    auto &field = find<QLineEdit>(sheet, "sizeField");
    QWidget &row = *slider.parentWidget();
    QLabel *title = nullptr, *unit = nullptr;
    for (QLabel *label : row.findChildren<QLabel *>()) {
        if (label->text() == "Size")
            title = label;
        if (label->text() == "px")
            unit = label;
    }
    // Title 64, slider 130, field 48, then the unit.
    QVERIFY(title && unit && title->buddy() == &slider);
    QVERIFY(title->x() == 0 && title->width() == 64 && slider.x() == 74 && slider.width() == 130);
    QVERIFY(field.x() == 214 && field.width() == 48 && unit->x() == 264);
    QVERIFY(field.alignment() == Qt::AlignRight && field.accessibleName() == "Size" && field.placeholderText() == "Size");
    // Twenty points round the sheet, sixteen between its rows.
    const QWidget &colour = *find<QAbstractButton>(sheet, "effectColor").parentWidget();
    QCOMPARE(colour.x(), 20);
    QCOMPARE(row.y() - colour.geometry().bottom() - 1, 16);
    QCOMPARE(colour.findChild<QLabel *>()->width(), 64);
    // Cancel, then the default OK ten points on.
    const QPushButton &cancel = find<QPushButton>(sheet, "effectsCancel"), &ok = find<QPushButton>(sheet, "effectsOK");
    QVERIFY(ok.x() - cancel.geometry().right() - 1 == 10 && ok.geometry().right() == 319);
    QVERIFY(ok.isDefault() && !cancel.autoDefault());
    // Swift's segments touch.
    const QList<QToolButton *> position = find<QWidget>(sheet, "strokePosition").findChildren<QToolButton *>();
    QCOMPARE(position[1]->x(), position[0]->geometry().right() + 1);
}

void EffectsPanelTests::eachKindHasSwiftsControls_data()
{
    QTest::addColumn<LayerEffectKind>("kind");
    QTest::addColumn<QString>("headline");
    QTest::addColumn<QStringList>("fields");
    // Title, typed floor and ceiling, then the slider's.
    const QStringList shadow{"Opacity:0:100:100", "Angle:-180:180:180", "Distance:0:5000:100", "Blur:0:500:100"};
    QTest::newRow("stroke") << LayerEffectKind::stroke << QString("Stroke") << QStringList{"Size:0:500:20", "Opacity:0:100:100"};
    QTest::newRow("shadow") << LayerEffectKind::shadow << QString("Drop Shadow") << shadow;
    QTest::newRow("overlay") << LayerEffectKind::colorOverlay << QString("Color Overlay") << QStringList{"Opacity:0:100:100"};
    QTest::newRow("inner shadow") << LayerEffectKind::innerShadow << QString("Inner Shadow")
                                  << QStringList{"Opacity:0:100:100", "Angle:-180:180:180", "Distance:0:5000:50", "Blur:0:500:100"};
    QTest::newRow("outer glow") << LayerEffectKind::outerGlow << QString("Outer Glow") << QStringList{"Size:0:500:100", "Opacity:0:100:100"};
}

void EffectsPanelTests::eachKindHasSwiftsControls()
{
    QFETCH(LayerEffectKind, kind);
    QFETCH(QString, headline);
    QFETCH(QStringList, fields);
    Editor editor;
    editor.session.addEffect(kind);
    QVERIFY(editor.keyed());
    EffectsSheet &sheet = *editor.sheet();
    QCOMPARE(editor.panel().windowTitle(), headline);
    const QLabel *head = nullptr;
    for (const QLabel *label : sheet.findChildren<QLabel *>()) {
        if (label->text() == headline)
            head = label;
    }
    QVERIFY(head && head->font().pixelSize() == 13 && head->font().weight() == QFont::DemiBold);
    // The stroke chooses its side; the rest show their colour.
    QCOMPARE(sheet.findChild<QWidget *>("strokePosition") != nullptr, kind == LayerEffectKind::stroke);
    // Swift's fields in order, each clamped to its range's ends.
    QStringList names;
    for (const QLineEdit *field : sheet.findChildren<QLineEdit *>())
        names << field->accessibleName();
    QStringList titles;
    for (const QString &spec : fields) {
        const QStringList parts = spec.split(':');
        titles << parts[0];
        auto &field = *sheet.findChild<QLineEdit *>(parts[0].toLower() + "Field");
        type(field, "99999");
        QCOMPARE(field.text(), parts[2]);
        type(field, "-99999");
        QCOMPARE(field.text(), parts[1]);
        // The slider's end sets its range's top.
        auto &slider = *sheet.findChild<QSlider *>(parts[0].toLower() + "Slider");
        slider.setValue(1000);
        QCOMPARE(field.text(), parts[3]);
    }
    QCOMPARE(names, titles);
}

void EffectsPanelTests::aMissingEffectHidesItsControls()
{
    Editor editor;
    editor.session.addEffect(LayerEffectKind::stroke);
    // For a kind the layer lacks, the headline alone.
    EffectsSheet other(editor.session, LayerEffectKind::shadow);
    QVERIFY(!find<QSlider>(other, "blurSlider").isVisibleTo(&other) && !find<QAbstractButton>(other, "effectColor").isVisibleTo(&other));
    editor.session.changeEffects([](LayerEffects &effects) { effects.shadow = ShadowEffect(); });
    QVERIFY(find<QSlider>(other, "blurSlider").isVisibleTo(&other) && find<QAbstractButton>(other, "effectColor").isVisibleTo(&other));
}

void EffectsPanelTests::cancelAndEscapePutTheEffectBack()
{
    Editor editor;
    // Return on a focused Cancel still means OK, Swift's default.
    editor.session.addEffect(LayerEffectKind::shadow);
    QVERIFY(editor.keyed());
    type(find<QLineEdit>(*editor.sheet(), "angleField"), "-45");
    auto &cancel = find<QPushButton>(*editor.sheet(), "effectsCancel");
    cancel.setFocus();
    QTest::keyClick(&cancel, Qt::Key_Return);
    QVERIFY(editor.effects().shadow->angle == -45 && !editor.session.effectsEditing());
    // An existing shadow: Cancel puts it back as it was.
    editor.session.selectEffect(LayerEffectKind::shadow, editor.layer, true);
    QCOMPARE(find<QLineEdit>(*editor.sheet(), "distanceField").text(), QString("20"));
    find<QSlider>(*editor.sheet(), "distanceSlider").setValue(400);
    QCOMPARE(editor.effects().shadow->distance, 40.0);
    find<QPushButton>(*editor.sheet(), "effectsCancel").click();
    QVERIFY(editor.effects().shadow->distance == 20 && !editor.session.effectsEditing());
    // A new overlay's panel: Escape takes the overlay away again.
    editor.session.addEffect(LayerEffectKind::colorOverlay);
    QVERIFY(editor.effects().colorOverlay);
    QTest::keyClick(&editor.panel(), Qt::Key_Escape);
    QVERIFY(!editor.effects().colorOverlay && editor.effects().shadow && !editor.session.effectsEditing());
}

void EffectsPanelTests::anEffectThatGoesTakesItsPanel()
{
    Editor editor;
    editor.session.addEffect(LayerEffectKind::stroke);
    editor.session.openEffectColorPicker(LayerEffectKind::stroke);
    QVERIFY(editor.sheet() && editor.session.colorPicker());
    // Undoing the stroke takes its picker and its panel.
    editor.session.undo();
    QVERIFY(!editor.session.activeLayer().value().effects);
    QVERIFY(!editor.session.colorPicker() && !editor.session.effectsEditing() && !editor.session.effectsEditingOriginal());
    QTRY_VERIFY(!editor.sheet());
    // A palette's picker stays: only an effect's goes with it.
    editor.session.addEffect(LayerEffectKind::stroke);
    editor.session.openColorPicker(false);
    QVERIFY(editor.session.colorPicker().value().target.kind == ColorPickerTarget::Kind::palette);
    editor.session.undo();
    QVERIFY(editor.session.colorPicker() && !editor.session.effectsEditing());
    editor.session.closeColorPicker(false);
    // Its kind gone while another stays closes the panel too.
    LayerEffects shadow;
    shadow.shadow = ShadowEffect();
    editor.session.setEffects(shadow);
    editor.session.addEffect(LayerEffectKind::stroke);
    editor.session.undo();
    QVERIFY(editor.effects().shadow && !editor.effects().stroke && !editor.session.effectsEditing());
    // So does its layer, deleted.
    editor.session.selectEffect(LayerEffectKind::shadow, editor.layer, true);
    editor.session.dropEffectSelection();
    editor.session.deleteLayerOrMask();
    QVERIFY(editor.session.document().value().layers.empty() && !editor.session.effectsEditing());
    QTRY_VERIFY(!editor.sheet());
}

void EffectsPanelTests::deletingNamesAChosenEffect()
{
    Bar bar;
    EditorSession &session = bar.session();
    session.createDocument(4, 4);
    session.insert(white());
    LayerEffects effects;
    effects.stroke = StrokeEffect();
    session.setEffects(effects);
    auto &trash = find<QToolButton>(bar.window, "deleteLayer");
    QVERIFY(bar.action("deleteLayer").text() == "Delete Layer" && trash.toolTip() == "Delete selected layer");
    session.selectEffect(LayerEffectKind::stroke, session.activeLayerID().value());
    QCOMPARE(bar.action("deleteLayer").text(), QString("Delete Stroke"));
    QVERIFY(trash.toolTip() == "Delete selected effect" && trash.accessibleName() == "Delete selected effect");
    // A choice an undo took away names the layer again.
    session.undo();
    QVERIFY(session.effectSelection() && !session.selectedEffect());
    QVERIFY(bar.action("deleteLayer").text() == "Delete Layer" && trash.toolTip() == "Delete selected layer");
    session.redo();
    session.selectEffect(LayerEffectKind::stroke, session.activeLayerID().value());
    // Delete takes the effect; the layer stays.
    bar.action("deleteLayer").trigger();
    QVERIFY(!session.activeLayer().value().effects && session.document().value().layers.size() == 1);
    QVERIFY(bar.action("deleteLayer").text() == "Delete Layer" && trash.toolTip() == "Delete selected layer");
}

void EffectsPanelTests::aCanvasPressLetsGoOfTheChosenEffect()
{
    Editor editor;
    LayerEffects effects;
    effects.stroke = StrokeEffect();
    editor.session.setEffects(effects);
    editor.session.selectEffect(LayerEffectKind::stroke, editor.layer);
    CanvasView &canvas = *editor.view.findChild<CanvasView *>();
    const QPoint middle = canvas.rect().center();
    // Another button keeps it; the left lets go before guards.
    QTest::mouseClick(&canvas, Qt::RightButton, {}, middle);
    QVERIFY(editor.session.effectSelection());
    editor.session.setIsProjectBusy(true);
    QTest::mouseClick(&canvas, Qt::LeftButton, {}, middle);
    QVERIFY(!editor.session.effectSelection());
    editor.session.setIsProjectBusy(false);
}

QTEST_MAIN(EffectsPanelTests)
#include "EffectsPanelTests.moc"
