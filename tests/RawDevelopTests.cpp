#include "BudgetFixtures.h"
#include "ContentView.h"
#include "DNGFixture.h"
#include "IO/RawImporter.h"
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QThreadPool>
#include <QTimer>
#include <QtTest>

// A camera RAW through the import and the develop sheet.
namespace {
QStringList names(const EditorSession &session)
{
    QStringList listed;
    for (const ImageLayer &each : session.document().value().layers)
        listed << each.name;
    return listed;
}

QUrl png(const QTemporaryDir &folder)
{
    QImage image(4, 2, QImage::Format_RGBA8888);
    image.fill(Qt::red);
    const QString path = folder.filePath("Plain.png");
    if (!image.save(path))
        throw std::runtime_error("could not write a PNG");
    return QUrl::fromLocalFile(path);
}

QUrl dng(const QTemporaryDir &folder, const QString &name, const DNGFixture::Options &options = {})
{
    return QUrl::fromLocalFile(DNGFixture::write(folder.filePath(name), options));
}

QColor centre(const ImageLayer &layer)
{
    const QImage image = layer.asset.value().image();
    return image.pixelColor(image.width() / 2, image.height() / 2);
}

QColor centre(const QImage &image)
{
    return image.pixelColor(image.width() / 2, image.height() / 2);
}

QString label(QWidget *sheet, const char *name)
{
    return sheet->findChild<QLabel *>(name)->text();
}
}

class RawDevelopTests : public QObject {
    Q_OBJECT
private slots:
    void theSheetsSettingsDevelopTheLayer();
    void cancelSkipsTheFileAndTheRestImport();
    void whatCannotBeDevelopedIsReported();
    void theLimitsLetTheirEdgeThrough();
    void theSheetPreviewsAndAnswers();
};

void RawDevelopTests::theSheetsSettingsDevelopTheLayer()
{
    const QTemporaryDir folder;
    const QUrl raw = dng(folder, "Gray.dng");
    EditorSession session;
    bool done = false;
    session.importImages({raw, png(folder)}, std::nullopt, [&done] { done = true; });
    QTRY_VERIFY(session.rawDevelop());
    QCOMPARE(session.rawDevelop().value().path, raw.toLocalFile());
    QVERIFY(session.rawDevelop().value().settings == RawImporter::asShot(raw.toLocalFile()).value());
    QVERIFY(session.isImporting() && !session.document());
    RawDevelopSettings settings = session.rawDevelop().value().settings;
    settings.boost = 0;
    settings.exposure = 1;
    // The sheet hears at once that it has gone.
    QSignalSpy changed(&session, &EditorSession::changed);
    session.finishRawDevelop(settings);
    QVERIFY(!session.rawDevelop() && changed.count() == 1);
    QTRY_VERIFY(done);
    QCOMPARE(names(session), (QStringList{"Gray", "Plain"}));
    const ImageLayer &developed = session.document().value().layers.front();
    QCOMPARE(developed.asset.value().size(), QSize(64, 48));
    QCOMPARE(centre(developed), QColor(205, 205, 205));
    QCOMPARE(developed.asset.value().thumbnail.size(), QSize(64, 48));
    QCOMPARE(session.history.undoName(), QString("Import Images"));
    QVERIFY(!session.importError());
}

void RawDevelopTests::cancelSkipsTheFileAndTheRestImport()
{
    const QTemporaryDir folder;
    EditorSession session;
    bool done = false;
    session.importImages({dng(folder, "Gray.dng"), png(folder)}, std::nullopt, [&done] { done = true; });
    QTRY_VERIFY(session.rawDevelop());
    session.finishRawDevelop(std::nullopt);
    QTRY_VERIFY(done);
    QCOMPARE(names(session), QStringList{"Plain"});
    QVERIFY(!session.importError() && !session.rawDevelop());
}

