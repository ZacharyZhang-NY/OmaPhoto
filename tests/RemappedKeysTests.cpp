#include "InlineTextFixtures.h"
#include "UI/CanvasSizeSheet.h"
#include "UI/ColorPickerSheet.h"
#include "UI/ColorRangeSheet.h"
#include "UI/EffectsSheet.h"
#include "UI/FilterSheet.h"
#include "UI/HueSaturationSheet.h"
#include "UI/ImageSizeSheet.h"
#include "UI/JPEGExportSheet.h"
#include "UI/KeyboardShortcuts.h"
#include "UI/LevelsSheet.h"
#include "UI/NewCanvasSheet.h"
#include "UI/TransformInspector.h"
#include "UI/TrimSheet.h"
#include <QShortcut>
#include "UI/LassoControls.h"
#include "UI/NativeLayerList.h"
#include <QDialog>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>

// Remapped keys on canvas, list, sheets and open text.
namespace {
void remap(const QHash<QString, ShortcutChord> &values)
{
    if (!ShortcutSettings::shared().save(values))
        throw std::runtime_error("the overrides were refused");
}
}

class RemappedKeysTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanup();
    void theCanvasReadsTheNewKeysAndDropsTheOld();
    void aRemappedSpacePansUntilItsOwnKeyIsLetGo();
    void theListAndTheWindowReadTheNewKeys();
    void aSheetsButtonsFollowApplyAndCancel();
    void openTextReadsItsOwnNewKeys();
    void everySheetBindsSwiftsKeys();
    void aKeyLeftAloneReachesTheParent();
};

namespace {
// Counts the keys its children leave to it.
struct Parent : QWidget {
    int keys = 0;
    void keyPressEvent(QKeyEvent *event) override
    {
        keys += 1;
        event->accept();
    }
};
}

void RemappedKeysTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void RemappedKeysTests::cleanup()
{
    QSettings().remove(QLatin1String(ShortcutSettings::storageKey));
    ShortcutSettings::shared().reload();
}

void RemappedKeysTests::theCanvasReadsTheNewKeysAndDropsTheOld()
{
    Canvas shown;
    remap({{"Canvas & Layers:Brush tool", ShortcutChord("k")}, {"Canvas & Layers:Increase brush size", ShortcutChord("o")}});
    QTRY_VERIFY(shown.canvas->hasFocus());
    QTest::keyClick(shown.canvas, Qt::Key_B);
    QCOMPARE(shown.session.tool(), NavigationTool::marquee);
    QTest::keyClick(shown.canvas, Qt::Key_K);
    QCOMPARE(shown.session.tool(), NavigationTool::brush);
    const double size = shown.session.brushSettings().diameter;
    QTest::keyClick(shown.canvas, Qt::Key_O);
    QVERIFY(shown.session.brushSettings().diameter > size);
    const double larger = shown.session.brushSettings().diameter;
    QTest::keyClick(shown.canvas, Qt::Key_BracketRight);
    QCOMPARE(shown.session.brushSettings().diameter, larger);
    // A remapped hardness key steps hardness, not the size.
    remap({{"Canvas & Layers:Brush tool", ShortcutChord("k")}, {"Canvas & Layers:Decrease brush hardness", ShortcutChord("o", 8)}});
    const double hardness = shown.session.brushSettings().hardness;
    QTest::keyClick(shown.canvas, Qt::Key_O, Qt::ShiftModifier);
    QVERIFY(shown.session.brushSettings().hardness < hardness);
    QCOMPARE(shown.session.brushSettings().diameter, larger);
}

void RemappedKeysTests::aRemappedSpacePansUntilItsOwnKeyIsLetGo()
{
    Canvas shown;
    remap({{"Canvas & Layers:Temporary Hand tool (hold)", ShortcutChord("q")}});
    QTRY_VERIFY(shown.canvas->hasFocus());
    const Qt::CursorShape resting = shown.canvas->cursor().shape();
    QTest::keyPress(shown.canvas, Qt::Key_Q);
    QCOMPARE(shown.canvas->cursor().shape(), Qt::OpenHandCursor);
    // Space itself is swallowed; Q's release ends the pan.
    QTest::keyRelease(shown.canvas, Qt::Key_Space);
    QCOMPARE(shown.canvas->cursor().shape(), Qt::OpenHandCursor);
    QTest::keyRelease(shown.canvas, Qt::Key_Q);
    QCOMPARE(shown.canvas->cursor().shape(), resting);
    QTest::keyPress(shown.canvas, Qt::Key_Space);
    QCOMPARE(shown.canvas->cursor().shape(), resting);
    // Unmapped again, Space pans and ends as before.
    cleanup();
    QTest::keyPress(shown.canvas, Qt::Key_Space);
    QCOMPARE(shown.canvas->cursor().shape(), Qt::OpenHandCursor);
    QTest::keyRelease(shown.canvas, Qt::Key_Space);
    QCOMPARE(shown.canvas->cursor().shape(), resting);
}

