#include "ProjectFixtures.h"
#include <QTemporaryDir>
#include <QtTest>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
// What the store's system calls must do, and did.
struct Faults {
    // The errno exchanges fail with, from the nth one on.
    int exchangeError = 0;
    int firstFailingExchange = 1;
    int failingRename = 0;
    bool fileSyncFails = false;
    // The nth folder sync fails; 0 for none.
    int failingFolderSync = 0;
    // Manifest reads come 4096 bytes at most, then fail.
    bool manifestReadsFail = false;
    int renames = 0;
    int exchanges = 0;
    int folderSyncs = 0;
    int manifestReads = 0;
    QStringList trace;
} faults;

const QDir::Filters everything = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden;

// Counts critical logs on their way to Qt Test.
int criticals = 0;
QtMessageHandler forward = nullptr;
void countCriticals(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    criticals += type == QtCriticalMsg;
    forward(type, context, message);
}
}

// These take libc's place for the store linked in here.
extern "C" int renameat2(int oldDirectory, const char *from, int newDirectory, const char *to, unsigned int flags)
{
    if (flags != 0 && faults.exchangeError != 0 && ++faults.exchanges >= faults.firstFailingExchange) {
        errno = faults.exchangeError;
        return -1;
    }
    faults.trace << (flags != 0 ? "exchange" : "rename");
    return int(syscall(SYS_renameat2, oldDirectory, from, newDirectory, to, flags));
}

extern "C" int rename(const char *from, const char *to)
{
    if (++faults.renames == faults.failingRename) {
        errno = EIO;
        return -1;
    }
    faults.trace << "rename";
    return int(syscall(SYS_renameat2, AT_FDCWD, from, AT_FDCWD, to, 0));
}

extern "C" int fsync(int descriptor)
{
    struct stat info;
    const bool folder = fstat(descriptor, &info) == 0 && S_ISDIR(info.st_mode);
    if (folder ? ++faults.folderSyncs == faults.failingFolderSync : faults.fileSyncFails) {
        errno = EIO;
        return -1;
    }
    faults.trace << (folder ? "sync folder" : "sync file");
    return int(syscall(SYS_fsync, descriptor));
}

extern "C" ssize_t read(int descriptor, void *buffer, size_t count)
{
    char path[512] = {};
    const QByteArray link = "/proc/self/fd/" + QByteArray::number(descriptor);
    const bool manifest = faults.manifestReadsFail && readlink(link.constData(), path, sizeof(path) - 1) > 0
        && QByteArray(path).endsWith("manifest.json");
    if (manifest && ++faults.manifestReads > 1) {
        errno = EIO;
        return -1;
    }
    return syscall(SYS_read, descriptor, buffer, manifest ? std::min<size_t>(count, 4096) : count);
}

class ProjectReplaceTests : public QObject {
    Q_OBJECT
private slots:
    void init();
    void everythingIsSyncedBeforeTheSwap();
    void withoutAnExchangeTheProjectIsReplacedInTwoSteps_data();
    void withoutAnExchangeTheProjectIsReplacedInTwoSteps();
    void aFailedSwapLeavesTheOldProject_data();
    void aFailedSwapLeavesTheOldProject();
    void aSyncFailingAfterTheSwapPutsTheOldProjectBack_data();
    void aSyncFailingAfterTheSwapPutsTheOldProjectBack();
    void aFirstSaveThatCannotBeSyncedIsTakenAway();
    void aManifestThatStopsReadingIsRefused();
    void aDiskThatTakesNoMoreBytesFailsTheSave();
    void savesLeakNoDescriptors();
};

void ProjectReplaceTests::init()
{
    faults = Faults();
}

void ProjectReplaceTests::everythingIsSyncedBeforeTheSwap()
{
    QTemporaryDir root;
    const QString path = root.filePath("Durable.comp");
    ProjectStore::save(twoLayers(), path);
    // The manifest, the image, both folders, the move, the parent.
    QCOMPARE(faults.trace, (QStringList{"sync file", "sync file", "sync folder", "sync folder", "rename", "sync folder"}));
    faults.trace.clear();
    ProjectStore::save(twoLayers(), path);
    QCOMPARE(faults.trace, (QStringList{"sync file", "sync file", "sync folder", "sync folder", "exchange", "sync folder"}));
    QCOMPARE(QDir(root.path()).entryList(everything), QStringList{"Durable.comp"});
}

