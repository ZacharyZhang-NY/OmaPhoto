#include "SessionFixtures.h"
#include "UI/ColorPickerSheet.h"
#include <QApplication>
#include <QSignalSpy>
#include <QtTest>
#include <cmath>

// Swift's ColorPickerTests, and the session's picker.
namespace {
QWidget *shownPanel()
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() == ColorPickerPanelController::identifier() && widget->isVisible())
            return widget;
    }
    return nullptr;
}

// Red over its top two rows, blue under.
std::unique_ptr<EditorSession> splitSession()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(4, 4);
    QImage image(4, 4, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::blue);
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 4; ++x)
            image.setPixelColor(x, y, Qt::red);
    }
    session->insert(ImportedImage(image, image, QStringLiteral("Split")));
    return session;
}
}

class ColorPickerTests : public QObject {
    Q_OBJECT
private slots:
    void hexParsesFullShorthandAndRejectsInvalid();
    void hsbRoundTripsEightBitColors();
    void graysAndBlackKeepPreviousHueAndSaturation();
    void hueTurnsTheWheelBySixths();
    void canvasSamplingReadsCompositeAndCommitsOnlyOnOK();
    void samplingUndoesPremultiplication();
    void pickersOpenOnlyWhereTheyMay();
    void textPickersColourTheTextAndTheSwatch();
    void pickerReopensWhereItWasLastLeft();
};

void ColorPickerTests::hexParsesFullShorthandAndRejectsInvalid()
{
    QCOMPARE(PaletteColor::fromHex(QStringLiteral("#FF8000")), std::optional(PaletteColor{1, 128.0 / 255, 0}));
    QCOMPARE(PaletteColor::fromHex(QStringLiteral("0f0")), std::optional(PaletteColor{0, 1, 0}));
    QCOMPARE(PaletteColor::fromHex(QStringLiteral(" 00ff00 ")), std::optional(PaletteColor{0, 1, 0}));
    QVERIFY(!PaletteColor::fromHex(QStringLiteral("12345")));
    QVERIFY(!PaletteColor::fromHex(QStringLiteral("GGGGGG")));
    QCOMPARE((PaletteColor{1, 128.0 / 255, 0}).hex(), QString("FF8000"));
    // Swift trims tabs and wide spaces, never line breaks.
    QCOMPARE(PaletteColor::fromHex(QStringLiteral("\t#abc") + QChar(0x00A0)), std::optional(PaletteColor{170.0 / 255, 187.0 / 255, 204.0 / 255}));
    QVERIFY(!PaletteColor::fromHex(QStringLiteral("\n00ff00")));
    // Its UInt32 takes signs, minus for zero only; no 0x.
    QCOMPARE(PaletteColor::fromHex(QStringLiteral("+0F800")), std::optional(PaletteColor{0, 248.0 / 255, 0}));
    QCOMPARE(PaletteColor::fromHex(QStringLiteral("-00000")), std::optional(PaletteColor::black()));
    QVERIFY(!PaletteColor::fromHex(QStringLiteral("-00001")));
    QVERIFY(!PaletteColor::fromHex(QStringLiteral("0x1234")));
    QVERIFY(!PaletteColor::fromHex(QStringLiteral("#")));
    QVERIFY(!PaletteColor::fromHex(QStringLiteral("##abc")));
    QCOMPARE((PaletteColor{0.2, 0.5, 1}).quantized(), (PaletteColor{51.0 / 255, 128.0 / 255, 1}));
    // Halves round up, as Swift's; every digit and case reads.
    QCOMPARE((PaletteColor{0.5, 0.5, 0.5}).hex(), QString("808080"));
    QCOMPARE(PaletteColor::fromHex(QStringLiteral("#99AAbb")), std::optional(PaletteColor{153.0 / 255, 170.0 / 255, 187.0 / 255}));
    QVERIFY(!PaletteColor::fromHex(QStringLiteral("gggggg")));
}

void ColorPickerTests::hsbRoundTripsEightBitColors()
{
    for (const char *hex : {"000000", "FFFFFF", "FF0000", "00FF00", "0000FF", "FF8000", "7F3FA2", "123456"})
        QCOMPARE(PickerHSB(PaletteColor::fromHex(QString::fromLatin1(hex)).value()).rgb().quantized().hex(), QString::fromLatin1(hex));
}

