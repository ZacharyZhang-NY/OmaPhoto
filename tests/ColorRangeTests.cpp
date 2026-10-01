#include "Document/EditorSession.h"
#include "SelectionFixtures.h"
#include <QFutureWatcher>
#include <QtTest>

extern "C" {
#include "WandPixels.h"
}

// Swift 1.3.4's Color Range: model, kernel, session.
namespace {
// Red, a band of blue, and a clear corner.
QImage picture()
{
    QImage image(40, 20, QImage::Format_RGBA8888_Premultiplied);
    image.fill(QColor(200, 30, 30));
    for (int y = 0; y < 20; ++y)
        for (int x = 20; x < 30; ++x)
            image.setPixelColor(x, y, QColor(30, 40, 210));
    for (int y = 0; y < 5; ++y)
        for (int x = 35; x < 40; ++x)
            image.setPixelColor(x, y, Qt::transparent);
    return image;
}

struct Opened {
    EditorSession session;
    Opened()
    {
        session.createDocument(40, 20);
        session.insert(ImportedImage(picture(), picture(), QStringLiteral("Picture")));
        session.beginColorRange();
    }
    const ColorRangeEdit &edit() const { return session.colorRange().value(); }
    // A change, then its match landing: a second signal.
    void land(const std::function<void()> &change)
    {
        QSignalSpy changed(&session, &EditorSession::changed);
        change();
        QVERIFY(QTest::qWaitFor([&changed] { return changed.count() >= 2; }));
    }
    void sample(QPointF point, bool shift = false, bool option = false)
    {
        land([&] { session.sampleColorRange(point, shift, option); });
    }
    int covered(int x, int y) const { return coverage(session, x, y); }
};

std::vector<uchar> rgb(int r, int g, int b)
{
    return {uchar(r), uchar(g), uchar(b)};
}

// A 2100 checkerboard, black in its corner: past the limit.
QImage checkerboard()
{
    QImage board(2100, 2100, QImage::Format_RGBA8888_Premultiplied);
    for (int y = 0; y < board.height(); ++y) {
        auto *row = reinterpret_cast<quint32 *>(board.scanLine(y));
        for (int x = 0; x < board.width(); ++x)
            row[x] = (x + y) % 2 && (x > 2 || y > 2) ? 0xFFFFFFFF : 0xFF000000;
    }
    return board;
}

// The session's workers: its own watchers and one a change.
int watchers(const EditorSession &session)
{
    return int(session.findChildren<QFutureWatcherBase *>(Qt::FindDirectChildrenOnly).size());
}
}

class ColorRangeTests : public QObject {
    Q_OBJECT
private slots:
    void theKernelMatchesWithinFuzziness();
    void aClickPicksTheAverageAroundIt();
    void addTakeAwayAndStartOver();
    void fuzzinessInvertAndThePreview();
    void okIsOneStepAndCancelPutsBack();
    void theGatesWaitForThePanel();
    void everyChangeIsAnnounced();
    void anOutlineTooDetailedIsShownAndKeptOut();
    void anErrorKeepsTheLastMatch();
    void onlyTheNewestMatchLands();
    void thePreviewTruncatesAsSwifts();
};

