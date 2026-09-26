#include "Document/DocumentHistory.h"
#include <algorithm>
#include <set>
#include <utility>

DocumentHistory::DocumentHistory(int entryLimit, qint64 retainedByteLimit)
    : entryLimit(std::max(0, entryLimit)), retainedByteLimit(std::max<qint64>(0, retainedByteLimit))
{
}

void DocumentHistory::reset()
{
    m_past.clear();
    m_future.clear();
    m_pending.reset();
    m_depth = 0;
    m_revision = QUuid::createUuid();
    m_savedRevision = m_revision;
}

// A closed step changes these three; pending state is spent.
void DocumentHistory::adopt(DocumentHistory &&staged) noexcept
{
    m_past = std::move(staged.m_past);
    m_future = std::move(staged.m_future);
    m_revision = staged.m_revision;
}

void DocumentHistory::begin(const QString &name, const std::optional<CanvasDocument> &document,
                            std::optional<QUuid> selection)
{
    if (m_depth == 0) {
        m_pending = Snapshot{document, selection, m_revision};
        m_pendingName = name;
    }
    m_depth += 1;
}

void DocumentHistory::end(const std::optional<CanvasDocument> &document, std::optional<QUuid> selection)
{
    if (m_depth == 0)
        return;
    m_depth -= 1;
    if (m_depth != 0 || !m_pending)
        return;
    const Snapshot before = *std::exchange(m_pending, std::nullopt);
    // Selecting, navigating and no-op edits keep redo history.
    if (before.document == document)
        return;
    m_revision = QUuid::createUuid();
    m_past.push_back({m_pendingName, before, Snapshot{document, selection, m_revision}});
    m_future.clear();
    trim(document);
}

std::optional<DocumentHistory::Snapshot> DocumentHistory::undo()
{
    if (!canUndo())
        return std::nullopt;
    Entry entry = std::move(m_past.back());
    m_past.pop_back();
    m_future.push_back(entry);
    m_revision = entry.before.revision;
    trim(entry.before.document);
    return entry.before;
}

std::optional<DocumentHistory::Snapshot> DocumentHistory::redo()
{
    if (!canRedo())
        return std::nullopt;
    Entry entry = std::move(m_future.back());
    m_future.pop_back();
    m_past.push_back(entry);
    m_revision = entry.after.revision;
    trim(entry.after.document);
    return entry.after;
}

qint64 DocumentHistory::retainedBytes(const std::optional<CanvasDocument> &current) const
{
    const auto assets = [](const ImageLayer &layer) {
        std::vector<const ImportedImage *> result;
        if (layer.asset)
            result.push_back(&*layer.asset);
        if (layer.mask)
            result.push_back(&layer.mask->asset);
        return result;
    };
    std::set<ImageIdentity> seen;
    if (current) {
        for (const ImageLayer &layer : current->layers) {
            for (const ImportedImage *asset : assets(layer)) {
                seen.insert(asset->identity());
                seen.insert({nullptr, asset->thumbnail.cacheKey()});
            }
        }
    }
    qint64 bytes = 0;
    const auto count = [&](const Snapshot &snapshot) {
        if (!snapshot.document)
            return;
        for (const ImageLayer &layer : snapshot.document->layers) {
            for (const ImportedImage *asset : assets(layer)) {
                if (seen.insert(asset->identity()).second)
                    bytes += asset->byteCount();
                if (seen.insert({nullptr, asset->thumbnail.cacheKey()}).second)
                    bytes += asset->thumbnail.sizeInBytes();
            }
        }
    };
    for (const std::vector<Entry> *entries : {&m_past, &m_future}) {
        for (const Entry &entry : *entries) {
            count(entry.before);
            count(entry.after);
        }
    }
    return bytes;
}

void DocumentHistory::trim(const std::optional<CanvasDocument> &current)
{
    while (int(m_past.size() + m_future.size()) > entryLimit || retainedBytes(current) > retainedByteLimit) {
        if (!m_past.empty())
            m_past.erase(m_past.begin());
        else if (!m_future.empty())
            m_future.erase(m_future.begin());
        else
            break;
    }
}
