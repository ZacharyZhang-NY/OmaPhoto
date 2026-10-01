#include "SelectionCanvasFixtures.h"
#include "UI/ColorPaletteControls.h"
#include "UI/ColorPickerSheet+Dialog.h"
#include "UI/ColorPickerSheet.h"
#include <QDialog>
#include <QLabel>
#include <QVBoxLayout>

// Swift 1.3.5's dialog colours: the app's picker on a swatch.
class DialogColorPickerTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void thePickerTellsTheDialogAsItMoves();
    void theSwatchOpensItFollowsItAndClosesIt();
    void thePanelOpensOverAModalSheet();
    void aDialogsPickerSamplesNothing();
};

namespace {
const PaletteColor blue{0, 0, 1};

struct Heard {
    std::vector<PaletteColor> colours;
    std::function<void(const PaletteColor &)> listener()
    {
        return [this](const PaletteColor &colour) { colours.push_back(colour); };
    }
};
}

void DialogColorPickerTests::thePickerTellsTheDialogAsItMoves()
{
    EditorSession session;
    Heard heard;
    QSignalSpy changed(&session, &EditorSession::changed);
    session.openDialogColorPicker(QStringLiteral("JPEG Background"), PaletteColor::white(), heard.listener());
    QCOMPARE(changed.count(), 1);
    const ColorPickerState &picker = session.colorPicker().value();
    QVERIFY(picker.target.kind == ColorPickerTarget::Kind::dialog && picker.original == PaletteColor::white());
    QCOMPARE(picker.target.title(), QStringLiteral("Color Picker (JPEG Background)"));
    QVERIFY(session.pickingForDialog());
    // One picker at a time: a second ask changes nothing.
    Heard other;
    session.openDialogColorPicker(QStringLiteral("Extension Color"), blue, other.listener());
    QVERIFY(changed.count() == 1 && session.colorPicker().value().target.dialogTitle == QStringLiteral("JPEG Background"));
    // The working colour reaches the dialog, the session silent.
    session.setColorPickerHSB(PickerHSB(blue));
    changed.clear();
    session.previewDialogColor();
    QVERIFY(changed.isEmpty() && heard.colours == std::vector<PaletteColor>{blue});
    // Cancel tells the original; OK the chosen colour.
    session.closeColorPicker(false);
    QVERIFY(!session.pickingForDialog() && heard.colours.back() == PaletteColor::white() && changed.count() == 1);
    session.openDialogColorPicker(QStringLiteral("JPEG Background"), PaletteColor::white(), heard.listener());
    session.setColorPickerHSB(PickerHSB(blue));
    session.closeColorPicker(true);
    QVERIFY(heard.colours.back() == blue && other.colours.empty());
    // Not a dialog's picker: nothing to tell, nothing told.
    session.openColorPicker(false);
    QVERIFY(session.colorPicker() && !session.pickingForDialog());
    const size_t told = heard.colours.size();
    session.previewDialogColor();
    QCOMPARE(heard.colours.size(), told);
    // The picker's sheet asks no sample for a dialog.
    QVERIFY(ColorPickerSheet(session, [](bool) {}).findChild<QLabel *>(QStringLiteral("pickerHint")));
    session.closeColorPicker(false);
    session.openDialogColorPicker(QStringLiteral("JPEG Background"), PaletteColor::white(), heard.listener());
    QVERIFY(!ColorPickerSheet(session, [](bool) {}).findChild<QLabel *>(QStringLiteral("pickerHint")));
}

