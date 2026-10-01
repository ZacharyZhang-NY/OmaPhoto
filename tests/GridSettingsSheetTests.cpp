#include "UI/ColorPickerSheet+Dialog.h"
#include "UI/ColorPickerSheet.h"
#include "UI/GridSettingsSheet.h"
#include <QComboBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QtTest>

// Swift 1.3.5's GridSettingsSheet: the controls, previews and answers.
class GridSettingsSheetTests : public QObject {
    Q_OBJECT
private slots:
    void theSheetLaysOutAsSwifts();
    void eachChangePreviewsTheGrid();
    void aPickedColourBecomesCustom();
    void restoreDefaultsOkAndCancel();
    void theRowsHoldSwiftsControls();
    void aSliderOrScrubReplacesTyping();
};

namespace {
// Drags a scrubbable title `points` to the right.
void scrub(QLabel &title, int points)
{
    QTest::mousePress(&title, Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));
    QMouseEvent move(QEvent::MouseMove, QPointF(2 + points, 2), title.mapToGlobal(QPointF(2 + points, 2)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&title, &move);
    QTest::mouseRelease(&title, Qt::LeftButton, Qt::NoModifier, QPoint(2 + points, 2));
}

struct Sheet {
    EditorSession session;
    std::vector<GridSettingsSheet::Settings> previews;
    std::optional<std::optional<GridSettingsSheet::Settings>> answer;
    std::unique_ptr<GridSettingsSheet> sheet;
    explicit Sheet(LayoutGrid grid = LayoutGrid(), GridAppearance appearance = GridAppearance())
        : sheet(std::make_unique<GridSettingsSheet>(
              session, grid, appearance, [this](const GridSettingsSheet::Settings &settings) { previews.push_back(settings); },
              [this](std::optional<GridSettingsSheet::Settings> settings) { answer = settings; }))
    {
        sheet->show();
        if (!QTest::qWaitForWindowActive(sheet.get()))
            throw std::runtime_error("the sheet never became active");
    }
    template <typename Widget> Widget &find(const char *name)
    {
        Widget *found = sheet->findChild<Widget *>(QString::fromLatin1(name));
        if (!found)
            throw std::runtime_error(std::string("no widget named ") + name);
        return *found;
    }
    // Typed, then Return: the field commits.
    void type(const char *name, const QString &text)
    {
        auto &field = find<PickerField>(name);
        field.setFocus();
        field.selectAll();
        QTest::keyClicks(&field, text);
        QTest::keyClick(&field, Qt::Key_Return);
    }
    QString note() { return find<QLabel>("gridNote").text(); }
    const GridSettingsSheet::Settings &last() const { return previews.back(); }
};
}

