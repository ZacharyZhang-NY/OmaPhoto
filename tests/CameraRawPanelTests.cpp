#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include "UI/CameraRawControls.h"
#include "UI/ColorPickerSheet.h"
#include "UI/LayerIcons.h"
#include <QComboBox>
#include <QLabel>
#include <QMenu>
#include <QScrollArea>
#include <QToolButton>
#include <QtTest>

// The panel's scope, groups, Light, Color and Effects.
namespace {
struct Panel {
    EditorSession session;
    std::unique_ptr<CameraRawControls> controls;
    Panel()
    {
        session.createDocument(8, 8);
        QImage image = BrushRaster::context(8, 8, false);
        image.fill(QColor(200, 120, 60));
        session.insert(ImportedImage(image, image, QStringLiteral("Warm")));
        session.beginFilter(FilterKind::cameraRaw);
        controls = std::make_unique<CameraRawControls>(session);
        controls->resize(392, 700);
        controls->show();
    }
    template <typename Widget> Widget &child(const QString &name) const
    {
        auto *found = controls->findChild<Widget *>(name);
        if (!found)
            throw std::runtime_error(name.toStdString());
        return *found;
    }
    const CameraRawSettings &raw() const { return session.filterEdit().value().settings.cameraRaw; }
    const CameraRawPanel &panel() const { return session.filterEdit().value().rawPanel; }
    void arm(const std::function<void(CameraRawPanel &)> &change)
    {
        CameraRawPanel copy = panel();
        change(copy);
        session.setCameraRawPanel(copy);
    }
};

struct Field {
    const char *name;
    double CameraRawSettings::*member;
    double low;
    double high;
    double reset;
    const char *title;
    std::optional<CameraRawClipping> clipping;
    const char *help;
};

constexpr auto H = CameraRawClipping::highlights, S = CameraRawClipping::shadows;
const std::optional<CameraRawClipping> none;

// Swift's rows: range, reset, title, clipping view and help.
const std::vector<Field> fields{
    {"exposure", &CameraRawSettings::exposure, -5, 5, 0, "Exposure", H,
     "Brightens or darkens the whole picture, in stops of light. Hold Alt to see clipped highlights."},
    {"contrast", &CameraRawSettings::contrast, -100, 100, 0, "Contrast", none,
     "Makes light and dark tones more or less different, mostly around the middle."},
    {"highlights", &CameraRawSettings::highlights, -100, 100, 0, "Highlights", H,
     "Adjusts the bright parts of the picture. Hold Alt to see clipped highlights."},
    {"shadows", &CameraRawSettings::shadows, -100, 100, 0, "Shadows", S, "Adjusts the dark parts of the picture. Hold Alt to see clipped shadows."},
    {"whites", &CameraRawSettings::whites, -100, 100, 0, "Whites", H, "Sets the brightest point. Hold Alt to see clipped highlights."},
    {"blacks", &CameraRawSettings::blacks, -100, 100, 0, "Blacks", S, "Sets the darkest point. Hold Alt to see clipped shadows."},
    {"temperature", &CameraRawSettings::temperature, -100, 100, 0, "Temperature", none, "Shifts the picture from blue to yellow."},
    {"tint", &CameraRawSettings::tint, -100, 100, 0, "Tint", none, "Shifts the picture from green to mauve."},
    {"vibrance", &CameraRawSettings::vibrance, -100, 100, 0, "Vibrance", none,
     "Strengthens quiet colors more than colors that are already strong, and protects skin tones."},
    {"saturation", &CameraRawSettings::saturation, -100, 100, 0, "Saturation", none, "Strengthens or weakens every color by the same amount."},
    {"texture", &CameraRawSettings::texture, -100, 100, 0, "Texture", none, "Adds or softens small detail."},
    {"clarity", &CameraRawSettings::clarity, -100, 100, 0, "Clarity", none, "Adds or softens contrast along broader shapes."},
    {"dehaze", &CameraRawSettings::dehaze, -100, 100, 0, "Dehaze", none, "Clears haze when raised, and adds haze when lowered."},
    {"glow", &CameraRawSettings::glow, 0, 100, 0, "Glow", none, "Spreads a glow from the bright areas."},
    {"glowRange", &CameraRawSettings::glowRange, -100, 100, 0, "Range", none,
     "Chooses how bright an area must be to glow. Has no effect until Glow is raised."},
    {"glowSpread", &CameraRawSettings::glowSpread, -100, 100, 0, "Spread", none,
     "Sets how far the glow reaches. Has no effect until Glow is raised."},
    {"glowWarmth", &CameraRawSettings::glowWarmth, -100, 100, 0, "Warmth", none,
     "Shifts the glow from cool to warm. Halation stays red. Has no effect until Glow is raised."},
    {"vignetteAmount", &CameraRawSettings::vignetteAmount, -100, 100, 0, "Amount", none,
     "Darkens or lightens the edges. The center does not change."},
    {"vignetteMidpoint", &CameraRawSettings::vignetteMidpoint, 0, 100, 50, "Midpoint", none,
     "Sets where the vignette begins, from the center outward."},
    {"vignetteRoundness", &CameraRawSettings::vignetteRoundness, -100, 100, 0, "Roundness", none, "Makes the vignette rounder or more square."},
    {"vignetteFeather", &CameraRawSettings::vignetteFeather, 0, 100, 50, "Feather", none, "Softens the edge of the vignette."},
    {"vignetteHighlights", &CameraRawSettings::vignetteHighlights, 0, 100, 0, "Highlights", none,
     "Protects bright pixels while a dark vignette is applied. Used by Highlight Priority."},
    {"grainAmount", &CameraRawSettings::grainAmount, 0, 100, 0, "Amount", none, "Adds film grain, strongest in the middle tones."},
    {"grainSize", &CameraRawSettings::grainSize, 0, 100, 25, "Size", none, "Makes the grain coarser or finer."},
    {"grainRoughness", &CameraRawSettings::grainRoughness, 0, 100, 50, "Roughness", none, "Makes the grain smoother or more uneven."},
};
}

