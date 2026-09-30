#pragma once
#include "Document/EditorSession+Model.h"
#include <QString>
#include <QUuid>
#include <optional>
#include <vector>

// Snapshots share image data; layer edits copy no pixels.
class DocumentHistory {
public:
    struct Snapshot {
        std::optional<CanvasDocument> document;
        std::optional<QUuid> activeLayerID;
        QUuid revision;
    };

    const int entryLimit;
    const qint64 retainedByteLimit;

    explicit DocumentHistory(int entryLimit = 100, qint64 retainedByteLimit = 256LL * 1024 * 1024);

    bool canUndo() const { return m_depth == 0 && !m_past.empty(); }
    bool canRedo() const { return m_depth == 0 && !m_future.empty(); }
    QString undoName() const { return m_past.empty() ? QString() : m_past.back().name; }
    QString redoName() const { return m_future.empty() ? QString() : m_future.back().name; }
    bool isModified() const { return m_revision != m_savedRevision; }
    int undoCount() const { return int(m_past.size()); }
    void markSaved() { m_savedRevision = m_revision; }
    // The document now, for a save that finishes later.
    QUuid currentRevision() const { return m_revision; }
    // A save of `saved` finished; later edits stay modified.
    void markSaved(QUuid saved) { m_savedRevision = saved; }
    void reset();
    // Takes the steps a staged copy of this history recorded.
    void adopt(DocumentHistory &&staged) noexcept;

    void begin(const QString &name, const std::optional<CanvasDocument> &document, std::optional<QUuid> selection);
    void end(const std::optional<CanvasDocument> &document, std::optional<QUuid> selection);
    std::optional<Snapshot> undo();
    std::optional<Snapshot> redo();
    qint64 retainedBytes(const std::optional<CanvasDocument> &current) const;

private:
    struct Entry {
        QString name;
        Snapshot before;
        Snapshot after;
    };

    void trim(const std::optional<CanvasDocument> &current);

    std::vector<Entry> m_past;
    std::vector<Entry> m_future;
    QUuid m_revision = QUuid::createUuid();
    QUuid m_savedRevision = m_revision;
    std::optional<Snapshot> m_pending;
    QString m_pendingName = QStringLiteral("Edit");
    int m_depth = 0;
};