void ColorRangeTests::theKernelMatchesWithinFuzziness()
{
    // Four pixels: red, red off by 10, blue, clear.
    const std::array<uchar, 16> pixels{200, 30, 30, 255, 210, 30, 30, 255, 30, 40, 210, 255, 0, 0, 0, 0};
    std::array<uchar, 4> mask{};
    const std::vector<uchar> red = rgb(200, 30, 30);
    QCOMPARE(color_range_mask(pixels.data(), 4, 1, 16, red.data(), 1, nullptr, 0, 9, 0, mask.data()), 1L);
    QCOMPARE(mask, (std::array<uchar, 4>{255, 0, 0, 0}));
    QCOMPARE(color_range_mask(pixels.data(), 4, 1, 16, red.data(), 1, nullptr, 0, 10, 0, mask.data()), 2L);
    QCOMPARE(mask, (std::array<uchar, 4>{255, 255, 0, 0}));
    // Taken away wins; inverted, clear pixels are selected too.
    const std::vector<uchar> off = rgb(210, 30, 30);
    QCOMPARE(color_range_mask(pixels.data(), 4, 1, 16, red.data(), 1, off.data(), 1, 10, 0, mask.data()), 0L);
    QCOMPARE(color_range_mask(pixels.data(), 4, 1, 16, red.data(), 1, nullptr, 0, 10, 1, mask.data()), 2L);
    QCOMPARE(mask, (std::array<uchar, 4>{0, 0, 255, 255}));
    // Premultiplied pixels are read straight: half clear red matches red.
    const std::array<uchar, 4> half{100, 15, 15, 128};
    std::array<uchar, 1> one{};
    QCOMPARE(color_range_mask(half.data(), 1, 1, 4, red.data(), 1, nullptr, 0, 1, 0, one.data()), 1L);
}

void ColorRangeTests::aClickPicksTheAverageAroundIt()
{
    Opened opened;
    QVERIFY(!opened.edit().hasColors() && opened.edit().preview.isNull());
    opened.sample(QPointF(5.7, 10.2));
    QCOMPARE(opened.edit().include, rgb(200, 30, 30));
    // On the seam: three red and six blue, averaged.
    opened.sample(QPointF(20, 10));
    QCOMPARE(opened.edit().include, rgb((3 * 200 + 6 * 30 + 4) / 9, (3 * 30 + 6 * 40 + 4) / 9, (3 * 30 + 6 * 210 + 4) / 9));
    // At the clear corner's edge only coloured pixels count.
    opened.sample(QPointF(34, 2));
    QCOMPARE(opened.edit().include, rgb(200, 30, 30));
    // The click's pixel is floored: 19.6 reads round 19.
    opened.sample(QPointF(19.6, 10));
    QCOMPARE(opened.edit().include, rgb((6 * 200 + 3 * 30 + 4) / 9, (6 * 30 + 3 * 40 + 4) / 9, (6 * 30 + 3 * 210 + 4) / 9));
    // In the clear corner, past the edge, no number: nothing.
    for (const QPointF point : {QPointF(38, 2), QPointF(-1, 3), QPointF(40, 3), QPointF(qQNaN(), 3)}) {
        const int generation = opened.edit().generation;
        opened.session.sampleColorRange(point, false, false);
        QCOMPARE(opened.edit().generation, generation);
    }
    // Three rows, one of each colour: all three count.
    QImage rows(3, 3, QImage::Format_RGBA8888_Premultiplied);
    for (int x = 0; x < 3; ++x) {
        rows.setPixelColor(x, 0, QColor(90, 0, 0));
        rows.setPixelColor(x, 1, QColor(0, 90, 0));
        rows.setPixelColor(x, 2, QColor(0, 0, 90));
    }
    EditorSession session;
    session.createDocument(3, 3);
    session.insert(ImportedImage(rows, rows, QStringLiteral("Rows")));
    session.beginColorRange();
    session.sampleColorRange(QPointF(1, 1), false, false);
    QCOMPARE(session.colorRange().value().include, rgb(30, 30, 30));
}