class CameraRawPanelTests : public QObject {
    Q_OBJECT
private slots:
    void everySliderWritesItsFieldAndResets();
    void aTypedNumberAppliesAndIsHeldInRange();
    void altOnALightSliderShowsItsClippingUntilLetGo();
    void eachGroupOpensAndItsEyeHidesItFromThePreview();
    void theScopeSwitchesAndItsTrianglesShowClipping();
    void theReadoutNamesThePixel();
    void whiteBalanceAutoCustomAndTheEyedropper();
    void glowAndVignetteStylesAreWritten();
    void everyRowCarriesSwiftsTitleHelpAndClipping();
    void theScopeDrawsItsRibbonsAndItsCells();
    void theGroupsDrawTheirChevronsAndEyes();
};

void CameraRawPanelTests::everySliderWritesItsFieldAndResets()
{
    Panel panel;
    for (const Field &field : fields) {
        auto &slider = panel.child<CameraRawSlider>(QString::fromLatin1(field.name) + QStringLiteral("Slider"));
        slider.setValue(750);
        const double expected = field.low + (field.high - field.low) * 0.75;
        QVERIFY2(panel.raw().*field.member == expected, field.name);
        QCOMPARE(panel.child<PickerField>(QString::fromLatin1(field.name) + QStringLiteral("Field")).text(),
                 panel.controls->locale().toString(expected, 'f', field.low == -5 ? 1 : 0));
        // A double click on its title resets it.
        auto *label = slider.parentWidget()->findChild<QLabel *>();
        QTest::mouseDClick(label, Qt::LeftButton);
        QVERIFY2(panel.raw().*field.member == field.reset, field.name);
    }
    // Exposure shows two decimals, every other row whole numbers.
    FilterSettings fractional = panel.session.filterEdit().value().settings;
    for (const Field &field : fields)
        fractional.cameraRaw.*field.member = field.low + (field.high - field.low) * 0.063;
    panel.session.updateFilter(fractional, true);
    for (const Field &field : fields) {
        const double value = field.low + (field.high - field.low) * 0.063;
        QCOMPARE(panel.child<PickerField>(QString::fromLatin1(field.name) + QStringLiteral("Field")).text(),
                 panel.controls->locale().toString(value, 'f', field.low == -5 ? 2 : 0));
    }
    // Four rows colour their tracks; the rest stay gray.
    for (const Field &field : fields) {
        const QString name = QString::fromLatin1(field.name);
        if (name == QLatin1String("temperature") || name == QLatin1String("tint") || name == QLatin1String("vibrance") || name == QLatin1String("saturation"))
            continue;
        const QImage drawn = panel.child<CameraRawSlider>(name + QStringLiteral("Slider")).grab().toImage();
        const QColor end = drawn.pixelColor(drawn.width() - 4, drawn.height() / 2);
        QVERIFY2(std::abs(end.red() - end.green()) < 12 && std::abs(end.green() - end.blue()) < 12, field.name);
    }
    // Temperature and Tint make the balance Custom.
    FilterSettings automatic = panel.session.filterEdit().value().settings;
    automatic.cameraRaw.whiteBalance = CameraRawWhiteBalance::automatic;
    panel.session.updateFilter(automatic, true);
    panel.child<CameraRawSlider>(QStringLiteral("tintSlider")).setValue(600);
    QVERIFY(panel.raw().whiteBalance == CameraRawWhiteBalance::custom);
    // Each colour slider paints its own track: blue, green, gray.
    const auto left = [&panel](const char *name) {
        const QImage drawn = panel.child<CameraRawSlider>(QString::fromLatin1(name)).grab().toImage();
        return drawn.pixelColor(4, drawn.height() / 2);
    };
    QVERIFY(left("temperatureSlider").blue() > left("temperatureSlider").red() + 100);
    QVERIFY(left("tintSlider").green() > left("tintSlider").red() + 60);
    QVERIFY(std::abs(left("vibranceSlider").red() - left("vibranceSlider").green()) < 8);
    QVERIFY(std::abs(left("saturationSlider").red() - left("saturationSlider").blue()) < 8);
}

