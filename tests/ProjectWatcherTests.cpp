#include "IO/ProjectDigest.h"
#include "IO/ProjectWatcher.h"
#include <QDir>
#include <QFile>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QtEndian>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <QtTest>

// Swift's ProjectWatcher: one report a burst, the watch kept.
namespace {
void writeFile(const QString &file, const QByteArray &bytes)
{
    QFile out(file);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(bytes) != bytes.size())
        throw std::runtime_error("could not write " + file.toStdString());
}

// A package: a manifest and an images folder.
struct Package {
    QTemporaryDir root;
    QString path = root.filePath(QStringLiteral("Watched.comp"));
    int reports = 0;
    std::unique_ptr<ProjectWatcher> watcher;
    Package()
    {
        QDir().mkpath(path + QStringLiteral("/images"));
        writeFile(path + QStringLiteral("/manifest.json"), "{}");
        watcher = std::make_unique<ProjectWatcher>(path, [this] { reports += 1; });
    }
    QTimer &timer(const char *name) const { return *watcher->findChild<QTimer *>(QString::fromLatin1(name)); }
};
}

class ProjectWatcherTests : public QObject {
    Q_OBJECT
private slots:
    void aBurstOfWritesReportsOnce();
    void aManifestWrittenInPlaceIsSeen();
    void anImageAddedIsSeen();
    void reArmingStopsOnceEveryPathIsWatched();
    void aMissingPathIsTriedForTwoSeconds();
    void aDigestIgnoresTheOrderImagesWereWritten();
    void anExchangeKeepingTheOldPackageIsSeen();
    void deliveryWaitsTheWholeQuietTime();
    void aDigestHashesTheManifestNamesAndSizes();
    void anUnreadableManifestGivesNoDigest();
};

void ProjectWatcherTests::aBurstOfWritesReportsOnce()
{
    Package package;
    for (int count = 0; count < 3; ++count) {
        writeFile(package.path + QStringLiteral("/manifest.json"), QByteArray::number(count));
        QTest::qWait(50);
    }
    QCOMPARE(package.reports, 0);
    QTRY_COMPARE_WITH_TIMEOUT(package.reports, 1, 2000);
    // Single shot: quiet afterwards.
    QTest::qWait(1000);
    QCOMPARE(package.reports, 1);
    QVERIFY(!package.timer("delivery").isActive());
}

void ProjectWatcherTests::aManifestWrittenInPlaceIsSeen()
{
    Package package;
    // Its entries are unchanged: only the manifest's own watch speaks.
    writeFile(package.path + QStringLiteral("/manifest.json"), "{ \"changed\": true }");
    QTRY_COMPARE_WITH_TIMEOUT(package.reports, 1, 2000);
}

void ProjectWatcherTests::anImageAddedIsSeen()
{
    Package package;
    // Inside images/: only that folder's watch speaks.
    writeFile(package.path + QStringLiteral("/images/A.png"), "png");
    QTRY_COMPARE_WITH_TIMEOUT(package.reports, 1, 2000);
    writeFile(package.path + QStringLiteral("/images/B.png"), "png");
    QTRY_COMPARE_WITH_TIMEOUT(package.reports, 2, 2000);
}

