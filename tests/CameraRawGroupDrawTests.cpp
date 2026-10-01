#include "CameraRawRowTable.h"
#include "Document/BrushStroke.h"
#include "UI/CameraRawColorControls.h"
#include "UI/FilterSheet.h"
#include <QAbstractButton>
#include <QComboBox>
#include <QToolButton>
#include <QWindow>

// The groups' drawing, pages and gestures, and the shipped panel.
namespace {
template <typename Controls> struct Group {
    EditorSession session;
    std::unique_ptr<Controls> controls;
    Group()
    {
        session.createDocument(8, 8);
        QImage image = BrushRaster::context(8, 8, false);
        image.fill(QColor(200, 120, 60));
        session.insert(ImportedImage(image, image, QStringLiteral("Warm")));
        session.beginFilter(FilterKind::cameraRaw);
        controls = std::make_unique<Controls>(session);
        controls->resize(374, 900);
        controls->show();
        if (!QTest::qWaitForWindowExposed(controls.get()))
            throw std::runtime_error("the controls never showed");
    }
    template <typename Widget> Widget &child(const QString &name) const { return *controls->template findChild<Widget *>(name); }
    const CameraRawSettings &raw() const { return session.filterEdit().value().settings.cameraRaw; }
    void set(const std::function<void(CameraRawSettings &)> &change)
    {
        FilterSettings settings = session.filterEdit().value().settings;
        change(settings.cameraRaw);
        session.updateFilter(settings, true);
    }
    void arm(const std::function<void(CameraRawPanel &)> &change)
    {
        CameraRawPanel panel = session.filterEdit().value().rawPanel;
        change(panel);
        session.setCameraRawPanel(panel);
    }
};

bool near(QColor seen, QColor wanted, int tolerance = 4)
{
    return std::abs(seen.red() - wanted.red()) <= tolerance && std::abs(seen.green() - wanted.green()) <= tolerance
        && std::abs(seen.blue() - wanted.blue()) <= tolerance;
}

QColor over(QColor top, double alpha, QColor below)
{
    return QColor(int(std::lround(top.red() * alpha + below.red() * (1 - alpha))), int(std::lround(top.green() * alpha + below.green() * (1 - alpha))),
                  int(std::lround(top.blue() * alpha + below.blue() * (1 - alpha))));
}

// A real double click's four events, through the window.
void doubleClick(QWidget &widget, QPoint at)
{
    QTest::mouseDClick(widget.window()->windowHandle(), Qt::LeftButton, {}, widget.mapTo(widget.window(), at));
}
}

class CameraRawGroupDrawTests : public QObject {
    Q_OBJECT
private slots:
    void theCurvePagesShowTheirOwnControls();
    void theCurveGraphDrawsItsGround();
    void endPointsStayAndARealDoubleClickRemoves();
    void theCurveGroupWorksInsideTheShippedSheet();
    void swatchesAndTracksFollowTheirFamilies();
    void theWheelDrawsAndTakesItsOwnCircle();
    void aTypedFieldKeepsItsTypingThroughChanges();
};