void RemappedKeysTests::theListAndTheWindowReadTheNewKeys()
{
    EditorSession session;
    session.createDocument(100, 100, true);
    QWidget window;
    auto *layout = new QVBoxLayout(&window);
    auto *list = new NativeLayerList(session, &window);
    auto *field = new QLineEdit(&window);
    layout->addWidget(list);
    layout->addWidget(field);
    CanvasView canvas(session, &window);
    window.resize(300, 500);
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    remap({{"Canvas & Layers:Crop tool", ShortcutChord("k")}, {"Canvas & Layers:Next blend mode", ShortcutChord("n", 8)}});
    list->setFocus();
    QTRY_VERIFY(list->hasFocus());
    QTest::keyClick(list, Qt::Key_C);
    QVERIFY(session.tool() != NavigationTool::crop);
    QTest::keyClick(list, Qt::Key_K);
    QCOMPARE(session.tool(), NavigationTool::crop);
    // The window blend keys read the new chord anywhere.
    session.selectTool(NavigationTool::move);
    session.insert(ImportedImage(QImage(4, 4, QImage::Format_RGBA8888_Premultiplied), QImage(), "Pixels"));
    QTest::keyClick(list, Qt::Key_Plus, Qt::ShiftModifier);
    QCOMPARE(session.activeLayer().value().blendMode, LayerBlendMode::normal);
    QTest::keyClick(list, Qt::Key_N, Qt::ShiftModifier);
    QCOMPARE(session.activeLayer().value().blendMode, allLayerBlendModes[1]);
}

void RemappedKeysTests::aSheetsButtonsFollowApplyAndCancel()
{
    const auto session = selectionSession();
    lasso(*session, square(20, 20, 40));
    session->promptSelectionAmount(SelectionAmountOperation::feather);
    remap({{"Canvas & Layers:Apply current canvas operation", ShortcutChord("k", 1)}});
    QDialog dialog;
    auto *sheet = new SelectionAmountSheet(*session, SelectionAmountOperation::feather, &dialog);
    (new QVBoxLayout(&dialog))->addWidget(sheet);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowActive(&dialog));
    auto &field = *sheet->findChild<QLineEdit *>("amountField");
    QTRY_VERIFY(field.hasFocus());
    // Return no longer takes OK; the new chord does.
    QTest::keyClick(&field, Qt::Key_Return);
    QVERIFY(session->selectionAmountOperation().has_value());
    // A resting OK takes no key either.
    field.setText(QStringLiteral("0"));
    QTest::keyClick(&field, Qt::Key_K, Qt::ControlModifier);
    QVERIFY(session->selectionAmountOperation().has_value());
    field.setText(QStringLiteral("2"));
    auto &ok = *sheet->findChild<QPushButton *>("amountOK");
    ok.hide();
    QTest::keyClick(&field, Qt::Key_K, Qt::ControlModifier);
    QVERIFY(session->selectionAmountOperation().has_value());
    ok.show();
    QTest::keyClick(&field, Qt::Key_K, Qt::ControlModifier);
    QVERIFY(!session->selectionAmountOperation());
    QCOMPARE(session->selection().value().feather, 2.0);
    // Back to Return, the default button takes it again.
    cleanup();
    session->promptSelectionAmount(SelectionAmountOperation::feather);
    QTest::keyClick(&field, Qt::Key_K, Qt::ControlModifier);
    QVERIFY(session->selectionAmountOperation().has_value());
    QTest::keyClick(&field, Qt::Key_Return);
    QVERIFY(!session->selectionAmountOperation());
    // Cancel moved onto Return takes it; Apply is elsewhere.
    remap({{"Canvas & Layers:Apply current canvas operation", ShortcutChord("k", 1)},
           {"Canvas & Layers:Cancel current canvas operation", ShortcutChord("\r")}});
    session->promptSelectionAmount(SelectionAmountOperation::feather);
    const double feather = session->selection().value().feather;
    QTest::keyClick(&field, Qt::Key_Return);
    QVERIFY(!session->selectionAmountOperation());
    QCOMPARE(session->selection().value().feather, feather);
}

