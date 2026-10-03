#include "DialogDesk.h"
#include "Document/ImageTrim.h"
#include "IO/ProjectController.h"
#include "UI/TrimSheet.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QtTest>

// Swift's ImageTrimTests, then the sheet, the controller and the menu.
namespace {
struct Colour {
    int r, g, b, a;
};

// Premultiplied RGBA, painted pixel by pixel.
QImage painted(int width, int height, const std::function<Colour(int, int)> &painter)
{
    QImage image(width, height, QImage::Format_RGBA8888_Premultiplied);
    for (int y = 0; y < height; ++y) {
        uchar *row = image.scanLine(y);
        for (int x = 0; x < width; ++x) {
            const Colour colour = painter(x, y);
            row[x * 4] = uchar(colour.r), row[x * 4 + 1] = uchar(colour.g), row[x * 4 + 2] = uchar(colour.b), row[x * 4 + 3] = uchar(colour.a);
        }
    }
    return image;
}

TrimOptions edges(TrimBasedOn basedOn, bool top, bool bottom, bool left, bool right)
{
    return TrimOptions{.basedOn = basedOn, .top = top, .bottom = bottom, .left = left, .right = right};
}

struct Desk {
    EditorSession session;
    ProjectController controller{session};
    QWidget window;
    bool done = false;
    Desk()
    {
        session.createDocument(100, 100);
        session.insert(ImportedImage(painted(40, 40, [](int, int) { return Colour{200, 50, 50, 255}; }), QImage(), "Square"), QPointF(40, 50));
        window.resize(600, 400);
        window.show();
        controller.window = &window;
    }
    QDialog &dialog()
    {
        auto *shown = window.findChild<QDialog *>();
        if (!shown)
            throw std::runtime_error("no sheet shown");
        return *shown;
    }
    std::function<void()> finished()
    {
        done = false;
        return [this] { done = true; };
    }
};
}

class ImageTrimTests : public QObject {
    Q_OBJECT
private slots:
    void calculateTrimRectTransparentPixels();
    void calculateTrimRectTopLeftPixelColor();
    void calculateTrimRectBottomRightPixelColor();
    void uniformImageReturnsNil();
    void trimImageDirectly();
    void sessionTrimIntegrationAndUndo();
    void toleranceAndAlphaDecideAMatch();
    void theSheetSaysSwiftsWordsAndReturnsItsChoice();
    void theControllerTrimsThroughTheSheetAsOneStep();
    void nothingToTrimOrACancelChangesNothing();
    void aTrimThatFailsIsExplained();
};

void ImageTrimTests::calculateTrimRectTransparentPixels()
{
    // Content x 2..<17, y 4..<15, clear around it.
    const QImage image = painted(20, 20, [](int x, int y) {
        return x >= 2 && x < 17 && y >= 4 && y < 15 ? Colour{255, 0, 0, 255} : Colour{0, 0, 0, 0};
    });
    QCOMPARE(ImageTrim::calculateTrimRect(image, edges(TrimBasedOn::transparentPixels, true, true, true, true)), std::optional(QRect(2, 4, 15, 11)));
    QCOMPARE(ImageTrim::calculateTrimRect(image, edges(TrimBasedOn::transparentPixels, true, true, false, false)), std::optional(QRect(0, 4, 20, 11)));
    QCOMPARE(ImageTrim::calculateTrimRect(image, edges(TrimBasedOn::transparentPixels, false, false, true, true)), std::optional(QRect(2, 0, 15, 20)));
    // One edge alone, each.
    QCOMPARE(ImageTrim::calculateTrimRect(image, edges(TrimBasedOn::transparentPixels, false, true, false, false)), std::optional(QRect(0, 0, 20, 15)));
    QCOMPARE(ImageTrim::calculateTrimRect(image, edges(TrimBasedOn::transparentPixels, false, false, false, true)), std::optional(QRect(0, 0, 17, 20)));
    // No edge chosen trims nothing.
    QVERIFY(!edges(TrimBasedOn::transparentPixels, false, false, false, false).trimsAny());
    QCOMPARE(ImageTrim::calculateTrimRect(image, edges(TrimBasedOn::transparentPixels, false, false, false, false)), std::nullopt);
}