void GridSettingsSheetTests::theSheetLaysOutAsSwifts()
{
    Sheet shown;
    auto *column = qobject_cast<QVBoxLayout *>(shown.sheet->layout());
    QVERIFY(column && shown.sheet->width() == 360 && column->spacing() == 18 && column->contentsMargins() == QMargins(24, 24, 24, 24));
    auto *heading = qobject_cast<QLabel *>(column->itemAt(0)->widget());
    QVERIFY(heading->text() == "Grid" && heading->font().pixelSize() == 17 && heading->font().bold());
    // Titles 110 wide, each naming its control.
    const auto rowOf = [column](int index) { return qobject_cast<QHBoxLayout *>(column->itemAt(index)->layout()); };
    const QStringList titles{"Color", "Style", "Opacity"};
    for (int index = 0; index < 3; ++index) {
        auto *title = qobject_cast<QLabel *>(rowOf(index + 1)->itemAt(0)->widget());
        QVERIFY(title->text() == titles[index] && title->width() == 110 && title->buddy());
    }
    auto &preset = shown.find<QComboBox>("gridPreset");
    QCOMPARE(preset.count(), 10);
    QVERIFY(preset.itemText(0) == "Light Gray" && preset.itemText(9) == "Custom" && preset.currentText() == "Light Gray");
    auto &swatch = shown.find<DialogColorSwatch>("gridColor");
    QVERIFY(swatch.accessibleName() == "Grid Color" && swatch.toolTip() == "Choose a custom grid color" && rowOf(1)->itemAt(2)->widget() == &swatch);
    auto &style = shown.find<QComboBox>("gridStyle");
    QVERIFY(style.count() == 3 && style.itemText(1) == "Dashed Lines" && style.currentText() == "Lines");
    auto &opacity = shown.find<PickerField>("gridOpacity");
    QVERIFY(opacity.text() == "45" && opacity.width() == 48 && opacity.alignment() == Qt::AlignRight && opacity.accessibleName() == "Opacity");
    QVERIFY(shown.find<QSlider>("gridOpacitySlider").minimum() == 1 && shown.find<QSlider>("gridOpacitySlider").maximum() == 100);
    QCOMPARE(shown.find<QSlider>("gridOpacitySlider").value(), 45);
    auto *rule = qobject_cast<QFrame *>(column->itemAt(4)->widget());
    QVERIFY(rule && rule->frameShape() == QFrame::HLine);
    QVERIFY(qobject_cast<QLabel *>(rowOf(5)->itemAt(0)->widget())->text() == "Gridline every" && shown.find<PickerField>("gridSpacing").text() == "64");
    QCOMPARE(qobject_cast<QLabel *>(rowOf(5)->itemAt(2)->widget())->text(), QString("pixels"));
    QVERIFY(qobject_cast<QLabel *>(rowOf(6)->itemAt(0)->widget())->text() == "Subdivisions" && shown.find<PickerField>("gridSubdivisions").text() == "8");
    QCOMPARE(shown.note(), QString("A subdivision every 8 pixels."));
    QCOMPARE(shown.find<QLabel>("gridNote").foregroundRole(), QPalette::PlaceholderText);
    auto *buttons = rowOf(8);
    QVERIFY(qobject_cast<QPushButton *>(buttons->itemAt(0)->widget())->text() == "Cancel" && qobject_cast<QPushButton *>(buttons->itemAt(1)->widget())->text() == "Restore Defaults");
    QVERIFY(buttons->itemAt(2)->spacerItem() && shown.find<QPushButton>("gridOK").isDefault());
    QVERIFY(shown.previews.empty());
}

