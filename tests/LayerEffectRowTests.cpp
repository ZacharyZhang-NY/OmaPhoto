#include "Document/ProjectWorkspace.h"
#include "Rendering/EditorCanvas.h"
#include "UI/LayerIcons.h"
#include "UI/NativeLayerList.h"
#include "SessionFixtures.h"
#include <QAccessible>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDrag>
#include <QMimeData>
#include <QtTest>

namespace {
// What a started drag offered: no drag loop runs offscreen.
struct StartedDrag {
    QByteArray effect;
    QByteArray source;
    Qt::DropActions supported;
    QImage cursor;
    QSize pixmap;
};
std::vector<StartedDrag> startedDrags;
}

// Takes libQt6Gui's place here, as the gesture tests do.
Qt::DropAction QDrag::exec(Qt::DropActions supportedActions, Qt::DropAction)
{
    startedDrags.push_back({mimeData()->data(NativeLayerList::effectType), mimeData()->data(NativeLayerList::sourceType), supportedActions,
                            dragCursor(Qt::CopyAction).toImage(), pixmap().size()});
    return Qt::IgnoreAction;
}

Qt::DropAction QDrag::exec(Qt::DropActions supportedActions)
{
    return exec(supportedActions, Qt::IgnoreAction);
}

namespace {
ImportedImage white(const QString &name)
{
    QImage image(40, 30, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::white);
    return ImportedImage(image, image, name);
}

// A shown list of white layers, the top one first.
struct Listed {
    EditorSession session;
    NativeLayerList list{session};
    explicit Listed(int layers = 2)
    {
        session.createDocument(40, 30);
        for (int each = 0; each < layers; ++each)
            session.insert(white(QStringLiteral("White %1").arg(each + 1)));
        list.resize(252, 400);
        list.show();
        if (!QTest::qWaitForWindowActive(&list))
            throw std::runtime_error("the list never became active");
    }
    LayerCell &cell(int row) { return *list.cells().at(size_t(row)); }
    QUuid id(int row) { return cell(row).layerID(); }
    // The effect rows a cell shows, top down.
    std::vector<LayerEffectRow *> effects(int row)
    {
        std::vector<LayerEffectRow *> shown;
        for (LayerEffectRow *each : cell(row).findChildren<LayerEffectRow *>()) {
            if (each->isVisibleTo(&cell(row)))
                shown.push_back(each);
        }
        std::sort(shown.begin(), shown.end(), [](const LayerEffectRow *a, const LayerEffectRow *b) { return a->y() < b->y(); });
        return shown;
    }
    QToolButton &eye(LayerEffectRow &row) { return *row.findChild<QToolButton *>("effectEye"); }
    QLabel &label(LayerEffectRow &row) { return *row.findChild<QLabel *>(); }
    // Presses reach the window, as real clicks do.
    QPoint at(QWidget &widget, QPoint point) { return widget.mapTo(&list, point); }
    void press(QWidget &widget, QPoint point, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QTest::mousePress(list.windowHandle(), Qt::LeftButton, modifiers, at(widget, point));
    }
    void release(QWidget &widget, QPoint point, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QTest::mouseRelease(list.windowHandle(), Qt::LeftButton, modifiers, at(widget, point));
    }
    void click(QWidget &widget, QPoint point, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QTest::mouseClick(list.windowHandle(), Qt::LeftButton, modifiers, at(widget, point));
    }
    std::optional<LayerEffectSelection> chosen() { return session.selectedEffect(); }
    int row(QUuid id)
    {
        for (size_t each = 0; each < list.cells().size(); ++each) {
            if (list.cells()[each]->layerID() == id)
                return int(each);
        }
        throw std::runtime_error("no such row");
    }
    // The eye shows this glyph in the faint ink.
    bool eyeShows(LayerEffectRow &row, LayerIcon glyph)
    {
        const double ratio = row.devicePixelRatio();
        const QImage expected = LayerIcons::pixmap(glyph, 16, row.palette().color(QPalette::PlaceholderText), ratio).toImage();
        return eye(row).icon().pixmap(QSize(16, 16), ratio).toImage().convertToFormat(expected.format()) == expected;
    }
    // The drag events a drop brings, offering a copy alone.
    bool drop(QMimeData *data, QPoint listPoint)
    {
        const QPointF inViewport = list.viewport()->mapFromParent(listPoint);
        QDragEnterEvent enter(inViewport.toPoint(), Qt::CopyAction, data, Qt::LeftButton, Qt::AltModifier);
        QApplication::sendEvent(list.viewport(), &enter);
        QDropEvent drop(inViewport, Qt::CopyAction, data, Qt::LeftButton, Qt::AltModifier);
        QApplication::sendEvent(list.viewport(), &drop);
        delete data;
        lastAction = drop.dropAction();
        return drop.isAccepted();
    }
    Qt::DropAction lastAction = Qt::IgnoreAction;
};

LayerEffects strokeAndShadow()
{
    return LayerEffects{.stroke = StrokeEffect(), .shadow = ShadowEffect()};
}

QMimeData *effectDrag(const NativeLayerList &list, const QString &payload)
{
    auto *data = new QMimeData;
    data->setData(NativeLayerList::effectType, payload.toUtf8());
    data->setData(NativeLayerList::sourceType, list.dragToken().toUtf8());
    return data;
}
}

