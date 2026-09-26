#pragma once
#include <QFile>
#include <QLibraryInfo>
#include <QVersionNumber>
#include <stdexcept>
#include <sys/resource.h>
#include <unistd.h>

// While alive, the process may map only `margin` more bytes.
class AddressSpaceLimit {
public:
    explicit AddressSpaceLimit(qint64 margin)
    {
        QFile statm("/proc/self/statm");
        if (!statm.open(QIODevice::ReadOnly) || getrlimit(RLIMIT_AS, &m_previous) != 0)
            throw std::runtime_error("cannot read the address space in use");
        const qint64 mapped = statm.readAll().split(' ').first().toLongLong() * sysconf(_SC_PAGESIZE);
        rlimit tight = m_previous;
        tight.rlim_cur = rlim_t(mapped + margin);
        if (setrlimit(RLIMIT_AS, &tight) != 0)
            throw std::runtime_error("cannot lower RLIMIT_AS");
    }
    ~AddressSpaceLimit() { setrlimit(RLIMIT_AS, &m_previous); }
    AddressSpaceLimit(const AddressSpaceLimit &) = delete;
    AddressSpaceLimit &operator=(const AddressSpaceLimit &) = delete;

private:
    rlimit m_previous;
};

// Failure points are placed by Qt 6.4's own allocations.
inline bool placesStarvation()
{
    return QLibraryInfo::version().majorVersion() == 6 && QLibraryInfo::version().minorVersion() == 4;
}
