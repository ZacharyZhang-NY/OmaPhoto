#include "UI/NewCanvasSheet.h"
#include <QBuffer>
#include <QClipboard>
#include <QImageReader>
#include <QImageWriter>
#include <QtTest>

// The welcome sheet: sizes, its three ways on, the clipboard.
namespace {
struct Sheet {
    EditorSession session;
    QList<QSize> created;
    int opened = 0;
    NewCanvasSheet sheet{session, [this](int width, int height) { created << QSize(width, height); }, [this] { opened += 1; }};
    QLineEdit &width = *sheet.findChild<QLineEdit *>("widthInput");
    QLineEdit &height = *sheet.findChild<QLineEdit *>("heightInput");
    QLabel &note = *sheet.findChild<QLabel *>("canvasNote");
    QPushButton &create = *sheet.findChild<QPushButton *>("createCanvas");
};

QByteArray encoded(const QImage &image, const char *format, QImageIOHandler::Transformations turned = QImageIOHandler::TransformationNone)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    QImageWriter writer(&buffer, format);
    writer.setTransformation(turned);
    if (!writer.write(image))
        throw std::runtime_error("the fixture could not be encoded");
    return bytes;
}
}

class NewCanvasSheetTests : public QObject {
    Q_OBJECT
private slots:
    void cleanup();
    void itStartsAtFullHighDefinition();
    void onlyWholeNumbersInRangeMayCreate();
    void returnInEitherFieldCreates();
    void theOtherTwoButtonsOpenAndImport();
    void itGivesWayWhileTheSessionWorks();
    void aCopiedPicturesSizeIsSuggestedOnce();
    void theFirstTabSkipsTheClipboardOnce();
    void clipboardSizesComeFromHeadersAlone();
};

void NewCanvasSheetTests::cleanup()
{
    QApplication::clipboard()->clear();
}

void NewCanvasSheetTests::itStartsAtFullHighDefinition()
{
    Sheet fixture;
    fixture.sheet.show();
    QVERIFY(QTest::qWaitForWindowExposed(&fixture.sheet));
    QCOMPARE(fixture.width.text(), QString("1920"));
    QCOMPARE(fixture.height.text(), QString("1080"));
    QCOMPARE(fixture.note.text(), QString("Transparent canvas · sRGB"));
    QCOMPARE(fixture.note.foregroundRole(), QPalette::PlaceholderText);
    QVERIFY(fixture.create.isEnabled() && fixture.create.isDefault() && fixture.width.hasFocus());
    QCOMPARE(fixture.sheet.width(), 500);
    QTest::mouseClick(&fixture.create, Qt::LeftButton);
    QCOMPARE(fixture.created, (QList<QSize>{QSize(1920, 1080)}));
}

void NewCanvasSheetTests::onlyWholeNumbersInRangeMayCreate()
{
    Sheet fixture;
    // Shown and active, so that Return reaches the sheet.
    fixture.sheet.show();
    fixture.sheet.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&fixture.sheet));
    const auto valid = [&](const QString &width, const QString &height) {
        fixture.width.setText(width);
        fixture.height.setText(height);
        return fixture.create.isEnabled();
    };
    QVERIFY(valid("1", "30000"));
    QVERIFY(valid("30000", "1"));
    QVERIFY(valid(" 640 ", "480"));
    // Either field alone refuses, by button, Return and Enter.
    const auto refused = [&] {
        const bool warned = fixture.note.text() == "Enter whole numbers from 1 to 30,000 pixels." && fixture.note.foregroundRole() == QPalette::BrightText;
        fixture.create.click();
        QTest::keyClick(&fixture.width, Qt::Key_Return);
        QTest::keyClick(&fixture.height, Qt::Key_Enter);
        fixture.sheet.findChild<QPushButton *>("openProject")->setFocus();
        QTest::keyClick(&fixture.sheet, Qt::Key_Return);
        return warned && fixture.created.isEmpty();
    };
    for (const QString &bad : {QString("0"), QString("30001"), QString(""), QString("12.5"), QString("-4"), QString("1e3"), QString("wide")}) {
        QVERIFY2(!valid(bad, "480") && refused(), qPrintable(bad));
        QVERIFY2(!valid("640", bad) && refused(), qPrintable(bad));
    }
    QVERIFY(fixture.created.isEmpty() && fixture.opened == 0);
    QVERIFY(valid("640", "480"));
    QCOMPARE(fixture.note.text(), QString("Transparent canvas · sRGB"));
}

