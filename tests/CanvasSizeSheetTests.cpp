#include "UI/CanvasSizeSheet.h"
#include "UI/ColorPaletteControls.h"
#include "UI/ColorPickerSheet+Dialog.h"
#include "UI/ColorPickerSheet.h"
#include <QCheckBox>
#include <QHBoxLayout>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QApplication>
#include <QWheelEvent>
#include <QtTest>

// Swift's CanvasSizeSheet: fields, units, anchor, extension, answers.
class CanvasSizeSheetTests : public QObject {
    Q_OBJECT
private slots:
    void theSheetShowsTheCanvasAndItsMemory();
    void fieldsSetTheDraftInItsUnits();
    void anInvalidSizeRestsOK();
    void theAnchorAndTheExtensionGoWithOK();
    void aCustomColorComesFromTheAppsPicker();
    void theAnchorsDrawTheirCircles();
    void aFieldKeepsItsTypingAndItsNumber();
    void aWheelOverUnitsCommitsTheTypingFirst();
    void halfwayNumbersRoundToEven();
};

namespace {
CanvasDocument document(qint64 width, qint64 height)
{
    CanvasDocument canvas{int(width), int(height)};
    canvas.resolution = 72;
    return canvas;
}

// A session whose palette holds these two colours.
EditorSession &painted(EditorSession &session, PaletteColor foreground, PaletteColor background)
{
    session.setPaletteColor(foreground, false);
    session.setPaletteColor(background, true);
    return session;
}

struct Sheet {
    std::optional<std::optional<CanvasSizeOptions>> answer;
    EditorSession session;
    CanvasSizeSheet sheet;
    explicit Sheet(qint64 width = 1200, qint64 height = 800, PaletteColor foreground = PaletteColor::black(),
                   PaletteColor background = PaletteColor::white())
        : sheet(document(width, height), painted(session, foreground, background),
                [this](std::optional<CanvasSizeOptions> options) { answer = options; })
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
    // Typed, then Return: Swift's value binding commits.
    void type(const char *name, const QString &text)
    {
        auto &field = find<PickerField>(name);
        field.setFocus();
        field.selectAll();
        QTest::keyClicks(&field, text);
        QTest::keyClick(&field, Qt::Key_Return);
    }
    QString note() { return find<QLabel>("canvasNote").text(); }
    QString shown(const char *name) { return find<PickerField>(name).text(); }
};

QStringList labels(const QWidget &sheet)
{
    QStringList texts;
    for (const QLabel *label : sheet.findChildren<QLabel *>())
        texts << label->text();
    return texts;
}
}

void CanvasSizeSheetTests::theSheetShowsTheCanvasAndItsMemory()
{
    Sheet shown;
    const QStringList texts = labels(shown.sheet);
    QVERIFY(texts.contains("Canvas Size"));
    QVERIFY(texts.contains("Current: 1,200 × 800 pixels"));
    QVERIFY(texts.contains("3.7 MB uncompressed RGBA canvas"));
    QCOMPARE(shown.sheet.width(), 450);
    QCOMPARE(shown.shown("canvasWidth"), QString("1200"));
    QCOMPARE(shown.shown("canvasHeight"), QString("800"));
    QCOMPARE(shown.note(), QString("New: 1,200 × 800 pixels · 3.7 MB uncompressed"));
    // Foundation's memory counts: bytes, whole KB, one place, two.
    QVERIFY(labels(Sheet(1, 1).sheet).contains("4 bytes uncompressed RGBA canvas"));
    QVERIFY(labels(Sheet(16, 16).sheet).contains("1 KB uncompressed RGBA canvas"));
    QVERIFY(labels(Sheet(250, 1).sheet).contains("1,000 bytes uncompressed RGBA canvas"));
    QVERIFY(labels(Sheet(2560, 1).sheet).contains("10 KB uncompressed RGBA canvas"));
    QVERIFY(labels(Sheet(512, 500).sheet).contains("1,000 KB uncompressed RGBA canvas"));
    QVERIFY(labels(Sheet(1024, 512).sheet).contains("2 MB uncompressed RGBA canvas"));
    QVERIFY(labels(Sheet(30'000, 30'000).sheet).contains("3.35 GB uncompressed RGBA canvas"));
    QVERIFY(labels(Sheet(23'170, 23'170).sheet).contains("2 GB uncompressed RGBA canvas"));
}

