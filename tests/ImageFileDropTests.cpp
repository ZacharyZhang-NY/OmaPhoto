#include "ContentView.h"
#include "IO/ImageFileDrop.h"
#include "UI/ProjectTabs.h"
#include "UI/ProjectWorkspaceView.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QtTest>
#include <csignal>
#include <sys/resource.h>

// Swift's ImageFileDrop: files and pictures reach the importer in order.
class ImageFileDropTests : public QObject {
    Q_OBJECT
private slots:
    void fileDropProvidersReachImporterInOrder();
    void aPictureIsCopiedToAFileFirst();
    void whatCannotBeReadIsSaid();
    void onlyFilesAndPicturesAreHeld();
    void theCanvasTheTabsAndTheNewButtonTakeFiles();
    void aCanvasAloneImportsUnderSwiftsGuards();
    void anOpenAlertShowsTheGrownError();
    void eachFileWaitsBehindTheErrorBeforeIt();
    void aPictureThatCannotBeKeptIsUnreadable();
    void aPictureReachesATabWhole();
    void aLazyDragGivesOnlyThePictureChosen();
};

namespace {
QString fixture(const QTemporaryDir &folder, const QString &name, const char *format)
{
    QImage image(6, 4, QImage::Format_RGBA8888);
    image.fill(Qt::red);
    const QString path = folder.filePath(name);
    if (!image.save(path, format))
        throw std::runtime_error("could not write a fixture");
    return path;
}

QMimeData *files(const QStringList &paths, const QList<QUrl> &others = {})
{
    auto *data = new QMimeData;
    QList<QUrl> urls;
    for (const QString &path : paths)
        urls << QUrl::fromLocalFile(path);
    data->setUrls(urls + others);
    return data;
}

QStringList names(const EditorSession &session)
{
    QStringList listed;
    if (session.document())
        for (const ImageLayer &layer : session.document().value().layers)
            listed << layer.name;
    return listed;
}

bool dropOn(QWidget &target, QMimeData *data, QPoint at, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QDragEnterEvent entered(at, Qt::CopyAction, data, Qt::LeftButton, modifiers);
    QApplication::sendEvent(&target, &entered);
    QDropEvent dropped(at, Qt::CopyAction, data, Qt::LeftButton, modifiers);
    QApplication::sendEvent(&target, &dropped);
    delete data;
    return entered.isAccepted() && dropped.isAccepted();
}
}

