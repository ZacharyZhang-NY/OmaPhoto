#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include "UI/FilterSheet.h"
#include "UI/SliderSnap.h"
#include <QApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QLayout>
#include <QLineEdit>
#include <QtTest>

// Swift 1.3.2's coloured filter sliders: tracks, resets, aligned titles.
namespace {
using Kind = CameraRawSliderTrack::Kind;

QImage solid(QColor colour)
{
    QImage image(4, 4, QImage::Format_RGBA8888_Premultiplied);
    image.fill(colour);
    return image;
}

struct Sheet {
    EditorSession session;
    std::unique_ptr<FilterSheet> sheet;
    explicit Sheet(FilterKind kind)
    {
        session.createDocument(4, 4);
        const QImage image = solid(QColor::fromRgbF(0.8f, 0.3f, 0.2f));
        session.insert(ImportedImage(image, image, QStringLiteral("Color")));
        session.beginFilter(kind);
        sheet = std::make_unique<FilterSheet>(session);
        sheet->show();
    }
    const FilterSettings &settings() const { return session.filterEdit().value().settings; }
    void set(const std::function<void(FilterSettings &)> &change, bool preview = true)
    {
        FilterSettings changed = settings();
        change(changed);
        session.updateFilter(changed, preview);
    }
    // The slider a title names, and that title.
    QSlider &slider(const QString &title) const
    {
        for (QLabel *label : sheet->findChildren<QLabel *>()) {
            if (label->text() == title && label->buddy())
                return *static_cast<QSlider *>(label->buddy());
        }
        throw std::runtime_error("no slider titled " + title.toStdString());
    }
    QLabel &title(const QString &text) const
    {
        for (QLabel *label : sheet->findChildren<QLabel *>()) {
            if (label->text() == text && label->buddy())
                return *label;
        }
        throw std::runtime_error("no title " + text.toStdString());
    }
    CameraRawSlider &coloured(const QString &title) const
    {
        auto *found = qobject_cast<CameraRawSlider *>(&slider(title));
        if (!found)
            throw std::runtime_error("no coloured slider titled " + title.toStdString());
        return *found;
    }
};

// Swift's Color Balance ends, written out.
const std::array<std::vector<QColor>, 3> swiftEnds{
    std::vector{QColor::fromRgbF(0.10f, 0.72f, 0.80f), QColor::fromRgbF(0.86f, 0.18f, 0.20f)},
    std::vector{QColor::fromRgbF(0.80f, 0.22f, 0.70f), QColor::fromRgbF(0.24f, 0.70f, 0.30f)},
    std::vector{QColor::fromRgbF(0.95f, 0.82f, 0.18f), QColor::fromRgbF(0.22f, 0.40f, 0.92f)}};

void send(QWidget &widget, QEvent::Type type, QPointF at, Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, at, widget.mapToGlobal(at), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
}

// Counts the paints a widget receives.
class PaintCount : public QObject {
public:
    explicit PaintCount(QWidget &widget) { widget.installEventFilter(this); }
    int paints = 0;

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        paints += event->type() == QEvent::Paint;
        return false;
    }
};

// A point on the knob, found along the slider's middle.
QPoint knob(const CameraRawSlider &slider)
{
    for (int x = 0; x < slider.width(); ++x) {
        const QPoint point(x, slider.height() / 2);
        if (slider.isOnKnob(point))
            return point;
    }
    throw std::runtime_error("no knob");
}

QList<QColor> pixels(const QImage &image)
{
    QList<QColor> colours;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            colours << image.pixelColor(x, y);
    return colours;
}

// Shown rows' titles: the widest's width, sixty at least.
void checkAligned(const Sheet &shown, const QStringList &visible, const QStringList &hidden)
{
    int widest = 60;
    for (const QString &text : visible)
        widest = std::max(widest, shown.title(text).sizeHint().width());
    for (const QString &text : visible + hidden) {
        QCOMPARE(shown.title(text).minimumWidth(), widest);
        QCOMPARE(shown.title(text).maximumWidth(), widest);
    }
}
}

class ColoredSliderTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { SliderSnap::install(); }
    void colorBalanceTracksRunFromEachColorToItsOpposite();
    void theSpectrumCentresOnItsHue();
    void resettingAColoredFilterSliderRestoresTheDefault();
    void blackAndWhiteAndColorBalanceSlidersAreColoured();
    void aDoubleClickResetsItsOwnSliderOnly();
    void titlesLineUpToTheWidestShown();
    void rowsLayOutAsSwifts();
    void aResetReplacesTheFieldsTyping();
    void theTrackPaintsEveryStopInPlace();
    void aTitleResetsOnlyForALeftDoubleClickWhileEnabled();
    void aResetThenScrubsFromTheDefault();
    void colorBalanceResetsEachOfItsNineRows();
    void aNewTrackRepaintsTheSlider();
};

void ColoredSliderTests::colorBalanceTracksRunFromEachColorToItsOpposite()
{
    const std::vector<QColor> cyanRed = FilterSheet::cyanRedTrack().colors().value();
    QVERIFY(cyanRed[0].blueF() > cyanRed[0].redF() && cyanRed[1].redF() > cyanRed[1].blueF());
    const std::vector<QColor> magentaGreen = FilterSheet::magentaGreenTrack().colors().value();
    QVERIFY(magentaGreen[0].redF() > magentaGreen[0].greenF() && magentaGreen[1].greenF() > magentaGreen[1].redF());
    const std::vector<QColor> yellowBlue = FilterSheet::yellowBlueTrack().colors().value();
    QVERIFY(yellowBlue[0].greenF() > yellowBlue[0].blueF() && yellowBlue[1].blueF() > yellowBlue[1].greenF());
    const QColor greens = CameraRawSliderTrack{Kind::luminance, 120}.colors().value().back();
    QVERIFY(greens.greenF() > greens.redF() && greens.greenF() > greens.blueF());
    // Swift's exact ends.
    QVERIFY(cyanRed == swiftEnds[0] && magentaGreen == swiftEnds[1] && yellowBlue == swiftEnds[2]);
}

void ColoredSliderTests::theSpectrumCentresOnItsHue()
{
    // Thirteen stops every 30 degrees, the hue in the middle.
    const std::vector<QColor> circle = CameraRawSliderTrack{Kind::spectrum, 60}.colors().value();
    QCOMPARE(int(circle.size()), 13);
    for (size_t index = 0; index < circle.size(); ++index) {
        const double hue = std::fmod(60 - 180 + 30.0 * double(index) + 720, 360);
        QVERIFY2(std::abs(circle[index].hsvHueF() * 360 - hue) < 0.5 || std::abs(circle[index].hsvHueF() * 360 - hue) > 359.5, qPrintable(QString::number(index)));
        QVERIFY(std::abs(circle[index].hsvSaturationF() - 0.85) < 0.01 && std::abs(circle[index].valueF() - 0.9) < 0.01);
    }
    // Black to white, as Lightness runs.
    const std::vector<QColor> ends = CameraRawSliderTrack{.kind = Kind::opposing, .from = Qt::black, .to = Qt::white}.colors().value();
    QVERIFY(ends == (std::vector<QColor>{Qt::black, Qt::white}));
}