void CameraRawPanelTests::aTypedNumberAppliesAndIsHeldInRange()
{
    Panel panel;
    auto &field = panel.child<PickerField>(QStringLiteral("exposureField"));
    field.setFocus();
    field.selectAll();
    QTest::keyClicks(&field, QStringLiteral("1.25"));
    QTest::keyClick(&field, Qt::Key_Return);
    QCOMPARE(panel.raw().exposure, 1.25);
    QCOMPARE(panel.child<CameraRawSlider>(QStringLiteral("exposureSlider")).shown(), 1.25);
    field.selectAll();
    QTest::keyClicks(&field, QStringLiteral("9"));
    QTest::keyClick(&field, Qt::Key_Return);
    QCOMPARE(panel.raw().exposure, 5.0);
    // Text that is no number goes back.
    field.selectAll();
    QTest::keyClicks(&field, QStringLiteral("x"));
    QTest::keyClick(&field, Qt::Key_Return);
    QCOMPARE(field.text(), QString("5"));
}

void CameraRawPanelTests::altOnALightSliderShowsItsClippingUntilLetGo()
{
    Panel panel;
    QWindow *window = panel.controls->windowHandle();
    QTest::keyPress(window, Qt::Key_Alt, Qt::AltModifier);
    panel.child<CameraRawSlider>(QStringLiteral("whitesSlider")).setValue(700);
    QCOMPARE(panel.panel().clipping, std::optional(CameraRawClipping::highlights));
    panel.child<CameraRawSlider>(QStringLiteral("blacksSlider")).setValue(300);
    QCOMPARE(panel.panel().clipping, std::optional(CameraRawClipping::shadows));
    // Contrast has no clipping view.
    panel.child<CameraRawSlider>(QStringLiteral("contrastSlider")).setValue(700);
    QVERIFY(!panel.panel().clipping);
    panel.child<CameraRawSlider>(QStringLiteral("exposureSlider")).setValue(700);
    QVERIFY(panel.panel().clipping);
    panel.arm([](CameraRawPanel &raw) { raw.sharpenMask = true; });
    // A repeated release is no release: the views stay.
    QKeyEvent repeat(QEvent::KeyRelease, Qt::Key_Alt, Qt::NoModifier, QString(), true);
    QApplication::sendEvent(panel.controls.get(), &repeat);
    QVERIFY(panel.panel().clipping && panel.panel().sharpenMask);
    QTest::keyRelease(window, Qt::Key_Alt);
    QVERIFY(!panel.panel().clipping && !panel.panel().sharpenMask);
    // Without Alt a Light slider shows the grade.
    panel.child<CameraRawSlider>(QStringLiteral("whitesSlider")).setValue(800);
    QVERIFY(!panel.panel().clipping);
}

