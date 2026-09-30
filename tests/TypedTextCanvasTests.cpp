#include "InlineTextDrawFixtures.h"
#include "Document/LayerEffects+Renderer.h"

// Swift 1.2.11: typed text drawn as its pixels.
namespace {
// Only the box's inside, where frame and handles never draw.
int largestChange(const QImage &before, const QImage &after, QRect inside)
{
    int largest = 0;
    inside &= before.rect();
    for (int y = inside.top(); y <= inside.bottom(); ++y) {
        for (int x = inside.left(); x <= inside.right(); ++x) {
            const QRgb a = before.pixel(x, y), b = after.pixel(x, y);
            largest = std::max({largest, std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)), std::abs(qBlue(a) - qBlue(b))});
        }
    }
    return largest;
}

LayerEffects yellowStroke()
{
    LayerEffects effects;
    effects.stroke = StrokeEffect{.size = 3, .red = 1, .green = 1, .blue = 0, .opacity = 1};
    return effects;
}

// The caret draws in the text's colour: keys go elsewhere.
void unfocus(DrawnText &shown)
{
    QCoreApplication::processEvents();
    shown.canvas->clearFocus();
    if (!QTest::qWaitFor([&] { return !shown.canvas->hasFocus(); }))
        throw std::runtime_error("the canvas kept the keys");
}

// Debug lines on the rendering category, counted.
int remakes = 0;
QtMessageHandler passOn = nullptr;
void countRemakes(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (message == QLatin1String("typed text remade"))
        ++remakes;
    passOn(type, context, message);
}

// The union of the regions a widget paints.
struct PaintedRegion : QObject {
    QRegion region;
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::Paint)
            region |= static_cast<QPaintEvent *>(event)->region();
        return false;
    }
};

bool yellowish(QColor colour)
{
    return colour.red() > 200 && colour.green() > 200 && colour.blue() < 60;
}
}

class TypedTextCanvasTests : public QObject {
    Q_OBJECT
private slots:
    void textLooksTheSameWhileEditingAndOnceCommitted_data();
    void textLooksTheSameWhileEditingAndOnceCommitted();
    void newTextSitsAboveTheActiveLayer();
    void editedTextKeepsItsOpacityAndEffects();
    void typedTextFollowsEachChange();
    void aMaskHidesTypedTextsEffects();
    void cancelledTextHandsOnNothing();
    void typingRepaintsTheWholeView();
    void aHandleThatOnlyMovesRepaintsTheWholeView();
    void typedTextBlendsAsItsLayer();
    void typedTextIsRemadeOnlyWhenItChanges();
    void aMovedBoxRedoesItsTypedEffects();
};

void TypedTextCanvasTests::textLooksTheSameWhileEditingAndOnceCommitted_data()
{
    QTest::addColumn<double>("zoom");
    QTest::addColumn<bool>("coloured");
    QTest::newRow("smooth") << 1.5 << false;
    QTest::newRow("hard") << 4.0 << false;
    // Letters in their own colours: the layer draws them.
    QTest::newRow("coloured smooth") << 1.5 << true;
    QTest::newRow("coloured hard") << 4.0 << true;
}

void TypedTextCanvasTests::textLooksTheSameWhileEditingAndOnceCommitted()
{
    QFETCH(double, zoom);
    QFETCH(bool, coloured);
    DrawnText shown;
    shown.session.zoom(zoom);
    shown.canvas->synchronizeDisplay();
    const QImage blank = shown.grab();
    // The box's middle near the view's; it grows by four.
    shown.session.beginText(QPointF(163, 205), true);
    shown.session.changeTextStyle([](LayerTextStyle &style) {
        style.content = QStringLiteral("Sharp");
        style.fontSize = 24;
    });
    if (coloured)
        shown.session.changeTextStyle([](LayerTextStyle &style) { style.setColor(PaletteColor{0, 1, 0}, {1, 3}); });
    // Unfocused, no caret draws; the frame stays at the edge.
    unfocus(shown);
    const QRect inside = shown.editor().boxTransform().mapRect(QRectF(QPointF(0, 0), shown.editor().logicalSize())).toAlignedRect().adjusted(10, 10, -10, -10);
    const QImage editing = shown.grab();
    QVERIFY(largestChange(blank, editing, inside) > 100);
    QVERIFY(shown.session.finishText());
    QVERIFY(shown.session.activeLayer().value().liveText());
    const QImage committed = shown.grab();
    QVERIFY2(largestChange(editing, committed, inside) <= 2, qPrintable(QString::number(largestChange(editing, committed, inside))));
}