void RawDevelopTests::whatCannotBeDevelopedIsReported()
{
    const QTemporaryDir folder;
    QFile fake(folder.filePath("Fake.dng"));
    QVERIFY(fake.open(QIODevice::WriteOnly) && fake.write("not a raw") == 9);
    fake.close();
    DNGFixture::Options wide, tall, big;
    wide.claimed = QSize(30'001, 48);
    tall.claimed = QSize(64, 30'001);
    big.claimed = QSize(30'000, 30'000);
    // Rows of 30,000 within the document budget.
    const int rows = int(DocumentLimits::documentPixelBudget() / 30'000);
    DNGFixture::Options fits;
    fits.claimed = QSize(30'000, rows);
    const QUrl fitting = dng(folder, "Fits.dng", fits);
    QCOMPARE(RawImporter::pixelSize(fitting.toLocalFile()), std::optional(QSize(30'000, rows)));
    EditorSession session;
    bool done = false;
    const QList<QUrl> urls{QUrl::fromLocalFile(fake.fileName()), dng(folder, "Wide.dng", wide), dng(folder, "Tall.dng", tall), dng(folder, "Big.dng", big), png(folder)};
    session.importImages(urls, std::nullopt, [&done] { done = true; });
    QTRY_VERIFY(done);
    QVERIFY(!session.rawDevelop());
    const QString unreadable = ImageImportError(ImageImportError::Kind::unreadable).what(), tooLarge = ImageImportError(ImageImportError::Kind::tooLarge).what();
    QCOMPARE(session.importError().value(), QStringList({"Fake.dng: " + unreadable, "Wide.dng: " + tooLarge, "Tall.dng: " + tooLarge, "Big.dng: " + tooLarge}).join("\n\n"));
    QCOMPARE(names(session), QStringList{"Plain"});
    // Accepted, then unreadable when the full develop runs.
    session.setImportError(std::nullopt);
    done = false;
    const QUrl gray = dng(folder, "Gray.dng");
    session.importImages({gray}, std::nullopt, [&done] { done = true; });
    QTRY_VERIFY(session.rawDevelop());
    QFile broken(gray.toLocalFile());
    QVERIFY(broken.open(QIODevice::WriteOnly) && broken.write("gone") == 4);
    broken.close();
    session.finishRawDevelop(session.rawDevelop().value().settings);
    QTRY_VERIFY(done);
    QCOMPARE(session.importError().value(), "Gray.dng: " + unreadable);
    QCOMPARE(names(session), QStringList{"Plain"});
    // A white the camera cannot record says so.
    session.setImportError(std::nullopt);
    done = false;
    const QUrl warm = dng(folder, "Warm.dng");
    session.importImages({warm}, std::nullopt, [&done] { done = true; });
    QTRY_VERIFY(session.rawDevelop());
    RawDevelopSettings candle = session.rawDevelop().value().settings;
    candle.temperature = 2000;
    session.finishRawDevelop(candle);
    QTRY_VERIFY(done);
    QCOMPARE(session.importError().value(), "Warm.dng: " + QString(ImageImportError(ImageImportError::Kind::whiteBalance).what()));
    QCOMPARE(names(session), QStringList{"Plain"});
    // Past one surface yet within the document budget, it opens.
    session.setImportError(std::nullopt);
    done = false;
    session.importImages({fitting}, std::nullopt, [&done] { done = true; });
    QTRY_VERIFY(session.rawDevelop());
    session.finishRawDevelop(std::nullopt);
    QTRY_VERIFY(done);
    QCOMPARE(session.importError(), std::nullopt);
    // The slack filled, the canvas's 8 pixels tip it.
    for (const ImportedImage &layer : claiming(DocumentLimits::documentPixelBudget() - qint64(rows) * 30'000))
        session.insert(layer);
    session.setImportError(std::nullopt);
    done = false;
    session.importImages({fitting}, std::nullopt, [&done] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(session.importError().value(), "Fits.dng: " + tooLarge);
}

void RawDevelopTests::theLimitsLetTheirEdgeThrough()
{
    const QTemporaryDir folder;
    for (const QSize claimed : {QSize(30'000, 48), QSize(64, 30'000), QSize(10'000, 10'000)}) {
        DNGFixture::Options edge;
        edge.claimed = claimed;
        EditorSession session;
        bool done = false;
        session.importImages({dng(folder, "Edge.dng", edge)}, std::nullopt, [&done] { done = true; });
        QTRY_VERIFY(session.rawDevelop());
        session.finishRawDevelop(std::nullopt);
        QTRY_VERIFY(done);
        QVERIFY(!session.importError() && !session.document());
    }
}

void RawDevelopTests::theSheetPreviewsAndAnswers()
{
    const QTemporaryDir folder;
    const QUrl raw = dng(folder, "Gray.dng");
    const QString path = raw.toLocalFile();
    EditorSession session;
    ContentView view(session);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    bool done = false;
    session.importImages({raw}, std::nullopt, [&done] { done = true; });
    QDialog *sheet = view.findChild<QDialog *>("rawDevelopSheet");
    QVERIFY(sheet);
    // The first preview waits for nothing.
    QTimer *wait = sheet->findChild<QTimer *>("rawWait");
    QVERIFY(!wait->isActive());
    // Another change keeps the one sheet.
    session.notify();
    QCOMPARE(view.findChildren<QDialog *>("rawDevelopSheet").size(), qsizetype(1));
    QVERIFY(sheet->isVisible() && sheet->windowModality() == Qt::WindowModal);
    QStringList words;
    for (const QLabel *each : sheet->findChildren<QLabel *>())
        words << each->text();
    QVERIFY(words.contains("Develop “Gray.dng”"));
    QCOMPARE(label(sheet, "rawExposureValue"), QString("0.00 EV"));
    QCOMPARE(label(sheet, "rawTemperatureValue"), QString("6501 K"));
    QCOMPARE(label(sheet, "rawTintValue"), QString("10"));
    QCOMPARE(label(sheet, "rawBoostValue"), QString("1.00"));
    QPushButton *reset = sheet->findChild<QPushButton *>("rawReset");
    QVERIFY(!reset->isEnabled());
    // The first preview lands: the boosted gray, fitted.
    QWidget *preview = sheet->findChild<QWidget *>("rawPreview");
    QTRY_VERIFY(sheet->findChild<QProgressBar *>("rawSpinner")->isHidden());
    QCOMPARE(preview->grab().toImage().pixelColor(200, 170), QColor(139, 139, 139));
    // A develop under way spins; a newer change drops it.
    QSlider *exposure = sheet->findChild<QSlider *>("rawExposure");
    QProgressBar *spinner = sheet->findChild<QProgressBar *>("rawSpinner");
    bool spinning = false;
    const QMetaObject::Connection once = connect(wait, &QTimer::timeout, wait, [&] {
        spinning = !spinner->isHidden();
        exposure->setValue(-100);
        wait->stop();
    });
    exposure->setValue(100);
    QTRY_VERIFY(spinning);
    disconnect(once);
    QThreadPool::globalInstance()->waitForDone();
    QTest::qWait(50);
    QCOMPARE(preview->grab().toImage().pixelColor(200, 170), QColor(139, 139, 139));
    QVERIFY(!spinner->isHidden());
    RawDevelopSettings dimmer = session.rawDevelop().value().settings;
    dimmer.exposure = -1;
    wait->start();
    QTRY_COMPARE_WITH_TIMEOUT(preview->grab().toImage().pixelColor(200, 170), centre(RawImporter::develop(path, dimmer)), 1000);
    QVERIFY(spinner->isHidden());
    // A slider changes the settings; the preview follows.
    exposure->setValue(100);
    QCOMPARE(label(sheet, "rawExposureValue"), QString("1.00 EV"));
    QVERIFY(reset->isEnabled());
    RawDevelopSettings brighter = session.rawDevelop().value().settings;
    brighter.exposure = 1;
    const QColor expected = centre(RawImporter::develop(path, brighter));
    QTRY_COMPARE(preview->grab().toImage().pixelColor(200, 170), expected);
    sheet->findChild<QSlider *>("rawTemperature")->setValue(3000);
    QCOMPARE(label(sheet, "rawTemperatureValue"), QString("3000 K"));
    reset->click();
    QCOMPARE(label(sheet, "rawExposureValue"), QString("0.00 EV"));
    QCOMPARE(label(sheet, "rawTemperatureValue"), QString("6501 K"));
    QCOMPARE(sheet->findChild<QSlider *>("rawExposure")->value(), 0);
    QVERIFY(!reset->isEnabled());
    QTRY_COMPARE(preview->grab().toImage().pixelColor(200, 170), QColor(139, 139, 139));
    // Import develops the whole frame with the sheet's settings.
    sheet->findChild<QSlider *>("rawExposure")->setValue(100);
    sheet->findChild<QPushButton *>("rawImport")->click();
    QVERIFY(!session.rawDevelop());
    QTRY_VERIFY(done);
    QTRY_VERIFY(!view.findChild<QDialog *>("rawDevelopSheet"));
    QCOMPARE(centre(session.document().value().layers.back()), expected);
    // Escape cancels; the kept decode goes with the sheet.
    done = false;
    session.importImages({raw}, std::nullopt, [&done] { done = true; });
    QTRY_VERIFY(view.findChild<QDialog *>("rawDevelopSheet"));
    sheet = view.findChild<QDialog *>("rawDevelopSheet");
    QTRY_VERIFY(sheet->findChild<QProgressBar *>("rawSpinner")->isHidden());
    QTest::keyClick(sheet, Qt::Key_Escape);
    QTRY_VERIFY(done);
    QTRY_VERIFY(!view.findChild<QDialog *>("rawDevelopSheet"));
    QCOMPARE(session.document().value().layers.size(), size_t(1));
    QThreadPool::globalInstance()->waitForDone();
    DNGFixture::Options darker;
    darker.value = [](int, int, int) { return quint16(10000); };
    DNGFixture::write(path, darker);
    QVERIFY(centre(RawImporter::Queue::shared().develop(path, RawImporter::asShot(path).value(), 800)).red() < 139);
    // Cancel answers no as well.
    done = false;
    session.importImages({raw}, std::nullopt, [&done] { done = true; });
    QTRY_VERIFY(view.findChild<QDialog *>("rawDevelopSheet"));
    view.findChild<QDialog *>("rawDevelopSheet")->findChild<QPushButton *>("rawCancel")->click();
    QTRY_VERIFY(done);
    QCOMPARE(session.document().value().layers.size(), size_t(1));
}

QTEST_MAIN(RawDevelopTests)
#include "RawDevelopTests.moc"