void ColorRangeTests::addTakeAwayAndStartOver()
{
    Opened opened;
    opened.sample(QPointF(5, 5));
    QCOMPARE(opened.covered(5, 5), 255);
    QCOMPARE(opened.covered(25, 5), 0);
    QVERIFY(opened.session.selection().value().antialiased);
    // The match takes the session's antialiasing.
    opened.session.setSelectionAntialiased(false);
    opened.sample(QPointF(6, 5));
    QVERIFY(!opened.session.selection().value().antialiased);
    // Shift adds blue; Alt takes red away.
    opened.sample(QPointF(25, 5), true, false);
    QCOMPARE(opened.edit().include, (std::vector<uchar>{200, 30, 30, 30, 40, 210}));
    QCOMPARE(opened.covered(25, 5), 255);
    opened.sample(QPointF(5, 5), true, true);
    QCOMPARE(opened.edit().exclude, rgb(200, 30, 30));
    QVERIFY(opened.covered(5, 5) == 0 && opened.covered(25, 5) == 255);
    // The chosen eyedroppers act without keys; replace starts over.
    opened.session.setColorRangeSampleMode(HueSampleMode::remove);
    opened.sample(QPointF(25, 5));
    QCOMPARE(opened.edit().exclude, (std::vector<uchar>{200, 30, 30, 30, 40, 210}));
    QVERIFY(!opened.session.selection());
    opened.session.setColorRangeSampleMode(HueSampleMode::replace);
    opened.sample(QPointF(25, 5));
    QVERIFY(opened.edit().include == rgb(30, 40, 210) && opened.edit().exclude.empty());
    QCOMPARE(opened.covered(25, 5), 255);
    // Held keys outrank the chosen eyedropper.
    opened.session.setColorRangeHeld(HueSampleMode::add);
    QCOMPARE(opened.edit().effectiveMode(), HueSampleMode::add);
}

void ColorRangeTests::fuzzinessInvertAndThePreview()
{
    Opened opened;
    opened.sample(QPointF(5, 5));
    // The preview: the mask at twice the panel's fit.
    const double scale = std::min(292.0 / 40, 200.0 / 20) * 2;
    QCOMPARE(opened.edit().preview.size(), QSize(int(40 * scale), int(20 * scale)));
    QCOMPARE(qGray(opened.edit().preview.pixel(10, 10)), 255);
    QCOMPARE(qGray(opened.edit().preview.pixel(int(25 * scale), 10)), 0);
    // Fuzziness: whole, 0 to 200; wide, blue joins.
    opened.land([&] { opened.session.setColorRangeFuzziness(250.4); });
    QCOMPARE(opened.edit().fuzziness, 200.0);
    QCOMPARE(opened.covered(25, 5), 255);
    opened.session.setColorRangeFuzziness(-3);
    QCOMPARE(opened.edit().fuzziness, 0.0);
    opened.session.setColorRangeFuzziness(12.6);
    QCOMPARE(opened.edit().fuzziness, 13.0);
    // Invert: all but red, the clear corner included.
    opened.land([&] { opened.session.setColorRangeInvert(true); });
    QVERIFY(opened.covered(5, 5) == 0 && opened.covered(25, 5) == 255 && opened.covered(38, 2) == 255);
    // No colours: the old selection and no preview.
    Opened empty;
    empty.session.updateColorRange();
    QVERIFY(!empty.session.selection() && empty.edit().preview.isNull());
}

void ColorRangeTests::okIsOneStepAndCancelPutsBack()
{
    Opened opened;
    QPainterPath corner;
    corner.addRect(0, 0, 4, 4);
    const DocumentSelection before{corner, true};
    opened.session.cancelColorRange();
    opened.session.setSelection(before, QStringLiteral("Before"));
    opened.session.beginColorRange();
    QCOMPARE(opened.edit().original.value().path, before.path);
    opened.sample(QPointF(25, 5));
    QCOMPARE(opened.covered(1, 1), 0);
    // Cancel: the selection there was, no step.
    const int steps = opened.session.history.undoCount();
    opened.session.cancelColorRange();
    QVERIFY(!opened.session.colorRange() && opened.covered(1, 1) == 255 && opened.covered(25, 5) == 0);
    QCOMPARE(opened.session.history.undoCount(), steps);
    // OK: one step, named; undo puts the old back.
    opened.session.beginColorRange();
    opened.sample(QPointF(25, 5));
    opened.session.commitColorRange();
    QCOMPARE(opened.session.history.undoCount(), steps + 1);
    QCOMPARE(opened.session.history.undoName(), QStringLiteral("Color Range"));
    QCOMPARE(opened.covered(25, 5), 255);
    opened.session.undo();
    QVERIFY(opened.covered(1, 1) == 255 && opened.covered(25, 5) == 0);
    // OK with no colour keeps the old selection, no step.
    opened.session.beginColorRange();
    opened.session.commitColorRange();
    QCOMPARE(opened.session.history.undoCount(), steps);
    QCOMPARE(opened.covered(1, 1), 255);
    // OK on a match of nothing deselects.
    opened.session.beginColorRange();
    opened.sample(QPointF(5, 5));
    opened.sample(QPointF(5, 5), false, true);
    QVERIFY(!opened.session.selection());
    opened.session.commitColorRange();
    QVERIFY(!opened.session.selection());
    QCOMPARE(opened.session.history.undoName(), QStringLiteral("Deselect"));
}

