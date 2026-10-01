#include "CameraRawRowTable.h"
#include "Document/BrushStroke.h"
#include "UI/CameraRawColorControls.h"
#include "UI/CameraRawDetailOpticsControls.h"
#include "UI/CameraRawGeometryCalibrationControls.h"
#include "Rendering/EyedropperIcon.h"
#include <QCheckBox>
#include <QAbstractButton>
#include <QComboBox>
#include <QPushButton>
#include <QToolButton>

// The groups' words, captions, pages and the states they show.
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
    }
    void show()
    {
        controls = std::make_unique<Controls>(session);
        controls->resize(374, 900);
        controls->show();
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

// Every text a view holds: labels, buttons, menus and tooltips.
QSet<QString> words(QWidget &root)
{
    QSet<QString> found;
    for (QWidget *widget : root.findChildren<QWidget *>()) {
        found << widget->toolTip();
        if (auto *label = qobject_cast<QLabel *>(widget))
            found << label->text();
        if (auto *button = qobject_cast<QAbstractButton *>(widget))
            found << button->text();
        if (auto *menu = qobject_cast<QComboBox *>(widget))
            for (int index = 0; index < menu->count(); ++index)
                found << menu->itemText(index);
    }
    return found;
}

void holds(QWidget &root, const QStringList &expected)
{
    const QSet<QString> found = words(root);
    for (const QString &text : expected)
        QVERIFY2(found.contains(text), qPrintable(text));
}
}

class CameraRawGroupWordsTests : public QObject {
    Q_OBJECT
private slots:
    void everyViewCarriesSwiftsWords();
    void captionsBuddiesAndTitlesThatDoNotReset();
    void pickersAndButtonsShowThePanel();
    void theMixerShowsOnePageAtATime();
    void theWheelReadsItsLowerHalfAndDrawsItsRim();
    void theCurveShowsItsLineItsSelectedPointAndRepaints();
    void widgetsFollowTheModelAndGuardsHold();
};

void CameraRawGroupWordsTests::everyViewCarriesSwiftsWords()
{
    Group<CameraRawCurveControls> curve;
    curve.show();
    holds(*curve.controls, {"Parametric", "Point", "RGB", "Red", "Green", "Blue", "Parametric lifts tonal regions. Point places anchors on the curve.",
                            "RGB changes brightness. Red, green, and blue also shift the color.",
                            "Drag up or down to lift or lower those tones. Drag a divider along the bottom to change which tones each region covers.", "Targeted Adjustment", "Preset"});
    Group<CameraRawMixerControls> mixer;
    mixer.set([](CameraRawSettings &settings) { settings.mixer.points = {CameraRawPointColor{.hue = 30, .saturation = 1, .luminance = 0.5}}; });
    mixer.show();
    holds(*mixer.controls, {"HSL", "Color", "Point Color", "Hue", "Saturation", "Luminance",
                            "HSL lists every color. Color edits one family. Point Color adjusts a color you pick.",
                            "Hue shifts the color, Saturation its strength, and Luminance its brightness.",
                            "Click the picture to save a color. Up to eight colors.", "Visualize Range",
                            "Dims the picture outside this color's range. It is not kept when you press OK.", "Targeted Adjustment",
                            "Drag a color in the picture. Nearby color families move together.", "Select this picked color."});
    Group<CameraRawGradingControls> grading;
    grading.show();
    holds(*grading.controls, {"Three-Way", "Shadows", "Midtones", "Highlights", "Global",
                              "Three-Way shows shadows, midtones, and highlights. The other choices show one wheel.",
                              "Drag inside the wheel. Angle sets hue, distance sets saturation.", "Hue and saturation of this wheel.",
                              "Brightness added by this wheel.", "0°  0"});
    Group<CameraRawDetailControls> detail;
    detail.show();
    holds(*detail.controls, {"Sharpening", "Noise Reduction"});
    Group<CameraRawOpticsControls> optics;
    optics.show();
    holds(*optics.controls, {"Remove Chromatic Aberration", "Pulls red and blue fringes apart toward the center to reduce color edging.",
                             "Enable Lens Profile Corrections", "Applies generic profile strength when camera metadata is not available.",
                             "No lens metadata on this layer. Profile sliders set generic correction strength.", "Manual", "Defringe",
                             "Click a purple or green fringe to set its hue range.",
                             "Click the fringe on the layer. Click the eyedropper again to stop.", "Purple Hue", "Green Hue",
                             "Hue range where purple defringe runs.", "Hue range where green defringe runs.", "Low", "High",
                             "Start of the hue range, in degrees.", "End of the hue range, in degrees."});
    Group<CameraRawGeometryControls> geometry;
    geometry.show();
    holds(*geometry.controls, {"Upright", "Off", "Guided", "Off leaves the picture as it is. Guided straightens from lines you draw on the picture.",
                               "Draw Guides", "Draw two or more lines on the preview that should be level or vertical.",
                               "Drag on the layer to place a guide. Draw at least two lines.", "Clear Guides", "Remove every guide line.",
                               "Projection", "Perspective", "Rectilinear", "Perspective allows stronger keystone. Rectilinear keeps the warp gentler.",
                               "Constrain Crop", "Crops empty edges after the transform and fits the result back into the frame."});
    Group<CameraRawCalibrationControls> calibration;
    calibration.show();
    holds(*calibration.controls, {"Process", "Version 1", "Version 6",
                                  "Chooses how strongly the calibration sliders below are applied. Version 6 is the current default.", "Shadows",
                                  "Red Primary", "Green Primary", "Blue Primary", summary(CameraRawProcessVersion::version6)});
}

