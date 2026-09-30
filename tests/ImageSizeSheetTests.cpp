#include "Document/DocumentLimits.h"
#include "UI/ColorPickerSheet.h"
#include "UI/ImageSizeSheet.h"
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QApplication>
#include <QWheelEvent>
#include <QtTest>

// Swift's ImageSizeSheet: pixels resampled or print size alone.
class ImageSizeSheetTests : public QObject {
    Q_OBJECT
private slots:
    void theSheetShowsTheCanvas();
    void pixelsFollowUnitsAndTheLock();
    void aNewResolutionKeepsThePrintSize();
    void aDocumentsOwnResolutionIsTheLastUsable();
    void withoutResamplingOnlyTheResolutionMoves();
    void anInvalidSizeRestsResize();
    void returnWithoutTypingKeepsTheNumbers();
    void aWheelOverUnitsCommitsTheTypingFirst();
    void halfwayNumbersRoundToEven();
};

namespace {
CanvasDocument document(int width, int height, double resolution)
{
    CanvasDocument made{width, height};
    made.resolution = resolution;
    return made;
}

struct Sheet {
    std::optional<std::optional<ImageSizeOptions>> answer;
    ImageSizeSheet sheet;
    explicit Sheet(int width = 1200, int height = 800, double resolution = 72)
        : sheet(document(width, height, resolution), [this](std::optional<ImageSizeOptions> options) { answer = options; })
    {
        sheet.show();
    }
    template <typename Widget> Widget &find(const char *name)
    {
        Widget *found = sheet.findChild<Widget *>(QString::fromLatin1(name));
        if (!found)
            throw std::runtime_error(std::string("no widget named ") + name);
        return *found;
    }
    void type(const char *name, const QString &text)
    {
        auto &field = find<PickerField>(name);
        field.setFocus();
        field.selectAll();
        QTest::keyClicks(&field, text);
        QTest::keyClick(&field, Qt::Key_Return);
    }
    void unit(const QString &name)
    {
        auto &units = find<QComboBox>("imageUnits");
        const int index = units.findText(name);
        units.setCurrentIndex(index);
        emit units.activated(index);
    }
    QString shown(const char *name) { return find<PickerField>(name).text(); }
    QString result() { return find<QLabel>("imageResult").text(); }
    QStringList units()
    {
        QStringList names;
        auto &units = find<QComboBox>("imageUnits");
        for (int index = 0; index < units.count(); ++index)
            names << units.itemText(index);
        return names;
    }
};
}

void ImageSizeSheetTests::theSheetShowsTheCanvas()
{
    Sheet shown;
    QStringList texts;
    for (const QLabel *label : shown.sheet.findChildren<QLabel *>())
        texts << label->text();
    QVERIFY(texts.contains("Image Size"));
    QVERIFY(texts.contains("Current: 1,200 × 800 pixels"));
    QVERIFY(texts.contains("pixels/inch"));
    QCOMPARE(shown.sheet.width(), 430);
    QCOMPARE(shown.units(), (QStringList{"Pixels", "Percent", "Inches", "Centimeters"}));
    QCOMPARE(shown.shown("imageWidth"), QString("1200"));
    QCOMPARE(shown.shown("imageResolution"), QString("72"));
    QVERIFY(shown.find<QCheckBox>("imageLocked").isChecked());
    QVERIFY(shown.find<QCheckBox>("imageResample").isChecked());
    auto &sampling = shown.find<QComboBox>("imageSampling");
    QCOMPARE(sampling.currentText(), QString("High quality"));
    QCOMPARE(sampling.count(), 3);
    QCOMPARE(shown.find<QLabel>("imageExplanation").text(), QString("Resizes layer pixels and applies existing transforms. Undo restores the originals."));
    QCOMPARE(shown.result(), QString("Result: 1,200 × 800 pixels"));
    // Cancel answers with nothing; Resize with the size.
    shown.find<QPushButton>("imageCancel").click();
    QVERIFY(!shown.answer.value().has_value());
    emit sampling.activated(0);
    shown.find<QPushButton>("imageResize").click();
    const ImageSizeOptions options = shown.answer.value().value();
    QVERIFY(options.width == 1200 && options.height == 800 && options.resolution == 72 && options.sampling == LayerSampling::nearest);
}

