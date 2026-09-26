#pragma once
#include <QThread>
#include <QThreadPool>
#include <QtConcurrent>
#include <utility>

// QtConcurrent's blockingMap, which deadlocks a full pool from inside.
namespace PoolMap {
// A pool thread waiting on the pool lends its place.
template <typename Sequence, typename Function>
void blocking(Sequence &sequence, Function &&function)
{
    struct Lent {
        QThreadPool *const pool;
        const bool lent;
        ~Lent()
        {
            if (lent)
                pool->reserveThread();
        }
    } guard{QThreadPool::globalInstance(), QThreadPool::globalInstance()->contains(QThread::currentThread())};
    if (guard.lent)
        guard.pool->releaseThread();
    QtConcurrent::blockingMap(sequence, std::forward<Function>(function));
}
}