void CameraRawPanelTests::eachGroupOpensAndItsEyeHidesItFromThePreview()
{
    Panel panel;
    const QStringList groups{"Light", "Color", "Effects", "Curve", "ColorMixer", "ColorGrading", "Detail", "Optics", "Geometry", "Calibration"};
    for (const QString &group : groups) {
        auto &disclosure = panel.child<QToolButton>(group + QStringLiteral("Section"));
        auto *body = disclosure.parentWidget()->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly).last();
        const bool open = group == QLatin1String("Light") || group == QLatin1String("Color");
        QCOMPARE(body->isVisible(), open);
        disclosure.click();
        QCOMPARE(body->isVisible(), !open);
        // An eye shows only while the group adjusts.
        QVERIFY(!panel.child<QToolButton>(group + QStringLiteral("Eye")).isVisible());
    }
    panel.child<CameraRawSlider>(QStringLiteral("exposureSlider")).setValue(700);
    auto &eye = panel.child<QToolButton>(QStringLiteral("LightEye"));
    QVERIFY(eye.isVisible());
    QCOMPARE(eye.toolTip(), QString("Hide Light in the preview"));
    QCOMPARE(eye.accessibleName(), QString("Hide Light"));
    eye.click();
    QVERIFY(!panel.panel().shows.light && panel.panel().shows.color);
    QCOMPARE(eye.toolTip(), QString("Show Light in the preview"));
    QVERIFY(!panel.session.filterEdit().value().previewJob().settings.cameraRaw.adjustsLight());
    eye.click();
    QVERIFY(panel.panel().shows.light);
}

void CameraRawPanelTests::theScopeSwitchesAndItsTrianglesShowClipping()
{
    Panel panel;
    auto &scope = panel.child<QWidget>(QStringLiteral("cameraRawScope"));
    QCOMPARE(scope.accessibleName(), QString("RGB histogram"));
    QTimer::singleShot(0, [] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        menu->actions().at(1)->trigger();
        menu->close();
    });
    QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(10, 10), scope.mapToGlobal(QPoint(10, 10)));
    QApplication::sendEvent(&scope, &event);
    QVERIFY(panel.panel().scopeMode == CameraRawScopeMode::vectorscope);
    QCOMPARE(scope.accessibleName(), QString("Vectorscope"));
    QVERIFY(scope.toolTip().startsWith("Hue around the wheel"));
    panel.child<QToolButton>(QStringLiteral("shadowClipping")).click();
    QVERIFY(panel.panel().showsShadowClipping && !panel.panel().showsHighlightClipping);
    panel.child<QToolButton>(QStringLiteral("highlightClipping")).click();
    QVERIFY(panel.panel().showsHighlightClipping);
    // Lit, the shadows' triangle is blue.
    const QImage lit = panel.child<QToolButton>(QStringLiteral("shadowClipping")).grab().toImage();
    QCOMPARE(lit.pixelColor(7, 9), QColor(0, 145, 255));
    QCOMPARE(panel.child<QToolButton>(QStringLiteral("highlightClipping")).grab().toImage().pixelColor(7, 9), QColor(255, 66, 69));
    QTRY_VERIFY(!panel.session.filterEdit().value().preparing);
    QVERIFY(panel.session.filterEdit().value().previewJob().showsShadowClipping);
}

void CameraRawPanelTests::theReadoutNamesThePixel()
{
    Panel panel;
    auto &readout = panel.child<QLabel>(QStringLiteral("cameraRawReadout"));
    QCOMPARE(readout.text(), QString("R —   G —   B —"));
    panel.session.updateCameraRawReadout(QPointF(2, 2));
    QCOMPARE(readout.text(), QString("R 200   G 120   B 60"));
}