void ProjectWatcherTests::reArmingStopsOnceEveryPathIsWatched()
{
    Package package;
    writeFile(package.path + QStringLiteral("/manifest.json"), "[]");
    QTRY_VERIFY_WITH_TIMEOUT(package.timer("rearm").isActive(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(!package.timer("rearm").isActive(), 500);
    QTRY_COMPARE_WITH_TIMEOUT(package.reports, 1, 2000);
    // A swapped-in manifest is watched again by path.
    QFile::remove(package.path + QStringLiteral("/manifest.json"));
    writeFile(package.path + QStringLiteral("/manifest.json"), "{ \"swapped\": true }");
    QTRY_COMPARE_WITH_TIMEOUT(package.reports, 2, 2000);
    writeFile(package.path + QStringLiteral("/manifest.json"), "{ \"again\": true }");
    QTRY_COMPARE_WITH_TIMEOUT(package.reports, 3, 2000);
}

void ProjectWatcherTests::aMissingPathIsTriedForTwoSeconds()
{
    Package package;
    int tries = 0;
    connect(&package.timer("rearm"), &QTimer::timeout, [&] { tries += 1; });
    QVERIFY(QDir(package.path + QStringLiteral("/images")).removeRecursively());
    QTRY_VERIFY_WITH_TIMEOUT(package.timer("rearm").isActive(), 1000);
    // A hundred milliseconds a try, twenty tries.
    QTest::qWait(1000);
    QVERIFY(package.timer("rearm").isActive());
    QTRY_VERIFY_WITH_TIMEOUT(!package.timer("rearm").isActive(), 2000);
    QCOMPARE(tries, 20);
}

void ProjectWatcherTests::aDigestIgnoresTheOrderImagesWereWritten()
{
    QTemporaryDir root;
    const auto make = [&](const QString &name, const QStringList &order) {
        const QString path = root.filePath(name);
        QDir().mkpath(path + QStringLiteral("/images"));
        writeFile(path + QStringLiteral("/manifest.json"), "{}");
        for (const QString &image : order)
            writeFile(path + QStringLiteral("/images/") + image, image.toUtf8());
        return ProjectDigest::compute(path);
    };
    const QStringList names{QStringLiteral("A.png"), QStringLiteral("BB.png"), QStringLiteral("CCC.png"), QStringLiteral("D.mask.png")};
    QStringList reversed = names;
    std::reverse(reversed.begin(), reversed.end());
    QCOMPARE(make(QStringLiteral("One.comp"), names), make(QStringLiteral("Two.comp"), reversed));
}

void ProjectWatcherTests::anExchangeKeepingTheOldPackageIsSeen()
{
    Package package;
    const QString other = package.root.filePath(QStringLiteral("Other.comp"));
    QDir().mkpath(other + QStringLiteral("/images"));
    writeFile(other + QStringLiteral("/manifest.json"), "{ \"other\": true }");
    QTest::qWait(500);
    QCOMPARE(package.reports, 0);
    // Another writer swaps packages and keeps the old one aside.
    QCOMPARE(syscall(SYS_renameat2, AT_FDCWD, QFile::encodeName(other).constData(), AT_FDCWD, QFile::encodeName(package.path).constData(), 2), 0L);
    QTRY_COMPARE_WITH_TIMEOUT(package.reports, 1, 2000);
    // The new package's own files are watched now.
    writeFile(package.path + QStringLiteral("/manifest.json"), "{ \"edited\": true }");
    QTRY_COMPARE_WITH_TIMEOUT(package.reports, 2, 2000);
    writeFile(package.path + QStringLiteral("/images/A.png"), "png");
    QTRY_COMPARE_WITH_TIMEOUT(package.reports, 3, 2000);
    // A sibling that is not the package says nothing.
    writeFile(package.root.filePath(QStringLiteral("Unrelated.txt")), "text");
    QTest::qWait(600);
    QCOMPARE(package.reports, 3);
}

void ProjectWatcherTests::deliveryWaitsTheWholeQuietTime()
{
    Package package;
    QCOMPARE(package.timer("delivery").timerType(), Qt::PreciseTimer);
    QCOMPARE(package.timer("rearm").timerType(), Qt::PreciseTimer);
    QCOMPARE(package.timer("delivery").interval(), 300);
    QCOMPARE(package.timer("rearm").interval(), 100);
    // Quiet time counts from the last write, not the first.
    writeFile(package.path + QStringLiteral("/manifest.json"), "[]");
    QTest::qWait(200);
    QCOMPARE(package.reports, 0);
    QElapsedTimer clock;
    clock.start();
    writeFile(package.path + QStringLiteral("/manifest.json"), "[1]");
    QTRY_COMPARE_WITH_TIMEOUT(package.reports, 1, 2000);
    QVERIFY2(clock.elapsed() >= 300, qPrintable(QString::number(clock.elapsed())));
    QTest::qWait(500);
    QCOMPARE(package.reports, 1);
}

void ProjectWatcherTests::aDigestHashesTheManifestNamesAndSizes()
{
    QTemporaryDir root;
    const QString path = root.filePath(QStringLiteral("Digest.comp"));
    const QString images = path + QStringLiteral("/images/");
    QDir().mkpath(images + QStringLiteral("folder"));
    writeFile(path + QStringLiteral("/manifest.json"), "M");
    writeFile(images + QStringLiteral("b.png"), "bbb");
    writeFile(images + QStringLiteral("a.png"), "aa");
    writeFile(images + QStringLiteral(".hidden"), "h");
    QVERIFY(QFile::link(images + QStringLiteral("a.png"), images + QStringLiteral("link.png")));
    QVERIFY(QFile::link(images + QStringLiteral("gone.png"), images + QStringLiteral("dangling.png")));
    // The oracle: manifest, then sorted names and sizes.
    QCryptographicHash oracle(QCryptographicHash::Sha256);
    oracle.addData(QByteArrayView("M"));
    for (const auto &[name, size] : {std::pair(".hidden", 1), std::pair("a.png", 2), std::pair("b.png", 3)}) {
        oracle.addData(QByteArrayView(name));
        const quint64 little = qToLittleEndian(quint64(size));
        oracle.addData(QByteArrayView(reinterpret_cast<const char *>(&little), sizeof little));
    }
    QCOMPARE(ProjectDigest::compute(path), oracle.result());
    // Same size, other bytes: not read, so unchanged.
    writeFile(images + QStringLiteral("a.png"), "zz");
    QCOMPARE(ProjectDigest::compute(path), oracle.result());
    // Same size, another name: changed.
    QVERIFY(QFile::rename(images + QStringLiteral("b.png"), images + QStringLiteral("c.png")));
    QVERIFY(ProjectDigest::compute(path) != oracle.result());
}

void ProjectWatcherTests::anUnreadableManifestGivesNoDigest()
{
    QTemporaryDir root;
    const QString path = root.filePath(QStringLiteral("Broken.comp"));
    // A folder opens but cannot be read.
    QDir().mkpath(path + QStringLiteral("/manifest.json"));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, ProjectDigest::compute(path));
    // A size that is not what reads: /proc says zero.
    QVERIFY(QDir(path + QStringLiteral("/manifest.json")).removeRecursively());
    QVERIFY(QFile::link(QStringLiteral("/proc/self/status"), path + QStringLiteral("/manifest.json")));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, ProjectDigest::compute(path));
}

QTEST_GUILESS_MAIN(ProjectWatcherTests)
#include "ProjectWatcherTests.moc"