void ImageFileDropTests::fileDropProvidersReachImporterInOrder()
{
    QTemporaryDir folder;
    const QString first = fixture(folder, "first.png", "PNG"), second = fixture(folder, "second.jpg", "JPEG");
    EditorSession session;
    bool done = false;
    std::unique_ptr<QMimeData> data(files({first, second}));
    ImageFileDrop::importProviders(*data, session, std::nullopt, nullptr, std::nullopt, [&done] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(names(session), (QStringList{"first", "second"}));
    QVERIFY(!session.importError());
}

void ImageFileDropTests::aPictureIsCopiedToAFileFirst()
{
    QImage image(3, 5, QImage::Format_RGBA8888);
    image.fill(Qt::blue);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    // A browser's picture: a web address and the bytes.
    QMimeData data;
    data.setUrls({QUrl("https://example.com/picture.png")});
    data.setData("image/jpeg", "not read: PNG comes first");
    data.setData("image/png", png);
    EditorSession session;
    bool done = false;
    ImageFileDrop::importProviders(data, session, QPointF(2, 2), nullptr, std::nullopt, [&done] { done = true; });
    QTRY_VERIFY(done);
    QVERIFY(!session.importError());
    const QStringList listed = names(session);
    QCOMPARE(listed.size(), 1);
    QVERIFY(listed.value(0).startsWith("Dropped-"));
    QCOMPARE(session.document().value().size(), QSize(3, 5));
    QVERIFY(QFileInfo::exists(QDir::temp().filePath(listed.value(0) + ".png")));
    QFile::remove(QDir::temp().filePath(listed.value(0) + ".png"));
}

void ImageFileDropTests::whatCannotBeReadIsSaid()
{
    const QString said = "Some dropped items couldn’t be read. Drag JPEG, PNG, HEIC, or TIFF files from the file manager.";
    EditorSession session;
    bool done = false;
    std::unique_ptr<QMimeData> link(files({}, {QUrl("https://example.com/page")}));
    ImageFileDrop::importProviders(*link, session, std::nullopt, nullptr, std::nullopt, [&done] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(session.importError(), std::optional(said));
    QVERIFY(!session.document());
    // A file among them arrives; the words join the importer's.
    QTemporaryDir folder;
    const QString broken = folder.filePath("broken.png");
    QFile file(broken);
    QVERIFY(file.open(QIODevice::WriteOnly) && file.write("no picture") > 0);
    file.close();
    session.setImportError(std::nullopt);
    done = false;
    std::unique_ptr<QMimeData> mixed(files({fixture(folder, "kept.png", "PNG"), broken}, {QUrl("https://example.com/page")}));
    ImageFileDrop::importProviders(*mixed, session, std::nullopt, nullptr, std::nullopt, [&done] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(names(session), QStringList{"kept"});
    const QString error = session.importError().value();
    QVERIFY(error.startsWith("broken.png") && error.endsWith("\n\n" + said));
}

void ImageFileDropTests::onlyFilesAndPicturesAreHeld()
{
    QMimeData data;
    QVERIFY(!ImageFileDrop::holdsImages(data));
    data.setText("words");
    data.setUrls({QUrl("https://example.com/picture.png")});
    QVERIFY(!ImageFileDrop::holdsImages(data));
    data.setData("image/webp", "bytes");
    QVERIFY(ImageFileDrop::holdsImages(data));
    QMimeData file;
    file.setUrls({QUrl::fromLocalFile("/tmp/any.png")});
    QVERIFY(ImageFileDrop::holdsImages(file));
}

void ImageFileDropTests::theCanvasTheTabsAndTheNewButtonTakeFiles()
{
    QTemporaryDir folder;
    const QString path = fixture(folder, "dropped.png", "PNG");
    ProjectWorkspace workspace;
    ProjectWorkspaceView window{workspace};
    ProjectTab &first = workspace.current();
    first.session.createDocument(40, 20);
    ProjectTab &second = workspace.addTab(false);
    second.session.createDocument(8, 8);
    workspace.select(first.id);
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *content = window.findChild<ContentView *>();
    QVERIFY(content && dropOn(*content, files({path}), content->rect().center()));
    QTRY_COMPARE(names(first.session), QStringList{"dropped"});
    // Words are no files.
    auto *words = new QMimeData;
    words->setText("dropped.png");
    QVERIFY(!dropOn(*content, words, content->rect().center()));
    // Alt refuses only layer rows; files go where dropped.
    ProjectTabButton *secondButton = nullptr;
    for (ProjectTabButton *button : window.tabs()->buttons())
        if (button->tab->id == second.id)
            secondButton = button;
    QTest::keyPress(window.windowHandle(), Qt::Key_Alt, Qt::AltModifier);
    QVERIFY(secondButton && dropOn(*secondButton, files({path}), QPoint(5, 5), Qt::AltModifier));
    QTest::keyRelease(window.windowHandle(), Qt::Key_Alt, Qt::NoModifier);
    QTRY_COMPARE(names(second.session), QStringList{"dropped"});
    // The last import ends before the next drop is taken.
    QTRY_VERIFY(workspace.canSwitch());
    auto *newButton = window.findChild<NewCanvasButton *>();
    QVERIFY(newButton && dropOn(*newButton, files({path}), QPoint(5, 5)));
    QTRY_COMPARE(int(workspace.tabs().size()), 3);
    QTRY_COMPARE(names(workspace.current().session), QStringList{"dropped"});
}

// Without a workspace, drops go to the canvas's session.
void ImageFileDropTests::aCanvasAloneImportsUnderSwiftsGuards()
{
    QTemporaryDir folder;
    const QString path = fixture(folder, "alone.png", "PNG");
    EditorSession session;
    session.createDocument(40, 20);
    QImage red(10, 10, QImage::Format_RGBA8888_Premultiplied);
    red.fill(Qt::red);
    session.insert(ImportedImage(red, QImage(), "Red"), QPointF(20, 10));
    ContentView content(session);
    content.resize(1000, 700);
    content.show();
    const auto refused = [&] { return !dropOn(content, files({path}), content.rect().center()); };
    session.setIsProjectBusy(true);
    QVERIFY(refused());
    session.setIsProjectBusy(false);
    session.setShowsNewDocument(true);
    QVERIFY(refused());
    session.setShowsNewDocument(false);
    session.setShowsImporter(true);
    QVERIFY(refused());
    session.setShowsImporter(false);
    session.setRenamingLayerID(session.activeLayerID());
    QVERIFY(refused());
    session.setRenamingLayerID(std::nullopt);
    session.beginLevels();
    QVERIFY(session.levels() && refused());
    session.cancelLevels();
    QVERIFY(dropOn(content, files({path}), content.rect().center()));
    QTRY_COMPARE(names(session), (QStringList{"Red", "alone"}));
}

// Words added to a shown error reach the shown alert.
void ImageFileDropTests::anOpenAlertShowsTheGrownError()
{
    EditorSession session;
    ContentView content(session);
    content.show();
    session.setImportError(QStringLiteral("broken.png could not be read."));
    auto *alert = content.findChild<QMessageBox *>();
    QVERIFY(alert && alert->isVisible());
    session.setImportError(QStringLiteral("broken.png could not be read.\n\nSome dropped items couldn’t be read."));
    QCOMPARE(content.findChildren<QMessageBox *>().size(), 1);
    QCOMPARE(alert->informativeText(), QString("broken.png could not be read.\n\nSome dropped items couldn’t be read."));
}

// Swift's gate between providers: an error holds the next.
void ImageFileDropTests::eachFileWaitsBehindTheErrorBeforeIt()
{
    QTemporaryDir folder;
    for (const char *name : {"first.png", "second.png"}) {
        QFile broken(folder.filePath(name));
        QVERIFY(broken.open(QIODevice::WriteOnly) && broken.write("no picture") > 0);
    }
    ProjectWorkspace workspace;
    EditorSession &session = workspace.current().session;
    session.createDocument(8, 8);
    bool done = false;
    std::unique_ptr<QMimeData> data(files({folder.filePath("first.png"), folder.filePath("second.png")}));
    workspace.receiveProviders(*data, workspace.current().id, std::nullopt, [&done] { done = true; });
    QTRY_VERIFY(session.importError().has_value());
    QVERIFY(session.importError().value().startsWith("first.png"));
    // The second waits while the first error stands.
    QTest::qWait(150);
    QVERIFY(!done && !session.importError().value().contains("second.png"));
    session.setImportError(std::nullopt);
    QTRY_VERIFY(done);
    QVERIFY(session.importError().value().startsWith("second.png"));
}

// A copy cut short by a size limit imports nothing.
void ImageFileDropTests::aPictureThatCannotBeKeptIsUnreadable()
{
    QImage noisy(64, 64, QImage::Format_RGBA8888);
    quint32 seed = 7;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            noisy.setPixel(x, y, (seed = seed * 1664525u + 1013904223u) | 0xff000000u);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    noisy.save(&buffer, "PNG");
    QVERIFY(png.size() > 4096);
    QMimeData data;
    data.setData("image/png", png);
    const auto dropped = [] { return QDir::temp().entryList({"Dropped-*"}, QDir::Files).size(); };
    const qsizetype before = dropped();
    EditorSession session;
    bool done = false;
    signal(SIGXFSZ, SIG_IGN);
    rlimit room{};
    QCOMPARE(getrlimit(RLIMIT_FSIZE, &room), 0);
    rlimit tight = room;
    tight.rlim_cur = 1024;
    QCOMPARE(setrlimit(RLIMIT_FSIZE, &tight), 0);
    ImageFileDrop::importProviders(data, session, std::nullopt, nullptr, std::nullopt, [&done] { done = true; });
    QCOMPARE(setrlimit(RLIMIT_FSIZE, &room), 0);
    QTRY_VERIFY(done);
    QCOMPARE(session.importError(), std::optional<QString>("Some dropped items couldn’t be read. Drag JPEG, PNG, HEIC, or TIFF files from the file manager."));
    QVERIFY(!session.document());
    QCOMPARE(dropped(), before);
}

// A picture with its address is one provider, whole.
void ImageFileDropTests::aPictureReachesATabWhole()
{
    QImage image(5, 3, QImage::Format_RGBA8888);
    image.fill(Qt::green);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    QMimeData data;
    data.setUrls({QUrl("https://example.com/picture.png")});
    data.setData("image/png", png);
    ProjectWorkspace workspace;
    EditorSession &session = workspace.current().session;
    session.createDocument(8, 8);
    bool done = false;
    workspace.receiveProviders(data, workspace.current().id, std::nullopt, [&done] { done = true; });
    QTRY_VERIFY(done);
    QVERIFY(!session.importError());
    const QStringList listed = names(session);
    QCOMPARE(listed.size(), 1);
    QVERIFY(listed.value(0).startsWith("Dropped-"));
    QFile::remove(QDir::temp().filePath(listed.value(0) + ".png"));
}

namespace {
// A drag that hands each format over only when asked.
class LazyDrag : public QMimeData {
public:
    QByteArray png;
    mutable QStringList asked;
    QStringList formats() const override { return {"text/uri-list", "image/bmp", "image/png", "image/jpeg"}; }

protected:
    QVariant retrieveData(const QString &type, QMetaType) const override
    {
        asked << type;
        if (type == QLatin1String("text/uri-list"))
            return QVariantList{QUrl("https://example.com/picture.png")};
        return type == QLatin1String("image/png") ? QVariant(png) : QVariant();
    }
};
}

void ImageFileDropTests::aLazyDragGivesOnlyThePictureChosen()
{
    QImage image(4, 6, QImage::Format_RGBA8888);
    image.fill(Qt::blue);
    LazyDrag drag;
    QBuffer buffer(&drag.png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    ProjectWorkspace workspace;
    EditorSession &session = workspace.current().session;
    session.createDocument(8, 8);
    bool done = false;
    workspace.receiveProviders(drag, workspace.current().id, std::nullopt, [&done] { done = true; });
    QTRY_VERIFY(done);
    QVERIFY(!session.importError());
    QCOMPARE(names(session).size(), 1);
    QFile::remove(QDir::temp().filePath(names(session).value(0) + ".png"));
    drag.asked.removeDuplicates();
    drag.asked.sort();
    QCOMPARE(drag.asked, (QStringList{"image/png", "text/uri-list"}));
}

QTEST_MAIN(ImageFileDropTests)
#include "ImageFileDropTests.moc"