void NewCanvasSheetTests::returnInEitherFieldCreates()
{
    Sheet fixture;
    fixture.sheet.show();
    QVERIFY(QTest::qWaitForWindowExposed(&fixture.sheet));
    fixture.width.setText("300");
    fixture.height.setText(" 200");
    QTest::keyClick(&fixture.width, Qt::Key_Return);
    QTest::keyClick(&fixture.height, Qt::Key_Return);
    QCOMPARE(fixture.created, (QList<QSize>{QSize(300, 200), QSize(300, 200)}));
    // Return with a button in focus creates as well.
    fixture.sheet.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&fixture.sheet));
    fixture.sheet.findChild<QPushButton *>("openProject")->setFocus();
    QTest::keyClick(&fixture.sheet, Qt::Key_Return);
    fixture.sheet.findChild<QPushButton *>("importImage")->setFocus();
    QTest::keyClick(&fixture.sheet, Qt::Key_Enter);
    QCOMPARE(fixture.created.size(), 4);
    QCOMPARE(fixture.opened, 0);
    QVERIFY(!fixture.session.showsImporter());
}

void NewCanvasSheetTests::theOtherTwoButtonsOpenAndImport()
{
    Sheet fixture;
    QSignalSpy changes(&fixture.session, &EditorSession::changed);
    fixture.sheet.findChild<QPushButton *>("openProject")->click();
    QCOMPARE(fixture.opened, 1);
    QCOMPARE(changes.count(), 0);
    QVERIFY(!fixture.session.showsImporter());
    fixture.sheet.findChild<QPushButton *>("importImage")->click();
    QVERIFY(fixture.session.showsImporter());
    QVERIFY(fixture.created.isEmpty() && fixture.opened == 1);
}

void NewCanvasSheetTests::itGivesWayWhileTheSessionWorks()
{
    Sheet fixture;
    QVERIFY(fixture.sheet.isEnabled());
    fixture.session.setIsImporting(true);
    QVERIFY(!fixture.sheet.isEnabled());
    fixture.session.setIsImporting(false);
    QVERIFY(fixture.sheet.isEnabled());
    // Busy dims only once it shows, a quarter second in.
    fixture.session.setIsProjectBusy(true);
    QVERIFY(fixture.sheet.isEnabled());
    QTRY_VERIFY(!fixture.sheet.isEnabled());
    fixture.session.setIsProjectBusy(false);
    QVERIFY(fixture.sheet.isEnabled());
}

void NewCanvasSheetTests::aCopiedPicturesSizeIsSuggestedOnce()
{
    auto *copied = new QMimeData;
    copied->setData("image/png", encoded(QImage(640, 360, QImage::Format_RGBA8888), "png"));
    QApplication::clipboard()->setMimeData(copied);
    Sheet fixture;
    QCOMPARE(fixture.width.text(), QString("1920"));
    fixture.sheet.show();
    QVERIFY(QTest::qWaitForWindowExposed(&fixture.sheet));
    QCOMPARE(fixture.width.text(), QString("640"));
    QCOMPARE(fixture.height.text(), QString("360"));
    // Shown again, it keeps what the user has typed since.
    fixture.width.setText("111");
    fixture.height.setFocus();
    fixture.sheet.hide();
    fixture.sheet.show();
    QCOMPARE(fixture.width.text(), QString("111"));
    // Shown again, the width is the field to type in.
    QCOMPARE(fixture.sheet.focusWidget(), &fixture.width);
}