// The effects listed under their layer: rows, presses, copies.
class LayerEffectRowTests : public QObject {
    Q_OBJECT
private slots:
    void rowsListTheLayersEffects();
    void aClickChoosesAndADoubleClickEdits();
    void theEyeShowsOrHidesItsEffect();
    void pressesFollowSwiftsOrder();
    void aPressBelowTheRowsLetsGoOfEverything();
    void anAltDragCopiesTheEffect();
    void effectDropsLandOnTheRowUnderThePointer();
    void aRowRebuiltUnderItsOwnPressLivesOn();
    void aScreenReaderPressChoosesTheEffect();
    void altShowsTheCursorsOverATallerRow();
};

void LayerEffectRowTests::rowsListTheLayersEffects()
{
    Listed listed;
    const QUuid top = listed.id(0);
    QCOMPARE(listed.cell(0).height(), LayerCell::rowHeight);
    listed.session.setEffects(strokeAndShadow(), top);
    // Each effect adds a row under the layer, Swift's order.
    QCOMPARE(listed.cell(0).height(), 100);
    const std::vector<LayerEffectRow *> rows = listed.effects(0);
    QVERIFY(rows.size() == 2 && rows[0]->kind == LayerEffectKind::stroke && rows[1]->kind == LayerEffectKind::shadow && rows[1]->layerID == top);
    QCOMPARE(rows[0]->geometry(), QRect(0, 52, listed.cell(0).width(), 24));
    QCOMPARE(rows[1]->geometry(), QRect(0, 76, listed.cell(0).width(), 24));
    LayerEffectRow &shadow = *rows[1];
    QCOMPARE(listed.eye(shadow).geometry(), QRect(38, 1, 20, 22));
    QVERIFY(listed.label(shadow).text() == "Drop Shadow" && listed.label(shadow).font().pixelSize() == 11);
    QCOMPARE(listed.label(shadow).geometry(), QRect(66, 0, listed.cell(0).width() - 74, 24));
    QCOMPARE(shadow.toolTip(), QString("Click to select; double-click to edit; Alt-drag to copy drop shadow"));
    QVERIFY(shadow.accessibleName() == "Drop Shadow effect" && listed.eye(shadow).accessibleName() == "Hide Drop Shadow");
    QVERIFY(listed.eyeShows(shadow, LayerIcon::eye) && listed.label(shadow).foregroundRole() == QPalette::WindowText);
    // A flat eye; every control reports its hover.
    QVERIFY(listed.eye(shadow).autoRaise() && shadow.hasMouseTracking());
    for (const QWidget *child : shadow.findChildren<QWidget *>())
        QVERIFY2(child->hasMouseTracking(), qPrintable(child->objectName()));
    // The row's edge moves down with it.
    const QImage cell = listed.cell(0).grab().toImage();
    QVERIFY(cell.pixel(200, 99) != cell.pixel(200, 98) && cell.pixel(200, 51) == cell.pixel(200, 50));
    // Hidden, the effect dims; rows stay while kinds do.
    listed.session.toggleEffect(LayerEffectKind::shadow, top);
    QVERIFY(listed.effects(0) == rows && listed.eye(shadow).accessibleName() == "Show Drop Shadow");
    QVERIFY(listed.eyeShows(shadow, LayerIcon::eyeSlash) && listed.label(shadow).foregroundRole() == QPalette::PlaceholderText);
    listed.session.selectEffect(LayerEffectKind::stroke, top);
    QVERIFY(listed.effects(0) == rows);
    // Other kinds make new rows; the old go later.
    const QPointer<LayerEffectRow> old = rows[0];
    listed.session.setEffects(LayerEffects{.colorOverlay = ColorOverlayEffect()}, top);
    QVERIFY(listed.effects(0).size() == 1 && listed.effects(0)[0]->kind == LayerEffectKind::colorOverlay && listed.cell(0).height() == 76);
    QTRY_VERIFY(!old);
    // A clipped layer's effects step in with it.
    listed.session.toggleClippingMask(top);
    QCOMPARE(listed.eye(*listed.effects(0)[0]).x(), 62);
    // Narrow, a name ends in an ellipsis.
    listed.session.setEffects(strokeAndShadow(), top);
    listed.list.resize(150, 400);
    QVERIFY(listed.label(*listed.effects(0)[1]).text().endsWith(QChar(0x2026)) && listed.label(*listed.effects(0)[1]).width() == 150 - 98);
    // Without effects the row is 52 again.
    listed.session.setEffects(LayerEffects(), top);
    QVERIFY(listed.effects(0).empty() && listed.cell(0).height() == 52);
}