void ColorRangeTests::theGatesWaitForThePanel()
{
    Opened opened;
    QVERIFY(!opened.session.canEditLayers() && !opened.session.canUseHistory() && !opened.session.canStartProjectOperation());
    QVERIFY(!opened.session.canSelectColorRange());
    // A second begin keeps the open edit.
    const QUuid id = opened.edit().id;
    opened.session.beginColorRange();
    QCOMPARE(opened.edit().id, id);
    opened.session.cancelColorRange();
    QVERIFY(opened.session.canEditLayers() && opened.session.canUseHistory() && opened.session.canStartProjectOperation());
    QVERIFY(opened.session.canSelectColorRange());
    // A request waiting on the panel runs once it closes.
    opened.session.beginColorRange();
    bool ran = false;
    opened.session.waitForFileRequest([&ran] { ran = true; });
    QTest::qWait(10);
    QVERIFY(!ran);
    opened.session.cancelColorRange();
    QTRY_VERIFY(ran);
    // OK frees one too.
    opened.session.beginColorRange();
    ran = false;
    opened.session.waitForFileRequest([&ran] { ran = true; });
    QTest::qWait(10);
    QVERIFY(!ran);
    opened.session.commitColorRange();
    QTRY_VERIFY(ran);
    // Without a document there is nothing to open.
    EditorSession empty;
    QVERIFY(!empty.canSelectColorRange());
    empty.beginColorRange();
    QVERIFY(!empty.colorRange());
}

void ColorRangeTests::everyChangeIsAnnounced()
{
    Opened opened;
    QSignalSpy changed(&opened.session, &EditorSession::changed);
    // Before a colour, Fuzziness and Invert still say so.
    opened.session.setColorRangeFuzziness(50);
    QCOMPARE(changed.count(), 1);
    opened.session.setColorRangeInvert(true);
    QCOMPARE(changed.count(), 2);
    opened.session.setColorRangeInvert(false);
    // Each eyedropper and held key; an unchanged one is silent.
    changed.clear();
    opened.session.setColorRangeSampleMode(HueSampleMode::add);
    opened.session.setColorRangeSampleMode(HueSampleMode::add);
    opened.session.setColorRangeHeld(HueSampleMode::remove);
    opened.session.setColorRangeHeld(HueSampleMode::remove);
    opened.session.setColorRangeFuzziness(50);
    QCOMPARE(changed.count(), 2);
    // A click says so now, and as its match lands.
    opened.session.setColorRangeHeld(std::nullopt);
    opened.session.setColorRangeSampleMode(HueSampleMode::replace);
    changed.clear();
    opened.session.sampleColorRange(QPointF(5, 5), false, false);
    QCOMPARE(changed.count(), 1);
    QTRY_COMPARE(changed.count(), 2);
    QCOMPARE(opened.covered(5, 5), 255);
}

void ColorRangeTests::anOutlineTooDetailedIsShownAndKeptOut()
{
    const QImage board = checkerboard();
    EditorSession session;
    session.createDocument(2100, 2100);
    session.insert(ImportedImage(board, board, QStringLiteral("Board")));
    session.beginColorRange();
    session.setColorRangeFuzziness(0);
    session.sampleColorRange(QPointF(1, 1), false, false);
    QTRY_VERIFY_WITH_TIMEOUT(session.colorRange().value().error, 30000);
    QCOMPARE(session.colorRange().value().error.value(), QString::fromUtf8(MagicWandError(MagicWandError::Kind::tooDetailed).what()));
    // The preview still shows; the selection stays as it was.
    QVERIFY(!session.colorRange().value().preview.isNull() && !session.selection());
    const int steps = session.history.undoCount();
    session.commitColorRange();
    QVERIFY(!session.selection() && !session.colorRange());
    QCOMPARE(session.history.undoCount(), steps);
}