void CanvasSizeSheetTests::fieldsSetTheDraftInItsUnits()
{
    Sheet shown;
    shown.type("canvasWidth", "1600");
    QCOMPARE(shown.shown("canvasWidth"), QString("1600"));
    QCOMPARE(shown.shown("canvasHeight"), QString("800"));
    QCOMPARE(shown.note(), QString("New: 1,600 × 800 pixels · 4.9 MB uncompressed"));
    // Locking takes the width's proportions at once.
    shown.find<QCheckBox>("canvasLocked").click();
    QCOMPARE(shown.shown("canvasHeight"), QString("1066.667"));
    shown.type("canvasHeight", "400");
    QCOMPARE(shown.shown("canvasWidth"), QString("600"));
    // Relative fields show what is added; units convert.
    shown.find<QCheckBox>("canvasRelative").click();
    QCOMPARE(shown.shown("canvasWidth"), QString("-600"));
    QCOMPARE(shown.shown("canvasHeight"), QString("-400"));
    shown.type("canvasWidth", "0");
    QCOMPARE(shown.shown("canvasHeight"), QString("0"));
    auto &units = shown.find<QComboBox>("canvasUnits");
    QCOMPARE(units.count(), 4);
    units.setCurrentIndex(1);
    emit units.activated(1);
    shown.type("canvasWidth", "50");
    QCOMPARE(shown.note(), QString("New: 1,800 × 1,200 pixels · 8.2 MB uncompressed"));
    shown.find<QCheckBox>("canvasRelative").click();
    units.setCurrentIndex(2);
    emit units.activated(2);
    QCOMPARE(shown.shown("canvasWidth"), QString("25"));
    units.setCurrentIndex(3);
    emit units.activated(3);
    QCOMPARE(shown.shown("canvasWidth"), QString("63.5"));
    // Text that is no number goes back to the draft's.
    shown.type("canvasWidth", "wide");
    QCOMPARE(shown.shown("canvasWidth"), QString("63.5"));
    units.setCurrentIndex(0);
    emit units.activated(0);
    QCOMPARE(shown.shown("canvasWidth"), QString("1800"));
}

void CanvasSizeSheetTests::anInvalidSizeRestsOK()
{
    Sheet shown;
    auto &ok = shown.find<QPushButton>("canvasOK");
    QVERIFY(ok.isEnabled() && ok.isDefault());
    shown.type("canvasWidth", "30001");
    QCOMPARE(shown.note(), QString("Final dimensions must be 1–30,000 pixels per side."));
    QCOMPARE(shown.find<QLabel>("canvasNote").foregroundRole(), QPalette::BrightText);
    QVERIFY(!ok.isEnabled());
    shown.type("canvasWidth", "0.4");
    QVERIFY(!ok.isEnabled());
    shown.type("canvasWidth", "0.5");
    QVERIFY(ok.isEnabled());
    QCOMPARE(shown.find<QLabel>("canvasNote").foregroundRole(), QPalette::PlaceholderText);
    // Cancel answers with nothing.
    shown.find<QPushButton>("canvasCancel").click();
    QVERIFY(!shown.answer.value().has_value());
}