void LayerEffectRowTests::aClickChoosesAndADoubleClickEdits()
{
    Listed listed;
    const QUuid top = listed.id(0), below = listed.id(1);
    listed.session.setEffects(strokeAndShadow(), top);
    listed.session.selectLayer(below);
    const QImage unselected = listed.cell(0).grab().toImage();
    LayerEffectRow &stroke = *listed.effects(0)[0];
    // A click chooses the effect and its layer, taking keys.
    listed.list.clearFocus();
    listed.click(stroke, QPoint(150, 12));
    QVERIFY(listed.chosen() == (LayerEffectSelection{top, LayerEffectKind::stroke}));
    QVERIFY(listed.session.selectedLayerIDs() == QSet<QUuid>{top} && !listed.session.effectsEditing() && listed.list.hasFocus());
    // The chosen row shows the accent; the layer's highlight hides.
    const QImage chosen = listed.cell(0).grab().toImage();
    QVERIFY(chosen.pixel(200, 20) == unselected.pixel(200, 20) && chosen.pixel(200, 88) == unselected.pixel(200, 88));
    const QColor under = unselected.pixelColor(200, 64), accent = stroke.palette().color(QPalette::Highlight), shown = chosen.pixelColor(200, 64);
    QVERIFY(std::abs(shown.red() - (0.3 * accent.red() + 0.7 * under.red())) <= 1.5);
    QVERIFY(std::abs(shown.blue() - (0.3 * accent.blue() + 0.7 * under.blue())) <= 1.5);
    // Other buttons choose nothing.
    listed.session.dropEffectSelection();
    QTest::mousePress(&stroke, Qt::RightButton, Qt::NoModifier, QPoint(150, 12));
    QTest::mouseRelease(&stroke, Qt::RightButton, Qt::NoModifier, QPoint(150, 12));
    QVERIFY(!listed.chosen());
    // Let go of, the layer's highlight comes back.
    listed.click(listed.cell(0), QPoint(listed.cell(0).width() - 20, 20));
    QVERIFY(!listed.chosen() && listed.cell(0).grab().toImage().pixel(200, 20) != unselected.pixel(200, 20));
    // A double click opens the effect's panel, and renames nothing.
    QTest::mouseDClick(listed.list.windowHandle(), Qt::LeftButton, Qt::NoModifier, listed.at(*listed.effects(0)[1], QPoint(150, 12)));
    QVERIFY(listed.session.effectsEditing() == (LayerEffectSelection{top, LayerEffectKind::shadow}));
    QVERIFY(listed.chosen() == (LayerEffectSelection{top, LayerEffectKind::shadow}) && !listed.session.renamingLayerID());
}

