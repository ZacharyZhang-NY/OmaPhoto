#include "IO/ProjectDigest.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <dirent.h>
#include <dlfcn.h>
#include <unistd.h>

// An image gone between listing and looking: no digest.
namespace {
// The file to remove once its folder's listing ends.
QByteArray vanishing;
bool vanished = false;

bool listsImages(DIR *directory)
{
    char path[4096];
    const QByteArray link = "/proc/self/fd/" + QByteArray::number(dirfd(directory));
    const ssize_t size = readlink(link.constData(), path, sizeof path - 1);
    return size > 0 && QByteArray(path, int(size)).endsWith("/images");
}

// At the listing's end the image goes, before any lookup.
template <typename Entry> Entry *listed(DIR *directory, Entry *entry)
{
    if (!entry && !vanishing.isEmpty() && !vanished && listsImages(directory))
        vanished = ::unlink(vanishing.constData()) == 0;
    return entry;
}
}

// These take libc's place in the test executable.
extern "C" struct dirent *readdir(DIR *directory)
{
    static const auto real = reinterpret_cast<struct dirent *(*)(DIR *)>(dlsym(RTLD_NEXT, "readdir"));
    return listed(directory, real(directory));
}

extern "C" struct dirent64 *readdir64(DIR *directory)
{
    static const auto real = reinterpret_cast<struct dirent64 *(*)(DIR *)>(dlsym(RTLD_NEXT, "readdir64"));
    return listed(directory, real(directory));
}

class ProjectDigestRaceTests : public QObject {
    Q_OBJECT
private slots:
    void anImageGoneMidListingGivesNoDigest();
};

void ProjectDigestRaceTests::anImageGoneMidListingGivesNoDigest()
{
    QTemporaryDir root;
    const QString path = root.filePath(QStringLiteral("Race.comp"));
    QVERIFY(QDir().mkpath(path + QStringLiteral("/images")));
    for (const QString &name : {QStringLiteral("manifest.json"), QStringLiteral("images/A.png"), QStringLiteral("images/B.png")}) {
        QFile file(path + QLatin1Char('/') + name);
        QVERIFY(file.open(QIODevice::WriteOnly) && file.write("bytes") == 5);
    }
    // Listed whole, one image looked up after it is gone.
    const QByteArray whole = ProjectDigest::compute(path);
    vanishing = QFile::encodeName(path + QStringLiteral("/images/A.png"));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, ProjectDigest::compute(path));
    QVERIFY(vanished);
    // Settled, the package fingerprints again, without it.
    vanishing.clear();
    QVERIFY(ProjectDigest::compute(path) != whole);
}

QTEST_GUILESS_MAIN(ProjectDigestRaceTests)
#include "ProjectDigestRaceTests.moc"