void CanvasSizeSheetTests::theAnchorAndTheExtensionGoWithOK()
{
    Sheet shown(1200, 800, PaletteColor{1, 0, 0}, PaletteColor{0, 0, 1});
    QCOMPARE(shown.find<QLabel>("anchorName").text(), QString("Center"));
    QCOMPARE(shown.find<QToolButton>("anchor4").accessibleDescription(), QString("Selected"));
    shown.find<QToolButton>("anchor0").click();
    QCOMPARE(shown.find<QLabel>("anchorName").text(), QString("Top left"));
    QCOMPARE(shown.find<QToolButton>("anchor0").toolTip(), QString("Top left"));
    QCOMPARE(shown.find<QToolButton>("anchor0").accessibleDescription(), QString("Selected"));
    QVERIFY(shown.find<QToolButton>("anchor4").accessibleDescription().isEmpty());
    auto &extension = shown.find<QComboBox>("canvasExtension");
    QStringList choices;
    for (int index = 0; index < extension.count(); ++index)
        choices << extension.itemText(index);
    QCOMPARE(choices, (QStringList{"Transparent", "Foreground", "Background", "Black", "White", "Custom"}));
    QVERIFY(!shown.find<QWidget>("extensionColorRow").isVisible());
    const std::pair<int, std::optional<std::array<double, 3>>> fills[] = {
        {0, std::nullopt}, {1, std::array<double, 3>{1, 0, 0}}, {2, std::array<double, 3>{0, 0, 1}}, {3, std::array<double, 3>{0, 0, 0}},
        {4, std::array<double, 3>{1, 1, 1}}};
    shown.type("canvasWidth", "1600.6");
    shown.type("canvasHeight", "799.5");
    for (const auto &[index, fill] : fills) {
        extension.setCurrentIndex(index);
        emit extension.activated(index);
        shown.find<QPushButton>("canvasOK").click();
        const CanvasSizeOptions options = shown.answer.value().value();
        QCOMPARE(options.width, 1601);
        QCOMPARE(options.height, 800);
        QCOMPARE(options.anchor, 0);
        QCOMPARE(options.fill.has_value(), fill.has_value());
        if (fill)
            QVERIFY(options.fill.value().red == fill.value()[0] && options.fill.value().green == fill.value()[1] && options.fill.value().blue == fill.value()[2]);
    }
}

void CanvasSizeSheetTests::aCustomColorComesFromTheAppsPicker()
{
    Sheet shown;
    auto &extension = shown.find<QComboBox>("canvasExtension");
    extension.setCurrentIndex(5);
    emit extension.activated(5);
    QVERIFY(shown.find<QWidget>("extensionColorRow").isVisible());
    QCOMPARE(qobject_cast<QHBoxLayout *>(shown.find<QWidget>("extensionColorRow").layout())->spacing(), 8);
    auto &swatch = shown.find<DialogColorSwatch>("extensionColor");
    QVERIFY(swatch.accessibleName() == QString("Extension Color") && swatch.toolTip() == QString("Color for the added canvas"));
    swatch.click();
    QCOMPARE(shown.session.colorPicker().value().target.title(), QString("Color Picker (Extension Color)"));
    QVERIFY(shown.session.colorPicker().value().original == PaletteColor::white());
    // OK takes the open picker's colour, closing it first.
    const PaletteColor sky{0, 128 / 255.0, 1};
    shown.session.setColorPickerHSB(PickerHSB(sky));
    QCOMPARE(swatch.grab().toImage().pixelColor(17, 9), QColor(0, 128, 255));
    shown.find<QPushButton>("canvasOK").click();
    QVERIFY(!shown.session.colorPicker());
    const CanvasExtensionColor fill = shown.answer.value().value().fill.value();
    QVERIFY(fill.red == 0 && fill.green == 128 / 255.0 && fill.blue == 1);
    // Chosen again, the picker opens on the colour kept.
    swatch.click();
    QVERIFY(shown.session.colorPicker().value().original == sky);
    shown.session.setColorPickerHSB(PickerHSB(PaletteColor{1, 0, 0}));
    shown.session.closeColorPicker(false);
    QCOMPARE(swatch.grab().toImage().pixelColor(17, 9), QColor(0, 128, 255));
    // Another extension hides the swatch: the picker goes, colour kept.
    swatch.click();
    shown.session.setColorPickerHSB(PickerHSB(PaletteColor{1, 0, 0}));
    extension.setCurrentIndex(0);
    emit extension.activated(0);
    QVERIFY(!shown.session.colorPicker());
    extension.setCurrentIndex(5);
    emit extension.activated(5);
    QCOMPARE(swatch.grab().toImage().pixelColor(17, 9), QColor(255, 0, 0));
    // Cancel and the sheet going close it too.
    swatch.click();
    shown.find<QPushButton>("canvasCancel").click();
    QVERIFY(!shown.session.colorPicker() && !shown.answer.value());
    EditorSession session;
    auto gone = std::make_unique<CanvasSizeSheet>(document(10, 10), session, [](std::optional<CanvasSizeOptions>) {});
    gone->findChild<DialogColorSwatch *>()->click();
    gone.reset();
    QVERIFY(!session.colorPicker());
}