void ColorRangeTests::anErrorKeepsTheLastMatch()
{
    // A red band on top; a black block amid checks.
    QImage board = checkerboard();
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < board.width(); ++x)
            board.setPixelColor(x, y, QColor(200, 30, 30));
    for (int y = 100; y < 103; ++y)
        for (int x = 100; x < 103; ++x)
            board.setPixelColor(x, y, Qt::black);
    EditorSession session;
    session.createDocument(2100, 2100);
    session.insert(ImportedImage(board, board, QStringLiteral("Board")));
    session.beginColorRange();
    session.setColorRangeFuzziness(0);
    session.sampleColorRange(QPointF(10, 4), false, false);
    QTRY_VERIFY_WITH_TIMEOUT(session.selection(), 30000);
    QVERIFY(coverage(session, 10, 4) == 255 && coverage(session, 500, 500) == 0);
    // Black matches every dark check: too detailed, the band stays.
    session.sampleColorRange(QPointF(101, 101), false, false);
    QTRY_VERIFY_WITH_TIMEOUT(session.colorRange().value().error, 30000);
    QVERIFY(coverage(session, 10, 4) == 255 && coverage(session, 500, 500) == 0);
    // OK after an error: the old selection, no step.
    const int steps = session.history.undoCount();
    session.commitColorRange();
    QVERIFY(!session.selection() && session.history.undoCount() == steps);
}

void ColorRangeTests::onlyTheNewestMatchLands()
{
    EditorSession session;
    session.createDocument(2100, 2100);
    const QImage board = checkerboard();
    session.insert(ImportedImage(board, board, QStringLiteral("Board")));
    const int idle = watchers(session);
    session.beginColorRange();
    session.setColorRangeFuzziness(0);
    // A slow trace, then a gray that matches nothing, quickly.
    session.sampleColorRange(QPointF(1, 1), false, false);
    session.sampleColorRange(QPointF(3, 3), false, false);
    QTRY_COMPARE_WITH_TIMEOUT(watchers(session), idle, 30000);
    QVERIFY(!session.colorRange().value().error && !session.colorRange().value().preview.isNull());
    // A closed edit's trace lands on no later edit.
    session.cancelColorRange();
    session.beginColorRange();
    session.sampleColorRange(QPointF(1, 1), false, false);
    session.cancelColorRange();
    session.beginColorRange();
    // Both at generation one: the edit's id tells them apart.
    session.sampleColorRange(QPointF(3, 3), false, false);
    QCOMPARE(session.colorRange().value().generation, 1);
    QTRY_COMPARE_WITH_TIMEOUT(watchers(session), idle, 30000);
    QVERIFY(!session.colorRange().value().error);
    // Nor on another document.
    Opened opened;
    const int quiet = watchers(opened.session);
    opened.session.sampleColorRange(QPointF(5, 5), false, false);
    opened.session.createDocument(40, 20);
    QTRY_COMPARE_WITH_TIMEOUT(watchers(opened.session), quiet, 30000);
    QVERIFY(!opened.session.selection() && opened.edit().preview.isNull());
}

void ColorRangeTests::thePreviewTruncatesAsSwifts()
{
    EditorSession session;
    session.createDocument(400, 300);
    QImage red(400, 300, QImage::Format_RGBA8888_Premultiplied);
    red.fill(QColor(200, 30, 30));
    session.insert(ImportedImage(red, red, QStringLiteral("Red")));
    session.beginColorRange();
    session.sampleColorRange(QPointF(5, 5), false, false);
    QTRY_VERIFY(!session.colorRange().value().preview.isNull());
    QCOMPARE(session.colorRange().value().preview.size(), QSize(533, 400));
}

QTEST_MAIN(ColorRangeTests)
#include "ColorRangeTests.moc"