void ProjectReplaceTests::withoutAnExchangeTheProjectIsReplacedInTwoSteps_data()
{
    QTest::addColumn<int>("error");
    QTest::newRow("EINVAL, a file system without it") << EINVAL;
    QTest::newRow("ENOSYS, a kernel without it") << ENOSYS;
    QTest::newRow("ENOTSUP") << ENOTSUP;
}

void ProjectReplaceTests::withoutAnExchangeTheProjectIsReplacedInTwoSteps()
{
    QFETCH(int, error);
    QTemporaryDir root;
    const QString path = root.filePath("Stick.comp");
    ProjectSnapshot snapshot = twoLayers();
    ProjectStore::save(snapshot, path);
    faults.exchangeError = error;
    // A first save needs no exchange.
    ProjectStore::save(snapshot, root.filePath("Fresh.comp"));
    QCOMPARE(contents(root.filePath("Fresh.comp/manifest.json")), contents(path + "/manifest.json"));
    // An overwrite falls back, says so, and leaves nothing behind.
    snapshot.manifest.layers.erase(snapshot.manifest.layers.begin());
    snapshot.images.clear();
    faults.trace.clear();
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("this file system cannot exchange paths; replacing \".*Stick.comp\" in two steps"));
    ProjectStore::save(snapshot, path);
    // The old copy goes only once the swap is synced.
    QCOMPARE(faults.trace, (QStringList{"sync file", "sync folder", "sync folder", "rename", "rename", "sync folder", "rename"}));
    const ProjectSnapshot loaded = ProjectStore::load(path);
    QCOMPARE(int(loaded.manifest.layers.size()), 1);
    QVERIFY(QDir(path + "/images").entryList(QDir::Files).isEmpty());
    QCOMPARE(QDir(root.path()).entryList(everything), (QStringList{"Fresh.comp", "Stick.comp"}));
    // A plain file in the way goes the same road.
    overwrite(root.filePath("WasAFile.comp"), "x");
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("this file system cannot exchange paths; .*"));
    ProjectStore::save(snapshot, root.filePath("WasAFile.comp"));
    QCOMPARE(int(ProjectStore::load(root.filePath("WasAFile.comp")).manifest.layers.size()), 1);
    QCOMPARE(QDir(root.path()).entryList(everything), (QStringList{"Fresh.comp", "Stick.comp", "WasAFile.comp"}));
}

void ProjectReplaceTests::aFailedSwapLeavesTheOldProject_data()
{
    QTest::addColumn<int>("exchangeError");
    QTest::addColumn<int>("failingRename");
    QTest::addColumn<bool>("fileSyncFails");
    QTest::addColumn<int>("failingFolderSync");
    QTest::addColumn<QString>("message");
    QTest::newRow("the exchange itself fails") << EIO << 0 << false << 0 << "could not replace .*Kept.comp: Input/output error";
    QTest::newRow("the old project cannot move aside") << EINVAL << 1 << false << 0 << "could not replace .*Kept.comp: Input/output error";
    QTest::newRow("the new project cannot move in") << EINVAL << 2 << false << 0 << "could not replace .*Kept.comp: Input/output error";
    QTest::newRow("a file cannot be synced") << 0 << 0 << true << 0 << "could not write .*manifest.json: Input/output error";
    QTest::newRow("the images folder cannot be synced") << 0 << 0 << false << 1 << "could not sync .*images: Input/output error";
    QTest::newRow("the sibling cannot be synced") << 0 << 0 << false << 2 << "could not sync .*tmp: Input/output error";
}

void ProjectReplaceTests::aFailedSwapLeavesTheOldProject()
{
    QFETCH(int, exchangeError);
    QFETCH(int, failingRename);
    QFETCH(bool, fileSyncFails);
    QFETCH(int, failingFolderSync);
    QFETCH(QString, message);
    QTemporaryDir root;
    const QString path = root.filePath("Kept.comp");
    ProjectSnapshot snapshot = twoLayers();
    ProjectStore::save(snapshot, path);
    const QByteArray original = contents(path + "/manifest.json");
    snapshot.manifest.layers.erase(snapshot.manifest.layers.begin());
    snapshot.images.clear();
    faults = Faults();
    faults.exchangeError = exchangeError;
    faults.failingRename = failingRename;
    faults.fileSyncFails = fileSyncFails;
    faults.failingFolderSync = failingFolderSync;
    if (exchangeError == EINVAL)
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("this file system cannot exchange paths; .*"));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(message));
    try {
        ProjectStore::save(snapshot, path);
        QFAIL("the save went through");
    } catch (const std::runtime_error &error) {
        QVERIFY2(QRegularExpression(message).match(error.what()).hasMatch(), error.what());
    }
    faults = Faults();
    QCOMPARE(contents(path + "/manifest.json"), original);
    QCOMPARE(int(ProjectStore::load(path).manifest.layers.size()), 2);
    QCOMPARE(QDir(root.path()).entryList(everything), QStringList{"Kept.comp"});
    // A first save that cannot move in leaves nothing.
    faults.failingRename = 1;
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("could not move the project to .*First.comp: Input/output error"));
    QVERIFY_EXCEPTION_THROWN(ProjectStore::save(snapshot, root.filePath("First.comp")), std::runtime_error);
    QCOMPARE(QDir(root.path()).entryList(everything), QStringList{"Kept.comp"});
}