void TypedTextCanvasTests::newTextSitsAboveTheActiveLayer()
{
    DrawnText shown;
    // A drawn active layer, a green dot, under a cover.
    QImage green = BrushRaster::context(2, 2, false);
    green.fill(Qt::green);
    shown.session.addPixelLayer(green, QPointF(300, 250), QStringLiteral("Dot"), QStringLiteral("Add"));
    const QUuid below = shown.session.activeLayerID().value();
    QImage white = BrushRaster::context(400, 300, false);
    white.fill(Qt::white);
    shown.session.addPixelLayer(white, QPointF(0, 0), QStringLiteral("Cover"), QStringLiteral("Add"));
    const QUuid cover = shown.session.activeLayerID().value();
    shown.session.selectLayer(below);
    shown.session.selectTool(NavigationTool::type);
    beginTextAt(shown.session, QPointF(40, 40));
    shown.session.changeTextStyle([](LayerTextStyle &style) { style.content = QStringLiteral("Hidden"); });
    unfocus(shown);
    QVERIFY(!where(shown.grab(), reddish).isValid());
    shown.session.cancelText();
    // Above the cover it shows, as its layer will.
    shown.session.selectLayer(cover);
    beginTextAt(shown.session, QPointF(40, 40));
    shown.session.changeTextStyle([](LayerTextStyle &style) { style.content = QStringLiteral("Shown"); });
    unfocus(shown);
    QVERIFY(where(shown.grab(), reddish).isValid());
    shown.session.cancelText();
    // A hidden active layer draws nothing: text goes on top.
    shown.session.toggleLayerVisibility(below);
    shown.session.selectLayer(below);
    beginTextAt(shown.session, QPointF(40, 40));
    shown.session.changeTextStyle([](LayerTextStyle &style) { style.content = QStringLiteral("Top"); });
    unfocus(shown);
    QVERIFY(where(shown.grab(), reddish).isValid());
}

void TypedTextCanvasTests::editedTextKeepsItsOpacityAndEffects()
{
    DrawnText shown;
    const QUuid id = shown.text(QPointF(40, 40), QStringLiteral("HH"));
    shown.session.beginOpacityEdit();
    shown.session.setLayerOpacity(0.5);
    shown.session.finishOpacityEdit();
    shown.session.editActiveText();
    unfocus(shown);
    // Half opacity: half red over the checkerboard, no full red.
    const QRect ink = where(shown.grab(), [](QColor colour) { return colour.red() > 150 && colour.red() < 190 && colour.green() < 60; });
    QVERIFY(ink.isValid());
    QVERIFY(!where(shown.grab(), [](QColor colour) { return colour.red() > 240 && colour.green() < 60; }).isValid());
    shown.session.cancelText();
    shown.session.beginOpacityEdit();
    shown.session.setLayerOpacity(1);
    shown.session.finishOpacityEdit();
    // Effects stay on while typing, redone at once.
    shown.session.setEffects(yellowStroke());
    shown.session.editActiveText();
    shown.session.changeTextStyle([](LayerTextStyle &style) { style.content = QStringLiteral("HHH"); });
    QVERIFY(where(shown.grab(), yellowish).isValid());
    // Committed, the typed effects stand in: nothing blinks.
    QVERIFY(shown.session.finishText());
    shown.grab();
    const EffectsPreviewCache::Result seeded = shown.session.effectsPreviews.rendered(id).value();
    QVERIFY(seeded.placement);
    QVERIFY(where(shown.grab(), yellowish).isValid());
    // Handed on once: drawing again seeds nothing more.
    QTRY_VERIFY((shown.grab(), !shown.session.effectsPreviews.rendered(id).value().placement));
    // Cancelled, typed effects the layer never took hand on nothing.
    shown.session.editActiveText();
    shown.session.changeTextStyle([](LayerTextStyle &style) { style.content = QStringLiteral("HHHH"); });
    shown.grab();
    shown.session.cancelText();
    shown.grab();
    QVERIFY(!shown.session.effectsPreviews.rendered(id).value().placement);
}

