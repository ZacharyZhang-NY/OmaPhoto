#include "ContentView.h"
#include "PSDFixture.h"
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QTemporaryDir>
#include <QtTest>

// A Photoshop file through the session's import and the sheet.
namespace {
PSDRecord layer(const QString &name, QRectF bounds, const char *blend = "norm")
{
    PSDRecord record = PSDFixture::record(name, PSDFixture::colorImage(int(bounds.width()), int(bounds.height()), 0, 0, 1), bounds);
    record.blendKey = blend;
    return record;
}

QUrl write(const QTemporaryDir &folder, const QString &name, const QByteArray &bytes)
{
    QFile file(folder.filePath(name));
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        throw std::runtime_error("could not write a fixture");
    return QUrl::fromLocalFile(file.fileName());
}

QUrl photoshop(const QTemporaryDir &folder, const QString &name, const std::vector<PSDRecord> &layers, QSize size = QSize(4, 2), double dpi = 72)
{
    return write(folder, name, PSDFixture::data(PSDDocument{size.width(), size.height(), dpi, layers}, PSDFixture::colorImage(size.width(), size.height(), 0, 0, 0, 0)));
}

QStringList names(const EditorSession &session)
{
    QStringList listed;
    for (const ImageLayer &each : session.document().value().layers)
        listed << each.name;
    return listed;
}

void import(EditorSession &session, const QList<QUrl> &urls, std::optional<QPointF> point = std::nullopt)
{
    bool done = false;
    session.importImages(urls, point, [&done] { done = true; });
    QVERIFY(QTest::qWaitFor([&done] { return done; }, 20000));
}

PSDImport blanks(size_t count)
{
    PSDImport imported{8, 8, 72, {}, {}};
    for (size_t index = 0; index < count; ++index)
        imported.layers.push_back(ImageLayer(QStringLiteral("Blank"), QSizeF(8, 8)));
    return imported;
}

std::optional<ImageImportError::Kind> refusal(const std::function<void()> &run)
{
    try {
        run();
    } catch (const ImageImportError &error) {
        return error.kind;
    }
    return std::nullopt;
}
}

class PSDImportTests : public QObject {
    Q_OBJECT
private slots:
    void importCreatesDocumentAndExistingCanvasGetsAGroup();
    void cancelledConversionLeavesTheDocumentUnchanged();
    void aConfirmedConversionImportsAndCancelWhileReadingStops();
    void failuresAreReportedAndStepsNamedByTheFiles();
    void aFileLandsAtThePointInsideTheActiveFolder();
    void layerLimitsAndDeepFoldersRefuseTheFile();
    void theSheetReadsThenListsAndAnswers();
    void aFileTheSessionRefusesIsReported();
    void aDirectInsertIsOneStepAndFitsTheView();
    void theSheetKeepsItsLayoutAndItsContent();
};

void PSDImportTests::importCreatesDocumentAndExistingCanvasGetsAGroup()
{
    const QTemporaryDir folder;
    const QUrl url = photoshop(folder, "Trip.psd", {layer("Sky", QRectF(0, 0, 4, 2))}, QSize(4, 2), 144);
    EditorSession session;
    QSignalSpy changed(&session, &EditorSession::changed);
    import(session, {url});
    QCOMPARE(session.document().value().size(), QSizeF(4, 2));
    QCOMPARE(session.document().value().resolution, 144.0);
    QCOMPARE(names(session), QStringList{"Sky"});
    QVERIFY(!session.importError() && !session.showsConversionSheet() && !session.conversionRequest());
    QCOMPARE(session.activeLayerID(), std::optional(session.document().value().layers[0].id));
    QCOMPARE(session.history.undoName(), QString("Import Photoshop File"));
    QVERIFY(changed.count() > 0);
    import(session, {url});
    const std::vector<ImageLayer> &layers = session.document().value().layers;
    QCOMPARE(names(session), (QStringList{"Sky", "Trip", "Sky"}));
    QVERIFY(layers[1].isGroup && !layers[1].parentID);
    QCOMPARE(layers[1].size(), QSizeF(4, 2));
    QCOMPARE(layers[2].parentID, std::optional(layers[1].id));
    QCOMPARE(session.activeLayerID(), std::optional(layers[1].id));
    session.undo();
    QCOMPARE(names(session), QStringList{"Sky"});
}