void ColoredSliderTests::resettingAColoredFilterSliderRestoresTheDefault()
{
    EditorSession session;
    session.createDocument(4, 4);
    const QImage base = solid(QColor::fromRgbF(0.8f, 0.3f, 0.2f));
    session.insert(ImportedImage(base, base, QStringLiteral("Color")));
    session.beginFilter(FilterKind::blackWhite);
    FilterSettings settings = session.filterEdit().value().settings;
    settings.blackWhite.reds = 250;
    settings.blackWhite.blues = -100;
    session.updateFilter(settings, true);
    settings = FilterSheet::resetting([](FilterSettings &each) -> double & { return each.blackWhite.reds; }, settings);
    QCOMPARE(settings.blackWhite.reds, BlackWhiteSettings().reds);
    // Only the double-clicked slider resets.
    QCOMPARE(settings.blackWhite.blues, -100.0);
    session.updateFilter(FilterSheet::resetting([](FilterSettings &each) -> double & { return each.blackWhite.blues; }, settings), true);
    QVERIFY(session.filterEdit().value().settings.blackWhite == BlackWhiteSettings());
    QVERIFY(session.filterEdit().value().preview);
    bool done = false;
    session.commitFilter([&done] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(pixels(session.activeLayer().value().asset.value().image()), pixels(BlackWhiteSettings().apply(base)));
    session.undo();

    session.beginFilter(FilterKind::colorBalance);
    FilterSettings balance = session.filterEdit().value().settings;
    balance.colorBalance.midCyanRed = -80;
    session.updateFilter(balance, false);
    session.updateFilter(FilterSheet::resetting([](FilterSettings &each) -> double & { return each.colorBalance.midCyanRed; }, balance), false);
    QVERIFY(session.filterEdit().value().settings.colorBalance == ColorBalanceSettings());
    // A reset leaves Preview off.
    QVERIFY(!session.filterEdit().value().preview);
    session.cancelFilter();
    QCOMPARE(pixels(session.activeLayer().value().asset.value().image()), pixels(base));
}

void ColoredSliderTests::blackAndWhiteAndColorBalanceSlidersAreColoured()
{
    Sheet blackWhite(FilterKind::blackWhite);
    const std::pair<const char *, double> families[] = {{"Reds", 0}, {"Yellows", 60}, {"Greens", 120}, {"Cyans", 180}, {"Blues", 240}, {"Magentas", 300}};
    for (const auto &[title, degrees] : families) {
        const CameraRawSlider &slider = blackWhite.coloured(QString::fromLatin1(title));
        QVERIFY(slider.track().kind == Kind::luminance && slider.track().degrees == degrees);
        QCOMPARE(slider.toolTip(), QString::fromLatin1(title) + QStringLiteral(". Double-click to reset."));
    }
    // Tint's Hue is plain; Saturation follows the tint's hue.
    QCOMPARE(blackWhite.coloured(QStringLiteral("Hue")).track().kind, Kind::plain);
    QVERIFY(blackWhite.coloured(QStringLiteral("Saturation")).track().kind == Kind::saturation
            && blackWhite.coloured(QStringLiteral("Saturation")).track().degrees == 40);
    blackWhite.set([](FilterSettings &settings) { settings.blackWhite.tintHue = 200; });
    QCOMPARE(blackWhite.coloured(QStringLiteral("Saturation")).track().degrees, 200.0);
    // Its knob sits at 40, between -200 and 300.
    QCOMPARE(blackWhite.coloured(QStringLiteral("Reds")).shown(), 40.0);
    // Dragged, the value lands whole.
    blackWhite.coloured(QStringLiteral("Reds")).setValue(501);
    QCOMPARE(blackWhite.settings().blackWhite.reds, 51.0);

    Sheet balance(FilterKind::colorBalance);
    const QList<CameraRawSlider *> sliders = balance.sheet->findChildren<CameraRawSlider *>();
    QCOMPARE(sliders.size(), 9);
    for (int index = 0; index < 9; ++index)
        QVERIFY(sliders[index]->track().kind == Kind::opposing && sliders[index]->track().colors().value() == swiftEnds[size_t(index % 3)]);
    // Other filters keep the plain slider.
    Sheet blur(FilterKind::gaussianBlur);
    QVERIFY(!qobject_cast<CameraRawSlider *>(&blur.slider(QStringLiteral("Radius"))));
}

void ColoredSliderTests::aDoubleClickResetsItsOwnSliderOnly()
{
    Sheet shown(FilterKind::blackWhite);
    QVERIFY(QTest::qWaitForWindowExposed(shown.sheet.get()));
    shown.set([](FilterSettings &settings) {
        settings.blackWhite.reds = 250;
        settings.blackWhite.blues = -100;
        settings.blackWhite.tint = true;
        settings.blackWhite.tintHue = 90;
    }, false);
    // On the title: that row alone, Preview left off.
    QTest::mouseDClick(&shown.title(QStringLiteral("Reds")), Qt::LeftButton);
    QVERIFY(shown.settings().blackWhite.reds == 40 && shown.settings().blackWhite.blues == -100);
    QVERIFY(!shown.session.filterEdit().value().preview);
    QCOMPARE(shown.sheet->findChild<QLineEdit *>(QStringLiteral("redsField"))->text(), QString("40"));
    // On the knob.
    CameraRawSlider &blues = shown.coloured(QStringLiteral("Blues"));
    QTest::mouseDClick(&blues, Qt::LeftButton, {}, knob(blues));
    QCOMPARE(shown.settings().blackWhite.blues, 20.0);
    // The plain Hue resets too.
    QTest::mouseDClick(&shown.title(QStringLiteral("Hue")), Qt::LeftButton);
    QCOMPARE(shown.settings().blackWhite.tintHue, 40.0);
    // A plain filter's title keeps its value.
    Sheet blur(FilterKind::gaussianBlur);
    blur.set([](FilterSettings &settings) { settings.radius = 25; });
    QTest::mouseDClick(&blur.title(QStringLiteral("Radius")), Qt::LeftButton);
    QCOMPARE(blur.settings().radius, 25.0);
}

void ColoredSliderTests::titlesLineUpToTheWidestShown()
{
    const QStringList pairs{"Cyan / Red", "Magenta / Green", "Yellow / Blue"};
    Sheet balance(FilterKind::colorBalance);
    checkAligned(balance, pairs, {});
    QVERIFY(balance.title(QStringLiteral("Magenta / Green")).width() > 60);
    // Tint's rows count only while shown.
    Sheet blackWhite(FilterKind::blackWhite);
    const QStringList families{"Reds", "Yellows", "Greens", "Cyans", "Blues", "Magentas"};
    checkAligned(blackWhite, families, {"Hue", "Saturation"});
    blackWhite.set([](FilterSettings &settings) { settings.blackWhite.tint = true; });
    checkAligned(blackWhite, families + QStringList{"Hue", "Saturation"}, {});
    // Hidden again, they stop counting.
    blackWhite.set([](FilterSettings &settings) { settings.blackWhite.tint = false; });
    checkAligned(blackWhite, families, {"Hue", "Saturation"});
    // A lone short title keeps sixty.
    Sheet blur(FilterKind::gaussianBlur);
    QCOMPARE(blur.title(QStringLiteral("Radius")).width(), 60);
}

void ColoredSliderTests::rowsLayOutAsSwifts()
{
    // Title, slider 10 on, field 10 on, unit 2 past.
    for (const auto &[kind, text, field] : {std::tuple(FilterKind::gaussianBlur, "Radius", "radiusField"), std::tuple(FilterKind::blackWhite, "Reds", "redsField")}) {
        Sheet shown(kind);
        QVERIFY(QTest::qWaitForWindowExposed(shown.sheet.get()));
        QLabel &title = shown.title(QString::fromLatin1(text));
        QSlider &slider = shown.slider(QString::fromLatin1(text));
        QLineEdit &entry = *shown.sheet->findChild<QLineEdit *>(QString::fromLatin1(field));
        QWidget *box = title.parentWidget();
        QCOMPARE(title.x(), 0);
        QCOMPARE(slider.x() - title.geometry().right() - 1, 10);
        QCOMPARE(entry.x() - slider.geometry().right() - 1, 10);
        QLabel *unit = box->findChildren<QLabel *>().last();
        QCOMPARE(unit->x() - entry.geometry().right() - 1, 2);
        QCOMPARE(unit->geometry().right() + 1, box->width());
        QCOMPARE(box->height(), box->sizeHint().height());
        // The row sits in the sheet's column, among the rest.
        QVERIFY(shown.sheet->layout()->indexOf(box) >= 0);
    }
}

void ColoredSliderTests::aResetReplacesTheFieldsTyping()
{
    Sheet shown(FilterKind::blackWhite);
    QVERIFY(QTest::qWaitForWindowActive(shown.sheet.get()));
    shown.set([](FilterSettings &settings) { settings.blackWhite.reds = 250; });
    auto &field = *shown.sheet->findChild<QLineEdit *>(QStringLiteral("redsField"));
    field.setFocus();
    field.selectAll();
    QTest::keyClicks(&field, QStringLiteral("7"));
    QVERIFY(field.isModified());
    QTest::mouseDClick(&shown.title(QStringLiteral("Reds")), Qt::LeftButton);
    QVERIFY(!field.isModified());
    QCOMPARE(field.text(), QString("40"));
    // Leaving the field applies nothing more.
    shown.sheet->findChild<QLineEdit *>(QStringLiteral("bluesField"))->setFocus();
    QCOMPARE(shown.settings().blackWhite.reds, 40.0);
}

void ColoredSliderTests::theTrackPaintsEveryStopInPlace()
{
    // Round red: violet a quarter in, red in the middle.
    CameraRawSlider slider(-100, 100, CameraRawSliderTrack{Kind::spectrum, 0}, QStringLiteral("Probe"), [](double) {}, [] {});
    slider.resize(400, 22);
    slider.display(100);
    const QImage painted = slider.grab().toImage();
    const auto hueAt = [&](double share) { return painted.pixelColor(int(share * painted.width()), painted.height() / 2).hsvHueF() * 360; };
    QVERIFY2(std::abs(hueAt(0.25) - 270) < 25, qPrintable(QString::number(hueAt(0.25))));
    const double middle = hueAt(0.5);
    QVERIFY2(middle < 20 || middle > 340, qPrintable(QString::number(middle)));
    QVERIFY2(std::abs(hueAt(0.75) - 90) < 25, qPrintable(QString::number(hueAt(0.75))));
}

void ColoredSliderTests::aTitleResetsOnlyForALeftDoubleClickWhileEnabled()
{
    Sheet shown(FilterKind::blackWhite);
    QVERIFY(QTest::qWaitForWindowExposed(shown.sheet.get()));
    shown.set([](FilterSettings &settings) { settings.blackWhite.reds = 250; });
    for (const Qt::MouseButton button : {Qt::RightButton, Qt::MiddleButton}) {
        QTest::mouseDClick(&shown.title(QStringLiteral("Reds")), button);
        QCOMPARE(shown.settings().blackWhite.reds, 250.0);
    }
    // A resting sheet resets nothing.
    shown.sheet->setEnabled(false);
    send(shown.title(QStringLiteral("Reds")), QEvent::MouseButtonDblClick, QPointF(2, 2), Qt::LeftButton, Qt::LeftButton);
    QCOMPARE(shown.settings().blackWhite.reds, 250.0);
    shown.sheet->setEnabled(true);
    QTest::mouseDClick(&shown.title(QStringLiteral("Reds")), Qt::LeftButton);
    QCOMPARE(shown.settings().blackWhite.reds, 40.0);
}

void ColoredSliderTests::aResetThenScrubsFromTheDefault()
{
    // A real double click: the second press goes on scrubbing.
    Sheet shown(FilterKind::blackWhite);
    QVERIFY(QTest::qWaitForWindowExposed(shown.sheet.get()));
    shown.set([](FilterSettings &settings) { settings.blackWhite.reds = 250; });
    QLabel &title = shown.title(QStringLiteral("Reds"));
    send(title, QEvent::MouseButtonPress, QPointF(2, 2), Qt::LeftButton, Qt::LeftButton);
    send(title, QEvent::MouseButtonRelease, QPointF(2, 2), Qt::LeftButton, Qt::NoButton);
    send(title, QEvent::MouseButtonDblClick, QPointF(2, 2), Qt::LeftButton, Qt::LeftButton);
    QCOMPARE(shown.settings().blackWhite.reds, 40.0);
    send(title, QEvent::MouseMove, QPointF(14, 2), Qt::NoButton, Qt::LeftButton);
    send(title, QEvent::MouseButtonRelease, QPointF(14, 2), Qt::LeftButton, Qt::NoButton);
    QCOMPARE(shown.settings().blackWhite.reds, 52.0);
}

void ColoredSliderTests::colorBalanceResetsEachOfItsNineRows()
{
    Sheet shown(FilterKind::colorBalance);
    QVERIFY(QTest::qWaitForWindowExposed(shown.sheet.get()));
    const std::array<double ColorBalanceSettings::*, 9> fields{
        &ColorBalanceSettings::shadowCyanRed, &ColorBalanceSettings::shadowMagentaGreen, &ColorBalanceSettings::shadowYellowBlue,
        &ColorBalanceSettings::midCyanRed,    &ColorBalanceSettings::midMagentaGreen,    &ColorBalanceSettings::midYellowBlue,
        &ColorBalanceSettings::highlightCyanRed, &ColorBalanceSettings::highlightMagentaGreen, &ColorBalanceSettings::highlightYellowBlue};
    const QList<CameraRawSlider *> sliders = shown.sheet->findChildren<CameraRawSlider *>();
    QCOMPARE(sliders.size(), 9);
    for (size_t index = 0; index < fields.size(); ++index) {
        shown.set([&fields](FilterSettings &settings) {
            for (size_t each = 0; each < fields.size(); ++each)
                settings.colorBalance.*fields[each] = double(each + 1) * 10;
        }, false);
        CameraRawSlider &slider = *sliders[qsizetype(index)];
        // Titles on even rows, knobs on odd ones.
        if (index % 2 == 0) {
            QLabel *title = nullptr;
            for (QLabel *label : shown.sheet->findChildren<QLabel *>())
                title = label->buddy() == &slider ? label : title;
            QTest::mouseDClick(title, Qt::LeftButton);
            // The double click's own release ends its scrub.
            send(*title, QEvent::MouseButtonRelease, QPointF(2, 2), Qt::LeftButton, Qt::NoButton);
        } else {
            QTest::mouseDClick(&slider, Qt::LeftButton, {}, knob(slider));
        }
        for (size_t each = 0; each < fields.size(); ++each)
            QCOMPARE(shown.settings().colorBalance.*fields[each], each == index ? 0.0 : double(each + 1) * 10);
        QVERIFY(!shown.session.filterEdit().value().preview);
    }
}

void ColoredSliderTests::aNewTrackRepaintsTheSlider()
{
    // Swift's needsDisplay: the knob stays, the gradient changes.
    CameraRawSlider slider(-100, 100, CameraRawSliderTrack{Kind::saturation, 0}, QStringLiteral("Probe"), [](double) {}, [] {});
    slider.resize(200, 22);
    slider.display(20);
    slider.show();
    QVERIFY(QTest::qWaitForWindowExposed(&slider));
    QTest::qWait(50);
    PaintCount count(slider);
    slider.reshape(-100, 100, CameraRawSliderTrack{Kind::saturation, 200}, 20);
    QTRY_COMPARE(count.paints, 1);
    // Mid-drag too.
    slider.setSliderDown(true);
    slider.reshape(-100, 100, CameraRawSliderTrack{Kind::spectrum, 90}, 60);
    QTRY_COMPARE(count.paints, 2);
    slider.setSliderDown(false);
    // In a sheet: Tint's saturation follows its hue.
    Sheet shown(FilterKind::blackWhite);
    shown.set([](FilterSettings &settings) { settings.blackWhite.tint = true; });
    QVERIFY(QTest::qWaitForWindowExposed(shown.sheet.get()));
    QTest::qWait(50);
    PaintCount saturation(shown.coloured(QStringLiteral("Saturation")));
    shown.set([](FilterSettings &settings) { settings.blackWhite.tintHue = 200; });
    QTRY_VERIFY(saturation.paints >= 1);
}

QTEST_MAIN(ColoredSliderTests)
#include "ColoredSliderTests.moc"