void CameraRawGroupDrawTests::theCurvePagesShowTheirOwnControls()
{
    Group<CameraRawCurveControls> group;
    auto &channel = group.child<QToolButton>(QStringLiteral("pointChannel0"));
    auto &amounts = group.child<CameraRawSlider>(QStringLiteral("curveLightsSlider"));
    auto &preset = group.child<QComboBox>(QStringLiteral("curvePreset"));
    QVERIFY(!channel.isVisible() && amounts.isVisible() && !preset.isVisible());
    QCOMPARE(group.child<QToolButton>(QStringLiteral("curvePage0")).toolTip(), QString("Parametric lifts tonal regions. Point places anchors on the curve."));
    group.arm([](CameraRawPanel &panel) { panel.curvePage = CameraRawCurvePage::point; });
    QVERIFY(channel.isVisible() && !amounts.isVisible() && preset.isVisible());
    QStringList presets;
    for (int index = 0; index < preset.count(); ++index)
        presets << preset.itemText(index);
    QCOMPARE(presets, (QStringList{"Custom", "Linear", "Medium Contrast", "Strong Contrast"}));
    QCOMPARE(preset.toolTip(), QString("Replaces this curve with a straight line or a contrast curve."));
    QCOMPARE(channel.toolTip(), QString("RGB changes brightness. Red, green, and blue also shift the color."));
    QCOMPARE(group.child<QWidget>(QStringLiteral("cameraRawCurveGraph")).toolTip(),
             QString("Drag a point. Click to add one. Double-click a point to remove it."));
    QCOMPARE(group.child<QWidget>(QStringLiteral("curveSelectedPoint")).toolTip(), QString("Input and output of the selected curve point."));
    QCOMPARE(group.child<QAbstractButton>(QStringLiteral("curveTargeted")).toolTip(),
             QString("Drag on the picture to move the curve for the tone under the pointer."));
}

void CameraRawGroupDrawTests::theCurveGraphDrawsItsGround()
{
    Group<CameraRawCurveControls> group;
    auto &graph = group.child<QWidget>(QStringLiteral("cameraRawCurveGraph"));
    QCOMPARE(graph.height(), 150);
    const QColor window = graph.palette().color(QPalette::Window);
    const QColor ground = over(Qt::black, 0.35, window);
    QImage drawn = graph.grab().toImage();
    const int w = drawn.width(), h = drawn.height();
    // Away from every line: black at 35% over the window.
    QVERIFY2(near(drawn.pixelColor(w * 3 / 4, h * 3 / 4), ground), qPrintable(drawn.pixelColor(w * 3 / 4, h * 3 / 4).name()));
    // Each divider rises 8 points, 3 wide, from the bottom.
    for (const double split : {25.0, 50.0, 75.0}) {
        QVERIFY(near(drawn.pixelColor(int(split / 100 * w), h - 3), Qt::white, 30));
        QVERIFY(near(drawn.pixelColor(int(split / 100 * w), h - 12), ground));
    }
    group.arm([](CameraRawPanel &panel) { panel.curvePage = CameraRawCurvePage::point; });
    group.set([](CameraRawSettings &settings) { settings.curve.rgb = {{0, 0}, {0.5, 0.25}, {1, 1}}; });
    drawn = graph.grab().toImage();
    // A dot 8 across; the diagonal at 25% white.
    QVERIFY(near(drawn.pixelColor(w / 2, int(h * 0.75)), Qt::white, 10));
    QVERIFY(near(drawn.pixelColor(w / 2 + 3, int(h * 0.75)), Qt::white, 60));
    QVERIFY(near(drawn.pixelColor(w / 4, h - int(h * 0.25)), over(Qt::white, 0.25, ground), 40));
}