void DialogColorPickerTests::theSwatchOpensItFollowsItAndClosesIt()
{
    EditorSession session;
    PaletteColor kept = PaletteColor::white();
    QWidget host;
    auto *swatch = new DialogColorSwatch(QStringLiteral("Extension Color"), [&kept] { return kept; }, [&kept](const PaletteColor &colour) { kept = colour; },
                                         session, &host);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    QVERIFY(swatch->size() == QSize(34, 18) && swatch->accessibleName() == QStringLiteral("Extension Color"));
    // The brush's look: colour, white ring, black rim.
    QImage drawn = swatch->grab().toImage();
    QVERIFY(drawn.pixelColor(0, 9).value() < 40 && drawn.pixelColor(1, 9) == QColor(Qt::white) && drawn.pixelColor(17, 9) == QColor(Qt::white));
    swatch->click();
    QVERIFY(session.pickingForDialog() && session.colorPicker().value().original == PaletteColor::white());
    // The dialog follows the working colour; the swatch repaints.
    QCoreApplication::processEvents();
    PaintSpy painted(*swatch);
    session.setColorPickerHSB(PickerHSB(blue));
    QVERIFY(kept == blue);
    QTRY_VERIFY(painted.count > 0);
    drawn = swatch->grab().toImage();
    QCOMPARE(drawn.pixelColor(17, 9), QColor(Qt::blue));
    // Rim, white ring, colour; a corner rounded by four.
    QVERIFY(drawn.pixelColor(0, 9) == QColor(Qt::black) && drawn.pixelColor(1, 9) == QColor(Qt::white) && drawn.pixelColor(2, 9) == QColor(Qt::blue));
    QVERIFY(drawn.pixelColor(0, 0).red() > 220 && drawn.pixelColor(1, 1).red() < 20);
    // A palette picker leaves the swatch alone.
    session.closeColorPicker(true);
    session.openColorPicker(true);
    session.setColorPickerHSB(PickerHSB(PaletteColor::black()));
    DialogColorSwatch::closePicker(session);
    QVERIFY(kept == blue && session.colorPicker() && !session.pickingForDialog());
    session.closeColorPicker(false);
    // Hidden, the swatch closes its picker, keeping the colour.
    swatch->click();
    session.setColorPickerHSB(PickerHSB(PaletteColor{1, 0, 0}));
    // A minimized window hides it spontaneously: the picker stays.
    host.showMinimized();
    QCoreApplication::processEvents();
    QVERIFY(session.pickingForDialog());
    host.showNormal();
    swatch->hide();
    QVERIFY(!session.colorPicker() && kept == (PaletteColor{1, 0, 0}));
}

void DialogColorPickerTests::thePanelOpensOverAModalSheet()
{
    EditorSession session;
    QWidget window;
    window.setLayout(new QVBoxLayout);
    window.layout()->addWidget(new ColorPaletteControls(session, &window));
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    const auto panel = [] {
        for (QWidget *widget : QApplication::topLevelWidgets())
            if (widget->objectName() == ColorPickerPanelController::identifier() && widget->isVisible())
                return widget;
        return static_cast<QWidget *>(nullptr);
    };
    session.openColorPicker(false);
    QTRY_VERIFY(panel());
    QCOMPARE(panel()->parentWidget(), &window);
    session.closeColorPicker(false);
    QTRY_VERIFY(!panel());
    // Window-modal, as the controller's sheets: the panel moves under it.
    auto *sheet = new QDialog(&window);
    sheet->setWindowModality(Qt::WindowModal);
    sheet->open();
    QTRY_COMPARE(QApplication::activeModalWidget(), sheet);
    PaletteColor kept = PaletteColor::white();
    session.openDialogColorPicker(QStringLiteral("JPEG Background"), kept, [&kept](const PaletteColor &colour) { kept = colour; });
    QTRY_VERIFY(panel());
    QCOMPARE(panel()->parentWidget(), sheet);
    QCOMPARE(panel()->windowTitle(), QStringLiteral("Color Picker (JPEG Background)"));
    QTRY_COMPARE(QApplication::activeWindow(), panel());
    // It dies with the sheet; the next is the window's.
    session.closeColorPicker(true);
    QTRY_VERIFY(!panel());
    delete sheet;
    session.openColorPicker(false);
    QTRY_VERIFY(panel());
    QCOMPARE(panel()->parentWidget(), &window);
    session.closeColorPicker(false);
}

void DialogColorPickerTests::aDialogsPickerSamplesNothing()
{
    Canvas shown;
    const QCursor dropper = CanvasView::eyedropperCursor(shown.canvas->devicePixelRatio());
    shown.session.openDialogColorPicker(QStringLiteral("JPEG Background"), PaletteColor::white(), [](const PaletteColor &) {});
    shown.canvas->synchronizeDisplay();
    QVERIFY(!shown.shows(dropper));
    shown.session.closeColorPicker(false);
    shown.session.openColorPicker(false);
    shown.canvas->synchronizeDisplay();
    QVERIFY(shown.shows(dropper));
}

QTEST_MAIN(DialogColorPickerTests)
#include "DialogColorPickerTests.moc"