void CameraRawGroupWordsTests::captionsBuddiesAndTitlesThatDoNotReset()
{
    Group<CameraRawOpticsControls> optics;
    optics.show();
    // Low left of its slider, High left of the other.
    auto &low = optics.child<CameraRawSlider>(QStringLiteral("purpleHueLow"));
    auto &high = optics.child<CameraRawSlider>(QStringLiteral("purpleHueHigh"));
    QLabel *lowCaption = nullptr, *highCaption = nullptr;
    for (QLabel *label : low.parentWidget()->findChildren<QLabel *>()) {
        if (label->text() == QLatin1String("Low") && std::abs(label->y() - low.y()) < 20 && !lowCaption)
            lowCaption = label;
        if (label->text() == QLatin1String("High") && std::abs(label->y() - low.y()) < 20 && !highCaption)
            highCaption = label;
    }
    QVERIFY(lowCaption && highCaption);
    QVERIFY(lowCaption->x() < low.x() && low.x() < highCaption->x() && highCaption->x() < high.x());
    Group<CameraRawGeometryControls> geometry;
    geometry.show();
    auto &projection = geometry.child<QComboBox>(QStringLiteral("projection"));
    bool named = false;
    for (QLabel *label : geometry.controls->findChildren<QLabel *>())
        named = named || (label->text() == QLatin1String("Projection") && label->buddy() == &projection);
    QVERIFY(named);
    // A sub-view's title never resets; only the main groups' do.
    Group<CameraRawCurveControls> curve;
    curve.show();
    auto &lights = curve.child<CameraRawSlider>(QStringLiteral("curveLightsSlider"));
    lights.setValue(800);
    QTest::mouseDClick(lights.parentWidget()->findChild<QLabel *>(), Qt::LeftButton);
    QCOMPARE(curve.raw().curve.lights, 60.0);
}

void CameraRawGroupWordsTests::pickersAndButtonsShowThePanel()
{
    Group<CameraRawCurveControls> curve;
    curve.show();
    curve.arm([](CameraRawPanel &panel) {
        panel.curvePage = CameraRawCurvePage::point;
        panel.pointChannel = CameraRawPointChannel::blue;
    });
    QVERIFY(curve.child<QToolButton>(QStringLiteral("curvePage1")).isChecked() && curve.child<QToolButton>(QStringLiteral("pointChannel3")).isChecked());
    Group<CameraRawMixerControls> mixer;
    mixer.show();
    mixer.arm([](CameraRawPanel &panel) {
        panel.mixerPage = CameraRawMixerPage::point;
        panel.mixerTab = CameraRawMixerTab::luminance;
    });
    QVERIFY(mixer.child<QToolButton>(QStringLiteral("mixerPage2")).isChecked() && mixer.child<QToolButton>(QStringLiteral("mixerTab2")).isChecked());
    Group<CameraRawGeometryControls> geometry;
    geometry.show();
    geometry.set([](CameraRawSettings &settings) { settings.geometry.upright = CameraRawUprightMode::guided; });
    QVERIFY(geometry.child<QToolButton>(QStringLiteral("upright1")).isChecked());
    auto &draw = geometry.child<QPushButton>(QStringLiteral("drawGuides"));
    QVERIFY(!draw.isChecked());
    geometry.arm([](CameraRawPanel &panel) { panel.drawingGeometryGuide = true; });
    QVERIFY(draw.isChecked());
}

