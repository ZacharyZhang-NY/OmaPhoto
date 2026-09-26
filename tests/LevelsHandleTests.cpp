#include "LevelsSheetFixtures.h"

// The sheet's handles: where they sit, draw and drag.
class LevelsHandleTests : public QObject {
    Q_OBJECT
private slots:
    void handlesDrawSwiftsTriangles();
    void handlesDragTheirOwnLevels();
    void theTopmostHandleTakesThePress();
    void aHandleReplacesItsEntrysTyping();
};

void LevelsHandleTests::handlesDrawSwiftsTriangles()
{
    Sheet shown;
    QVERIFY(shown.show());
    const QWidget &input = shown.child<QWidget>("levelsInputHandles");
    const QColor window = shown.sheet->palette().color(QPalette::Window);
    // Black, gray, white inside; the window around them.
    const std::pair<const char *, QColor> fills[] = {{"Input black", Qt::black}, {"Gamma", QColor(142, 142, 147)}, {"Input white", Qt::white}};
    for (const auto &[handle, fill] : fills) {
        const QImage drawn = shown.handle(handle).grab().toImage();
        QCOMPARE(drawn.pixelColor(11, 12), fill);
        for (const QPoint outside : {QPoint(11, 3), QPoint(11, 16), QPoint(3, 14), QPoint(19, 14)})
            QCOMPARE(drawn.pixelColor(outside), window);
    }
    // Twelve wide at the base: its corners' pixels stay light.
    const QImage black = shown.handle("Input black").grab().toImage();
    QVERIFY(black.pixelColor(4, 14).red() > 200 && black.pixelColor(17, 14).red() > 200);
    QVERIFY(black.pixelColor(6, 14).red() < 100 && black.pixelColor(15, 14).red() < 100);
    // Under the base: the rim's three quarters, then the fill's.
    const QColor rim = shown.handle("Input black").grab().toImage().pixelColor(11, 15);
    QVERIFY2(std::abs(rim.red() - 0.75 * (0.75 * 142 + 0.25 * window.red())) <= 2, qPrintable(rim.name()));
    QVERIFY2(std::abs(rim.blue() - 0.75 * (0.75 * 147 + 0.25 * window.blue())) <= 2, qPrintable(rim.name()));
    // Past the track's edge the triangle still shows.
    QCOMPARE(shown.sheet->grab().toImage().pixelColor(21, input.y() + 12), QColor(Qt::black));
}

void LevelsHandleTests::handlesDragTheirOwnLevels()
{
    Sheet shown;
    QVERIFY(shown.show());
    const QWidget &input = shown.child<QWidget>("levelsInputHandles"), &output = shown.child<QWidget>("levelsOutputHandles");
    QWidget &black = shown.handle("Input black"), &gamma = shown.handle("Gamma"), &white = shown.handle("Input white");
    // Centred on each tone, a point above the track.
    QCOMPARE(black.pos(), QPoint(13, input.y() - 1));
    QCOMPARE(gamma.pos(), QPoint(209, input.y() - 1));
    QCOMPARE(white.pos(), QPoint(405, input.y() - 1));
    QCOMPARE(shown.handle("Output white").pos(), QPoint(405, output.y() - 1));
    QTest::mousePress(&black, Qt::LeftButton, Qt::NoModifier, QPoint(11, 10));
    QCOMPARE(shown.range(), LevelRange());
    drag(black, input, 51.4);
    QVERIFY(shown.range().black == 51);
    QCOMPARE(shown.child<QLineEdit>("levelsInputblack").text(), QString("51"));
    QCOMPARE(black.x(), 91);
    drag(black, input, 400);
    QCOMPARE(shown.range().black, 254.0);
    // Black stays a tone under white.
    shown.set(LevelRange{0, 1, 100, 0, 255});
    drag(black, input, 150);
    QCOMPARE(shown.range(), (LevelRange{99, 1, 100, 0, 255}));
    QTest::mouseRelease(&black, Qt::LeftButton, Qt::NoModifier, QPoint(11, 10));
    // White stays above black; whole tones, rounded.
    shown.set(LevelRange());
    QTest::mousePress(&white, Qt::LeftButton, Qt::NoModifier, QPoint(11, 10));
    drag(white, input, 100.6);
    QCOMPARE(shown.range().white, 101.0);
    shown.set(LevelRange{30, 1, 101, 0, 255});
    drag(white, input, -40);
    QCOMPARE(shown.range().white, 31.0);
    QTest::mouseRelease(&white, Qt::LeftButton, Qt::NoModifier, QPoint(11, 10));
    // The gamma follows the tone's share of the range.
    shown.set(LevelRange());
    QTest::mousePress(&gamma, Qt::LeftButton, Qt::NoModifier, QPoint(11, 10));
    drag(gamma, input, 191.25);
    QVERIFY(std::abs(shown.range().gamma - std::log(0.75) / std::log(0.5)) < 1e-9);
    QCOMPARE(shown.child<QLineEdit>("levelsGamma").text(), QString("0.42"));
    QCOMPARE(gamma.x(), 307);
    drag(gamma, input, 0);
    QVERIFY(std::abs(shown.range().gamma - std::log(0.001) / std::log(0.5)) < 1e-9);
    drag(gamma, input, 255);
    QCOMPARE(shown.range().gamma, 0.1);
    // The share is of the range between black and white.
    shown.set(LevelRange{51, 1, 255, 0, 255});
    drag(gamma, input, 204);
    QVERIFY(std::abs(shown.range().gamma - std::log(0.75) / std::log(0.5)) < 1e-9);
    QTest::mouseRelease(&gamma, Qt::LeftButton, Qt::NoModifier, QPoint(11, 10));
    // Outputs cross freely; other buttons drag nothing.
    shown.set(LevelRange());
    QWidget &outputBlack = shown.handle("Output black"), &outputWhite = shown.handle("Output white");
    QTest::mousePress(&outputWhite, Qt::LeftButton, Qt::NoModifier, QPoint(11, 10));
    drag(outputWhite, output, 64.4);
    QTest::mouseRelease(&outputWhite, Qt::LeftButton, Qt::NoModifier, QPoint(11, 10));
    QTest::mousePress(&outputBlack, Qt::LeftButton, Qt::NoModifier, QPoint(11, 10));
    drag(outputBlack, output, 128);
    QTest::mouseRelease(&outputBlack, Qt::LeftButton, Qt::NoModifier, QPoint(11, 10));
    QCOMPARE(shown.range(), (LevelRange{0, 1, 255, 128, 64}));
    QTest::mousePress(&outputBlack, Qt::RightButton, Qt::NoModifier, QPoint(0, 10));
    drag(outputBlack, output, 20, Qt::RightButton);
    QTest::mouseRelease(&outputBlack, Qt::RightButton, Qt::NoModifier, QPoint(0, 10));
    QCOMPARE(shown.range().outputBlack, 128.0);
    QCOMPARE(outputBlack.pos(), QPoint(210, output.y() - 1));
    // A drag keeps Preview as it stands.
    QVERIFY(!shown.session.levels().value().preview);
}

