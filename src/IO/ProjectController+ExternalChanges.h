#pragma once
#include "IO/ProjectWatcher.h"
#include <QByteArray>
#include <memory>
#include <optional>

class QTimer;

// Swift's ExternalChangeState: the watch's bookkeeping, per controller.
struct ExternalChangeState {
    std::unique_ptr<ProjectWatcher> watcher;
    // The package as last read or written.
    std::optional<QByteArray> knownDigest;
    bool checking = false;
    bool pending = false;
    // Our own save is writing: its events are ours.
    bool saving = false;
    QTimer *recheck = nullptr;
    int recheckAttempt = 0;
    // Reloads because the package changed on disk; tests read it.
    int reloadCount = 0;
};