void ImageTrimTests::calculateTrimRectTopLeftPixelColor()
{
    // A blue border of three around yellow.
    const QImage image = painted(16, 16, [](int x, int y) {
        return x >= 3 && x < 13 && y >= 3 && y < 13 ? Colour{255, 255, 0, 255} : Colour{0, 0, 255, 255};
    });
    QCOMPARE(ImageTrim::calculateTrimRect(image, edges(TrimBasedOn::topLeftPixelColor, true, true, true, true)), std::optional(QRect(3, 3, 10, 10)));
    QCOMPARE(ImageTrim::calculateTrimRect(image, edges(TrimBasedOn::topLeftPixelColor, true, false, false, false)), std::optional(QRect(0, 3, 16, 13)));
    QCOMPARE(ImageTrim::calculateTrimRect(image, edges(TrimBasedOn::topLeftPixelColor, false, false, true, false)), std::optional(QRect(3, 0, 13, 16)));
    // A staircase: the widest reach comes from middle rows.
    const QImage stairs = painted(8, 3, [](int x, int y) {
        const int from = std::array{1, 5, 3}[size_t(y)];
        return x >= from && x < from + 2 ? Colour{255, 255, 255, 255} : Colour{0, 0, 0, 255};
    });
    QCOMPARE(ImageTrim::calculateTrimRect(stairs, edges(TrimBasedOn::topLeftPixelColor, true, true, true, true)), std::optional(QRect(1, 0, 6, 3)));
}

void ImageTrimTests::calculateTrimRectBottomRightPixelColor()
{
    // Magenta along the bottom and right four, cyan elsewhere.
    const QImage image = painted(16, 16, [](int x, int y) { return x >= 12 || y >= 12 ? Colour{255, 0, 255, 255} : Colour{0, 255, 255, 255}; });
    QCOMPARE(ImageTrim::calculateTrimRect(image, edges(TrimBasedOn::bottomRightPixelColor, true, true, true, true)), std::optional(QRect(0, 0, 12, 12)));
    // Matches count from a row's ends alone.
    const QImage holed = painted(8, 3, [](int x, int y) { return y == 1 && (x == 1 || x == 6) ? Colour{9, 9, 9, 255} : Colour{0, 0, 0, 255}; });
    QCOMPARE(ImageTrim::calculateTrimRect(holed, edges(TrimBasedOn::bottomRightPixelColor, true, true, true, true)), std::optional(QRect(1, 1, 6, 1)));
}

void ImageTrimTests::uniformImageReturnsNil()
{
    const QImage solid = painted(8, 8, [](int, int) { return Colour{100, 150, 200, 255}; });
    QCOMPARE(ImageTrim::calculateTrimRect(solid, edges(TrimBasedOn::topLeftPixelColor, true, true, true, true)), std::nullopt);
    const QImage clear = painted(8, 8, [](int, int) { return Colour{0, 0, 0, 0}; });
    QCOMPARE(ImageTrim::calculateTrimRect(clear, TrimOptions()), std::nullopt);
}

// Swift's trimImage, uncalled in the app: the rect it crops.
void ImageTrimTests::trimImageDirectly()
{
    const QImage image = painted(10, 10, [](int x, int y) { return x >= 2 && x < 8 && y >= 2 && y < 8 ? Colour{255, 128, 0, 255} : Colour{0, 0, 0, 0}; });
    QCOMPARE(ImageTrim::calculateTrimRect(image, TrimOptions()), std::optional(QRect(2, 2, 6, 6)));
}

// Swift's session trim, uncalled: the controller's two steps.
void ImageTrimTests::sessionTrimIntegrationAndUndo()
{
    Desk desk;
    EditorSession &session = desk.session;
    QCOMPARE(session.activeLayer().value().origin(), QPointF(20, 30));
    const std::optional<ProjectSnapshot> trimmed = ImageTrim::trim(session.projectSnapshot().value(), TrimOptions());
    session.applyDocumentSize(trimmed.value(), QStringLiteral("Trim"));
    QVERIFY(session.document().value().width == 40 && session.document().value().height == 40);
    QCOMPARE(session.activeLayer().value().origin(), QPointF(0, 0));
    QCOMPARE(session.history.undoName(), QString("Trim"));
    session.undo();
    QVERIFY(session.document().value().width == 100 && session.document().value().height == 100);
    QCOMPARE(session.activeLayer().value().origin(), QPointF(20, 30));
    // A canvas with nothing to cut comes back whole.
    session.createDocument(4, 4);
    session.insert(ImportedImage(painted(4, 4, [](int, int) { return Colour{1, 2, 3, 255}; }), QImage(), "Full"));
    // The snapshot comes back whole, effects and all.
    session.setEffects(LayerEffects{.stroke = StrokeEffect()});
    const ProjectSnapshot full = session.projectSnapshot().value();
    QVERIFY(full.manifest.layers[0].effects);
    QVERIFY(ImageTrim::trim(full, TrimOptions()).value().manifest.encoded() == full.manifest.encoded());
    // A clear canvas has nothing left.
    session.createDocument(4, 4);
    QCOMPARE(ImageTrim::trim(session.projectSnapshot().value(), TrimOptions()).has_value(), false);
}