void CameraRawPanelTests::whiteBalanceAutoCustomAndTheEyedropper()
{
    Panel panel;
    auto &balance = panel.child<QComboBox>(QStringLiteral("whiteBalance"));
    QCOMPARE(balance.currentText(), QString("Custom"));
    balance.setCurrentIndex(1);
    emit balance.activated(1);
    QVERIFY(panel.raw().whiteBalance == CameraRawWhiteBalance::automatic);
    QTRY_VERIFY(panel.raw().temperature < 0);
    QCOMPARE(balance.currentText(), QString("Auto"));
    emit balance.activated(0);
    QVERIFY(panel.raw().whiteBalance == CameraRawWhiteBalance::custom && panel.raw().temperature < 0);
    auto &sampler = panel.child<QToolButton>(QStringLiteral("whiteBalanceSelector"));
    auto &hint = panel.child<QLabel>(QStringLiteral("whiteBalanceHint"));
    QVERIFY(!hint.isVisible());
    sampler.click();
    QVERIFY(panel.panel().samplesWhiteBalance && hint.isVisible());
    sampler.click();
    QVERIFY(!panel.panel().samplesWhiteBalance && !hint.isVisible());
}

void CameraRawPanelTests::glowAndVignetteStylesAreWritten()
{
    Panel panel;
    auto &glow = panel.child<QComboBox>(QStringLiteral("glowStyle"));
    glow.setCurrentIndex(2);
    emit glow.activated(2);
    QVERIFY(panel.raw().glowStyle == CameraRawGlowStyle::halation);
    auto &vignette = panel.child<QComboBox>(QStringLiteral("vignetteStyle"));
    vignette.setCurrentIndex(1);
    emit vignette.activated(1);
    QVERIFY(panel.raw().vignetteStyle == CameraRawVignetteStyle::colorPriority);
    // Settings written elsewhere show in both menus.
    FilterSettings settings = panel.session.filterEdit().value().settings;
    settings.cameraRaw.glowStyle = CameraRawGlowStyle::bloom;
    settings.cameraRaw.vignetteStyle = CameraRawVignetteStyle::paintOverlay;
    panel.session.updateFilter(settings, true);
    QCOMPARE(glow.currentText(), QString("Bloom"));
    QCOMPARE(vignette.currentText(), QString("Paint Overlay"));
}

void CameraRawPanelTests::everyRowCarriesSwiftsTitleHelpAndClipping()
{
    Panel panel;
    QWindow *window = panel.controls->windowHandle();
    QTest::keyPress(window, Qt::Key_Alt, Qt::AltModifier);
    for (const Field &field : fields) {
        auto &slider = panel.child<CameraRawSlider>(QString::fromLatin1(field.name) + QStringLiteral("Slider"));
        QCOMPARE(slider.parentWidget()->findChild<QLabel *>()->text(), QString::fromUtf8(field.title));
        QCOMPARE(slider.toolTip(), QString::fromUtf8(field.help));
        QCOMPARE(panel.child<PickerField>(QString::fromLatin1(field.name) + QStringLiteral("Field")).toolTip(), QString::fromUtf8(field.help));
        slider.setValue(slider.value() == 900 ? 100 : 900);
        QVERIFY2(panel.panel().clipping == field.clipping, field.name);
    }
    QTest::keyRelease(window, Qt::Key_Alt);
    // Exposure shows two decimals, the rest whole numbers.
    FilterSettings settings = panel.session.filterEdit().value().settings;
    settings.cameraRaw.exposure = 1.256;
    settings.cameraRaw.contrast = 12.6;
    panel.session.updateFilter(settings, true);
    QCOMPARE(panel.child<PickerField>(QStringLiteral("exposureField")).text(), panel.controls->locale().toString(1.26, 'f', 2));
    QCOMPARE(panel.child<PickerField>(QStringLiteral("contrastField")).text(), QString("13"));
}