void NewCanvasSheetTests::theFirstTabSkipsTheClipboardOnce()
{
    auto *copied = new QMimeData;
    copied->setData("image/png", encoded(QImage(640, 360, QImage::Format_RGBA8888), "png"));
    QApplication::clipboard()->setMimeData(copied);
    Sheet first;
    first.session.skipsInitialClipboardCanvasSize = true;
    first.sheet.show();
    QVERIFY(QTest::qWaitForWindowExposed(&first.sheet));
    QCOMPARE(first.width.text(), QString("1920"));
    QVERIFY(!first.session.skipsInitialClipboardCanvasSize);
    // The session's next sheet reads the clipboard again.
    NewCanvasSheet later(first.session, [](int, int) {}, [] {});
    later.show();
    QVERIFY(QTest::qWaitForWindowExposed(&later));
    QCOMPARE(later.findChild<QLineEdit *>("widthInput")->text(), QString("640"));
}

void NewCanvasSheetTests::clipboardSizesComeFromHeadersAlone()
{
    QVERIFY(!NewCanvasSheet::clipboardDimensions(nullptr).has_value());
    QMimeData clipboard;
    QVERIFY(!NewCanvasSheet::clipboardDimensions(&clipboard).has_value());
    clipboard.setText("640 x 360");
    QVERIFY(!NewCanvasSheet::clipboardDimensions(&clipboard).has_value());
    // A tall TIFF; a PNG beside it comes first.
    clipboard.setData("image/tiff", encoded(QImage(30, 50, QImage::Format_RGBA8888), "tiff"));
    QCOMPARE(NewCanvasSheet::clipboardDimensions(&clipboard), std::optional(QSize(30, 50)));
    clipboard.setData("image/png", encoded(QImage(70, 20, QImage::Format_RGBA8888), "png"));
    QCOMPARE(NewCanvasSheet::clipboardDimensions(&clipboard), std::optional(QSize(70, 20)));
    // No pixel is decoded: past Qt's limit it still counts.
    const int limit = QImageReader::allocationLimit();
    QImageReader::setAllocationLimit(1);
    QMimeData large;
    large.setData("image/png", encoded(QImage(2000, 1000, QImage::Format_RGBA8888), "png"));
    const std::optional<QSize> measured = NewCanvasSheet::clipboardDimensions(&large);
    QImageReader::setAllocationLimit(limit);
    QCOMPARE(measured, std::optional(QSize(2000, 1000)));
    // Too large for a canvas: the next type is tried.
    QMimeData vast;
    vast.setData("image/png", encoded(QImage(30001, 1, QImage::Format_Grayscale8), "png"));
    QVERIFY(!NewCanvasSheet::clipboardDimensions(&vast).has_value());
    vast.setData("image/tiff", encoded(QImage(30, 50, QImage::Format_RGBA8888), "tiff"));
    QCOMPARE(NewCanvasSheet::clipboardDimensions(&vast), std::optional(QSize(30, 50)));
    QMimeData broken;
    broken.setData("image/png", "not a picture");
    QVERIFY(!NewCanvasSheet::clipboardDimensions(&broken).has_value());
    // Too tall as well as too wide is refused.
    QMimeData tall;
    tall.setData("image/png", encoded(QImage(1, 30001, QImage::Format_Grayscale8), "png"));
    QVERIFY(!NewCanvasSheet::clipboardDimensions(&tall).has_value());
    // A TIFF stored on its side swaps its sides.
    QMimeData turned;
    turned.setData("image/tiff", encoded(QImage(30, 50, QImage::Format_RGBA8888), "tiff", QImageIOHandler::TransformationRotate90));
    QCOMPARE(NewCanvasSheet::clipboardDimensions(&turned), std::optional(QSize(50, 30)));
}

QTEST_MAIN(NewCanvasSheetTests)
#include "NewCanvasSheetTests.moc"