void ColorPickerTests::graysAndBlackKeepPreviousHueAndSaturation()
{
    PickerHSB hsb(PaletteColor::fromHex(QStringLiteral("FF8000")).value());
    const double hue = hsb.hue;
    hsb.setRGB(PaletteColor::fromHex(QStringLiteral("808080")).value());
    QVERIFY(hsb.hue == hue && hsb.saturation == 0);
    hsb.saturation = 0.5;
    hsb.setRGB(PaletteColor::black());
    QVERIFY(hsb.hue == hue && hsb.saturation == 0.5 && hsb.brightness == 0);
}

void ColorPickerTests::hueTurnsTheWheelBySixths()
{
    // Each sixth, and hues past the circle either way.
    const std::vector<std::pair<double, QString>> hues{{0, "FF0000"},   {30, "FF8000"},  {90, "80FF00"},   {150, "00FF80"}, {210, "0080FF"},
                                                       {270, "8000FF"}, {330, "FF0080"}, {360, "FF0000"}, {-60, "FF00FF"}, {420, "FFFF00"}};
    for (const auto &[hue, hex] : hues)
        QCOMPARE(PickerHSB(hue, 1, 1).rgb().quantized().hex(), hex);
    QCOMPARE(PickerHSB(0, 0.5, 0.5).rgb(), (PaletteColor{0.5, 0.25, 0.25}));
    // Hues from each channel on top, as Swift reads them.
    QCOMPARE(PickerHSB(PaletteColor{0, 1, 0.5}).hue, 150.0);
    QCOMPARE(PickerHSB(PaletteColor{0.5, 0, 1}).hue, 270.0);
    QCOMPARE(PickerHSB(PaletteColor{1, 0, 0.5}).hue, 330.0);
}

void ColorPickerTests::canvasSamplingReadsCompositeAndCommitsOnlyOnOK()
{
    const std::unique_ptr<EditorSession> session = splitSession();
    QCOMPARE(session->sampleCompositeColor(QPointF(1.5, 0.5)).value().hex(), QString("FF0000"));
    QCOMPARE(session->sampleCompositeColor(QPointF(1.5, 3.5)).value().hex(), QString("0000FF"));
    QVERIFY(!session->sampleCompositeColor(QPointF(-1, 1)));
    session->openColorPicker(false);
    session->sampleIntoColorPicker(QPointF(2, 3));
    QCOMPARE(session->colorPicker().value().color().hex(), QString("0000FF"));
    QCOMPARE(session->foregroundColor(), PaletteColor::black());
    session->closeColorPicker(false);
    QVERIFY(session->foregroundColor() == PaletteColor::black() && !session->colorPicker());
    session->openColorPicker(true);
    session->sampleIntoColorPicker(QPointF(2, 0));
    session->closeColorPicker(true);
    QVERIFY(session->backgroundColor().hex() == QString("FF0000") && session->foregroundColor() == PaletteColor::black());
}

void ColorPickerTests::samplingUndoesPremultiplication()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(4, 4);
    // Half-clear orange, stored premultiplied, reads as orange.
    QImage image(4, 4, QImage::Format_RGBA8888_Premultiplied);
    image.fill(QColor(255, 128, 0, 128));
    session->insert(ImportedImage(image, image, QStringLiteral("Orange")));
    QCOMPARE(session->sampleCompositeColor(QPointF(0, 0)).value(), (PaletteColor{1, 128.0 / 255, 0}));
    // Past the far edges is outside, even over a layer.
    QImage wide(6, 6, QImage::Format_RGBA8888_Premultiplied);
    wide.fill(Qt::blue);
    session->insert(ImportedImage(wide, wide, QStringLiteral("Wide")));
    for (const QPointF point : {QPointF(4, 1), QPointF(1, 4), QPointF(std::nan(""), 1), QPointF(1, std::nan(""))})
        QVERIFY(!session->sampleCompositeColor(point));
    QCOMPARE(session->sampleCompositeColor(QPointF(3.9, 3.9)).value().hex(), QString("0000FF"));
    // A clear pixel has no colour; neither does no document.
    session->createDocument(4, 4);
    QVERIFY(!session->sampleCompositeColor(QPointF(1, 1)));
    session->openColorPicker(false);
    const QUuid id = session->colorPicker().value().id;
    QSignalSpy changed(session.get(), &EditorSession::changed);
    session->sampleIntoColorPicker(QPointF(1, 1));
    QCOMPARE(changed.count(), 0);
    QCOMPARE(session->colorPicker().value().id, id);
    QVERIFY(!EditorSession().sampleCompositeColor(QPointF(0, 0)));
}

