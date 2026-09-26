#include "UI/ByteCounts.h"
#include "UI/ColorPaletteControls.h"
#include "UI/JPEGExportSheet.h"
#include <QColorDialog>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QStandardPaths>
#include <QTimer>
#include <QtTest>

// Swift's JPEGExportSheet: the preview waits, encodes aside, then exports.
class JPEGExportSheetTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void theSheetShowsTheRasterAndWaitsForItsPreview();
    void aChangeSupersedesThePreview();
    void aSupersededEncodingNeverLands();
    void theMatteComesFromTheColorDialog();
    void exportKeepsTheQualityAndCancelDoesNot();
    void theSavedQualityIsReadAsSwiftReadsIt();
    void anEncodingThatFailsIsShown();
};

namespace {
// Clear on the left, opaque red on the right.
ExportRaster halfRed(int width = 64, int height = 32)
{
    QImage image(width, height, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::transparent);
    for (int y = 0; y < height; ++y)
        for (int x = width / 2; x < width; ++x)
            image.setPixel(x, y, qRgba(255, 0, 0, 255));
    return {image, 144};
}

struct Sheet {
    std::optional<std::optional<QByteArray>> answer;
    JPEGExportSheet sheet;
    explicit Sheet(ExportRaster raster = halfRed())
        : sheet(std::move(raster), [this](std::optional<QByteArray> data) { answer = std::move(data); })
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
    bool ready() { return find<QPushButton>("jpegExport").isEnabled(); }
    QString note() { return find<QLabel>("jpegNote").text(); }
    QColor previewAt(QPoint point) { return find<QWidget>("jpegPreview").grab().toImage().pixelColor(point); }
};

struct PaintCount : QObject {
    int count = 0;
    explicit PaintCount(QWidget &widget) { widget.installEventFilter(this); }
    bool eventFilter(QObject *, QEvent *event) override
    {
        count += event->type() == QEvent::Paint;
        return false;
    }
};

QStringList labels(const QWidget &widget)
{
    QStringList texts;
    for (const QLabel *label : widget.findChildren<QLabel *>())
        texts << label->text();
    return texts;
}
}

void JPEGExportSheetTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QSettings().clear();
}

void JPEGExportSheetTests::theSheetShowsTheRasterAndWaitsForItsPreview()
{
    Sheet shown(halfRed(1200, 800));
    const QStringList texts = labels(shown.sheet);
    QVERIFY(texts.contains("Export JPEG"));
    QVERIFY(texts.contains("1,200 × 800 px · sRGB"));
    QVERIFY(texts.contains("Background for transparency"));
    QCOMPARE(shown.find<QLabel>("jpegPercent").text(), QString("85%"));
    QCOMPARE(shown.find<QSlider>("jpegQuality").value(), 85);
    QCOMPARE(shown.find<QWidget>("jpegPreview").size(), QSize(560, 330));
    // Swift's task sleeps 200 ms before it encodes.
    auto &wait = shown.find<QTimer>("jpegWait");
    QVERIFY(wait.isActive() && wait.isSingleShot());
    QCOMPARE(wait.interval(), 200);
    QCOMPARE(wait.timerType(), Qt::PreciseTimer);
    QVERIFY(!shown.ready());
    QCOMPARE(shown.note(), QString("Updating preview…"));
    QVERIFY(shown.find<QProgressBar>("jpegSpinner").isVisible());
    QVERIFY(!shown.find<QLabel>("jpegBytes").isVisible() && !shown.find<QLabel>("jpegError").isVisible());
    // The material plate behind the spinner, over the dark gray.
    QCOMPARE(shown.previewAt({2, 2}), QColor(31, 31, 31));
    const QColor window = shown.sheet.palette().color(QPalette::Window), plate = shown.previewAt({252, 165});
    QVERIFY(qAbs(plate.red() - qRound(window.red() * 0.85 + 31 * 0.15)) <= 1);
    QVERIFY(qAbs(plate.blue() - qRound(window.blue() * 0.85 + 31 * 0.15)) <= 1);
    QTRY_VERIFY(shown.ready());
    const JPEGResult expected = ImageExporter::jpeg(halfRed(1200, 800), {});
    QCOMPARE(shown.find<QLabel>("jpegBytes").text(), ByteCounts::file(expected.data.size()));
    QVERIFY(shown.find<QLabel>("jpegBytes").isVisible());
    QCOMPARE(shown.note(), QString("· encoded preview, fitted to window"));
    QVERIFY(!shown.find<QProgressBar>("jpegSpinner").isVisible());
    // Fitted to 495 × 330 and centred: white, then red.
    QCOMPARE(shown.previewAt({2, 2}), QColor(31, 31, 31));
    QCOMPARE(shown.previewAt({10, 165}), QColor(31, 31, 31));
    QVERIFY(qAbs(shown.previewAt({100, 165}).red() - 255) < 4 && qAbs(shown.previewAt({100, 165}).blue() - 255) < 4);
    const QColor red = shown.previewAt({460, 165});
    QVERIFY(red.red() > 240 && red.green() < 16 && red.blue() < 16);
}