void CameraRawGroupDrawTests::endPointsStayAndARealDoubleClickRemoves()
{
    Group<CameraRawCurveControls> group;
    group.arm([](CameraRawPanel &panel) { panel.curvePage = CameraRawCurvePage::point; });
    auto &graph = group.child<QWidget>(QStringLiteral("cameraRawCurveGraph"));
    const int w = graph.width(), h = graph.height();
    // An end point keeps its x; its output follows.
    QTest::mousePress(&graph, Qt::LeftButton, {}, QPoint(2, h - 2));
    QMouseEvent move(QEvent::MouseMove, QPointF(40, 40), graph.mapToGlobal(QPointF(40, 40)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&graph, &move);
    QTest::mouseRelease(&graph, Qt::LeftButton, {}, QPoint(40, 40));
    QCOMPARE(group.raw().curve.rgb.size(), size_t(2));
    QVERIFY(group.raw().curve.rgb[0].x == 0 && std::abs(group.raw().curve.rgb[0].y - (1 - 40.0 / h)) < 1e-9 && group.raw().curve.rgb[1] == (CurvePoint{1, 1}));
    // Past the start: a hundredth in, then repaired away.
    group.set([](CameraRawSettings &settings) { settings.curve.rgb = {{0, 0}, {0.5, 0.5}, {1, 1}}; });
    QTest::mousePress(&graph, Qt::LeftButton, {}, QPoint(w / 2, h / 2));
    QMouseEvent far(QEvent::MouseMove, QPointF(-50, h / 2), graph.mapToGlobal(QPointF(-50, h / 2)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&graph, &far);
    QTest::mouseRelease(&graph, Qt::LeftButton, {}, QPoint(-50, h / 2));
    QVERIFY(group.raw().curve.rgb == CameraRawCurveSettings::linear());
    // A real double click: press, release, double click, release.
    group.set([](CameraRawSettings &settings) { settings.curve.rgb = {{0, 0}, {0.5, 0.5}, {1, 1}}; });
    doubleClick(graph, QPoint(w / 2, h / 2));
    QCOMPARE(group.raw().curve.rgb.size(), size_t(2));
}

void CameraRawGroupDrawTests::theCurveGroupWorksInsideTheShippedSheet()
{
    EditorSession session;
    session.createDocument(8, 8);
    QImage image = BrushRaster::context(8, 8, false);
    image.fill(QColor(200, 120, 60));
    session.insert(ImportedImage(image, image, QStringLiteral("Warm")));
    session.beginFilter(FilterKind::cameraRaw);
    FilterSheet sheet(session);
    sheet.resize(440, 1400);
    sheet.show();
    QVERIFY(QTest::qWaitForWindowExposed(&sheet));
    sheet.findChild<QToolButton *>(QStringLiteral("CurveSection"))->click();
    CameraRawPanel panel = session.filterEdit().value().rawPanel;
    panel.curvePage = CameraRawCurvePage::point;
    session.setCameraRawPanel(panel);
    auto *graph = sheet.findChild<QWidget *>(QStringLiteral("cameraRawCurveGraph"));
    QVERIFY(graph && graph->isVisible());
    QVERIFY(!graph->grab().isNull());
    const QPoint at(graph->width() / 2, graph->height() / 4);
    QTest::mouseClick(graph->window()->windowHandle(), Qt::LeftButton, {}, graph->mapTo(graph->window(), at));
    QCOMPARE(session.filterEdit().value().settings.cameraRaw.curve.rgb.size(), size_t(3));
}

void CameraRawGroupDrawTests::swatchesAndTracksFollowTheirFamilies()
{
    Group<CameraRawMixerControls> group;
    group.arm([](CameraRawPanel &panel) {
        panel.mixerPage = CameraRawMixerPage::color;
        panel.mixerSwatch = 5;
    });
    // Each swatch: its centre at 0.8 and 0.9, chosen ringed.
    for (int index = 0; index < 8; ++index) {
        auto &swatch = group.child<QAbstractButton>(QStringLiteral("mixerSwatch%1").arg(index));
        const QImage drawn = swatch.grab().toImage();
        const QColor wanted = QColor::fromHsvF(float(CameraRawMixerSettings::centers.at(size_t(index)) / 360), 0.8f, 0.9f);
        QVERIFY2(near(drawn.pixelColor(drawn.width() / 2, drawn.height() / 2), wanted), qPrintable(QString::number(index)));
        QCOMPARE(near(drawn.pixelColor(drawn.width() / 2, 2), Qt::white, 40), index == 5);
        QCOMPARE(swatch.toolTip(), QStringLiteral("Edit %1.").arg(CameraRawMixerSettings::names.at(size_t(index))));
    }
    // The Color page's hue row spans Blues, 50 each side.
    const auto end = [&group](const char *name) {
        const QImage drawn = group.child<CameraRawSlider>(QString::fromLatin1(name)).grab().toImage();
        return drawn.pixelColor(drawn.width() - 6, drawn.height() / 2);
    };
    QVERIFY2(std::abs(end("colorHueSlider").hsvHue() - 290) < 8, qPrintable(end("colorHueSlider").name()));
    // A point picked anew with another hue rebuilds its tracks.
    group.arm([](CameraRawPanel &panel) {
        panel.mixerPage = CameraRawMixerPage::point;
        panel.pointIndex = 0;
    });
    group.set([](CameraRawSettings &settings) { settings.mixer.points = {CameraRawPointColor{.hue = 30, .saturation = 1, .luminance = 0.5}}; });
    QVERIFY2(std::abs(end("hueShiftSlider").hsvHue() - 80) < 8, qPrintable(end("hueShiftSlider").name()));
    group.set([](CameraRawSettings &settings) { settings.mixer.points = {CameraRawPointColor{.hue = 200, .saturation = 1, .luminance = 0.5}}; });
    QVERIFY2(std::abs(end("hueShiftSlider").hsvHue() - 250) < 8, qPrintable(end("hueShiftSlider").name()));
    const QImage picked = group.child<QAbstractButton>(QStringLiteral("pickedColor0")).grab().toImage();
    QVERIFY(near(picked.pixelColor(picked.width() / 2, picked.height() / 2), QColor::fromHsvF(200.0f / 360, 1, 0.5f)));
    QVERIFY(near(picked.pixelColor(picked.width() / 2, 2), Qt::white, 40));
}

void CameraRawGroupDrawTests::theWheelDrawsAndTakesItsOwnCircle()
{
    Group<CameraRawGradingControls> group;
    auto *shadows = group.controls->findChild<QWidget *>(QStringLiteral("gradeWheelShadows"));
    auto *midtones = group.controls->findChild<QWidget *>(QStringLiteral("gradeWheelMidtones"));
    // Swift's HStack: 30 points between the three columns.
    QCOMPARE(midtones->x() - (shadows->x() + shadows->width()), 30);
    QWidget *wheel = nullptr;
    for (QWidget *child : shadows->findChildren<QWidget *>())
        if (child->size() == QSize(86, 86))
            wheel = child;
    group.set([](CameraRawSettings &settings) { settings.grading.shadows = {90, 50, 0}; });
    const QImage drawn = wheel->grab().toImage();
    const QColor window = wheel->palette().color(QPalette::Window);
    // Red at 85% on the right; the dot sits above.
    QVERIFY2(near(drawn.pixelColor(80, 43), over(Qt::red, 0.85, window), 12), qPrintable(drawn.pixelColor(80, 43).name()));
    QVERIFY(near(drawn.pixelColor(43, 43 - 18), Qt::white, 10));
    QVERIFY(!near(drawn.pixelColor(43, 43 + 18), Qt::white, 60));
    // A double click in a corner resets nothing.
    QTest::mouseDClick(wheel, Qt::LeftButton, {}, QPoint(2, 2));
    QCOMPARE(group.raw().grading.shadows.saturation, 50.0);
    QTest::mouseDClick(wheel, Qt::LeftButton, {}, QPoint(43, 43));
    QCOMPARE(group.raw().grading.shadows.saturation, 0.0);
    QCOMPARE(wheel->toolTip(), QString("Drag to set hue and saturation. Double-click to reset this wheel."));
}

void CameraRawGroupDrawTests::aTypedFieldKeepsItsTypingThroughChanges()
{
    Group<CameraRawCurveControls> group;
    auto *field = group.controls->findChild<PickerField *>(QStringLiteral("curveLightsField"));
    field->setFocus();
    QTRY_VERIFY(field->hasFocus());
    field->selectAll();
    QTest::keyClicks(field, QStringLiteral("42"));
    group.set([](CameraRawSettings &settings) { settings.curve.darks = 10; });
    QCOMPARE(field->text(), QString("42"));
    QTest::keyClick(field, Qt::Key_Return);
    QCOMPARE(group.raw().curve.lights, 42.0);
}

QTEST_MAIN(CameraRawGroupDrawTests)
#include "CameraRawGroupDrawTests.moc"