void ProjectReplaceTests::aSyncFailingAfterTheSwapPutsTheOldProjectBack_data()
{
    QTest::addColumn<int>("exchangeError");
    QTest::addColumn<int>("firstFailingExchange");
    QTest::addColumn<int>("failingRename");
    // Where the old project stays, and what else is left.
    QTest::addColumn<QString>("kept");
    QTest::addColumn<QStringList>("left");
    QTest::newRow("after an exchange") << 0 << 1 << 0 << "" << QStringList{"Kept.comp"};
    QTest::newRow("after two moves") << EINVAL << 1 << 0 << "" << QStringList{"Kept.comp"};
    QTest::newRow("an exchange that cannot be undone") << EIO << 2 << 0 << ".tmp" << QStringList{".tmp", "Kept.comp"};
    // The third move takes the new project away again.
    QTest::newRow("two moves, the new project stuck") << EINVAL << 1 << 3 << ".tmp.old" << QStringList{".tmp.old", "Kept.comp"};
    QTest::newRow("two moves, the old project stuck") << EINVAL << 1 << 4 << ".tmp.old" << QStringList{".tmp", ".tmp.old"};
}

void ProjectReplaceTests::aSyncFailingAfterTheSwapPutsTheOldProjectBack()
{
    QFETCH(int, exchangeError);
    QFETCH(int, firstFailingExchange);
    QFETCH(int, failingRename);
    QFETCH(QString, kept);
    QFETCH(QStringList, left);
    QTemporaryDir root;
    const QString path = root.filePath("Kept.comp");
    ProjectSnapshot snapshot = twoLayers();
    ProjectStore::save(snapshot, path);
    const QByteArray original = contents(path + "/manifest.json");
    const QImage pixels = snapshot.images.begin()->second.image();
    snapshot.manifest.layers.erase(snapshot.manifest.layers.begin());
    snapshot.images.clear();
    faults = Faults();
    faults.exchangeError = exchangeError;
    faults.firstFailingExchange = firstFailingExchange;
    faults.failingRename = failingRename;
    // The third folder synced is the parent, after the swap.
    faults.failingFolderSync = 3;
    if (exchangeError == EINVAL)
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("this file system cannot exchange paths; .*"));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("could not sync .*: Input/output error"));
    const QString sibling = QStringLiteral("\\.Kept\\.comp\\.[0-9A-F-]{36}");
    if (!kept.isEmpty()) {
        const QString stays = "^the previous project could not be put back; it stays at " + QRegularExpression::escape(root.path()) + "/"
            + sibling + QRegularExpression::escape(kept) + "$";
        QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(stays));
    }
    criticals = 0;
    forward = qInstallMessageHandler(countCriticals);
    QVERIFY_EXCEPTION_THROWN(ProjectStore::save(snapshot, path), std::runtime_error);
    qInstallMessageHandler(forward);
    faults = Faults();
    // A way back that worked raises no alarm.
    QCOMPARE(criticals, kept.isEmpty() ? 0 : 1);
    // Nothing is deleted when the way back fails.
    QMap<QString, QString> paths;
    for (const QString &name : QDir(root.path()).entryList(everything)) {
        const QRegularExpressionMatch match = QRegularExpression("^" + sibling + "(\\.tmp(\\.old)?)$").match(name);
        paths.insert(match.hasMatch() ? match.captured(1) : name, root.filePath(name));
    }
    QCOMPARE(QStringList(paths.keys()), left);
    // The old project opens whole from where it stays.
    const QString previous = paths.value(kept.isEmpty() ? "Kept.comp" : kept);
    QCOMPARE(contents(previous + "/manifest.json"), original);
    const ProjectSnapshot old = ProjectStore::load(previous);
    QCOMPARE(int(old.manifest.layers.size()), 2);
    QCOMPARE(old.images.begin()->second.image(), pixels);
    // The new one stays too, wherever the moves left it.
    if (!kept.isEmpty())
        QCOMPARE(int(ProjectStore::load(paths.value(left.contains("Kept.comp") ? "Kept.comp" : ".tmp")).manifest.layers.size()), 1);
}