void ImageSizeSheetTests::pixelsFollowUnitsAndTheLock()
{
    Sheet shown;
    shown.type("imageWidth", "600");
    QCOMPARE(shown.shown("imageHeight"), QString("400"));
    shown.find<QCheckBox>("imageLocked").click();
    shown.type("imageHeight", "500");
    QCOMPARE(shown.shown("imageWidth"), QString("600"));
    QCOMPARE(shown.result(), QString("Result: 600 × 500 pixels"));
    shown.unit("Percent");
    QCOMPARE(shown.shown("imageWidth"), QString("50"));
    QCOMPARE(shown.shown("imageHeight"), QString("62.5"));
    shown.type("imageWidth", "25");
    QCOMPARE(shown.result(), QString("Result: 300 × 500 pixels"));
    shown.unit("Inches");
    QCOMPARE(shown.shown("imageWidth"), QString("4.167"));
    shown.type("imageWidth", "10");
    QCOMPARE(shown.result(), QString("Result: 720 × 500 pixels"));
    shown.unit("Centimeters");
    shown.type("imageWidth", "2.54");
    QCOMPARE(shown.result(), QString("Result: 72 × 500 pixels"));
    QCOMPARE(shown.shown("imageHeight"), QString("17.639"));
    // Percent counts each side against its own original.
    shown.unit("Percent");
    shown.type("imageHeight", "50");
    QCOMPARE(shown.result(), QString("Result: 72 × 400 pixels"));
    // Locked, a height takes the width along.
    shown.find<QCheckBox>("imageLocked").click();
    shown.unit("Pixels");
    shown.type("imageHeight", "800");
    QCOMPARE(shown.result(), QString("Result: 144 × 800 pixels"));
    shown.find<QCheckBox>("imageLocked").click();
    shown.type("imageWidth", "600.6");
    shown.find<QPushButton>("imageResize").click();
    QCOMPARE(shown.answer.value().value().width, 601);
    // Nothing, no number and zero are refused, as Swift's guard.
    for (const QString &refused : {QString("0"), QString("-3"), QString("wide")}) {
        shown.type("imageWidth", refused);
        QCOMPARE(shown.result(), QString("Result: 601 × 800 pixels"));
    }
}

void ImageSizeSheetTests::aNewResolutionKeepsThePrintSize()
{
    Sheet shown;
    // In physical units the pixels follow the resolution.
    shown.unit("Inches");
    shown.type("imageResolution", "144");
    QCOMPARE(shown.result(), QString("Result: 2,400 × 1,600 pixels"));
    QCOMPARE(shown.shown("imageWidth"), QString("16.667"));
    // In pixels they stay.
    shown.unit("Pixels");
    shown.type("imageResolution", "300");
    QCOMPARE(shown.result(), QString("Result: 2,400 × 1,600 pixels"));
    shown.find<QPushButton>("imageResize").click();
    QCOMPARE(shown.answer.value().value().resolution, 300.0);
    // Swift's 555417e: zero and below keep 8 by 5.333 inches.
    shown.unit("Inches");
    QCOMPARE(shown.shown("imageWidth"), QString("8"));
    shown.type("imageResolution", "0");
    QCOMPARE(shown.result(), QStringLiteral("Use 1–30,000 pixels per side, up to %1 megapixels, and 1–9,600 pixels/inch.").arg(DocumentLimits::maxSurfaceMegapixels()));
    // A size typed meanwhile is ignored.
    shown.type("imageWidth", "2");
    shown.type("imageResolution", "-5");
    shown.type("imageHeight", "3");
    shown.type("imageResolution", "150");
    QCOMPARE(shown.result(), QString("Result: 1,200 × 800 pixels"));
    QCOMPARE(shown.shown("imageWidth"), QString("8"));
    // Centimetres keep the print size alike.
    shown.unit("Centimeters");
    shown.type("imageResolution", "0");
    shown.type("imageResolution", "300");
    QCOMPARE(shown.result(), QString("Result: 2,400 × 1,600 pixels"));
}