void GridSettingsSheetTests::eachChangePreviewsTheGrid()
{
    Sheet shown;
    auto &preset = shown.find<QComboBox>("gridPreset");
    preset.setCurrentIndex(7);
    emit preset.activated(7);
    QVERIFY(shown.previews.size() == 1 && shown.last().second.preset == GridAppearance::Preset::cyan && shown.last().first == LayoutGrid());
    shown.find<QComboBox>("gridStyle").setCurrentIndex(2);
    emit shown.find<QComboBox>("gridStyle").activated(2);
    QCOMPARE(shown.last().second.style, GridAppearance::Style::dots);
    // Opacity: the slider, the field (clamped), its arrows.
    shown.find<QSlider>("gridOpacitySlider").setValue(80);
    QCOMPARE(shown.last().second.opacity, 80);
    shown.type("gridOpacity", "150");
    QVERIFY(shown.last().second.opacity == 100 && shown.find<PickerField>("gridOpacity").text() == "100");
    QTest::keyClick(&shown.find<PickerField>("gridOpacity"), Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(shown.last().second.opacity, 90);
    // The grid previews only while valid; the note says why.
    shown.type("gridSpacing", "100");
    QVERIFY(shown.last().first == LayoutGrid(100, 8));
    QCOMPARE(shown.note(), QString("A subdivision every 12.5 pixels."));
    shown.type("gridSubdivisions", "3");
    QCOMPARE(shown.note(), QString("A subdivision every 33.33 pixels."));
    const size_t seen = shown.previews.size();
    shown.type("gridSpacing", "1");
    QVERIFY(shown.previews.size() == seen && !shown.find<QPushButton>("gridOK").isEnabled());
    QCOMPARE(shown.note(), QString("Use gridlines every 2–4,096 pixels and 1–64 subdivisions, no more than the pixels between gridlines."));
    QCOMPARE(shown.find<QLabel>("gridNote").foregroundRole(), QPalette::BrightText);
    shown.type("gridSpacing", "2");
    QCOMPARE(shown.note().left(5), QString("Use g"));
    shown.type("gridSubdivisions", "2");
    QVERIFY(shown.last().first == LayoutGrid(2, 2) && shown.find<QPushButton>("gridOK").isEnabled());
    // Text that is no number goes back.
    shown.type("gridSpacing", "lots");
    QCOMPARE(shown.find<PickerField>("gridSpacing").text(), QString("2"));
    // The titles scrub: opacity a point per half.
    auto &title = *qobject_cast<QLabel *>(qobject_cast<QHBoxLayout *>(shown.sheet->layout()->itemAt(3)->layout())->itemAt(0)->widget());
    QTest::mousePress(&title, Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));
    QMouseEvent move(QEvent::MouseMove, QPointF(12, 2), title.mapToGlobal(QPointF(12, 2)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&title, &move);
    QTest::mouseRelease(&title, Qt::LeftButton, Qt::NoModifier, QPoint(12, 2));
    QCOMPARE(shown.last().second.opacity, 95);
}

void GridSettingsSheetTests::aPickedColourBecomesCustom()
{
    GridAppearance cyan;
    cyan.preset = GridAppearance::Preset::cyan;
    Sheet shown(LayoutGrid(), cyan);
    auto &swatch = shown.find<DialogColorSwatch>("gridColor");
    swatch.click();
    QCOMPARE(shown.session.colorPicker().value().target.title(), QString("Color Picker (Grid Color)"));
    QCOMPARE(shown.session.colorPicker().value().original, (PaletteColor{0, 1, 1}));
    // Its opening colour keeps the preset; another makes Custom.
    shown.session.previewDialogColor();
    QVERIFY(shown.previews.empty());
    const PaletteColor teal{0, 128 / 255.0, 128 / 255.0};
    shown.session.setColorPickerHSB(PickerHSB(teal));
    QVERIFY(shown.last().second.preset == GridAppearance::Preset::custom && shown.last().second.customColor == teal);
    QCOMPARE(shown.find<QComboBox>("gridPreset").currentText(), QString("Custom"));
    // Picking the first preset's colour again returns to it.
    shown.session.setColorPickerHSB(PickerHSB(PaletteColor{0, 1, 1}));
    QVERIFY(shown.last().second.preset == GridAppearance::Preset::cyan && shown.last().second.customColor == teal);
    shown.session.closeColorPicker(false);
    // Beyond Swift: Light Gray's 0.7, no byte, still stays.
    auto &preset = shown.find<QComboBox>("gridPreset");
    preset.setCurrentIndex(0);
    emit preset.activated(0);
    swatch.click();
    shown.session.closeColorPicker(true);
    QCOMPARE(shown.last().second.preset, GridAppearance::Preset::lightGray);
    swatch.click();
    shown.session.setColorPickerHSB(PickerHSB(teal));
    shown.session.setColorPickerHSB(PickerHSB(PaletteColor{0.7, 0.7, 0.7}));
    QCOMPARE(shown.last().second.preset, GridAppearance::Preset::lightGray);
    shown.session.closeColorPicker(false);
    // The picker's Cancel hands back 0.7 itself: Light Gray stays.
    const size_t seen = shown.previews.size();
    swatch.click();
    shown.session.closeColorPicker(false);
    QCOMPARE(shown.previews.size(), seen);
    swatch.click();
    shown.session.setColorPickerHSB(PickerHSB(teal));
    shown.session.closeColorPicker(false);
    QCOMPARE(shown.last().second.preset, GridAppearance::Preset::lightGray);
    // A preset chosen from the menu ends a pick.
    swatch.click();
    shown.session.setColorPickerHSB(PickerHSB(teal));
    preset.setCurrentIndex(8);
    emit preset.activated(8);
    shown.session.setColorPickerHSB(PickerHSB(PaletteColor{0.7, 0.7, 0.7}));
    QVERIFY(shown.last().second.preset == GridAppearance::Preset::custom && shown.last().second.customColor == (PaletteColor{0.7, 0.7, 0.7}).quantized());
    shown.session.closeColorPicker(false);
    // Restore Defaults ends a pick too.
    preset.setCurrentIndex(7);
    emit preset.activated(7);
    swatch.click();
    shown.session.setColorPickerHSB(PickerHSB(teal));
    shown.find<QPushButton>("gridDefaults").click();
    shown.session.setColorPickerHSB(PickerHSB(PaletteColor{0, 1, 1}));
    QVERIFY(shown.last().second.preset == GridAppearance::Preset::custom && shown.last().second.customColor == (PaletteColor{0, 1, 1}));
    // A changed preset repaints the swatch.
    shown.session.closeColorPicker(false);
    QCoreApplication::processEvents();
    int paints = 0;
    struct Counter : QObject {
        int &count;
        explicit Counter(int &count) : count(count) {}
        bool eventFilter(QObject *, QEvent *event) override
        {
            count += event->type() == QEvent::Paint;
            return false;
        }
    } counter(paints);
    swatch.installEventFilter(&counter);
    preset.setCurrentIndex(5);
    emit preset.activated(5);
    QTRY_VERIFY(paints > 0);
    swatch.removeEventFilter(&counter);
    shown.sheet.reset();
    QVERIFY(!shown.session.colorPicker());
}

void GridSettingsSheetTests::restoreDefaultsOkAndCancel()
{
    GridAppearance look{GridAppearance::Preset::custom, PaletteColor{0, 0.5, 0.5}, GridAppearance::Style::dots, 70};
    Sheet shown(LayoutGrid(100, 4), look);
    QVERIFY(shown.find<QComboBox>("gridPreset").currentText() == "Custom" && shown.find<PickerField>("gridSpacing").text() == "100");
    QVERIFY(shown.find<QComboBox>("gridStyle").currentText() == "Dots" && shown.find<PickerField>("gridOpacity").text() == "70");
    shown.find<QPushButton>("gridDefaults").click();
    const GridSettingsSheet::Settings restored = shown.last();
    QVERIFY(restored.first == LayoutGrid() && restored.second.preset == GridAppearance::Preset::lightGray && restored.second.style == GridAppearance::Style::lines);
    QVERIFY(restored.second.opacity == 45 && restored.second.customColor == look.customColor);
    QVERIFY(shown.find<PickerField>("gridSpacing").text() == "64" && shown.find<QComboBox>("gridStyle").currentText() == "Lines");
    // OK closes an open picker and answers what is shown.
    shown.find<DialogColorSwatch>("gridColor").click();
    shown.find<QPushButton>("gridOK").click();
    QVERIFY(!shown.session.colorPicker() && shown.answer.value().value() == restored);
    Sheet cancelled;
    cancelled.find<DialogColorSwatch>("gridColor").click();
    cancelled.find<QPushButton>("gridCancel").click();
    QVERIFY(!cancelled.session.colorPicker() && !cancelled.answer.value());
}

void GridSettingsSheetTests::theRowsHoldSwiftsControls()
{
    Sheet shown;
    auto *column = qobject_cast<QVBoxLayout *>(shown.sheet->layout());
    const auto rowOf = [column](int index) { return qobject_cast<QHBoxLayout *>(column->itemAt(index)->layout()); };
    for (const int index : {1, 2, 3, 5, 6})
        QCOMPARE(rowOf(index)->spacing(), 8);
    // Colour and Style: title, menu (and swatch), then room.
    QVERIFY(rowOf(1)->count() == 4 && rowOf(1)->itemAt(3)->spacerItem());
    QVERIFY(rowOf(2)->count() == 3 && rowOf(2)->itemAt(1)->widget() == &shown.find<QComboBox>("gridStyle") && rowOf(2)->itemAt(2)->spacerItem());
    // Opacity: the slider stretches; % sits two after.
    QVERIFY(rowOf(3)->itemAt(1)->widget() == &shown.find<QSlider>("gridOpacitySlider") && rowOf(3)->stretch(1) == 1);
    auto *suffixed = qobject_cast<QHBoxLayout *>(rowOf(3)->itemAt(2)->layout());
    QVERIFY(suffixed && suffixed->spacing() == 2 && suffixed->itemAt(0)->widget() == &shown.find<PickerField>("gridOpacity"));
    QCOMPARE(qobject_cast<QLabel *>(suffixed->itemAt(1)->widget())->text(), QString("%"));
    QCOMPARE(qobject_cast<QFrame *>(column->itemAt(4)->widget())->foregroundRole(), QPalette::Mid);
    // The fields stretch, named by their titles.
    auto &spacing = shown.find<PickerField>("gridSpacing");
    auto &subdivisions = shown.find<PickerField>("gridSubdivisions");
    QVERIFY(qobject_cast<QLabel *>(rowOf(5)->itemAt(0)->widget())->buddy() == &spacing && rowOf(5)->stretch(1) == 1);
    QVERIFY(qobject_cast<QLabel *>(rowOf(6)->itemAt(0)->widget())->buddy() == &subdivisions && rowOf(6)->stretch(1) == 1);
    QCOMPARE(qobject_cast<QLabel *>(rowOf(5)->itemAt(2)->widget())->foregroundRole(), QPalette::PlaceholderText);
    QVERIFY(spacing.placeholderText() == "Gridline every" && subdivisions.placeholderText() == "Subdivisions" && shown.find<PickerField>("gridOpacity").placeholderText() == "Opacity");
    auto &note = shown.find<QLabel>("gridNote");
    QVERIFY(note.wordWrap() && note.font().pixelSize() == 12);
    QCOMPARE(rowOf(8)->itemAt(3)->widget(), &shown.find<QPushButton>("gridOK"));
    // In the controller's dialog only OK takes Return.
    QDialog dialog;
    shown.sheet->setParent(&dialog);
    QVERIFY(!shown.find<QPushButton>("gridCancel").autoDefault() && !shown.find<QPushButton>("gridDefaults").autoDefault());
    QVERIFY(shown.find<QPushButton>("gridOK").autoDefault());
    shown.sheet->setParent(nullptr);
}

void GridSettingsSheetTests::aSliderOrScrubReplacesTyping()
{
    Sheet shown;
    auto &opacity = shown.find<PickerField>("gridOpacity");
    opacity.setFocus();
    opacity.selectAll();
    QTest::keyClicks(&opacity, QStringLiteral("77"));
    shown.find<QSlider>("gridOpacitySlider").setValue(60);
    QCOMPARE(opacity.text(), QString("60"));
    // Each title scrubs, and the note follows.
    auto *column = qobject_cast<QVBoxLayout *>(shown.sheet->layout());
    const auto titleOf = [column](int index) { return qobject_cast<QLabel *>(qobject_cast<QHBoxLayout *>(column->itemAt(index)->layout())->itemAt(0)->widget()); };
    auto &spacing = shown.find<PickerField>("gridSpacing");
    spacing.setFocus();
    spacing.selectAll();
    QTest::keyClicks(&spacing, QStringLiteral("99"));
    scrub(*titleOf(5), 10);
    QVERIFY(spacing.text() == "74" && shown.last().first == LayoutGrid(74, 8));
    QCOMPARE(shown.note(), QString("A subdivision every 9.25 pixels."));
    auto &subdivisions = shown.find<PickerField>("gridSubdivisions");
    subdivisions.setFocus();
    subdivisions.selectAll();
    QTest::keyClicks(&subdivisions, QStringLiteral("33"));
    scrub(*titleOf(6), 10);
    QVERIFY(subdivisions.text() == "10" && shown.last().first == LayoutGrid(74, 10));
    opacity.setFocus();
    opacity.selectAll();
    QTest::keyClicks(&opacity, QStringLiteral("33"));
    scrub(*titleOf(3), 10);
    QVERIFY(opacity.text() == "65" && shown.last().second.opacity == 65);
}

QTEST_MAIN(GridSettingsSheetTests)
#include "GridSettingsSheetTests.moc"