void ImageTrimTests::toleranceAndAlphaDecideAMatch()
{
    // A frame just off the corner, then a half-clear one.
    const QImage image = painted(6, 6, [](int x, int y) {
        if (x == 0 && y == 0)
            return Colour{10, 10, 10, 255};
        if (x == 0 || y == 0)
            return Colour{12, 9, 10, 255};
        if (x == 1 || y == 1)
            return Colour{10, 10, 10, 128};
        return Colour{200, 0, 0, 255};
    });
    TrimOptions options = edges(TrimBasedOn::topLeftPixelColor, true, false, true, false);
    QCOMPARE(ImageTrim::calculateTrimRect(image, options), std::optional(QRect(0, 0, 6, 6)));
    options.tolerance = 2;
    QCOMPARE(ImageTrim::calculateTrimRect(image, options), std::optional(QRect(1, 1, 5, 5)));
    // Alpha is a channel like the others.
    options.tolerance = 127;
    QCOMPARE(ImageTrim::calculateTrimRect(image, options), std::optional(QRect(2, 2, 4, 4)));
    QCOMPARE(rawValue(TrimBasedOn::transparentPixels), QString("Transparent Pixels"));
    QCOMPARE(rawValue(TrimBasedOn::topLeftPixelColor), QString("Top Left Pixel Color"));
    QCOMPARE(rawValue(TrimBasedOn::bottomRightPixelColor), QString("Bottom Right Pixel Color"));
    QVERIFY(TrimOptions() == edges(TrimBasedOn::transparentPixels, true, true, true, true) && TrimOptions().tolerance == 0);
}

void ImageTrimTests::theSheetSaysSwiftsWordsAndReturnsItsChoice()
{
    std::optional<std::optional<TrimOptions>> answer;
    TrimSheet sheet([&answer](std::optional<TrimOptions> options) { answer = options; });
    sheet.show();
    QCOMPARE(sheet.width(), 320);
    QStringList words;
    for (const QLabel *label : sheet.findChildren<QLabel *>())
        words << label->text();
    QCOMPARE(words, (QStringList{"Trim", "Based On", "Trim Away"}));
    QCOMPARE(sheet.findChildren<QLabel *>().at(0)->font().pixelSize(), 17);
    QVERIFY(sheet.findChildren<QLabel *>().at(0)->font().bold() && sheet.findChildren<QLabel *>().at(1)->font().weight() == QFont::DemiBold);
    // Swift's defaults: transparent pixels, every edge.
    for (const TrimBasedOn option : allTrimBasedOn) {
        auto *radio = sheet.findChild<QRadioButton *>(QStringLiteral("trimBasedOn%1").arg(int(option)));
        QVERIFY(radio && radio->text() == rawValue(option) && radio->isChecked() == (option == TrimBasedOn::transparentPixels));
    }
    auto &ok = *sheet.findChild<QPushButton *>("trimOK");
    QVERIFY(ok.isDefault() && ok.isEnabled() && !sheet.findChild<QPushButton *>("trimCancel")->autoDefault());
    for (const char *edge : {"trimTop", "trimBottom", "trimLeft", "trimRight"})
        QVERIFY(sheet.findChild<QCheckBox *>(edge)->isChecked());
    // OK rests only while every edge is off.
    for (const char *edge : {"trimTop", "trimBottom", "trimLeft"})
        sheet.findChild<QCheckBox *>(edge)->click();
    QVERIFY(ok.isEnabled());
    sheet.findChild<QCheckBox *>("trimRight")->click();
    QVERIFY(!ok.isEnabled());
    sheet.findChild<QCheckBox *>("trimBottom")->click();
    sheet.findChild<QRadioButton *>("trimBasedOn2")->click();
    ok.click();
    QVERIFY(answer.value() == edges(TrimBasedOn::bottomRightPixelColor, false, true, false, false));
    sheet.findChild<QPushButton *>("trimCancel")->click();
    QVERIFY(answer && !answer.value().has_value());
}