void RemappedKeysTests::openTextReadsItsOwnNewKeys()
{
    TextCanvas shown;
    remap({{"Text Editing:Finish editing text", ShortcutChord("\r", 4)}, {"Text Editing:Increase tracking", ShortcutChord("t", 2)}});
    QTRY_VERIFY(shown.canvas->hasFocus());
    shown.type("Hi");
    const double tracking = shown.session.textDraft().value().style.tracking;
    shown.key(Qt::Key_T, Qt::AltModifier);
    QVERIFY(shown.session.textDraft().value().style.tracking > tracking);
    // The old chords do nothing; the new one finishes.
    shown.key(Qt::Key_Return, Qt::ControlModifier);
    QVERIFY(shown.session.textDraft().has_value());
    QCOMPARE(shown.content(), QString("Hi"));
    shown.key(Qt::Key_Return, Qt::MetaModifier);
    QVERIFY(!shown.session.textDraft());
}

void RemappedKeysTests::everySheetBindsSwiftsKeys()
{
    remap({{"Canvas & Layers:Apply current canvas operation", ShortcutChord("k", 1)},
           {"Canvas & Layers:Cancel current canvas operation", ShortcutChord("q", 1)},
           {"Canvas & Layers:Toggle Levels preview", ShortcutChord("o", 2)}});
    // The keys each sheet's live shortcuts answer to.
    const auto keys = [](const QWidget &sheet) {
        QSet<QString> result;
        for (const QShortcut *shortcut : sheet.findChildren<QShortcut *>()) {
            if (shortcut->isEnabled())
                result.insert(shortcut->key().toString());
        }
        return result;
    };
    const QSet<QString> both{"Ctrl+K", "Ctrl+Q"};
    EditorSession session;
    session.createDocument(20, 20);
    QImage red(20, 20, QImage::Format_RGBA8888_Premultiplied);
    red.fill(Qt::red);
    session.insert(ImportedImage(red, red, "Red"));
    const CanvasDocument canvas = session.document().value();
    QCOMPARE(keys(CanvasSizeSheet(canvas, session, [](std::optional<CanvasSizeOptions>) {})), both);
    QCOMPARE(keys(ImageSizeSheet(canvas, [](std::optional<ImageSizeOptions>) {})), both);
    QCOMPARE(keys(TrimSheet([](std::optional<TrimOptions>) {})), both);
    QCOMPARE(keys(JPEGExportSheet(ExportRaster{red, 72}, session, [](std::optional<QByteArray>) {})), both);
    QCOMPARE(keys(ColorPickerSheet(session, [](bool) {})), both);
    QCOMPARE(keys(NewCanvasSheet(session, [](int, int) {}, [] {})), QSet<QString>{"Ctrl+K"});
    QCOMPARE(keys(TransformInspector(session)), both);
    QCOMPARE(keys(SelectionAmountSheet(session, SelectionAmountOperation::expand)), both);
    session.addEffect(LayerEffectKind::stroke);
    QCOMPARE(keys(EffectsSheet(session, LayerEffectKind::stroke)), both);
    session.finishEffectsEditing(false);
    session.beginLevels();
    QCOMPARE(keys(LevelsSheet(session)), (QSet<QString>{"Ctrl+K", "Ctrl+Q", "Alt+O"}));
    session.cancelLevels();
    session.beginHueSaturation();
    QCOMPARE(keys(HueSaturationSheet(session)), both);
    session.cancelHueSaturation();
    session.beginFilter(FilterKind::gaussianBlur);
    QCOMPARE(keys(FilterSheet(session)), both);
    session.cancelFilter();
    session.beginColorRange();
    QCOMPARE(keys(ColorRangeSheet(session)), both);
    session.cancelColorRange();
}

void RemappedKeysTests::aKeyLeftAloneReachesTheParent()
{
    EditorSession session;
    session.createDocument(20, 20, true);
    Parent parent;
    auto *canvas = new CanvasView(session, &parent);
    auto *list = new NativeLayerList(session, &parent);
    list->move(0, 200);
    parent.resize(300, 500);
    parent.show();
    QVERIFY(QTest::qWaitForWindowActive(&parent));
    remap({{"Canvas & Layers:Brush tool", ShortcutChord("k")}});
    QTest::keyClick(canvas, Qt::Key_Q);
    QCOMPARE(parent.keys, 1);
    QTest::keyClick(list, Qt::Key_Q);
    QCOMPARE(parent.keys, 2);
    // A key the canvas takes, or swallows, stays there.
    QTest::keyClick(canvas, Qt::Key_K);
    QTest::keyClick(canvas, Qt::Key_B);
    QTest::keyClick(list, Qt::Key_K);
    QCOMPARE(parent.keys, 2);
}

QTEST_MAIN(RemappedKeysTests)
#include "RemappedKeysTests.moc"