void CameraRawGroupWordsTests::theMixerShowsOnePageAtATime()
{
    Group<CameraRawMixerControls> mixer;
    mixer.show();
    const auto shown = [&mixer] {
        return std::tuple(mixer.child<CameraRawSlider>(QStringLiteral("familyRedsSlider")).isVisible(),
                          mixer.child<QAbstractButton>(QStringLiteral("mixerSwatch0")).isVisible(),
                          mixer.child<QToolButton>(QStringLiteral("pointColorSampler")).isVisible());
    };
    QVERIFY(shown() == std::tuple(true, false, false));
    mixer.arm([](CameraRawPanel &panel) { panel.mixerPage = CameraRawMixerPage::color; });
    QVERIFY(shown() == std::tuple(false, true, false));
    mixer.arm([](CameraRawPanel &panel) { panel.mixerPage = CameraRawMixerPage::point; });
    QVERIFY(shown() == std::tuple(false, false, true));
}

void CameraRawGroupWordsTests::theWheelReadsItsLowerHalfAndDrawsItsRim()
{
    Group<CameraRawGradingControls> grading;
    grading.show();
    QWidget *wheel = nullptr;
    for (QWidget *child : grading.controls->findChild<QWidget *>(QStringLiteral("gradeWheelShadows"))->findChildren<QWidget *>())
        if (child->size() == QSize(86, 86))
            wheel = child;
    // Straight down from the middle is hue 270.
    QTest::mousePress(wheel, Qt::LeftButton, {}, QPoint(43, 80));
    QTest::mouseRelease(wheel, Qt::LeftButton, {}, QPoint(43, 80));
    QVERIFY2(std::abs(grading.raw().grading.shadows.hue - 270) < 0.5, qPrintable(QString::number(grading.raw().grading.shadows.hue)));
    // The rim: white at 80% round the fill's edge.
    const QImage drawn = wheel->grab().toImage();
    const QColor rim = drawn.pixelColor(43, 0), inside = drawn.pixelColor(43, 4);
    QVERIFY2(std::min({rim.red(), rim.green(), rim.blue()}) > 150 && std::min({inside.red(), inside.green(), inside.blue()}) < 60,
             qPrintable(rim.name() + QStringLiteral(" ") + inside.name()));
}

void CameraRawGroupWordsTests::theCurveShowsItsLineItsSelectedPointAndRepaints()
{
    Group<CameraRawCurveControls> curve;
    curve.show();
    auto &graph = curve.child<QWidget>(QStringLiteral("cameraRawCurveGraph"));
    // The drawn curve is the model's parametric curve.
    curve.set([](CameraRawSettings &settings) { settings.curve.lights = 100; });
    QImage drawn = graph.grab().toImage();
    const int w = drawn.width(), h = drawn.height();
    // The column's whitest pixel sits on the lifted curve.
    int brightest = 0;
    for (int y = 0; y < h; ++y)
        if (qGray(drawn.pixel(int(0.625 * w), y)) > qGray(drawn.pixel(int(0.625 * w), brightest)))
            brightest = y;
    QVERIFY2(std::abs(brightest - (1 - curve.raw().curve.parametric(0.625)) * h) < 3, qPrintable(QString::number(brightest)));
    // A write repaints the graph without being asked.
    struct Paints : QObject {
        int count = 0;
        bool eventFilter(QObject *, QEvent *event) override
        {
            count += event->type() == QEvent::Paint;
            return false;
        }
    } paints;
    graph.installEventFilter(&paints);
    // Pending paints settle first; only the write's paint counts.
    QTest::qWait(100);
    paints.count = 0;
    curve.set([](CameraRawSettings &settings) { settings.curve.darkSplit = 60; });
    QTRY_VERIFY(paints.count > 0);
    // No point is chosen until one is pressed.
    curve.arm([](CameraRawPanel &panel) { panel.curvePage = CameraRawCurvePage::point; });
    curve.set([](CameraRawSettings &settings) { settings.curve.rgb = {{0, 0}, {0.2, 0.3}, {0.6, 0.7}, {1, 1}}; });
    auto &readout = curve.child<QLabel>(QStringLiteral("curveSelectedPoint"));
    QVERIFY(!readout.isVisible());
    QTest::mouseClick(&graph, Qt::LeftButton, {}, QPoint(int(0.6 * graph.width()), int(0.3 * graph.height())));
    QVERIFY(readout.isVisible());
    QCOMPARE(readout.text(), QString("In 153   Out 179"));
}