void PSDImportTests::cancelledConversionLeavesTheDocumentUnchanged()
{
    const QTemporaryDir folder;
    const QUrl url = photoshop(folder, "Vivid.psd", {layer("Vivid", QRectF(0, 0, 2, 2), "vLit")}, QSize(2, 2));
    EditorSession session;
    bool done = false;
    session.importImages({url}, std::nullopt, [&done] { done = true; });
    QVERIFY(session.showsConversionSheet() && session.conversionRequest().value().isReading);
    QTRY_VERIFY(!session.conversionRequest().value().isReading);
    const PSDConversionRequest &request = session.conversionRequest().value();
    QCOMPARE(request.title, QString("Open “Vivid.psd”?"));
    QCOMPARE(request.confirmTitle, QString("Import"));
    QCOMPARE(request.conversions.size(), size_t(1));
    QCOMPARE(request.conversions[0].layerName, QString("Vivid"));
    QVERIFY(session.isImporting() && !done);
    session.finishConversion(false);
    QVERIFY(!session.showsConversionSheet() && !session.conversionRequest());
    QTRY_VERIFY(done);
    QVERIFY(!session.document() && !session.isImporting() && !session.importError());
}

void PSDImportTests::aConfirmedConversionImportsAndCancelWhileReadingStops()
{
    const QTemporaryDir folder;
    const QUrl url = photoshop(folder, "Vivid.psd", {layer("Vivid", QRectF(0, 0, 2, 2), "vLit")}, QSize(2, 2));
    EditorSession session;
    bool done = false;
    session.importImages({url}, std::nullopt, [&done] { done = true; });
    QTRY_VERIFY(session.conversionRequest() && !session.conversionRequest().value().isReading);
    session.finishConversion(true);
    QTRY_VERIFY(done);
    QCOMPARE(names(session), QStringList{"Vivid"});
    QVERIFY(session.document().value().layers[0].blendMode == LayerBlendMode::normal);
    // Cancel while reading: nothing lands, nothing is said.
    EditorSession stopped;
    done = false;
    stopped.importImages({url}, std::nullopt, [&done] { done = true; });
    QVERIFY(stopped.conversionRequest().value().isReading);
    stopped.finishConversion(false);
    QVERIFY(!stopped.showsConversionSheet());
    QTRY_VERIFY(done);
    QVERIFY(!stopped.document() && !stopped.importError() && !stopped.showsConversionSheet());
    // Nothing to convert: the sheet goes unasked.
    const QUrl plain = photoshop(folder, "Plain.psd", {layer("Plain", QRectF(0, 0, 4, 2))});
    done = false;
    stopped.importImages({plain}, std::nullopt, [&done] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(names(stopped), QStringList{"Plain"});
}

void PSDImportTests::failuresAreReportedAndStepsNamedByTheFiles()
{
    const QTemporaryDir folder;
    const QUrl broken = write(folder, "Broken.psd", PSDFixture::data(PSDDocument{4, 2, 72, {}}, PSDFixture::colorImage(4, 2, 0, 0, 0)).left(40));
    EditorSession session;
    import(session, {broken});
    QCOMPARE(session.importError(), std::optional<QString>("Broken.psd: The Photoshop file could not be read. It may be damaged or incomplete."));
    QVERIFY(!session.document() && !session.showsConversionSheet());
    session.setImportError(std::nullopt);
    // A request of other files keeps the images' step name.
    QImage png(3, 3, QImage::Format_RGBA8888);
    png.fill(Qt::red);
    QVERIFY(png.save(folder.filePath("Red.png")));
    const QUrl url = photoshop(folder, "Sky.psd", {layer("Sky", QRectF(0, 0, 4, 2))});
    import(session, {QUrl::fromLocalFile(folder.filePath("Red.png")), url});
    QCOMPARE(session.history.undoName(), QString("Import Images"));
    QCOMPARE(names(session), (QStringList{"Red", "Sky", "Sky"}));
}

void PSDImportTests::aFileLandsAtThePointInsideTheActiveFolder()
{
    const QTemporaryDir folder;
    const QUrl url = photoshop(folder, "Pair.psd", {layer("Left", QRectF(0, 0, 2, 2)), layer("Right", QRectF(6, 4, 2, 2))}, QSize(8, 8));
    EditorSession session;
    session.createDocument(100, 100);
    session.addGroup();
    const QUuid active = session.activeLayerID().value();
    session.toggleGroupExpansion(active);
    QVERIFY(session.collapsedGroupIDs().contains(active));
    import(session, {url}, QPointF(50, 50));
    const std::vector<ImageLayer> &layers = session.document().value().layers;
    QCOMPARE(names(session), (QStringList{"Folder 1", "Pair", "Left", "Right"}));
    QCOMPARE(layers[1].parentID, std::optional(active));
    QCOMPARE(layers[2].parentID, std::optional(layers[1].id));
    // The pixels, (0, 0) to (8, 6), centre there.
    QCOMPARE(layers[2].origin(), QPointF(46, 47));
    QCOMPARE(layers[3].origin(), QPointF(52, 51));
    QVERIFY(!session.collapsedGroupIDs().contains(active) && !session.collapsedGroupIDs().contains(layers[1].id));
    QCOMPARE(session.activeLayerID(), std::optional(layers[1].id));
    // Beside an active layer inside a folder, within that folder.
    session.selectLayer(layers[2].id);
    import(session, {url});
    const std::vector<ImageLayer> &again = session.document().value().layers;
    QCOMPARE(again[again.size() - 3].name, QString("Pair"));
    QCOMPARE(again[again.size() - 3].parentID, std::optional(layers[1].id));
}

void PSDImportTests::layerLimitsAndDeepFoldersRefuseTheFile()
{
    EditorSession session;
    QCOMPARE(refusal([&] { session.insertPhotoshop(blanks(10'001), "Many"); }), std::optional(ImageImportError::Kind::tooLarge));
    session.insertPhotoshop(blanks(10'000), "Many");
    QCOMPARE(session.document().value().layers.size(), size_t(10'000));
    QCOMPARE(session.activeLayerID(), std::optional(session.document().value().layers.back().id));
    EditorSession open;
    open.createDocument(8, 8);
    QCOMPARE(refusal([&] { open.insertPhotoshop(blanks(10'000), "Many"); }), std::optional(ImageImportError::Kind::tooLarge));
    open.insertPhotoshop(blanks(9'999), "Many");
    QCOMPARE(open.document().value().layers.size(), size_t(10'000));
    // Folders nested past the hierarchy's depth refuse the whole file.
    PSDImport deep = blanks(0);
    std::optional<QUuid> parent;
    for (int depth = 0; depth < 65; ++depth) {
        ImageLayer folder(QStringLiteral("Folder"), QSizeF(8, 8));
        folder.isGroup = true;
        folder.parentID = parent;
        parent = folder.id;
        deep.layers.push_back(folder);
    }
    EditorSession refused;
    try {
        refused.insertPhotoshop(deep, "Deep");
        QFAIL("a hierarchy past its depth was taken");
    } catch (const PSDError &error) {
        QVERIFY(error.kind == PSDError::Kind::truncated);
    }
    QVERIFY(!refused.document());
    deep.layers.pop_back();
    refused.insertPhotoshop(deep, "Deep");
    QCOMPARE(refused.document().value().layers.size(), size_t(64));
    QCOMPARE(refused.activeLayerID(), std::optional(deep.layers.front().id));
}

void PSDImportTests::theSheetReadsThenListsAndAnswers()
{
    EditorSession session;
    ContentView view(session);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    session.beginPSDReading("Open “Trip.psd”?", "Import");
    QDialog *sheet = view.findChild<QDialog *>("conversionSheet");
    QVERIFY(sheet && sheet->isVisible() && sheet->windowModality() == Qt::WindowModal);
    QCOMPARE(sheet->windowTitle(), QString("Open “Trip.psd”?"));
    QVERIFY(sheet->findChild<QProgressBar *>("conversionSpinner"));
    QVERIFY(!sheet->findChild<QPushButton *>("conversionConfirm")->isEnabled());
    QCOMPARE(sheet->findChild<QPushButton *>("conversionConfirm")->text(), QString("Import"));
    QStringList words;
    for (const QLabel *label : sheet->findChildren<QLabel *>())
        words << label->text();
    QVERIFY(words.contains("Reading the file to see what needs converting.") && words.contains("Reading the Photoshop file…"));
    // The list replaces the reading row; Return answers yes.
    std::optional<bool> answer;
    session.finishPSDReading({PSDConversion{.layerName = "Vivid", .message = "Blend mode “vLit” isn’t supported."}}, [&answer](bool yes) { answer = yes; });
    QCOMPARE(view.findChild<QDialog *>("conversionSheet"), sheet);
    QTRY_VERIFY(!sheet->findChild<QProgressBar *>("conversionSpinner"));
    QScrollArea *list = sheet->findChild<QScrollArea *>("conversionList");
    QVERIFY(list);
    words.clear();
    for (const QLabel *label : list->findChildren<QLabel *>())
        words << label->text();
    QCOMPARE(words, (QStringList{"Vivid", "Blend mode “vLit” isn’t supported."}));
    QVERIFY(sheet->findChild<QPushButton *>("conversionConfirm")->isEnabled());
    QTest::keyClick(sheet, Qt::Key_Return);
    QTRY_COMPARE(answer, std::optional(true));
    QVERIFY(!session.showsConversionSheet());
    QTRY_VERIFY(!view.findChild<QDialog *>("conversionSheet"));
    // Escape and Cancel answer no.
    for (const bool escape : {true, false}) {
        answer = std::nullopt;
        session.beginPSDReading("Open “Trip.psd”?", "Import");
        session.finishPSDReading({PSDConversion{.layerName = "Words", .message = "Text"}}, [&answer](bool yes) { answer = yes; });
        QDialog *shown = view.findChild<QDialog *>("conversionSheet");
        QTRY_VERIFY(shown && shown->findChild<QScrollArea *>("conversionList"));
        if (escape)
            QTest::keyClick(shown, Qt::Key_Escape);
        else
            shown->findChild<QPushButton *>("conversionCancel")->click();
        QTRY_COMPARE(answer, std::optional(false));
        QTRY_VERIFY(!view.findChild<QDialog *>("conversionSheet"));
    }
    // Taken away by the session: the sheet goes, answering nothing.
    session.beginPSDReading("Open “Trip.psd”?", "Import");
    QVERIFY(view.findChild<QDialog *>("conversionSheet"));
    session.endPSDReading();
    QTRY_VERIFY(!view.findChild<QDialog *>("conversionSheet"));
    QVERIFY(!session.showsConversionSheet());
}

void PSDImportTests::aFileTheSessionRefusesIsReported()
{
    const QTemporaryDir folder;
    const QUrl url = photoshop(folder, "Sky.psd", {layer("Sky", QRectF(0, 0, 4, 2))});
    EditorSession session;
    session.insertPhotoshop(blanks(9'999), "Many");
    import(session, {url});
    QCOMPARE(session.importError(),
             std::optional<QString>("Sky.psd: This import exceeds the current 100-megapixel document budget or 30,000-pixel side limit."));
    QCOMPARE(session.document().value().layers.size(), size_t(9'999));
}

void PSDImportTests::aDirectInsertIsOneStepAndFitsTheView()
{
    EditorSession session;
    session.viewport.viewSize = QSizeF(400, 200);
    PSDImport imported = blanks(2);
    imported.resolution = 300;
    session.insertPhotoshop(imported, "Blanks");
    CanvasViewport fitted;
    fitted.viewSize = QSizeF(400, 200);
    fitted.fit(QSizeF(8, 8));
    QVERIFY(session.viewport == fitted);
    QCOMPARE(session.document().value().resolution, 300.0);
    QCOMPARE(session.history.undoName(), QString("Import Photoshop File"));
    session.undo();
    QVERIFY(!session.document());
    session.redo();
    QCOMPARE(names(session), (QStringList{"Blank", "Blank"}));
}

void PSDImportTests::theSheetKeepsItsLayoutAndItsContent()
{
    EditorSession session;
    session.createDocument(8, 8);
    ContentView view(session);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    session.beginPSDReading("Open “Trip.psd”?", "Import");
    QDialog *sheet = view.findChild<QDialog *>("conversionSheet");
    QVERIFY(sheet);
    PSDConversionSheet *reading = sheet->findChild<PSDConversionSheet *>();
    QTRY_VERIFY(reading && reading->isVisible());
    QCOMPARE(reading->minimumSize(), QSize(520, 360));
    QVERIFY(sheet->width() >= 520 && sheet->height() >= 360);
    QCOMPARE(reading->layout()->contentsMargins(), QMargins(24, 24, 24, 24));
    QCOMPARE(reading->layout()->spacing(), 16);
    const QProgressBar *spinner = sheet->findChild<QProgressBar *>("conversionSpinner");
    QVERIFY(spinner->minimum() == 0 && spinner->maximum() == 0 && !spinner->isTextVisible() && spinner->width() == 16);
    QCOMPARE(spinner->parentWidget()->minimumHeight(), 180);
    // Unrelated changes keep the content; the spinner is centred.
    session.selectTool(NavigationTool::hand);
    QCOMPARE(sheet->findChildren<PSDConversionSheet *>(), QList<PSDConversionSheet *>{reading});
    const QRect row = spinner->parentWidget()->rect();
    const QLabel *words = spinner->parentWidget()->findChild<QLabel *>();
    QCOMPARE(words->geometry().left() - spinner->geometry().right() - 1, 10);
    QVERIFY(std::abs(spinner->geometry().left() - (row.width() - 1 - words->geometry().right())) <= 1);
    // A title as text, not markup, bold at 17 pixels.
    std::optional<bool> answer;
    session.finishPSDReading({PSDConversion{.layerName = "<b>Bold</b>", .message = "A note that runs long enough to wrap over the sheet's width, "
                                                                                     "which the list keeps whole instead of cutting it off at the edge."}},
                             [&answer](bool yes) { answer = yes; });
    // The reading content hides at once, before it is deleted.
    QVERIFY(!reading->isVisible());
    PSDConversionSheet *listed = nullptr;
    QTRY_VERIFY((listed = [&] {
        for (PSDConversionSheet *each : sheet->findChildren<PSDConversionSheet *>())
            if (each->isVisible())
                return each;
        return static_cast<PSDConversionSheet *>(nullptr);
    }()) != nullptr && listed != reading);
    int visible = 0;
    for (PSDConversionSheet *each : sheet->findChildren<PSDConversionSheet *>())
        visible += each->isVisible();
    QCOMPARE(visible, 1);
    const QList<QLabel *> labels = listed->findChildren<QLabel *>();
    const QLabel *title = labels.value(0);
    QCOMPARE(title->text(), QString("Open “Trip.psd”?"));
    QCOMPARE(title->font().pixelSize(), 17);
    QCOMPARE(title->font().weight(), QFont::Bold);
    QCOMPARE(labels.value(1)->foregroundRole(), QPalette::PlaceholderText);
    const QScrollArea *list = listed->findChild<QScrollArea *>("conversionList");
    QVERIFY(list->widgetResizable() && list->minimumHeight() == 180 && list->widget());
    const QLabel *name = list->widget()->findChildren<QLabel *>().value(0), *note = list->widget()->findChildren<QLabel *>().value(1);
    QCOMPARE(name->textFormat(), Qt::PlainText);
    QVERIFY(name->wordWrap() && note->wordWrap());
    QCOMPARE(name->font().weight(), QFont::Bold);
    QCOMPARE(note->font().pixelSize(), 13);
    // Rows stack in the list, the name above its note.
    QCOMPARE(note->geometry().top() - name->geometry().bottom() - 1, 4);
    QCOMPARE(name->geometry().top(), list->widget()->layout()->contentsMargins().top() + 4);
    QCOMPARE(name->geometry().left(), list->widget()->layout()->contentsMargins().left() + 8);
    QVERIFY(note->height() > note->fontMetrics().height());
    // Rows keep the height their words need, no more.
    QCOMPARE(note->height(), note->heightForWidth(note->width()));
    // Cancel stands left of confirm, both at the right.
    const QPushButton *cancel = listed->findChild<QPushButton *>("conversionCancel"), *confirm = listed->findChild<QPushButton *>("conversionConfirm");
    QVERIFY(!cancel->autoDefault() && confirm->isDefault());
    QVERIFY(cancel->geometry().right() < confirm->geometry().left());
    QCOMPARE(confirm->geometry().right(), listed->width() - 25);
    QVERIFY(cancel->geometry().left() > listed->width() / 2);
    session.finishConversion(false);
    QTRY_COMPARE(answer, std::optional(false));
}

QTEST_MAIN(PSDImportTests)
#include "PSDImportTests.moc"