void CanvasSizeSheetTests::theAnchorsDrawTheirCircles()
{
    Sheet shown;
    const QImage chosen = shown.find<QToolButton>("anchor4").grab().toImage();
    const QImage other = shown.find<QToolButton>("anchor0").grab().toImage();
    const QColor accent = shown.sheet.palette().color(QPalette::Highlight);
    // The chosen one filled in the accent; the others ringed.
    QCOMPARE(chosen.pixelColor(12, 12), accent);
    QCOMPARE(chosen.pixelColor(16, 12), accent);
    QVERIFY(other.pixelColor(12, 12) != accent);
    QVERIFY(other.pixelColor(12, 12) != other.pixelColor(17, 12));
    QCOMPARE(other.pixelColor(12, 12), other.pixelColor(20, 12));
}

void CanvasSizeSheetTests::aFieldKeepsItsTypingAndItsNumber()
{
    // Typing waits for Return while the sheet redraws.
    Sheet shown;
    auto &width = shown.find<PickerField>("canvasWidth");
    width.setFocus();
    width.selectAll();
    QTest::keyClicks(&width, "1234");
    emit shown.find<QComboBox>("canvasExtension").activated(1);
    QCOMPARE(width.text(), QString("1234"));
    QTest::keyClick(&width, Qt::Key_Return);
    QCOMPARE(shown.note(), QString("New: 1,234 × 800 pixels · 3.8 MB uncompressed"));
    // Return without typing keeps the number, not what it shows.
    Sheet thin(30'000, 1);
    thin.find<QCheckBox>("canvasLocked").click();
    thin.type("canvasWidth", "29999");
    QCOMPARE(thin.shown("canvasHeight"), QString("1"));
    auto &height = thin.find<PickerField>("canvasHeight");
    height.setFocus();
    QTest::keyClick(&height, Qt::Key_Return);
    QCOMPARE(thin.note(), QString("New: 29,999 × 1 pixels · 117 KB uncompressed"));
}

// Scrolled, Units take no focus; the typing meant pixels.
void CanvasSizeSheetTests::aWheelOverUnitsCommitsTheTypingFirst()
{
    Sheet shown;
    // Focus moves only in the active window.
    shown.sheet.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&shown.sheet));
    auto &width = shown.find<PickerField>("canvasWidth");
    width.setFocus();
    width.selectAll();
    QTest::keyClicks(&width, "600");
    auto &units = shown.find<QComboBox>("canvasUnits");
    QWheelEvent wheel(QPointF(5, 5), units.mapToGlobal(QPointF(5, 5)), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&units, &wheel);
    QCOMPARE(units.currentText(), QString("Percent"));
    QCOMPARE(width.text(), QString("50"));
    QCOMPARE(shown.find<QLabel>("canvasNote").text(), QString("New: 600 × 800 pixels · 1.8 MB uncompressed"));
}

// Foundation rounds halves to even: 17 pixels, 16 per inch.
void CanvasSizeSheetTests::halfwayNumbersRoundToEven()
{
    CanvasDocument canvas{17, 19};
    canvas.resolution = 16;
    EditorSession session;
    CanvasSizeSheet sheet(canvas, session, [](std::optional<CanvasSizeOptions>) {});
    auto &units = *sheet.findChild<QComboBox *>(QStringLiteral("canvasUnits"));
    const int inches = units.findText(QStringLiteral("Inches"));
    units.setCurrentIndex(inches);
    emit units.activated(inches);
    QCOMPARE(sheet.findChild<PickerField *>(QStringLiteral("canvasWidth"))->text(), QString("1.062"));
    QCOMPARE(sheet.findChild<PickerField *>(QStringLiteral("canvasHeight"))->text(), QString("1.188"));
}

QTEST_MAIN(CanvasSizeSheetTests)
#include "CanvasSizeSheetTests.moc"