void TypedTextCanvasTests::typedTextFollowsEachChange()
{
    DrawnText shown;
    shown.text(QPointF(40, 40), QStringLiteral("H"));
    shown.session.setEffects(yellowStroke());
    shown.session.editActiveText();
    unfocus(shown);
    const auto count = [&](bool (*test)(QColor)) {
        const QImage image = shown.grab();
        int found = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                found += test(image.pixelColor(x, y));
        return found;
    };
    const int ink = count(reddish), rim = count(yellowish);
    QVERIFY(ink > 0 && rim > 0);
    // More letters: more pixels and more effects, redrawn.
    shown.session.changeTextStyle([](LayerTextStyle &style) { style.content = QStringLiteral("HHH"); });
    QVERIFY(count(reddish) > ink * 2);
    QVERIFY(count(yellowish) > rim * 2);
}

void TypedTextCanvasTests::aMaskHidesTypedTextsEffects()
{
    // A black mask hides the whole text, linked.
    DrawnText shown;
    const QUuid id = shown.text(QPointF(40, 150), QStringLiteral("HHHH"));
    shown.session.setEffects(yellowStroke());
    shown.session.addLayerMask(false);
    shown.session.editActiveText();
    unfocus(shown);
    QVERIFY(!where(shown.grab(), reddish).isValid());
    QVERIFY(!where(shown.grab(), yellowish).isValid());
    shown.session.cancelText();
    // A frame between, as the app always draws one.
    shown.grab();
    // Placed over the left half, its black middle hides there.
    LayerTransform box;
    rewrite(shown.session, [&](ProjectSnapshot &snapshot) {
        ProjectLayerRecord &text = record(snapshot, id);
        box = text.transform;
        QImage ring = BrushRaster::context(30, 30, true);
        ring.fill(255);
        QPainter hole(&ring);
        hole.fillRect(QRect(10, 10, 10, 10), Qt::black);
        hole.end();
        snapshot.masks.insert_or_assign(id, ImportedImage(ring, ring, QStringLiteral("Mask")));
        text.maskPlacement = LayerTransform{.origin = box.origin, .size = QSizeF(box.size.width() / 2, box.size.height())};
        text.maskLinked = false;
    });
    shown.session.selectLayer(id);
    shown.session.editActiveText();
    unfocus(shown);
    const QImage image = shown.grab();
    const QSizeF size = shown.documentSize();
    const auto view = [&](double x, double y) {
        return shown.session.viewport.viewPoint(box.origin + QPointF(box.size.width() * x, box.size.height() * y), size).toPoint();
    };
    // Stretched over the whole text, that band would show.
    QVERIFY(!where(image.copy(QRect(view(1 / 6.0 + 0.02, 0.4), view(1 / 3.0 - 0.02, 0.6))), reddish).isValid());
    QVERIFY(where(image.copy(QRect(view(0.5, 0), view(1, 1))), reddish).isValid());
}

