#include "Document/DocumentLimits.h"
#include <QLocale>
#include <algorithm>
#include <unistd.h>

// Swift's physicalMemory / 16, read once as a static let.
qint64 DocumentLimits::documentPixelBudget()
{
    static const qint64 budget = budgetFor(qint64(sysconf(_SC_PHYS_PAGES)) * qint64(sysconf(_SC_PAGE_SIZE)));
    return budget;
}

qint64 DocumentLimits::budgetFor(qint64 memory)
{
    return std::min<qint64>(800'000'000, std::max(maxSurfacePixels, memory / 16));
}

qint64 DocumentLimits::maxSurfaceMegapixels()
{
    return maxSurfacePixels / 1'000'000;
}

qint64 DocumentLimits::documentBudgetMegapixels()
{
    return documentPixelBudget() / 1'000'000;
}

QString DocumentLimits::maxSideText()
{
    return QLocale(QLocale::English).toString(maxSide);
}