void JPEGExportSheetTests::aChangeSupersedesThePreview()
{
    Sheet shown;
    QTRY_VERIFY(shown.ready());
    auto &quality = shown.find<QSlider>("jpegQuality");
    // Each change waits anew; the stale preview stays beneath.
    quality.setValue(10);
    QCOMPARE(shown.find<QLabel>("jpegPercent").text(), QString("10%"));
    QVERIFY(!shown.ready());
    QCOMPARE(shown.note(), QString("Updating preview…"));
    QVERIFY(shown.find<QTimer>("jpegWait").isActive());
    QVERIFY(shown.find<QProgressBar>("jpegSpinner").isVisible());
    quality.setValue(100);
    QTRY_VERIFY(shown.ready());
    QCOMPARE(shown.find<QLabel>("jpegBytes").text(), ByteCounts::file(ImageExporter::jpeg(halfRed(), {.quality = 1}).data.size()));
    shown.find<QPushButton>("jpegExport").click();
    QCOMPARE(shown.answer.value().value(), ImageExporter::jpeg(halfRed(), {.quality = 1}).data);
}

// Slow to encode: the change comes while it runs.
void JPEGExportSheetTests::aSupersededEncodingNeverLands()
{
    QImage noisy(6000, 4000, QImage::Format_RGBA8888_Premultiplied);
    quint32 seed = 1;
    for (int y = 0; y < noisy.height(); ++y) {
        auto *row = reinterpret_cast<quint32 *>(noisy.scanLine(y));
        for (int x = 0; x < noisy.width(); ++x)
            row[x] = (seed = seed * 1664525u + 1013904223u) | 0xff000000u;
    }
    Sheet shown({noisy, 72});
    QTRY_VERIFY(!shown.find<QTimer>("jpegWait").isActive());
    shown.find<QSlider>("jpegQuality").setValue(50);
    // Only the newest request's preview may ever show.
    bool stale = false;
    while (!shown.ready()) {
        stale = stale || shown.previewAt({100, 165}) != QColor(31, 31, 31);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    QVERIFY(!stale);
    QCOMPARE(shown.find<QLabel>("jpegPercent").text(), QString("50%"));
}

void JPEGExportSheetTests::theMatteComesFromTheColorDialog()
{
    QSettings().remove(JPEGExportSheet::qualityKey);
    Sheet shown;
    QTRY_VERIFY(shown.ready());
    shown.find<SwatchButton>("jpegMatte").click();
    auto *dialog = shown.sheet.findChild<QColorDialog *>();
    QVERIFY(dialog && dialog->isVisible());
    QVERIFY(!dialog->testOption(QColorDialog::ShowAlphaChannel));
    QCOMPARE(dialog->currentColor(), QColor(Qt::white));
    // The same colour asks for nothing.
    dialog->setCurrentColor(Qt::white);
    dialog->accept();
    QVERIFY(shown.ready() && !shown.find<QTimer>("jpegWait").isActive());
    shown.find<SwatchButton>("jpegMatte").click();
    dialog = shown.sheet.findChild<QColorDialog *>();
    dialog->setCurrentColor(QColor(0, 0, 255));
    // The well repaints with its colour.
    PaintCount painted(shown.find<SwatchButton>("jpegMatte"));
    dialog->accept();
    QTRY_VERIFY(painted.count > 0);
    QVERIFY(!shown.ready());
    QCOMPARE(shown.find<SwatchButton>("jpegMatte").grab().toImage().pixelColor(18, 9), QColor(0, 0, 255));
    QTRY_VERIFY(shown.ready());
    shown.find<QPushButton>("jpegExport").click();
    const QImage written = QImage::fromData(shown.answer.value().value(), "jpeg");
    const QColor left = written.pixelColor(8, 16), right = written.pixelColor(56, 16);
    QVERIFY(left.blue() > 240 && left.red() < 16 && left.green() < 16);
    QVERIFY(right.red() > 240 && right.blue() < 16);
    QCOMPARE(shown.answer.value().value(), ImageExporter::jpeg(halfRed(), {.red = 0, .green = 0, .blue = 1}).data);
}

void JPEGExportSheetTests::exportKeepsTheQualityAndCancelDoesNot()
{
    QSettings().remove(JPEGExportSheet::qualityKey);
    {
        Sheet shown;
        shown.find<QSlider>("jpegQuality").setValue(40);
        QTRY_VERIFY(shown.ready());
        shown.find<QPushButton>("jpegCancel").click();
        QVERIFY(!shown.answer.value().has_value());
        QVERIFY(!QSettings().contains(JPEGExportSheet::qualityKey));
    }
    {
        Sheet shown;
        QCOMPARE(shown.find<QSlider>("jpegQuality").value(), 85);
        shown.find<QSlider>("jpegQuality").setValue(40);
        QTRY_VERIFY(shown.ready());
        shown.find<QPushButton>("jpegExport").click();
        QCOMPARE(QSettings().value(JPEGExportSheet::qualityKey).toDouble(), 0.4);
    }
    Sheet next;
    QCOMPARE(next.find<QSlider>("jpegQuality").value(), 40);
    QCOMPARE(next.find<QLabel>("jpegPercent").text(), QString("40%"));
    // Return reaches Export once it is ready.
    QTRY_VERIFY(next.ready());
    QVERIFY(next.find<QPushButton>("jpegExport").isDefault());
    QCOMPARE(next.find<QPushButton>("jpegExport").text(), QString("Export…"));
}

void JPEGExportSheetTests::theSavedQualityIsReadAsSwiftReadsIt()
{
    const auto started = [](const QVariant &saved) {
        QSettings().setValue(JPEGExportSheet::qualityKey, saved);
        Sheet shown;
        return shown.find<QLabel>("jpegPercent").text();
    };
    QCOMPARE(started(0.333), QString("33%"));
    QCOMPARE(started(0.875), QString("88%"));
    QCOMPARE(started(7), QString("100%"));
    QCOMPARE(started(-2), QString("0%"));
    QCOMPARE(started(QStringLiteral("high")), QString("85%"));
    QCOMPARE(started(std::nan("")), QString("85%"));
    QCOMPARE(started(qInf()), QString("85%"));
    QSettings().remove(JPEGExportSheet::qualityKey);
}

void JPEGExportSheetTests::anEncodingThatFailsIsShown()
{
    // JPEG holds at most 65,500 pixels a side.
    Sheet shown(halfRed(70'000, 1));
    auto &error = shown.find<QLabel>("jpegError");
    QTRY_VERIFY(error.isVisible());
    QCOMPARE(error.text(), QString::fromUtf8(ExportError(ExportError::Kind::encode).what()));
    QCOMPARE(error.foregroundRole(), QPalette::BrightText);
    QVERIFY(!shown.ready() && !shown.find<QLabel>("jpegNote").isVisible() && !shown.find<QProgressBar>("jpegSpinner").isVisible());
    // A change clears the error while it waits.
    shown.find<QSlider>("jpegQuality").setValue(50);
    QVERIFY(!error.isVisible());
    QCOMPARE(shown.note(), QString("Updating preview…"));
    QTRY_VERIFY(error.isVisible());
}

QTEST_MAIN(JPEGExportSheetTests)
#include "JPEGExportSheetTests.moc"