void TypedTextCanvasTests::cancelledTextHandsOnNothing()
{
    // Effects never rendered for these pixels: a seed would show.
    DrawnText shown;
    const QUuid id = shown.text(QPointF(40, 40), QStringLiteral("HH"));
    shown.session.setEffects(yellowStroke());
    shown.session.editActiveText();
    shown.session.changeTextStyle([](LayerTextStyle &style) { style.content = QStringLiteral("HHHH"); });
    shown.grab();
    shown.session.cancelText();
    shown.grab();
    const std::optional<EffectsPreviewCache::Result> shownEffects = shown.session.effectsPreviews.rendered(id);
    QVERIFY(!shownEffects || !shownEffects.value().placement);
}

void TypedTextCanvasTests::typingRepaintsTheWholeView()
{
    // Box text's style alone changes; its effects need the view.
    DrawnText shown;
    shown.session.beginText(QRectF(40, 100, 100, 60));
    shown.session.changeTextStyle([](LayerTextStyle &style) {
        style.content = QStringLiteral("H");
        style.fontSize = 20;
    });
    QVERIFY(shown.session.finishText());
    shown.session.setEffects(yellowStroke());
    shown.session.editActiveText();
    // Any preview still to land has landed and painted.
    QTest::qWait(400);
    PaintedRegion painted;
    shown.canvas->installEventFilter(&painted);
    shown.session.changeTextStyle([](LayerTextStyle &style) { style.content = QStringLiteral("HH"); });
    QTRY_VERIFY(!painted.region.isEmpty());
    QCOMPARE(painted.region.boundingRect(), shown.canvas->rect());
}

void TypedTextCanvasTests::aHandleThatOnlyMovesRepaintsTheWholeView()
{
    // At 4x, one point's drag moves the box, same size.
    DrawnText shown;
    shown.session.beginText(QRectF(185, 135, 50, 40));
    shown.session.changeTextStyle([](LayerTextStyle &style) {
        style.content = QStringLiteral("H");
        style.fontSize = 20;
    });
    shown.session.zoom(4);
    shown.canvas->synchronizeDisplay();
    const LayerTextStyle before = shown.session.textDraft().value().style;
    const QPointF left = shown.editor().boxTransform().map(QPointF(0, shown.editor().logicalSize().height() / 2));
    shown.press(left);
    QTest::qWait(50);
    PaintedRegion painted;
    shown.canvas->installEventFilter(&painted);
    shown.move(left + QPointF(1, 0));
    QCOMPARE(shown.session.textDraft().value().style, before);
    QTRY_VERIFY(!painted.region.isEmpty());
    QCOMPARE(painted.region.boundingRect(), shown.canvas->rect());
    shown.release(left + QPointF(1, 0));
}

void TypedTextCanvasTests::typedTextBlendsAsItsLayer()
{
    // Red multiplied over green shows black, typed as committed.
    for (const bool effects : {false, true}) {
        DrawnText shown;
        QImage green = BrushRaster::context(400, 300, false);
        green.fill(Qt::green);
        shown.session.addPixelLayer(green, QPointF(0, 0), QStringLiteral("Green"), QStringLiteral("Add"));
        shown.text(QPointF(40, 150), QStringLiteral("HH"));
        shown.session.setLayerBlendMode(LayerBlendMode::multiply);
        if (effects)
            shown.session.setEffects(yellowStroke());
        QTRY_VERIFY(!effects || (shown.grab(), shown.session.effectsPreviews.rendered(shown.session.activeLayerID().value())));
        const QImage committed = shown.grab();
        shown.session.editActiveText();
        unfocus(shown);
        const QRect inside = shown.editor().boxTransform().mapRect(QRectF(QPointF(0, 0), shown.editor().logicalSize())).toAlignedRect().adjusted(10, 10, -10, -10);
        const QImage typed = shown.grab();
        QVERIFY(!where(typed.copy(inside), reddish).isValid());
        QVERIFY2(largestChange(committed, typed, inside) <= 2, qPrintable(QString::number(largestChange(committed, typed, inside))));
    }
}

