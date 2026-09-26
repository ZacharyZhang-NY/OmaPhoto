#include "HueSaturationSheetFixtures.h"

// Swift's SpectrumEditor and the sheet's drawn glyphs.
namespace {
// How much ink a glyph lays down, over white.
int ink(const QImage &image)
{
    int total = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x)
            total += 255 - qGray(image.pixel(x, y));
    }
    return total;
}

// The box round every inked pixel.
QRect inked(const QImage &image)
{
    QRect box;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qGray(image.pixel(x, y)) < 255)
                box |= QRect(x, y, 1, 1);
        }
    }
    return box;
}
}

class HueSaturationSpectrumTests : public QObject {
    Q_OBJECT
private slots:
    void theSamplersDrawSwiftsGlyphs();
    void theSpectrumDragsItsNearestHandle();
    void theSpectrumShowsTheHuesAndTheirShift();
    void theSpectrumRepaintsWithTheBand();
};

void HueSaturationSpectrumTests::theSamplersDrawSwiftsGlyphs()
{
    Sheet shown;
    shown.set(HueSaturationSettings(0, 0, 0, false, ColorRange::reds));
    QVERIFY(shown.show());
    QToolButton &sample = shown.child<QToolButton>("hueSample"), &add = shown.child<QToolButton>("hueAdd"),
                &remove = shown.child<QToolButton>("hueRemove"), &targeting = shown.child<QToolButton>("hueTargeting");
    const QColor black = sample.palette().color(QPalette::WindowText), white(Qt::white);
    // Chosen, a quarter of the accent fills a rounded frame.
    QCOMPARE(drawn(sample).pixelColor(1, 10), white);
    shown.session.setHueSampleMode(HueSampleMode::replace);
    QCOMPARE(drawn(sample).pixelColor(1, 10), tinted(sample.palette().color(QPalette::Highlight)));
    QCOMPARE(drawn(sample).pixelColor(0, 0), white);
    shown.session.setHueSampleMode(std::nullopt);
    // Add and Remove carry a disc; a bar cuts across.
    QCOMPARE(drawn(sample).pixelColor(16, 12), white);
    QCOMPARE(drawn(add).pixelColor(16, 12), black);
    QCOMPARE(drawn(remove).pixelColor(16, 12), black);
    const QColor bar = drawn(remove).pixelColor(16, 13);
    QVERIFY(bar != black && bar != white);
    // Add's upright bar cuts where Remove's disc is whole.
    QCOMPARE(drawn(remove).pixelColor(17, 15), black);
    const QColor cut = drawn(add).pixelColor(17, 15);
    QVERIFY(cut != black && cut != white);
    // The eyedropper's bulb, top right, in each.
    for (QToolButton *each : {&sample, &add, &remove})
        QCOMPARE(drawn(*each).pixelColor(15, 6), black);
    // The hand: an outline, hollow in its palm.
    const QImage hand = drawn(targeting);
    QCOMPARE(hand.pixelColor(12, 11), white);
    // Every stroke measured once and pinned.
    QCOMPARE(ink(drawn(sample)), 10478);
    QCOMPARE(ink(drawn(add)), 20338);
    QCOMPARE(ink(drawn(remove)), 21338);
    QCOMPARE(ink(hand), 13232);
    QCOMPARE(inked(drawn(sample)), QRect(6, 4, 12, 12));
    QCOMPARE(inked(drawn(add)), QRect(6, 4, 16, 14));
    QCOMPARE(inked(drawn(remove)), QRect(6, 4, 16, 14));
    QCOMPARE(inked(hand), QRect(6, 3, 14, 16));
}

void HueSaturationSpectrumTests::theSpectrumDragsItsNearestHandle()
{
    Sheet shown;
    shown.set(HueSaturationSettings(0, 0, 0, false, ColorRange::reds));
    QVERIFY(shown.show());
    QWidget &handles = shown.child<QWidget>("spectrumHandles");
    QCOMPARE(handles.height(), 12);
    const double width = handles.width();
    QCOMPARE(width, 412.0);
    // Degrees in sixty-fourths of a turn map back exactly.
    const auto x = [width](double degrees) { return degrees / 360 * width; };
    // 22.5 degrees is nearest the core's end, at 15.
    send(handles, x(22.5), QEvent::MouseButtonPress, Qt::LeftButton);
    QCOMPARE(shown.settings().band(), (HueBand{315, 345, 22.5, 45}));
    // Held, it keeps its handle though 45 is nearer.
    send(handles, x(39.375), QEvent::MouseMove, Qt::LeftButton);
    QCOMPARE(shown.settings().band(), (HueBand{315, 345, 39.375, 45}));
    send(handles, x(39.375), QEvent::MouseButtonRelease, Qt::NoButton);
    // A new press takes the nearest again.
    send(handles, x(43.59375), QEvent::MouseButtonPress, Qt::LeftButton);
    QCOMPARE(shown.settings().band(), (HueBand{315, 345, 39.375, 43.59375}));
    // Past the end, the degrees stop at 360.
    send(handles, width + 50, QEvent::MouseButtonPress, Qt::LeftButton);
    QCOMPARE(shown.settings().band(), (HueBand{315, 0, 39.375, 43.59375}));
    QCOMPARE(shown.child<QLabel>("spectrumReadout").text(), QString("315°   0°   39°   44°"));
    // Nearness wraps: 348.75 is nearer 0 than 315.
    send(handles, x(348.75), QEvent::MouseButtonPress, Qt::LeftButton);
    QCOMPARE(shown.settings().band(), (HueBand{315, 348.75, 39.375, 43.59375}));
    // Before the start, the degrees stop at 0.
    send(handles, -30, QEvent::MouseButtonPress, Qt::LeftButton);
    QCOMPARE(shown.settings().band(), (HueBand{315, 0, 39.375, 43.59375}));
    // The right button neither presses nor drags a handle.
    send(handles, x(90), QEvent::MouseButtonPress, Qt::RightButton, Qt::RightButton);
    send(handles, x(5.625), QEvent::MouseMove, Qt::RightButton);
    QCOMPARE(shown.settings().band(), (HueBand{315, 0, 39.375, 43.59375}));
    // Past its shoulder the held core end is refused.
    send(handles, x(39.375), QEvent::MouseButtonPress, Qt::LeftButton);
    send(handles, x(50.625), QEvent::MouseMove, Qt::LeftButton);
    QCOMPARE(shown.settings().band(), (HueBand{315, 0, 39.375, 43.59375}));
}

