#pragma once
#include "ExternalChangeFixtures.h"
#include <QDialog>
#include <atomic>
#include <dirent.h>
#include <dlfcn.h>
#include <sys/syscall.h>
#include <unistd.h>

// Shared by the save tests: libc calls held and counted.
namespace {
// Each sync is slow, so overlapping writes show.
std::atomic<int> syncing = 0;
std::atomic<int> mostAtOnce = 0;
std::atomic<int> slowness = 0;
// While set, a package swap waits: a write held mid-save.
std::atomic<bool> holdSwap = false;
std::atomic<int> swaps = 0;
// While it names a folder, listing that folder waits.
QByteArray heldListing;
std::atomic<bool> holdListing = false;
std::atomic<int> listingsHeld = 0;
// Loads the store finished, counted from its log.
std::atomic<int> loads = 0;
QtMessageHandler forward = nullptr;

void countLoads(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (message.startsWith(QStringLiteral("loaded ")))
        loads += 1;
    forward(type, context, message);
}

// Every hold let go and every count cleared.
void resetHooks()
{
    mostAtOnce = 0;
    slowness = 20'000;
    holdSwap = false;
    swaps = 0;
    holdListing = false;
    listingsHeld = 0;
    loads = 0;
    if (!forward)
        forward = qInstallMessageHandler(countLoads);
}

bool lists(DIR *directory, const QByteArray &folder)
{
    char path[4096];
    const QByteArray link = "/proc/self/fd/" + QByteArray::number(dirfd(directory));
    const ssize_t size = readlink(link.constData(), path, sizeof path - 1);
    return size > 0 && QByteArray(path, int(size)) == folder;
}

template <typename Entry> Entry *held(DIR *directory, Entry *entry)
{
    if (!entry && holdListing && lists(directory, heldListing)) {
        listingsHeld += 1;
        while (holdListing)
            usleep(1000);
    }
    return entry;
}
}

// These take libc's place in the test executable.
extern "C" struct dirent *readdir(DIR *directory)
{
    static const auto real = reinterpret_cast<struct dirent *(*)(DIR *)>(dlsym(RTLD_NEXT, "readdir"));
    return held(directory, real(directory));
}

extern "C" struct dirent64 *readdir64(DIR *directory)
{
    static const auto real = reinterpret_cast<struct dirent64 *(*)(DIR *)>(dlsym(RTLD_NEXT, "readdir64"));
    return held(directory, real(directory));
}

extern "C" int renameat2(int oldDirectory, const char *from, int newDirectory, const char *to, unsigned int flags)
{
    while (holdSwap)
        usleep(1000);
    swaps += 1;
    return int(syscall(SYS_renameat2, oldDirectory, from, newDirectory, to, flags));
}

extern "C" int fsync(int descriptor)
{
    const int now = ++syncing;
    int seen = mostAtOnce;
    while (now > seen && !mostAtOnce.compare_exchange_weak(seen, now)) {
    }
    usleep(useconds_t(slowness.load()));
    const int result = int(syscall(SYS_fsync, descriptor));
    --syncing;
    return result;
}