void TypedTextCanvasTests::typedTextIsRemadeOnlyWhenItChanges()
{
    QLoggingCategory::setFilterRules(QStringLiteral("omaphoto.rendering.debug=true"));
    passOn = qInstallMessageHandler(countRemakes);
    DrawnText shown;
    beginTextAt(shown.session, QPointF(40, 150));
    shown.session.changeTextStyle([](LayerTextStyle &style) { style.content = QStringLiteral("H"); });
    remakes = 0;
    shown.grab();
    shown.grab();
    QCOMPARE(remakes, 1);
    shown.session.changeTextStyle([](LayerTextStyle &style) { style.content = QStringLiteral("HH"); });
    shown.grab();
    shown.grab();
    QCOMPARE(remakes, 2);
    // Ended, the image goes: the same style is made again.
    const LayerTextStyle typed = shown.session.textDraft().value().style;
    shown.session.cancelText();
    shown.grab();
    beginTextAt(shown.session, QPointF(40, 150));
    shown.session.changeTextStyle([&](LayerTextStyle &style) { style = typed; });
    shown.grab();
    QCOMPARE(remakes, 3);
    qInstallMessageHandler(passOn);
    QLoggingCategory::setFilterRules(QString());
}

void TypedTextCanvasTests::aMovedBoxRedoesItsTypedEffects()
{
    // A placed mask's band over box text with effects.
    DrawnText shown;
    shown.session.beginText(QRectF(185, 135, 50, 60));
    shown.session.changeTextStyle([](LayerTextStyle &style) {
        style.content = QStringLiteral("HH");
        style.fontSize = 20;
    });
    QVERIFY(shown.session.finishText());
    const QUuid id = shown.session.activeLayerID().value();
    shown.session.setEffects(yellowStroke());
    shown.session.addLayerMask(true);
    rewrite(shown.session, [&](ProjectSnapshot &snapshot) {
        ProjectLayerRecord &text = record(snapshot, id);
        QImage band = BrushRaster::context(50, 40, true);
        band.fill(255);
        QPainter painter(&band);
        painter.fillRect(QRect(20, 0, 10, 40), Qt::black);
        painter.end();
        snapshot.masks.insert_or_assign(id, ImportedImage(band, band, QStringLiteral("Mask")));
        text.maskPlacement = LayerTransform{.origin = text.transform.origin, .size = QSizeF(40, text.transform.size.height())};
        text.maskLinked = false;
    });
    shown.session.selectLayer(id);
    shown.session.editActiveText();
    unfocus(shown);
    shown.session.zoom(4);
    shown.canvas->synchronizeDisplay();
    shown.grab();
    // A fresh canvas renders the effects anew; they agree.
    const LayerTextStyle before = shown.session.textDraft().value().style;
    const LayerTransform moved = shown.editor().shownTransform();
    const QPointF left = shown.editor().boxTransform().map(QPointF(0, shown.editor().logicalSize().height() / 2));
    shown.press(left);
    shown.move(left + QPointF(1, 0));
    shown.release(left + QPointF(1, 0));
    QCOMPARE(shown.session.textDraft().value().style, before);
    QVERIFY(shown.editor().shownTransform().origin != moved.origin);
    QVERIFY(layerWith(shown.session, id).mask.value().placement);
    const QImage kept = shown.grab();
    QVERIFY(where(kept, reddish).isValid() && where(kept, yellowish).isValid());
    // A fresh canvas renders the effects anew: they must agree.
    CanvasView fresh(shown.session);
    fresh.resize(shown.canvas->size());
    fresh.synchronizeDisplay();
    const QImage anew = fresh.grab().toImage().convertToFormat(QImage::Format_RGB32);
    const QRect inside = shown.editor().boxTransform().mapRect(QRectF(QPointF(0, 0), shown.editor().logicalSize())).toAlignedRect().adjusted(10, 10, -10, -10);
    QVERIFY2(largestChange(kept, anew, inside) <= 2, qPrintable(QString::number(largestChange(kept, anew, inside))));
}

QTEST_MAIN(TypedTextCanvasTests)
#include "TypedTextCanvasTests.moc"