void LevelsHandleTests::theTopmostHandleTakesThePress()
{
    Sheet shown;
    QVERIFY(shown.show());
    shown.set(LevelRange{100, 1, 101, 0, 255});
    const QWidget &input = shown.child<QWidget>("levelsInputHandles"), &output = shown.child<QWidget>("levelsOutputHandles");
    // All three overlap; white lies on top, as Swift's last.
    QWidget *pressed = shown.sheet->childAt(179, input.y() + 9);
    QCOMPARE(pressed, &shown.handle("Input white"));
    QTest::mousePress(pressed, Qt::LeftButton, Qt::NoModifier, QPoint(11, 10));
    drag(*pressed, input, 200);
    QTest::mouseRelease(pressed, Qt::LeftButton, Qt::NoModifier, QPoint(11, 10));
    QCOMPARE(shown.range(), (LevelRange{100, 1, 200, 0, 255}));
    // Between handles the track takes nothing.
    QCOMPARE(shown.sheet->childAt(300, input.y() + 9), &input);
    // The output handles lie on the gradient's last row.
    QCOMPARE(shown.sheet->childAt(24, output.y() - 1), &shown.handle("Output black"));
}

void LevelsHandleTests::aHandleReplacesItsEntrysTyping()
{
    Sheet shown;
    QVERIFY(shown.show());
    const QWidget &input = shown.child<QWidget>("levelsInputHandles"), &output = shown.child<QWidget>("levelsOutputHandles");
    QLineEdit &black = shown.child<QLineEdit>("levelsInputblack"), &white = shown.child<QLineEdit>("levelsOutputwhite");
    // A handle takes no focus; its drag replaces the typing.
    type(black, QStringLiteral("20"));
    QTest::mousePress(&shown.handle("Input black"), Qt::LeftButton, Qt::NoModifier, QPoint(20, 10));
    QVERIFY(black.hasFocus() && black.text() == "6" && !black.isModified() && shown.range().black == 6);
    drag(shown.handle("Input black"), input, 51.4);
    QVERIFY(black.text() == "51" && !black.isModified());
    white.setFocus();
    QCOMPARE(shown.range().black, 51.0);
    // Another handle leaves the typing, which then applies.
    type(white, QStringLiteral("200"));
    QTest::mousePress(&shown.handle("Output black"), Qt::LeftButton, Qt::NoModifier, QPoint(11, 10));
    drag(shown.handle("Output black"), output, 30);
    QVERIFY(white.text() == "200" && white.isModified() && shown.range().outputBlack == 30);
    QTest::mousePress(&shown.handle("Output white"), Qt::LeftButton, Qt::NoModifier, QPoint(11, 10));
    drag(shown.handle("Output white"), output, 180);
    QVERIFY(white.text() == "180" && !white.isModified());
    black.setFocus();
    QCOMPARE(shown.range().outputWhite, 180.0);
}

QTEST_MAIN(LevelsHandleTests)
#include "LevelsHandleTests.moc"
