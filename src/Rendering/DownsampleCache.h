#pragma once
#include <QImage>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

// Sharp halved copies of layer images, least recently used dropped.
class DownsampleCache {
public:
    // Pixels of halved copies kept at once.
    static constexpr qint64 pixelBudget = 100'000'000;
    // Most halvings ever used; QPainter does the rest.
    static constexpr int maxLevel = 6;

    struct Reduced {
        QImage image;
        int level;
    };

    explicit DownsampleCache(qint64 pixelBudget = DownsampleCache::pixelBudget);
    static DownsampleCache &shared();

    static int level(double factor);
    QImage imageDrawnAt(const QImage &image, double factor);
    Reduced imageAtLevel(const QImage &image, int wanted);
    // Unthreaded when its caller already works side by side.
    static std::optional<QImage> halve(const QImage &image, bool threaded = true);

private:
    struct Entry {
        QImage source;
        std::vector<QImage> levels;
        quint64 lastUse;
        qint64 pixels() const;
    };

    void evict(qint64 keeping);

    const qint64 m_pixelBudget;
    std::unordered_map<qint64, Entry> m_entries;
    quint64 m_clock = 0;
    std::mutex m_lock;
};