void ImageTrimTests::theControllerTrimsThroughTheSheetAsOneStep()
{
    Desk desk;
    const int steps = desk.session.history.undoCount();
    desk.controller.trim(desk.finished());
    QVERIFY(desk.session.isProjectBusy());
    QCOMPARE(desk.dialog().windowTitle(), QString("Trim"));
    QVERIFY(desk.dialog().isVisible() && desk.dialog().windowModality() == Qt::WindowModal);
    QVERIFY(desk.dialog().findChild<TrimSheet *>());
    // Inside the dialog Cancel still defaults to nothing.
    QVERIFY(!desk.dialog().findChild<QPushButton *>("trimCancel")->autoDefault());
    // Return is OK: the canvas shrinks to the square.
    QTest::keyClick(&desk.dialog(), Qt::Key_Return);
    QTRY_VERIFY(desk.done);
    QVERIFY(desk.session.document().value().width == 40 && desk.session.document().value().height == 40);
    QCOMPARE(desk.session.history.undoCount(), steps + 1);
    QCOMPARE(desk.session.history.undoName(), QString("Trim"));
    QVERIFY(!desk.session.isProjectBusy());
    QTRY_VERIFY(!desk.window.findChild<QDialog *>());
}

void ImageTrimTests::nothingToTrimOrACancelChangesNothing()
{
    Desk desk;
    const CanvasDocument before = desk.session.document().value();
    const int steps = desk.session.history.undoCount();
    desk.controller.trim(desk.finished());
    QTest::keyClick(&desk.dialog(), Qt::Key_Escape);
    QTRY_VERIFY(desk.done);
    QVERIFY(desk.session.document().value() == before && desk.session.history.undoCount() == steps && !desk.session.isProjectBusy());
    // Everything matches the corner: no alert, no step.
    DialogDesk alerts;
    desk.session.createDocument(8, 8);
    desk.session.insert(ImportedImage(painted(8, 8, [](int, int) { return Colour{5, 5, 5, 255}; }), QImage(), "Flat"));
    const CanvasDocument flat = desk.session.document().value();
    const int flatSteps = desk.session.history.undoCount();
    desk.controller.trim(desk.finished());
    desk.dialog().findChild<QRadioButton *>("trimBasedOn1")->click();
    desk.dialog().findChild<QPushButton *>("trimOK")->click();
    QTRY_VERIFY(desk.done);
    QVERIFY(desk.session.document().value() == flat && desk.session.history.undoCount() == flatSteps);
    QVERIFY(alerts.seen.isEmpty() && !desk.session.isProjectBusy());
    // Busy, or without a window, no sheet opens.
    desk.session.setIsProjectBusy(true);
    desk.controller.trim(desk.finished());
    QTRY_VERIFY(desk.done);
    QVERIFY(!desk.window.findChild<QDialog *>());
    desk.session.setIsProjectBusy(false);
    desk.controller.window = nullptr;
    desk.controller.trim(desk.finished());
    QTRY_VERIFY(desk.done);
    QVERIFY(!desk.session.isProjectBusy());
}

void ImageTrimTests::aTrimThatFailsIsExplained()
{
    Desk desk;
    // Past the render's one surface, the trim cannot look.
    desk.session.createDocument(20'001, 10'000);
    const CanvasDocument before = desk.session.document().value();
    DialogDesk alerts;
    alerts.replies = {"OK"};
    alerts.note = [&] { return QStringLiteral("busy %1").arg(desk.session.isProjectBusy()); };
    desk.controller.trim(desk.finished());
    desk.dialog().findChild<QPushButton *>("trimOK")->click();
    QTRY_VERIFY(desk.done);
    QCOMPARE(alerts.seen, (QStringList{QStringLiteral("alert|2|Couldn’t trim image|%1|OK|busy 1")
                                           .arg(QString::fromUtf8(ExportError(ExportError::Kind::tooLarge).what()))}));
    QVERIFY(desk.session.document().value() == before && !desk.session.isProjectBusy());
}

QTEST_MAIN(ImageTrimTests)
#include "ImageTrimTests.moc"