void ColorPickerTests::pickersOpenOnlyWhereTheyMay()
{
    const std::unique_ptr<EditorSession> session = splitSession();
    QSignalSpy changed(session.get(), &EditorSession::changed);
    // Closing nothing is silent; so is an unchanged colour.
    session->closeColorPicker(true);
    QCOMPARE(changed.count(), 0);
    session->openColorPicker(true);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(session->colorPicker().value().target.title(), QString("Color Picker (Background Color)"));
    QCOMPARE(session->colorPicker().value().original, PaletteColor::white());
    session->setColorPickerHSB(session->colorPicker().value().hsb);
    QCOMPARE(changed.count(), 1);
    session->setColorPickerHSB(PickerHSB(120, 1, 1));
    QCOMPARE(changed.count(), 2);
    // Another swatch opens anew, its colour its own.
    const QUuid first = session->colorPicker().value().id;
    session->openColorPicker(false);
    QCOMPARE(session->colorPicker().value().target.title(), QString("Color Picker (Foreground Color)"));
    QVERIFY(session->colorPicker().value().id != first);
    QCOMPARE(session->colorPicker().value().color(), PaletteColor::black());
    // Cancel keeps the palette; OK writes the working colour.
    session->setColorPickerHSB(PickerHSB(240, 1, 1));
    session->closeColorPicker(false);
    QCOMPARE(session->foregroundColor(), PaletteColor::black());
    session->openColorPicker(false);
    session->setColorPickerHSB(PickerHSB(240, 1, 1));
    session->closeColorPicker(true);
    QCOMPARE(session->foregroundColor(), (PaletteColor{0, 0, 1}));
    // A mask's palette is black and white: no picker opens.
    const QUuid id = session->activeLayerID().value();
    session->addMask();
    session->selectLayerTarget(id, true);
    QVERIFY(session->isMaskSelected());
    changed.clear();
    session->openColorPicker(false);
    QVERIFY(!session->colorPicker());
    QCOMPARE(changed.count(), 0);
    // A mask chosen meanwhile keeps the palette on OK.
    session->selectLayerTarget(id, false);
    session->openColorPicker(true);
    session->setColorPickerHSB(PickerHSB(0, 1, 1));
    session->selectLayerTarget(id, true);
    session->closeColorPicker(true);
    QVERIFY(!session->colorPicker());
    QCOMPARE(session->backgroundColor(), PaletteColor::white());
    QVERIFY(!session->maskPaintWhite());
    // The background announces itself.
    changed.clear();
    session->selectLayerTarget(id, false);
    changed.clear();
    session->setBackgroundColor(PaletteColor{0, 0, 1});
    QCOMPARE(changed.count(), 1);
}

