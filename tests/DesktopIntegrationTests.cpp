#include <QDirIterator>
#include <QProcess>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QtTest>

// The desktop entry, the project type, and an install.
class DesktopIntegrationTests : public QObject {
    Q_OBJECT
private slots:
    void theEntryOpensSwiftsDocumentTypes();
    void aProjectFolderIsAType();
    void anInstallLaysDownTheAppItsEntryIconsAndLicense();
    void aDirectInstallRegistersTheProjectType();
};

namespace {
const QString id = QStringLiteral("io.github.ZacharyZhang_NY.OmaPhoto");

QString packaged(const QString &name)
{
    QFile file(QStringLiteral(QT_TESTCASE_SOURCEDIR "/packaging/") + name);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("no packaging file " + name.toStdString());
    return QString::fromUtf8(file.readAll());
}

void install(const QString &root, const QString &prefix, const QList<QPair<QString, QString>> &environment)
{
    QProcess process;
    QProcessEnvironment variables = QProcessEnvironment::systemEnvironment();
    // An empty value clears what the caller's environment holds.
    for (const auto &[name, value] : environment) {
        if (value.isEmpty())
            variables.remove(name);
        else
            variables.insert(name, value);
    }
    process.setProcessEnvironment(variables);
    process.start("cmake", {"--install", QStringLiteral(QT_TESTCASE_BUILDDIR), "--prefix", prefix});
    if (!process.waitForFinished(120'000) || process.exitCode() != 0)
        throw std::runtime_error("install into " + root.toStdString() + " failed: " + process.readAllStandardError().toStdString());
}

QStringList filesUnder(const QString &root)
{
    QStringList files;
    QDirIterator walk(root, QDir::Files, QDirIterator::Subdirectories);
    while (walk.hasNext())
        files << walk.next().mid(root.size() + 1);
    files.sort();
    return files;
}
}

void DesktopIntegrationTests::theEntryOpensSwiftsDocumentTypes()
{
    const QStringList lines = packaged(id + ".desktop").split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(lines.value(0), QString("[Desktop Entry]"));
    QMap<QString, QString> keys;
    for (const QString &line : lines.mid(1))
        keys.insert(line.section(QLatin1Char('='), 0, 0), line.section(QLatin1Char('='), 1));
    QCOMPARE(keys.value("Type"), QString("Application"));
    QCOMPARE(keys.value("Name"), QString("OmaPhoto"));
    // Files named at launch reach the workspace, as Open With.
    QCOMPARE(keys.value("Exec"), QString("omaphoto %F"));
    QCOMPARE(keys.value("Icon"), id);
    // X11 names the window class after the application.
    QCOMPARE(keys.value("StartupWMClass"), QString("OmaPhoto"));
    QCOMPARE(keys.value("Terminal"), QString("false"));
    QVERIFY(keys.value("Categories").split(QLatin1Char(';')).contains("Graphics"));
    // Swift's two document types: its projects, then four pictures.
    QCOMPARE(keys.value("MimeType"), QString("application/x-compositor-project;image/png;image/jpeg;image/heic;image/tiff;image/vnd.adobe.photoshop;image/x-dcraw;"));
}

void DesktopIntegrationTests::aProjectFolderIsAType()
{
    QXmlStreamReader xml(packaged(id + ".xml"));
    QStringList seen;
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement)
            continue;
        const QString name = xml.name().toString();
        if (name == QLatin1String("mime-type") || name == QLatin1String("sub-class-of"))
            seen << name + ":" + xml.attributes().value("type").toString();
        else if (name == QLatin1String("glob"))
            seen << "glob:" + xml.attributes().value("pattern").toString();
        else if (name == QLatin1String("comment"))
            seen << "comment:" + xml.readElementText();
    }
    QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));
    QCOMPARE(seen, (QStringList{"mime-type:application/x-compositor-project", "comment:OmaPhoto Project", "sub-class-of:inode/directory", "glob:*.comp"}));
}

void DesktopIntegrationTests::anInstallLaysDownTheAppItsEntryIconsAndLicense()
{
    // Staged, as packages install: their hooks refresh the databases.
    QTemporaryDir stage;
    QStringList expected = {"usr/bin/omaphoto", "usr/share/applications/" + id + ".desktop", "usr/share/licenses/omaphoto/U-2-Net.txt", "usr/share/licenses/omaphoto/LICENSE",
                            "usr/share/mime/packages/" + id + ".xml"};
    for (const int size : {16, 32, 64, 128, 256, 512})
        expected << QStringLiteral("usr/share/icons/hicolor/%1x%1/apps/%2.png").arg(size).arg(id);
    // Given the model, a build installs it as looked for.
    const QString model = QStringLiteral(OMAPHOTO_MODEL_PATH);
    if (!model.isEmpty())
        expected << "usr/share/omaphoto/u2net.onnx";
    // A bundled runtime: its library, the version's link, its notices.
    const bool bundled = OMAPHOTO_BUNDLED;
    const QString runtime = QStringLiteral("usr/" OMAPHOTO_LIBDIR "/omaphoto");
    if (bundled)
        expected << "usr/share/licenses/omaphoto/onnxruntime/LICENSE" << "usr/share/licenses/omaphoto/onnxruntime/ThirdPartyNotices.txt";
    expected.sort();
    install(stage.path(), "/usr", {{"DESTDIR", stage.path()}});
    if (bundled) {
        const QStringList files = QDir(stage.filePath(runtime)).entryList(QDir::Files | QDir::System);
        QCOMPARE(files.size(), 2);
        const QFileInfo link(stage.filePath(runtime + "/libonnxruntime.so.1"));
        QVERIFY(link.isSymLink() && QFileInfo(link.symLinkTarget()).fileName().startsWith("libonnxruntime.so.1."));
        for (const QString &file : files)
            expected << runtime + "/" + file;
        expected.sort();
    }
    QCOMPARE(filesUnder(stage.path()), expected);
    QCOMPARE(QImage(stage.filePath("usr/share/icons/hicolor/128x128/apps/" + id + ".png")).size(), QSize(128, 128));
    QVERIFY(QFileInfo(stage.filePath("usr/bin/omaphoto")).isExecutable());
    if (!model.isEmpty()) {
        const QFileInfo installed(stage.filePath("usr/share/omaphoto/u2net.onnx"));
        QVERIFY(!installed.isSymLink());
        QCOMPARE(installed.size(), QFileInfo(model).size());
    }
}

void DesktopIntegrationTests::aDirectInstallRegistersTheProjectType()
{
    // A stand-in for shared-mime-info's tool, first on the path.
    QTemporaryDir tools, prefix;
    QFile fake(tools.filePath("update-mime-database"));
    QVERIFY(fake.open(QIODevice::WriteOnly));
    fake.write("#!/bin/sh\necho \"$@\" > \"$0.called\"\n");
    fake.close();
    QVERIFY(fake.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    install(prefix.path(), prefix.path(), {{"PATH", tools.path() + ":" + qEnvironmentVariable("PATH")}, {"DESTDIR", QString()}});
    QFile called(tools.filePath("update-mime-database.called"));
    QVERIFY(called.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(called.readAll()).trimmed(), prefix.filePath("share/mime"));
}

QTEST_GUILESS_MAIN(DesktopIntegrationTests)
#include "DesktopIntegrationTests.moc"