void CameraRawPanelTests::theScopeDrawsItsRibbonsAndItsCells()
{
    Panel panel;
    // A gray ramp fills every bin: three full-height ribbons.
    QImage ramp = BrushRaster::context(256, 4, false);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 256; ++x)
            ramp.setPixelColor(x, y, QColor(x, x, x));
    panel.session.cancelFilter();
    panel.session.insert(ImportedImage(ramp, ramp, QStringLiteral("Ramp")));
    panel.session.beginFilter(FilterKind::cameraRaw);
    panel.controls = std::make_unique<CameraRawControls>(panel.session);
    panel.controls->resize(392, 700);
    panel.controls->show();
    panel.session.updateFilter(panel.session.filterEdit().value().settings, true);
    QTRY_VERIFY(panel.panel().scope);
    auto &scope = panel.child<QWidget>(QStringLiteral("cameraRawScope"));
    QImage drawn = scope.grab().toImage();
    // Black at 35% over the window, each ribbon at 55%.
    const QColor window = panel.controls->palette().color(QPalette::Window);
    const QColor ground(int(std::lround(window.red() * 0.65)), int(std::lround(window.green() * 0.65)), int(std::lround(window.blue() * 0.65)));
    const auto over = [](QColor top, QColor below) {
        return QColor(int(std::lround(top.red() * 0.55 + below.red() * 0.45)), int(std::lround(top.green() * 0.55 + below.green() * 0.45)),
                      int(std::lround(top.blue() * 0.55 + below.blue() * 0.45)));
    };
    const QColor stacked = over(QColor(0, 145, 255), over(QColor(48, 209, 88), over(QColor(255, 66, 69), ground)));
    for (const QPoint at : {QPoint(drawn.width() / 2, drawn.height() - 4), QPoint(drawn.width() / 3, 6)}) {
        const QColor seen = drawn.pixelColor(at);
        QVERIFY2(std::abs(seen.red() - stacked.red()) <= 3 && std::abs(seen.green() - stacked.green()) <= 3 && std::abs(seen.blue() - stacked.blue()) <= 3,
                 qPrintable(seen.name() + QStringLiteral(" against ") + stacked.name()));
    }
    // Blue lights its cell; rows past 18 fall off.
    QImage red = BrushRaster::context(8, 8, false);
    red.fill(QColor(0, 0, 255));
    panel.session.cancelFilter();
    panel.session.insert(ImportedImage(red, red, QStringLiteral("Blue")));
    panel.session.beginFilter(FilterKind::cameraRaw);
    panel.session.updateFilter(panel.session.filterEdit().value().settings, true);
    QTRY_VERIFY(panel.panel().scope);
    // Hue 240 at full reach: column 16, row 5.
    panel.arm([](CameraRawPanel &raw) { raw.scopeMode = CameraRawScopeMode::vectorscope; });
    drawn = scope.grab().toImage();
    const double cell = double(drawn.width()) / CameraRawScope::scopeSide;
    const QColor lit = drawn.pixelColor(int((16 + 0.5) * cell), int(drawn.height() - (5 + 0.5) * cell));
    QVERIFY2(lit.red() > 240 && lit.green() > 240 && lit.blue() > 240, qPrintable(lit.name()));
}

void CameraRawPanelTests::theGroupsDrawTheirChevronsAndEyes()
{
    Panel panel;
    const qreal ratio = panel.controls->devicePixelRatioF();
    const QColor ink = panel.controls->palette().color(QPalette::WindowText);
    const auto glyph = [ratio, &ink](LayerIcon icon, double side) { return LayerIcons::pixmap(icon, side, ink, ratio).toImage(); };
    const auto shows = [&](const QString &button, LayerIcon icon, double side) {
        const QToolButton &shown = panel.child<QToolButton>(button);
        return shown.icon().pixmap(QSize(int(side), int(side)), ratio).toImage() == glyph(icon, side);
    };
    QVERIFY(shows(QStringLiteral("LightSection"), LayerIcon::chevronDown, 12));
    QVERIFY(shows(QStringLiteral("EffectsSection"), LayerIcon::chevronRight, 12));
    panel.child<QToolButton>(QStringLiteral("EffectsSection")).click();
    QVERIFY(shows(QStringLiteral("EffectsSection"), LayerIcon::chevronDown, 12));
    panel.child<CameraRawSlider>(QStringLiteral("exposureSlider")).setValue(700);
    QVERIFY(shows(QStringLiteral("LightEye"), LayerIcon::eye, 14));
    panel.child<QToolButton>(QStringLiteral("LightEye")).click();
    QVERIFY(shows(QStringLiteral("LightEye"), LayerIcon::eyeSlash, 14));
}

QTEST_MAIN(CameraRawPanelTests)
#include "CameraRawPanelTests.moc"
