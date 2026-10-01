#include "DialogDesk.h"
#include "IO/ProjectController.h"
#include "UI/CanvasSizeSheet.h"
#include "UI/ColorPaletteControls.h"
#include "UI/ColorPickerSheet+Dialog.h"
#include "UI/ColorPickerSheet.h"
#include <QComboBox>
#include "UI/ImageSizeSheet.h"
#include <QDialog>
#include <QPushButton>
#include <QtTest>

// The controller's Canvas Size and Image Size: sheet, resize, step.
class ProjectResizeTests : public QObject {
    Q_OBJECT
private slots:
    void theCanvasSizeSheetResizesTheCanvasAsOneStep();
    void aCancelledOrRefusedSheetChangesNothing();
    void theImageSizeSheetScalesThePixelsAsOneStep();
    void aResizeThatFailsIsExplained();
    void theSheetsColourOpensTheAppsPickerOverIt();
};

namespace {
struct Desk {
    EditorSession session;
    ProjectController controller{session};
    QWidget window;
    bool done = false;
    Desk()
    {
        session.createDocument(100, 50);
        session.insert(ImportedImage(QImage(10, 10, QImage::Format_RGBA8888_Premultiplied), QImage(), "Image"), QPointF(45, 25));
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
    // Typed, then Return: the field commits, then the default button.
    void type(const char *name, const QString &text)
    {
        auto *field = dialog().findChild<PickerField *>(QString::fromLatin1(name));
        if (!field)
            throw std::runtime_error(std::string("no field named ") + name);
        field->setFocus();
        field->selectAll();
        QTest::keyClicks(field, text);
        QTest::keyClick(field, Qt::Key_Return);
    }
    std::function<void()> finished()
    {
        done = false;
        return [this] { done = true; };
    }
};
}

void ProjectResizeTests::theCanvasSizeSheetResizesTheCanvasAsOneStep()
{
    Desk desk;
    const int steps = desk.session.history.undoCount();
    desk.controller.canvasSize(desk.finished());
    // The sheet holds the window; the project is busy meanwhile.
    QVERIFY(desk.session.isProjectBusy());
    QCOMPARE(desk.dialog().windowTitle(), QString("Canvas Size"));
    QVERIFY(desk.dialog().isVisible() && desk.dialog().windowModality() == Qt::WindowModal);
    QCOMPARE(desk.dialog().minimumSize(), desk.dialog().maximumSize());
    QVERIFY(desk.dialog().findChild<CanvasSizeSheet *>());
    desk.type("canvasWidth", "200");
    QTRY_VERIFY(desk.done);
    QCOMPARE(desk.session.document().value().width, 200);
    QCOMPARE(desk.session.document().value().height, 50);
    // Centred: the layer moves half the growth to the right.
    QCOMPARE(desk.session.activeLayer().value().origin(), QPointF(90, 20));
    QCOMPARE(desk.session.history.undoCount(), steps + 1);
    QCOMPARE(desk.session.history.undoName(), QString("Canvas Size"));
    QVERIFY(!desk.session.isProjectBusy());
    QTRY_VERIFY(!desk.window.findChild<QDialog *>());
}

void ProjectResizeTests::aCancelledOrRefusedSheetChangesNothing()
{
    Desk desk;
    const CanvasDocument before = desk.session.document().value();
    // Escape and Cancel close the sheet; the project frees.
    desk.controller.canvasSize(desk.finished());
    QTest::keyClick(&desk.dialog(), Qt::Key_Escape);
    QTRY_VERIFY(desk.done);
    QVERIFY(!desk.session.isProjectBusy());
    desk.controller.imageSize(desk.finished());
    desk.dialog().findChild<QPushButton *>(QStringLiteral("imageCancel"))->click();
    QTRY_VERIFY(desk.done);
    QVERIFY(desk.session.document().value() == before);
    QVERIFY(!desk.session.isProjectBusy());
    QTRY_VERIFY(!desk.window.findChild<QDialog *>());
    // Busy, or without a window, no sheet opens.
    desk.session.setIsProjectBusy(true);
    desk.controller.canvasSize(desk.finished());
    QTRY_VERIFY(desk.done);
    QVERIFY(!desk.window.findChild<QDialog *>());
    desk.session.setIsProjectBusy(false);
    desk.controller.window = nullptr;
    desk.controller.imageSize(desk.finished());
    QTRY_VERIFY(desk.done);
    desk.controller.canvasSize(desk.finished());
    QTRY_VERIFY(desk.done);
    QVERIFY(!desk.session.isProjectBusy());
    QVERIFY(desk.session.document().value() == before);
}

void ProjectResizeTests::theImageSizeSheetScalesThePixelsAsOneStep()
{
    Desk desk;
    desk.controller.imageSize(desk.finished());
    QCOMPARE(desk.dialog().windowTitle(), QString("Image Size"));
    QVERIFY(desk.dialog().findChild<ImageSizeSheet *>());
    desk.type("imageWidth", "50");
    QTRY_VERIFY(desk.done);
    QCOMPARE(desk.session.document().value().width, 50);
    QCOMPARE(desk.session.document().value().height, 25);
    QCOMPARE(desk.session.activeLayer().value().transform.size, QSizeF(5, 5));
    QCOMPARE(desk.session.history.undoName(), QString("Image Size"));
    QVERIFY(!desk.session.isProjectBusy());
}

void ProjectResizeTests::aResizeThatFailsIsExplained()
{
    Desk desk;
    // Stretched past what twice the size may hold.
    LayerTransform wide = desk.session.activeLayer().value().transform;
    wide.size = QSizeF(20'000, 10);
    desk.session.beginTransform();
    desk.session.previewTransform(wide);
    desk.session.commitTransform();
    const CanvasDocument before = desk.session.document().value();
    DialogDesk alerts;
    alerts.replies = {"OK"};
    alerts.note = [&] { return QStringLiteral("busy %1").arg(desk.session.isProjectBusy()); };
    desk.controller.imageSize(desk.finished());
    desk.type("imageWidth", "200");
    QTRY_VERIFY(desk.done);
    QCOMPARE(alerts.seen, (QStringList{QStringLiteral("alert|2|Couldn’t resize the image|%1|OK|busy 1")
                                           .arg(QString::fromUtf8(ProjectError(ProjectError::Kind::tooLarge).what()))}));
    QVERIFY(desk.session.document().value() == before);
    QVERIFY(!desk.session.isProjectBusy());
    // The canvas's own failure, alike, under its own title.
    alerts.replies = {"OK"};
    wide.origin = QPointF(999'000, 0);
    wide.size = QSizeF(10, 10);
    desk.session.beginTransform();
    desk.session.previewTransform(wide);
    desk.session.commitTransform();
    desk.controller.canvasSize(desk.finished());
    desk.type("canvasWidth", "5000");
    QTRY_VERIFY(desk.done);
    QCOMPARE(alerts.seen.size(), 2);
    QVERIFY(alerts.seen.last().startsWith("alert|2|Couldn’t change canvas size|"));
}

void ProjectResizeTests::theSheetsColourOpensTheAppsPickerOverIt()
{
    Desk desk;
    new ColorPaletteControls(desk.session, &desk.window);
    desk.controller.canvasSize(desk.finished());
    auto &extension = *desk.dialog().findChild<QComboBox *>(QStringLiteral("canvasExtension"));
    extension.setCurrentIndex(5);
    emit extension.activated(5);
    desk.dialog().findChild<DialogColorSwatch *>()->click();
    // The panel sits over the sheet and takes the keys.
    QWidget *panel = nullptr;
    QTRY_VERIFY((panel = desk.dialog().findChild<QDialog *>(ColorPickerPanelController::identifier())) && panel->isVisible());
    QTRY_COMPARE(QApplication::activeWindow(), panel);
    desk.session.setColorPickerHSB(PickerHSB(PaletteColor{0, 0, 1}));
    // Escape there cancels the picker; the sheet stays.
    QTest::keyClick(panel, Qt::Key_Escape);
    QTRY_VERIFY(!desk.session.colorPicker() && !panel->isVisible());
    QVERIFY(desk.dialog().isVisible() && !desk.done);
    desk.dialog().findChild<DialogColorSwatch *>()->click();
    QTRY_VERIFY(panel->isVisible());
    desk.session.setColorPickerHSB(PickerHSB(PaletteColor{0, 0, 1}));
    panel->findChild<QPushButton *>(QStringLiteral("pickerOK"))->click();
    QVERIFY(!desk.session.colorPicker());
    desk.type("canvasWidth", "200");
    QTRY_VERIFY(desk.done);
    // The added canvas takes the colour picked.
    const ImageLayer bottom = desk.session.document().value().layers.front();
    QCOMPARE(bottom.asset.value().image().pixelColor(0, 25), QColor(0, 0, 255));
    QVERIFY(!desk.session.isProjectBusy());
}

QTEST_MAIN(ProjectResizeTests)
#include "ProjectResizeTests.moc"