void HueSaturationSpectrumTests::theSpectrumShowsTheHuesAndTheirShift()
{
    Sheet shown;
    shown.set(HueSaturationSettings(60, 0, 0, false, ColorRange::reds));
    QVERIFY(shown.show());
    QWidget &editor = shown.child<QWidget>("hueSpectrum");
    QWidget &before = shown.child<QWidget>("spectrumBefore"), &after = shown.child<QWidget>("spectrumAfter"), &handles = shown.child<QWidget>("spectrumHandles");
    QLabel &readout = shown.child<QLabel>("spectrumReadout");
    QCOMPARE(before.height(), 16);
    QCOMPARE(after.height(), 16);
    // Five apart, the readout centred below.
    QCOMPARE(handles.y() - before.geometry().bottom() - 1, 5);
    QCOMPARE(readout.y() - after.geometry().bottom() - 1, 5);
    QVERIFY(std::abs(2 * readout.x() + readout.width() - editor.width()) <= 1);
    QCOMPARE(readout.width(), readout.sizeHint().width());
    // Slices of five degrees: red first, cyan halfway.
    const int middle = before.width() / 2 + 2;
    QCOMPARE(drawn(before).pixelColor(3, 8), QColor(Qt::red));
    QCOMPARE(drawn(before).pixelColor(8, 8).rgb(), QColor::fromHsvF(5.0f / 360, 1, 1).rgb());
    QCOMPARE(drawn(before).pixelColor(middle, 8), QColor(Qt::cyan));
    // Slices overlap: no white between them.
    QCOMPARE(drawn(before).pixelColor(5, 8).blue(), 0);
    // Corners round off over white.
    QVERIFY(drawn(before).pixelColor(0, 0).green() > 200);
    // Reds turn yellow; cyan, outside their band, stays.
    QCOMPARE(drawn(after).pixelColor(3, 8), QColor(Qt::yellow));
    QCOMPARE(drawn(after).pixelColor(middle, 8), QColor(Qt::cyan));
    // Bars for the core, blocks for the shoulders.
    const QColor black = handles.palette().color(QPalette::WindowText), white(Qt::white);
    const QImage marks = drawn(handles);
    const int start = int(345.0 / 360 * handles.width()), end = int(15.0 / 360 * handles.width()), shoulder = int(45.0 / 360 * handles.width());
    QVERIFY(marks.pixelColor(start, 0) == black && marks.pixelColor(start, 11) == black);
    QVERIFY(marks.pixelColor(end, 0) == black && marks.pixelColor(end, 11) == black);
    QVERIFY(marks.pixelColor(shoulder, 6) == black && marks.pixelColor(shoulder, 1) == white && marks.pixelColor(shoulder, 10) == white);
    QVERIFY(marks.pixelColor(shoulder - 3, 6) == black && marks.pixelColor(shoulder + 3, 6) == black && marks.pixelColor(shoulder + 5, 6) == white);
    QCOMPARE(marks.pixelColor(100, 6), white);
    // The marks take the window's text colour.
    QPalette red = shown.sheet->palette();
    red.setColor(QPalette::WindowText, Qt::red);
    shown.sheet->setPalette(red);
    QCOMPARE(drawn(handles).pixelColor(start, 0), QColor(Qt::red));
}

void HueSaturationSpectrumTests::theSpectrumRepaintsWithTheBand()
{
    Sheet shown;
    shown.set(HueSaturationSettings(0, 0, 0, false, ColorRange::reds));
    QVERIFY(shown.show());
    Paints handles(shown.child<QWidget>("spectrumHandles")), after(shown.child<QWidget>("spectrumAfter"));
    QTest::qWait(50);
    handles.count = after.count = 0;
    HueSaturationSettings settings = shown.settings();
    HueBand band = settings.band();
    band.setHandle(2, 30);
    settings.setBand(band);
    shown.set(settings);
    QTRY_VERIFY(handles.count > 0 && after.count > 0);
}

QTEST_MAIN(HueSaturationSpectrumTests)
#include "HueSaturationSpectrumTests.moc"
