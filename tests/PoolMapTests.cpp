#include "Document/PixelAdjust.h"
#include "Rendering/HslBlend.h"
#include "Rendering/PoolMap.h"
#include <QtTest>
#include <atomic>
#include <cstdio>

// Parallel kernels in a worker finish on a one-thread pool.
class PoolMapTests : public QObject {
    Q_OBJECT
private slots:
    void aPoolThreadLendsItsPlaceWhileItWaits();
};

namespace {
// On the pool; a hang ends the binary, else waiting.
void finishes(const char *name, const std::function<void()> &work)
{
    QFuture<void> job = QtConcurrent::run(work);
    if (!QTest::qWaitFor([&job] { return job.isFinished(); }, 10'000)) {
        std::fprintf(stderr, "%s hung on a one-thread pool\n", name);
        std::_Exit(1);
    }
}
}

void PoolMapTests::aPoolThreadLendsItsPlaceWhileItWaits()
{
    QThreadPool *pool = QThreadPool::globalInstance();
    const int threads = pool->maxThreadCount();
    pool->setMaxThreadCount(1);
    QImage image(48, 40, QImage::Format_RGBA8888_Premultiplied);
    image.fill(qRgba(40, 80, 120, 255));
    QImage blended, blurred;
    finishes("the HSL blend", [&] { blended = HslBlend::blend(image, image, LayerBlendMode::hue, 1); });
    finishes("the Gaussian blur", [&] { blurred = PixelAdjust::gaussianBlur(image, 3, false); });
    QCOMPARE(blended.size(), image.size());
    QCOMPARE(blurred.size(), image.size());
    // The lent place comes back: the pool counts right again.
    std::atomic<int> seen = 0;
    std::vector<int> items(64);
    finishes("a map in a worker", [&] { PoolMap::blocking(items, [&seen](int &) { ++seen; }); });
    QCOMPARE(seen.load(), 64);
    QCOMPARE(pool->activeThreadCount(), 0);
    pool->setMaxThreadCount(threads);
}

QTEST_GUILESS_MAIN(PoolMapTests)
#include "PoolMapTests.moc"