void ColorPickerTests::textPickersColourTheTextAndTheSwatch()
{
    const std::unique_ptr<EditorSession> session = splitSession();
    const PaletteColor cool = PickerHSB(PaletteColor{0.2, 0.4, 0.6}).rgb().quantized();
    // Every channel apart from cool's, so each write shows.
    const PaletteColor warm = PickerHSB(PaletteColor{0.7, 0.5, 0.1}).rgb().quantized();
    // Type's swatch opens only under Type, and only once.
    session->openTextColorPicker();
    QVERIFY(!session->colorPicker());
    session->selectTool(NavigationTool::type);
    QSignalSpy changed(session.get(), &EditorSession::changed);
    session->openTextColorPicker();
    QCOMPARE(changed.count(), 1);
    QCOMPARE(session->colorPicker().value().target.title(), QString("Color Picker (Text Color)"));
    const QUuid id = session->colorPicker().value().id;
    session->openTextColorPicker();
    QCOMPARE(session->colorPicker().value().id, id);
    // Cancelled, it leaves the next text and the swatch.
    session->setColorPickerHSB(PickerHSB(cool));
    session->closeColorPicker(false);
    QCOMPARE(session->textDefaults().blue, 0.0);
    QCOMPARE(session->foregroundColor(), PaletteColor::black());
    // No text open: the next text's colour, and the foreground.
    session->openTextColorPicker();
    session->setColorPickerHSB(PickerHSB(cool));
    session->closeColorPicker(true);
    const LayerTextStyle defaults = session->textDefaults();
    QCOMPARE((PaletteColor{defaults.red, defaults.green, defaults.blue}), cool);
    QCOMPARE(session->foregroundColor(), cool);
    // Open text takes it, the swatch showing the text's colour.
    session->beginText(QPointF(1, 1), true);
    QCOMPARE(session->typeColor(), cool);
    session->openTextColorPicker();
    QCOMPARE(session->colorPicker().value().original, cool);
    session->setColorPickerHSB(PickerHSB(warm));
    session->closeColorPicker(true);
    QCOMPARE(session->typeColor(), warm);
    QCOMPARE(session->foregroundColor(), warm);
    // Another draft by then, or another tool, takes nothing.
    session->openTextColorPicker();
    session->setColorPickerHSB(PickerHSB(120, 1, 1));
    session->cancelText();
    session->closeColorPicker(true);
    QCOMPARE(session->textDefaults().blue, cool.blue);
    QCOMPARE(session->foregroundColor(), warm);
    session->openTextColorPicker();
    session->setColorPickerHSB(PickerHSB(120, 1, 1));
    session->selectTool(NavigationTool::brush);
    session->closeColorPicker(true);
    QCOMPARE(session->foregroundColor(), warm);
    // Beside live text, no draft: the defaults; nothing opens.
    session->selectTool(NavigationTool::type);
    session->beginText(QPointF(1, 1), true);
    TextDraft draft = session->textDraft().value();
    draft.style.content = QStringLiteral("Hi");
    session->setTextDraft(draft);
    QVERIFY(session->finishText());
    QVERIFY(session->activeLayer().value().liveText());
    // With its mask chosen, the swatch keeps its colour.
    session->addMask();
    QVERIFY(session->isMaskSelected());
    session->openTextColorPicker();
    session->setColorPickerHSB(PickerHSB(240, 1, 1));
    session->closeColorPicker(true);
    QVERIFY(!session->textDraft());
    QCOMPARE(session->textDefaults().blue, 1.0);
    QCOMPARE(session->foregroundColor(), warm);
    // Busy, nothing opens.
    session->setIsProjectBusy(true);
    session->openTextColorPicker();
    session->openColorPicker(false);
    QVERIFY(!session->colorPicker());
}

void ColorPickerTests::pickerReopensWhereItWasLastLeft()
{
    const std::unique_ptr<EditorSession> session = splitSession();
    QWidget window;
    window.resize(400, 300);
    window.show();
    ColorPickerPanelController controller(window);
    session->openColorPicker(false);
    controller.show(session->colorPicker().value(), *session);
    QWidget *panel = shownPanel();
    QVERIFY(panel);
    const QPoint spot(40, 40);
    panel->move(spot);
    session->closeColorPicker(false);
    controller.close();
    QVERIFY(!panel->isVisible());
    session->openColorPicker(true);
    controller.show(session->colorPicker().value(), *session);
    QCOMPARE(shownPanel(), panel);
    QCOMPARE(panel->pos(), spot);
    QCOMPARE(panel->windowTitle(), QString("Color Picker (Background Color)"));
    // Switching swatches while open keeps it in place too.
    session->openColorPicker(false);
    controller.show(session->colorPicker().value(), *session);
    QCOMPARE(panel->pos(), spot);
    session->closeColorPicker(false);
    controller.close();
}

QTEST_MAIN(ColorPickerTests)
#include "ColorPickerTests.moc"