void LayerEffectRowTests::theEyeShowsOrHidesItsEffect()
{
    Listed listed;
    const QUuid top = listed.id(0);
    listed.session.setEffects(strokeAndShadow(), top);
    LayerEffectRow &stroke = *listed.effects(0)[0];
    // One step each way; the choice stays as it was.
    listed.click(listed.eye(stroke), QPoint(10, 11));
    QVERIFY(!layerWith(listed.session, top).effects->stroke->isEnabled() && listed.session.history.undoName() == "Hide Stroke");
    QVERIFY(!listed.chosen() && listed.eye(stroke).accessibleName() == "Show Stroke");
    listed.click(listed.eye(stroke), QPoint(10, 11));
    QVERIFY(layerWith(listed.session, top).effects->stroke->isEnabled() && listed.session.history.undoName() == "Show Stroke");
    // It rests while layers cannot be edited.
    listed.session.setIsProjectBusy(true);
    QVERIFY(!listed.eye(stroke).isEnabled());
    listed.click(listed.eye(stroke), QPoint(10, 11));
    QVERIFY(layerWith(listed.session, top).effects->stroke->isEnabled() && !listed.chosen());
    listed.session.setIsProjectBusy(false);
    QVERIFY(listed.eye(stroke).isEnabled());
}

void LayerEffectRowTests::pressesFollowSwiftsOrder()
{
    Listed listed;
    const QUuid top = listed.id(0), below = listed.id(1);
    listed.session.setEffects(strokeAndShadow(), top);
    LayerEffectRow &stroke = *listed.effects(0)[0];
    LayerEffectRow &shadow = *listed.effects(0)[1];
    // Alt on the strip, over the last effect, clips.
    listed.session.selectEffect(LayerEffectKind::shadow, top);
    listed.click(shadow, QPoint(150, 20), Qt::AltModifier);
    QVERIFY(layerWith(listed.session, top).maskSourceID == below && !listed.chosen());
    // Above it, Alt arms a copy; the release chooses.
    listed.press(shadow, QPoint(150, 5), Qt::AltModifier);
    QVERIFY(!listed.chosen());
    listed.release(shadow, QPoint(150, 5), Qt::AltModifier);
    QVERIFY(listed.chosen() == (LayerEffectSelection{top, LayerEffectKind::shadow}) && layerWith(listed.session, top).maskSourceID == below);
    // Alt on the eye is the row's: nothing hides.
    listed.session.dropEffectSelection();
    listed.press(listed.eye(shadow), QPoint(10, 5), Qt::AltModifier);
    listed.release(listed.eye(shadow), QPoint(10, 5), Qt::AltModifier);
    QVERIFY(layerWith(listed.session, top).effects->shadow->isEnabled() && listed.chosen() == (LayerEffectSelection{top, LayerEffectKind::shadow}));
    // An Alt double click on the eye hides nothing either.
    listed.session.dropEffectSelection();
    QTest::mouseDClick(listed.list.windowHandle(), Qt::LeftButton, Qt::AltModifier, listed.at(listed.eye(shadow), QPoint(12, 5)));
    QVERIFY(layerWith(listed.session, top).effects->shadow->isEnabled() && listed.chosen() == (LayerEffectSelection{top, LayerEffectKind::shadow}));
    // On the strip, the eye's Alt press clips too.
    listed.click(listed.eye(shadow), QPoint(10, 20), Qt::AltModifier);
    QVERIFY(!layerWith(listed.session, top).maskSourceID && !listed.chosen() && layerWith(listed.session, top).effects->shadow->isEnabled());
    // Its own row's press lets go, keeping the layer.
    listed.session.selectEffect(LayerEffectKind::stroke, top);
    listed.click(listed.cell(0), QPoint(listed.cell(0).width() - 20, 20));
    QVERIFY(!listed.chosen() && listed.session.selectedLayerIDs() == QSet<QUuid>{top});
    // Ctrl on another row lets go too, extending the layers.
    listed.session.selectEffect(LayerEffectKind::stroke, top);
    listed.click(listed.cell(1), QPoint(listed.cell(1).width() - 20, 20), Qt::ControlModifier);
    QVERIFY(!listed.chosen() && listed.session.selectedLayerIDs() == (QSet<QUuid>{top, below}));
    // A thumbnail's Ctrl-click loads a selection and keeps the choice.
    listed.session.selectEffect(LayerEffectKind::stroke, top);
    listed.click(listed.cell(0).thumbnail(), QPoint(5, 5), Qt::ControlModifier);
    QVERIFY(listed.chosen() == (LayerEffectSelection{top, LayerEffectKind::stroke}) && listed.session.selection());
    // Ctrl with Alt copies nothing: rows choose, eyes hide.
    listed.press(shadow, QPoint(150, 5), Qt::AltModifier | Qt::ControlModifier);
    QVERIFY(listed.chosen() == (LayerEffectSelection{top, LayerEffectKind::shadow}));
    listed.release(shadow, QPoint(150, 5), Qt::AltModifier | Qt::ControlModifier);
    listed.click(listed.eye(shadow), QPoint(10, 5), Qt::AltModifier | Qt::ControlModifier);
    QVERIFY(!layerWith(listed.session, top).effects->shadow->isEnabled());
    // A plain press on another effect chooses it at once.
    listed.press(stroke, QPoint(150, 12));
    QVERIFY(listed.chosen() == (LayerEffectSelection{top, LayerEffectKind::stroke}));
    listed.release(stroke, QPoint(150, 12));
}