void ImageSizeSheetTests::aDocumentsOwnResolutionIsTheLastUsable()
{
    for (const QString &unit : {QStringLiteral("Inches"), QStringLiteral("Centimeters")}) {
        Sheet shown(1200, 800, 300);
        shown.unit(unit);
        shown.type("imageResolution", "0");
        shown.type("imageWidth", "1");
        shown.type("imageResolution", "-5");
        shown.type("imageResolution", "150");
        QCOMPARE(shown.result(), QString("Result: 600 × 400 pixels"));
    }
    // Pixels and Percent need no resolution: those edits apply.
    for (const auto &[unit, width] : {std::pair(QStringLiteral("Pixels"), QStringLiteral("600")), std::pair(QStringLiteral("Percent"), QStringLiteral("50"))}) {
        Sheet shown(1200, 800, 300);
        shown.unit(unit);
        shown.type("imageResolution", "0");
        shown.type("imageWidth", width);
        shown.type("imageResolution", "150");
        QCOMPARE(shown.result(), QString("Result: 600 × 400 pixels"));
    }
    // Infinity is no usable resolution either.
    for (const QString &unit : {QStringLiteral("Inches"), QStringLiteral("Centimeters")}) {
        Sheet shown(1200, 800, 300);
        shown.unit(unit);
        shown.type("imageResolution", "inf");
        QVERIFY(!shown.find<QPushButton>("imageResize").isEnabled());
        shown.type("imageWidth", "1");
        shown.type("imageResolution", "150");
        QCOMPARE(shown.result(), QString("Result: 600 × 400 pixels"));
        shown.find<QPushButton>("imageResize").click();
        const ImageSizeOptions options = shown.answer.value().value();
        QVERIFY(options.width == 600 && options.height == 400 && options.resolution == 150);
    }
    // Without resampling, print sizes wait for a usable resolution too.
    for (const QString &unit : {QStringLiteral("Inches"), QStringLiteral("Centimeters")}) {
        for (const QString &invalid : {QStringLiteral("0"), QStringLiteral("-5"), QStringLiteral("inf")}) {
            Sheet held(1200, 800, 300);
            held.unit(unit);
            held.find<QCheckBox>("imageResample").click();
            held.type("imageResolution", invalid);
            const QString shownResolution = held.shown("imageResolution");
            held.type("imageWidth", "1");
            held.type("imageHeight", "2");
            QCOMPARE(held.shown("imageResolution"), shownResolution);
            QVERIFY(!held.find<QPushButton>("imageResize").isEnabled());
            held.type("imageResolution", "150");
            QCOMPARE(held.result(), QString("Result: 1,200 × 800 pixels"));
            held.find<QCheckBox>("imageResample").click();
            held.type("imageResolution", "300");
            QCOMPARE(held.result(), QString("Result: 2,400 × 1,600 pixels"));
        }
    }
    // Any finite positive resolution counts, even past Resize's range.
    for (const QString &unit : {QStringLiteral("Inches"), QStringLiteral("Centimeters")}) {
        Sheet low(12, 8, 300);
        low.type("imageResolution", "0.5");
        low.unit(unit);
        low.type("imageResolution", "150");
        QCOMPARE(low.result(), QString("Result: 3,600 × 2,400 pixels"));
        Sheet high(300, 200, 300);
        high.type("imageResolution", "12000");
        high.unit(unit);
        high.type("imageResolution", "6000");
        QCOMPARE(high.result(), QString("Result: 150 × 100 pixels"));
    }
    // Without resampling the pixels stay, across the toggle.
    Sheet shown(1200, 800, 300);
    shown.type("imageResolution", "0");
    shown.find<QCheckBox>("imageResample").click();
    shown.type("imageResolution", "150");
    QCOMPARE(shown.result(), QString("Result: 1,200 × 800 pixels"));
    shown.find<QCheckBox>("imageResample").click();
    shown.type("imageResolution", "300");
    QCOMPARE(shown.result(), QString("Result: 2,400 × 1,600 pixels"));
}

void ImageSizeSheetTests::withoutResamplingOnlyTheResolutionMoves()
{
    Sheet shown;
    shown.type("imageWidth", "600");
    shown.find<QCheckBox>("imageLocked").click();
    shown.find<QCheckBox>("imageResample").click();
    // The pixels go back; physical units alone; locked.
    QCOMPARE(shown.units(), (QStringList{"Inches", "Centimeters"}));
    QCOMPARE(shown.find<QComboBox>("imageUnits").currentText(), QString("Inches"));
    QVERIFY(shown.find<QCheckBox>("imageLocked").isChecked());
    QVERIFY(!shown.find<QCheckBox>("imageLocked").isEnabled());
    QVERIFY(!shown.find<QWidget>("imageSamplingRow").isVisible());
    QCOMPARE(shown.find<QLabel>("imageExplanation").text(), QString("Only print dimensions and resolution change. Pixels stay unchanged."));
    QCOMPARE(shown.result(), QString("Result: 1,200 × 800 pixels"));
    shown.type("imageWidth", "10");
    QCOMPARE(shown.shown("imageResolution"), QString("120"));
    QCOMPARE(shown.shown("imageHeight"), QString("6.667"));
    shown.unit("Centimeters");
    shown.type("imageHeight", "25.4");
    QCOMPARE(shown.shown("imageResolution"), QString("80"));
    shown.find<QPushButton>("imageResize").click();
    const ImageSizeOptions options = shown.answer.value().value();
    QVERIFY(options.width == 1200 && options.height == 800 && options.resolution == 80);
    // Without resampling a resolution moves no pixels.
    shown.type("imageResolution", "160");
    QCOMPARE(shown.result(), QString("Result: 1,200 × 800 pixels"));
    // Back on, the units return; the pixels are kept.
    shown.find<QCheckBox>("imageResample").click();
    QCOMPARE(shown.units(), (QStringList{"Pixels", "Percent", "Inches", "Centimeters"}));
    QCOMPARE(shown.find<QComboBox>("imageUnits").currentText(), QString("Centimeters"));
    QVERIFY(shown.find<QCheckBox>("imageLocked").isEnabled());
    // A print-size edit updates the last usable resolution.
    shown.find<QCheckBox>("imageResample").click();
    shown.type("imageHeight", "25.4");
    QCOMPARE(shown.shown("imageResolution"), QString("80"));
    shown.find<QCheckBox>("imageResample").click();
    shown.type("imageResolution", "160");
    QCOMPARE(shown.result(), QString("Result: 2,400 × 1,600 pixels"));
}