void CameraRawGroupWordsTests::widgetsFollowTheModelAndGuardsHold()
{
    // Settings from before the view shows appear in every control.
    Group<CameraRawOpticsControls> optics;
    optics.set([](CameraRawSettings &settings) {
        settings.optics.removeChromaticAberration = true;
        settings.optics.enableLensProfile = true;
        settings.optics.greenHueLow = 100;
    });
    optics.show();
    QVERIFY(optics.child<QCheckBox>(QStringLiteral("removeChromaticAberration")).isChecked());
    QVERIFY(optics.child<QCheckBox>(QStringLiteral("enableLensProfile")).isChecked());
    QVERIFY(std::abs(optics.child<CameraRawSlider>(QStringLiteral("greenHueLow")).shown() - 100) <= 0.36);
    optics.arm([](CameraRawPanel &panel) { panel.samplesDefringe = true; });
    const qreal ratio = optics.controls->devicePixelRatioF();
    const auto accent = [ratio](const QWidget &button) { return EyedropperIcon::icon(button.palette().color(QPalette::Highlight), ratio).pixmap(QSize(14, 14), ratio).toImage(); };
    auto &defringe = optics.child<QToolButton>(QStringLiteral("defringeSampler"));
    QVERIFY(defringe.icon().pixmap(QSize(14, 14), ratio).toImage() == accent(defringe));
    Group<CameraRawGeometryControls> geometry;
    geometry.set([](CameraRawSettings &settings) {
        settings.geometry.projection = CameraRawProjection::rectilinear;
        settings.geometry.constrainCrop = true;
    });
    geometry.show();
    QCOMPARE(geometry.child<QComboBox>(QStringLiteral("projection")).currentIndex(), 1);
    QVERIFY(geometry.child<QCheckBox>(QStringLiteral("constrainCrop")).isChecked());
    Group<CameraRawCalibrationControls> calibration;
    calibration.show();
    QCOMPARE(calibration.child<QLabel>(QStringLiteral("processSummary")).toolTip(), summary(CameraRawProcessVersion::version6));
    Group<CameraRawMixerControls> mixer;
    mixer.show();
    mixer.arm([](CameraRawPanel &panel) {
        panel.mixerPage = CameraRawMixerPage::point;
        panel.samplesPointColor = true;
    });
    auto &sampler = mixer.child<QToolButton>(QStringLiteral("pointColorSampler"));
    QVERIFY(sampler.icon().pixmap(QSize(14, 14), ratio).toImage() == accent(sampler));
    // Grading's page, hidden label, luminance and a repaint.
    Group<CameraRawGradingControls> grading;
    grading.show();
    auto &page = grading.child<QComboBox>(QStringLiteral("gradePage"));
    grading.arm([](CameraRawPanel &panel) { panel.gradePage = CameraRawGradePage::highlights; });
    QCOMPARE(page.currentIndex(), 3);
    bool named = false;
    for (QLabel *label : grading.controls->findChildren<QLabel *>())
        named = named || (label->text() == QLatin1String("Grading") && label->buddy() == &page);
    QVERIFY(named);
    grading.arm([](CameraRawPanel &panel) { panel.gradePage = CameraRawGradePage::threeWay; });
    QWidget *box = grading.controls->findChild<QWidget *>(QStringLiteral("gradeWheelShadows"));
    grading.set([](CameraRawSettings &settings) { settings.grading.shadows.luminance = 50; });
    QVERIFY(std::abs(box->findChild<CameraRawSlider *>(QStringLiteral("gradeLuminance"))->shown() - 50) <= 0.2);
    QWidget *wheel = nullptr;
    for (QWidget *child : box->findChildren<QWidget *>())
        if (child->size() == QSize(86, 86))
            wheel = child;
    struct Paints : QObject {
        int count = 0;
        bool eventFilter(QObject *, QEvent *event) override
        {
            count += event->type() == QEvent::Paint;
            return false;
        }
    } paints;
    wheel->installEventFilter(&paints);
    QTest::qWait(100);
    paints.count = 0;
    grading.set([](CameraRawSettings &settings) { settings.grading.shadows.hue = 45; });
    QTRY_VERIFY(paints.count > 0);
    // A move that began off the circle drags nothing.
    QTest::mousePress(wheel, Qt::LeftButton, {}, QPoint(1, 1));
    QMouseEvent move(QEvent::MouseMove, QPointF(43, 6), wheel->mapToGlobal(QPointF(43, 6)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(wheel, &move);
    QTest::mouseRelease(wheel, Qt::LeftButton, {}, QPoint(43, 6));
    QCOMPARE(grading.raw().grading.shadows.hue, 45.0);
    // Clicks on the parametric graph leave the point curve alone.
    Group<CameraRawCurveControls> curve;
    curve.show();
    auto &graph = curve.child<QWidget>(QStringLiteral("cameraRawCurveGraph"));
    QTest::mouseClick(&graph, Qt::LeftButton, {}, QPoint(graph.width() / 2, 20));
    QTest::mouseDClick(graph.window()->windowHandle(), Qt::LeftButton, {}, graph.mapTo(graph.window(), QPoint(graph.width() / 2, 20)));
    QVERIFY(curve.raw().curve.rgb == CameraRawCurveSettings::linear());
}

QTEST_MAIN(CameraRawGroupWordsTests)
#include "CameraRawGroupWordsTests.moc"