void LayerEffectRowTests::aPressBelowTheRowsLetsGoOfEverything()
{
    Listed listed;
    const QUuid top = listed.id(0);
    listed.session.setEffects(strokeAndShadow(), top);
    const QPoint under = listed.at(listed.cell(1), QPoint(100, listed.cell(1).height() + 30));
    // Ctrl or Shift keep the layers, not the effect.
    for (const Qt::KeyboardModifier modifier : {Qt::ControlModifier, Qt::ShiftModifier}) {
        listed.session.selectEffect(LayerEffectKind::stroke, top);
        QTest::mouseClick(listed.list.windowHandle(), Qt::LeftButton, modifier, under);
        QVERIFY(!listed.chosen() && listed.session.selectedLayerIDs() == QSet<QUuid>{top});
    }
    // Between rows, as before, a press changes nothing.
    listed.session.selectEffect(LayerEffectKind::stroke, top);
    QTest::mouseClick(listed.list.windowHandle(), Qt::LeftButton, Qt::NoModifier, listed.at(listed.cell(0), QPoint(100, listed.cell(0).height())));
    QVERIFY(listed.chosen() && listed.session.selectedLayerIDs() == QSet<QUuid>{top});
    // A plain press deselects every layer, as Swift's table does.
    listed.list.clearFocus();
    QTest::mouseClick(listed.list.windowHandle(), Qt::LeftButton, Qt::NoModifier, under);
    QVERIFY(!listed.chosen() && listed.session.selectedLayerIDs().isEmpty() && !listed.session.activeLayerID() && listed.list.hasFocus());
    // An empty list takes the press too.
    Listed empty(0);
    QTest::mouseClick(empty.list.windowHandle(), Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
    QVERIFY(empty.session.selectedLayerIDs().isEmpty());
    // Other buttons leave everything.
    listed.session.selectEffect(LayerEffectKind::stroke, top);
    QTest::mouseClick(listed.list.windowHandle(), Qt::RightButton, Qt::NoModifier, under);
    QVERIFY(listed.chosen() && listed.session.selectedLayerIDs() == QSet<QUuid>{top});
}

void LayerEffectRowTests::anAltDragCopiesTheEffect()
{
    Listed listed;
    const QUuid top = listed.id(0);
    listed.session.setEffects(strokeAndShadow(), top);
    startedDrags.clear();
    LayerEffectRow &stroke = *listed.effects(0)[0];
    const int far = QApplication::startDragDistance() + 2;
    const QPoint at(150, 5);
    const auto move = [](QWidget &widget, QPoint point, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers) {
        QMouseEvent event(QEvent::MouseMove, QPointF(point), widget.mapToGlobal(QPointF(point)), Qt::NoButton, buttons, modifiers);
        QApplication::sendEvent(&widget, &event);
    };
    // Short of the distance nothing; at it, the effect goes.
    QTest::mousePress(&stroke, Qt::LeftButton, Qt::AltModifier, at);
    move(stroke, at + QPoint(QApplication::startDragDistance() - 1, 0), Qt::LeftButton, Qt::AltModifier);
    QCOMPARE(startedDrags.size(), size_t(0));
    move(stroke, at + QPoint(QApplication::startDragDistance(), 0), Qt::LeftButton, Qt::AltModifier);
    QCOMPARE(startedDrags.size(), size_t(1));
    QCOMPARE(startedDrags.back().effect, (uuidString(top) + ":Stroke").toUtf8());
    QCOMPARE(startedDrags.back().source, listed.list.dragToken().toUtf8());
    QCOMPARE(startedDrags.back().supported, Qt::DropActions(Qt::CopyAction));
    QCOMPARE(startedDrags.back().cursor, CanvasView::duplicateCursor(listed.list.devicePixelRatio()).pixmap().toImage());
    QCOMPARE(startedDrags.back().pixmap, stroke.size() * stroke.devicePixelRatio());
    // The drag spends the press: its release chooses nothing.
    QTest::mouseRelease(&stroke, Qt::LeftButton, Qt::AltModifier, at + QPoint(far, 0));
    QVERIFY(!listed.chosen());
    // Alt let go before the move: no drag, nothing chosen.
    QTest::mousePress(&stroke, Qt::LeftButton, Qt::AltModifier, at);
    move(stroke, at + QPoint(far, 0), Qt::LeftButton, Qt::NoModifier);
    QTest::mouseRelease(&stroke, Qt::LeftButton, Qt::NoModifier, at + QPoint(far, 0));
    QVERIFY(startedDrags.size() == 1 && !listed.chosen());
    // A hover drags nothing; another button's release chooses nothing.
    QTest::mousePress(&stroke, Qt::LeftButton, Qt::AltModifier, at);
    move(stroke, at + QPoint(far, 0), Qt::NoButton, Qt::AltModifier);
    QCOMPARE(startedDrags.size(), size_t(1));
    QTest::mouseRelease(&stroke, Qt::RightButton, Qt::AltModifier, at);
    QVERIFY(!listed.chosen());
    QTest::mouseRelease(&stroke, Qt::LeftButton, Qt::AltModifier, at);
    QVERIFY(listed.chosen() == (LayerEffectSelection{top, LayerEffectKind::stroke}));
    // A release lost, the next press starts afresh.
    QTest::mousePress(&stroke, Qt::LeftButton, Qt::AltModifier, at);
    QTest::mousePress(&stroke, Qt::LeftButton, Qt::NoModifier, at);
    move(stroke, at + QPoint(far, 0), Qt::LeftButton, Qt::AltModifier);
    QTest::mouseRelease(&stroke, Qt::LeftButton, Qt::NoModifier, at);
    QCOMPARE(startedDrags.size(), size_t(1));
    // From the eye, Alt drags the effect too.
    QTest::mousePress(&listed.eye(stroke), Qt::LeftButton, Qt::AltModifier, QPoint(10, 5));
    move(listed.eye(stroke), QPoint(10 + far, 5), Qt::LeftButton, Qt::AltModifier);
    QCOMPARE(startedDrags.size(), size_t(2));
    QTest::mouseRelease(&listed.eye(stroke), Qt::LeftButton, Qt::AltModifier, QPoint(10 + far, 5));
    // Layers resting, Alt arms nothing; resting later, nothing goes.
    listed.session.dropEffectSelection();
    listed.session.setIsProjectBusy(true);
    QTest::mousePress(&stroke, Qt::LeftButton, Qt::AltModifier, at);
    listed.session.setIsProjectBusy(false);
    move(stroke, at + QPoint(far, 0), Qt::LeftButton, Qt::AltModifier);
    QTest::mouseRelease(&stroke, Qt::LeftButton, Qt::AltModifier, at);
    QVERIFY(startedDrags.size() == 2 && !listed.chosen());
    QTest::mousePress(&stroke, Qt::LeftButton, Qt::AltModifier, at);
    listed.session.setIsProjectBusy(true);
    move(stroke, at + QPoint(far, 0), Qt::LeftButton, Qt::AltModifier);
    listed.session.setIsProjectBusy(false);
    QTest::mouseRelease(&stroke, Qt::LeftButton, Qt::AltModifier, at);
    QVERIFY(startedDrags.size() == 2 && !listed.chosen());
}

void LayerEffectRowTests::effectDropsLandOnTheRowUnderThePointer()
{
    Listed listed(3);
    const QUuid top = listed.id(0), middle = listed.id(1), bottom = listed.id(2);
    StrokeEffect wide;
    wide.size = 12;
    listed.session.setEffects(LayerEffects{.stroke = wide}, top);
    const QString payload = uuidString(top) + QStringLiteral(":Stroke");
    const QPoint onTop = listed.at(listed.cell(0), QPoint(100, 20)), onMiddle = listed.at(listed.cell(1), QPoint(100, 20));
    // The row under the pointer takes a copy.
    const std::unique_ptr<QMimeData> probe(effectDrag(listed.list, payload));
    QCOMPARE(listed.list.dropTarget(*probe, Qt::CopyAction, onMiddle).value(), (LayerDropTarget{.row = 1, .onRow = true, .atBottom = false, .copying = true}));
    QVERIFY(listed.drop(effectDrag(listed.list, payload), onMiddle));
    QVERIFY(listed.lastAction == Qt::CopyAction && layerWith(listed.session, middle).effects->stroke->size == 12);
    QVERIFY(listed.session.history.undoName() == "Copy Stroke" && listed.chosen() == (LayerEffectSelection{middle, LayerEffectKind::stroke}));
    // Refused: its own row, a missing kind, strangers, bad words.
    QVERIFY(!listed.drop(effectDrag(listed.list, payload), onTop));
    QVERIFY(!listed.drop(effectDrag(listed.list, uuidString(bottom) + QStringLiteral(":Stroke")), onMiddle));
    auto *stranger = effectDrag(listed.list, payload);
    stranger->setData(NativeLayerList::sourceType, QByteArray("another list"));
    QVERIFY(!listed.drop(stranger, listed.at(listed.cell(2), QPoint(100, 20))));
    for (const QString &bad : {uuidString(top) + QStringLiteral(":Glow"), QStringLiteral("Stroke"), uuidString(top), uuidString(top) + QStringLiteral(":Stroke:x")})
        QVERIFY(!listed.drop(effectDrag(listed.list, bad), listed.at(listed.cell(2), QPoint(100, 20))));
    QVERIFY(!listed.drop(effectDrag(listed.list, payload), listed.at(listed.cell(2), QPoint(100, listed.cell(2).height() + 30))));
    QVERIFY(!layerWith(listed.session, bottom).effects);
    // An empty layer and a folder hold no effects.
    listed.session.selectLayer(bottom);
    listed.session.addBlankLayer();
    const QUuid blank = listed.session.activeLayerID().value();
    QVERIFY(!listed.drop(effectDrag(listed.list, payload), listed.at(listed.cell(listed.row(blank)), QPoint(100, 20))));
    listed.session.addGroup();
    const QUuid folder = listed.session.activeLayerID().value();
    QVERIFY(!listed.drop(effectDrag(listed.list, payload), listed.at(listed.cell(listed.row(folder)), QPoint(100, 20))));
}

void LayerEffectRowTests::aRowRebuiltUnderItsOwnPressLivesOn()
{
    Listed listed;
    const QUuid top = listed.id(0);
    listed.session.setEffects(LayerEffects{.shadow = ShadowEffect()}, top);
    // A new stroke's panel: its Cancel takes the stroke away.
    listed.session.addEffect(LayerEffectKind::stroke);
    const QPointer<LayerEffectRow> shadow = listed.effects(0)[1];
    QCOMPARE(shadow->kind, LayerEffectKind::shadow);
    listed.list.clearFocus();
    // Editing the shadow cancels the stroke under the shadow's press.
    QTest::mouseDClick(shadow.data(), Qt::LeftButton, Qt::NoModifier, QPoint(150, 12));
    QVERIFY(shadow && !shadow->isVisible());
    QVERIFY(listed.session.effectsEditing() == (LayerEffectSelection{top, LayerEffectKind::shadow}) && listed.list.hasFocus());
    QVERIFY(!layerWith(listed.session, top).effects->stroke && listed.effects(0).size() == 1);
    QTRY_VERIFY(!shadow);
}

void LayerEffectRowTests::aScreenReaderPressChoosesTheEffect()
{
    Listed listed;
    listed.session.setEffects(strokeAndShadow(), listed.id(0));
    QAccessibleInterface *row = QAccessible::queryAccessibleInterface(listed.effects(0)[1]);
    QVERIFY(row && row->role() == QAccessible::Grouping && row->text(QAccessible::Name) == "Drop Shadow effect");
    QCOMPARE(QAccessible::queryAccessibleInterface(&listed.cell(0))->role(), QAccessible::Client);
    QCOMPARE(row->actionInterface()->actionNames(), QStringList{QAccessibleActionInterface::pressAction()});
    listed.list.clearFocus();
    row->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
    QVERIFY(listed.chosen() == (LayerEffectSelection{listed.id(0), LayerEffectKind::shadow}) && listed.list.hasFocus());
}

void LayerEffectRowTests::altShowsTheCursorsOverATallerRow()
{
    Listed listed;
    const QUuid top = listed.id(0);
    listed.session.setEffects(strokeAndShadow(), top);
    const double ratio = listed.list.devicePixelRatio();
    const auto image = [](const QCursor &cursor) { return cursor.pixmap().toImage(); };
    // The strip runs along the taller row's bottom.
    QCOMPARE(image(listed.list.cursorFor(listed.at(listed.cell(0), QPoint(200, 96)), Qt::AltModifier)), image(NativeLayerList::clippingCursor(false, ratio)));
    QCOMPARE(image(listed.list.cursorFor(listed.at(listed.cell(0), QPoint(200, 48)), Qt::AltModifier)), image(CanvasView::duplicateCursor(ratio)));
    QCOMPARE(image(listed.list.cursorFor(listed.at(listed.cell(0), QPoint(200, 60)), Qt::AltModifier)), image(CanvasView::duplicateCursor(ratio)));
    // Hovered with Alt, an effect shows the duplicate cursor.
    QMouseEvent hover(QEvent::MouseMove, QPointF(150, 12), listed.effects(0)[0]->mapToGlobal(QPointF(150, 12)), Qt::NoButton, Qt::NoButton,
                      Qt::AltModifier);
    QApplication::sendEvent(listed.effects(0)[0], &hover);
    QCOMPARE(image(listed.list.viewport()->cursor()), image(CanvasView::duplicateCursor(ratio)));
    // Where the strip was, Alt no longer clips.
    listed.click(listed.cell(0), QPoint(200, 48), Qt::AltModifier);
    QVERIFY(!layerWith(listed.session, top).maskSourceID);
    listed.click(listed.cell(0), QPoint(200, 96), Qt::AltModifier);
    QVERIFY(layerWith(listed.session, top).maskSourceID);
}

QTEST_MAIN(LayerEffectRowTests)
#include "LayerEffectRowTests.moc"
