#pragma once
#include "Document/EditorSession+Model.h"
#include <QObject>
#include <QThreadPool>
#include <atomic>
#include <functional>
#include <map>
#include <memory>

// Swift's EffectsPreviewCache: canvas previews off the UI thread.
class EffectsPreviewCache : public QObject {
    Q_OBJECT
public:
    explicit EffectsPreviewCache(QObject *parent = nullptr);
    ~EffectsPreviewCache() override;
    // Swift's tuple; a seed alone carries where it belongs.
    struct Result {
        QImage image;
        double inset;
        std::optional<LayerTransform> placement;
    };
    // Shown at `placement` until a fresh preview lands.
    void seed(QUuid id, const QImage &image, const LayerTransform &placement);
    // What is already rendered, asking for nothing new.
    std::optional<Result> rendered(QUuid id) const;
    // Drops layers without effects; they share the pixel budget.
    void prepare(const std::vector<ImageLayer> &layers);
    // The last result; `completion` runs once a new one lands.
    std::optional<Result> preview(const ImageLayer &layer, const std::optional<QImage> &mask, const LayerTransform &transform,
                                  const std::optional<LayerTransform> &maskPlacement, const std::function<void()> &completion);
    // Rendered at once, at the preview size: typed text's.
    std::optional<Result> renderNow(const QImage &image, const std::optional<QImage> &mask, const LayerEffects &effects) const;

private:
    struct Request {
        QUuid id;
        ImportedImage image;
        std::optional<QImage> mask;
        // Swift's `===` on the mask's enabled image.
        std::optional<ImageIdentity> maskSource;
        std::optional<LayerTransform> placement;
        LayerTransform transform;
        LayerEffects effects;
        int sideLimit;
        std::shared_ptr<std::atomic<bool>> cancelled;
        bool matches(const Request &other) const;
    };
    struct Entry {
        Request request;
        std::optional<Result> result;
    };
    static std::optional<Result> render(const Request &request);
    static Result scaled(const QImage &image, const std::optional<QImage> &mask, LayerEffects effects, int sideLimit);
    void cancel(QUuid id);
    void land(QUuid layerID, QUuid requestID, const std::optional<Result> &result, const std::function<void()> &completion);

    std::map<QUuid, Entry> m_entries;
    // Warped effects handed in, shown until the worker catches up.
    std::map<QUuid, Result> m_seeds;
    // Three finished previews a layer, newest last, for undo.
    std::map<QUuid, std::vector<Entry>> m_recent;
    int m_sideLimit = 1536;
    // Swift's serial queue: one render at a time.
    QThreadPool m_worker;
};