void ImageSizeSheetTests::anInvalidSizeRestsResize()
{
    Sheet shown;
    auto &resize = shown.find<QPushButton>("imageResize");
    QVERIFY(resize.isDefault() && resize.isEnabled());
    shown.find<QCheckBox>("imageLocked").click();
    shown.type("imageWidth", "30001");
    QCOMPARE(shown.result(), QStringLiteral("Use 1–30,000 pixels per side, up to %1 megapixels, and 1–9,600 pixels/inch.").arg(DocumentLimits::maxSurfaceMegapixels()));
    QCOMPARE(shown.find<QLabel>("imageResult").foregroundRole(), QPalette::BrightText);
    QVERIFY(!resize.isEnabled());
    // Past one surface's 200 megapixels while resampling.
    shown.type("imageWidth", "20000");
    shown.type("imageHeight", "10001");
    QVERIFY(!resize.isEnabled());
    shown.type("imageHeight", "10000");
    QVERIFY(resize.isEnabled());
    QCOMPARE(shown.find<QLabel>("imageResult").foregroundRole(), QPalette::PlaceholderText);
    shown.type("imageResolution", "9601");
    QVERIFY(!resize.isEnabled());
    shown.type("imageResolution", "9600");
    QVERIFY(resize.isEnabled());
    shown.type("imageResolution", "0.5");
    QVERIFY(!resize.isEnabled());
    shown.type("imageResolution", "72");
    shown.type("imageWidth", "30000");
    shown.type("imageHeight", "1");
    QVERIFY(resize.isEnabled());
    // Without resampling the pixels are the canvas's, however many.
    Sheet huge(30'000, 30'000);
    QVERIFY(!huge.find<QPushButton>("imageResize").isEnabled());
    huge.find<QCheckBox>("imageResample").click();
    QVERIFY(huge.find<QPushButton>("imageResize").isEnabled());
}

void ImageSizeSheetTests::returnWithoutTypingKeepsTheNumbers()
{
    Sheet shown;
    shown.find<QCheckBox>("imageResample").click();
    // A size shown to three places would move the resolution.
    auto &width = shown.find<PickerField>("imageWidth");
    width.setFocus();
    QTest::keyClick(&width, Qt::Key_Return);
    QCOMPARE(shown.shown("imageResolution"), QString("72"));
    shown.type("imageWidth", "7");
    auto &resolution = shown.find<PickerField>("imageResolution");
    QCOMPARE(resolution.text(), QString("171.429"));
    resolution.setFocus();
    QTest::keyClick(&resolution, Qt::Key_Return);
    shown.find<QPushButton>("imageResize").click();
    QCOMPARE(shown.answer.value().value().resolution, 1200.0 / 7);
}

// Scrolled, Units take no focus; the typing meant pixels.
void ImageSizeSheetTests::aWheelOverUnitsCommitsTheTypingFirst()
{
    Sheet shown;
    // Focus moves only in the active window.
    shown.sheet.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&shown.sheet));
    auto &width = shown.find<PickerField>("imageWidth");
    width.setFocus();
    width.selectAll();
    QTest::keyClicks(&width, "600");
    auto &units = shown.find<QComboBox>("imageUnits");
    QWheelEvent wheel(QPointF(5, 5), units.mapToGlobal(QPointF(5, 5)), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&units, &wheel);
    QCOMPARE(units.currentText(), QString("Percent"));
    QCOMPARE(width.text(), QString("50"));
    QCOMPARE(shown.result(), QString("Result: 600 × 400 pixels"));
}

// Foundation rounds halves to even: 17 pixels, 16 per inch.
void ImageSizeSheetTests::halfwayNumbersRoundToEven()
{
    CanvasDocument canvas{17, 19};
    canvas.resolution = 16;
    ImageSizeSheet sheet(canvas, [](std::optional<ImageSizeOptions>) {});
    auto &units = *sheet.findChild<QComboBox *>(QStringLiteral("imageUnits"));
    const int inches = units.findText(QStringLiteral("Inches"));
    units.setCurrentIndex(inches);
    emit units.activated(inches);
    QCOMPARE(sheet.findChild<PickerField *>(QStringLiteral("imageWidth"))->text(), QString("1.062"));
    QCOMPARE(sheet.findChild<PickerField *>(QStringLiteral("imageHeight"))->text(), QString("1.188"));
}

QTEST_MAIN(ImageSizeSheetTests)
#include "ImageSizeSheetTests.moc"