void ProjectReplaceTests::aFirstSaveThatCannotBeSyncedIsTakenAway()
{
    QTemporaryDir root;
    const QString path = root.filePath("First.comp");
    faults.failingFolderSync = 3;
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("could not sync .*: Input/output error"));
    criticals = 0;
    forward = qInstallMessageHandler(countCriticals);
    QVERIFY_EXCEPTION_THROWN(ProjectStore::save(twoLayers(), path), std::runtime_error);
    qInstallMessageHandler(forward);
    QCOMPARE(criticals, 0);
    QCOMPARE(QDir(root.path()).entryList(everything), QStringList());
    // What cannot be taken away stays whole, and is named.
    faults = Faults();
    faults.failingFolderSync = 3;
    faults.failingRename = 2;
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("could not sync .*: Input/output error"));
    QTest::ignoreMessage(QtCriticalMsg, QRegularExpression("^the unsynced project could not be taken from " + QRegularExpression::escape(path) + "$"));
    forward = qInstallMessageHandler(countCriticals);
    QVERIFY_EXCEPTION_THROWN(ProjectStore::save(twoLayers(), path), std::runtime_error);
    qInstallMessageHandler(forward);
    QCOMPARE(criticals, 1);
    faults = Faults();
    QCOMPARE(QDir(root.path()).entryList(everything), QStringList{"First.comp"});
    QCOMPARE(int(ProjectStore::load(path).manifest.layers.size()), 2);
}

void ProjectReplaceTests::aManifestThatStopsReadingIsRefused()
{
    QTemporaryDir root;
    const QString path = root.filePath("Read.comp");
    ProjectStore::save(twoLayers(), path);
    // Whole in 4096 bytes, then padded: a short read parses.
    const QByteArray manifest = contents(path + "/manifest.json");
    QVERIFY(manifest.size() < 4096);
    overwrite(path + "/manifest.json", manifest + QByteArray(8192, ' '));
    QCOMPARE(projectError([&] { ProjectStore::load(path); }), std::nullopt);
    faults.manifestReadsFail = true;
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("could not read \".*manifest.json\" : .*"));
    const std::optional<ProjectError::Kind> refused = projectError([&] { ProjectStore::load(path); });
    faults = Faults();
    QCOMPARE(refused, std::optional(ProjectError::Kind::invalid));
}

void ProjectReplaceTests::aDiskThatTakesNoMoreBytesFailsTheSave()
{
    QTemporaryDir root;
    const QString path = root.filePath("Full.comp");
    ProjectStore::save(twoLayers(), path);
    const QByteArray original = contents(path + "/manifest.json");
    // No file may pass 100 bytes: every write stops short.
    std::signal(SIGXFSZ, SIG_IGN);
    rlimit previous;
    QCOMPARE(getrlimit(RLIMIT_FSIZE, &previous), 0);
    rlimit tight = previous;
    tight.rlim_cur = 100;
    QCOMPARE(setrlimit(RLIMIT_FSIZE, &tight), 0);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("could not write .*manifest.json: .*"));
    bool thrown = false;
    try {
        ProjectStore::save(twoLayers(), path);
    } catch (const std::runtime_error &) {
        thrown = true;
    }
    QCOMPARE(setrlimit(RLIMIT_FSIZE, &previous), 0);
    QVERIFY(thrown);
    QCOMPARE(contents(path + "/manifest.json"), original);
    QCOMPARE(QDir(root.path()).entryList(everything), QStringList{"Full.comp"});
}

void ProjectReplaceTests::savesLeakNoDescriptors()
{
    QTemporaryDir root;
    const auto open = [] { return QDir("/proc/self/fd").entryList(QDir::NoDotAndDotDot | QDir::AllEntries).size(); };
    ProjectStore::save(twoLayers(), root.filePath("Again.comp"));
    const qsizetype before = open();
    for (int round = 0; round < 5; ++round) {
        ProjectStore::save(twoLayers(), root.filePath("Again.comp"));
        ProjectStore::load(root.filePath("Again.comp"));
    }
    QCOMPARE(open(), before);
}

QTEST_MAIN(ProjectReplaceTests)
#include "ProjectReplaceTests.moc"
