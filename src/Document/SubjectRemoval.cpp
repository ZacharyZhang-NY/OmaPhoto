#include "Document/SubjectRemoval.h"
#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include "Document/MaskTracing.h"
#include "Document/GuidedMatte.h"
#include "Document/PixelAdjust.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include <QMutex>
#include <QStandardPaths>
#include <QtConcurrent>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <onnxruntime_cxx_api.h>

namespace {
QString message(SubjectRemovalError::Kind kind, const QString &detail)
{
    switch (kind) {
    case SubjectRemovalError::Kind::noSubject:
        return QStringLiteral("No foreground subject was detected in this layer. Try an image with a more distinct subject.");
    case SubjectRemovalError::Kind::model: return QStringLiteral("Remove Background could not use its model: ") + detail;
    }
    throw std::logic_error("unknown subject removal failure");
}
}

SubjectRemovalError::SubjectRemovalError(Kind kind, const QString &detail) : std::runtime_error(message(kind, detail).toStdString()), kind(kind)
{
}

namespace {
using SubjectRemoval::side;

QImage checked(const QImage &image)
{
    if (image.isNull())
        throw ExportError(ExportError::Kind::render);
    return image;
}

QString modelPath()
{
    const QString name = QStringLiteral("omaphoto/u2net.onnx");
    const QString path = QStandardPaths::locate(QStandardPaths::GenericDataLocation, name);
    if (path.isEmpty()) {
        const QString folders = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation).join(QStringLiteral(", "));
        qCWarning(lcApp) << "Remove Background finds no model in" << folders;
        throw SubjectRemovalError(SubjectRemovalError::Kind::model, name + QStringLiteral(" is in no data folder (") + folders + QStringLiteral(")"));
    }
    qCInfo(lcApp) << "Remove Background loads its model from" << path;
    return path;
}

// The session and its names, loaded once for the process.
struct Model {
    Ort::Env environment{ORT_LOGGING_LEVEL_WARNING, "omaphoto"};
    Ort::Session session;
    std::string input;
    std::string output;
    explicit Model(const QString &path) : session(environment, path.toLocal8Bit().constData(), Ort::SessionOptions())
    {
        Ort::AllocatorWithDefaultOptions allocator;
        input = session.GetInputNameAllocated(0, allocator).get();
        output = session.GetOutputNameAllocated(0, allocator).get();
    }
};

// A failed load stays unset; the next use tries again.
Model &model()
{
    static Model loaded(modelPath());
    return loaded;
}

// U²-Net's mask at the image's size, white over the subject.
QImage infer(const QImage &image)
{
    std::vector<float> input = SubjectRemoval::modelInput(image);
    Model &loaded = model();
    const std::array<int64_t, 4> dimensions{1, 3, side, side};
    const Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    const Ort::Value tensor = Ort::Value::CreateTensor<float>(memory, input.data(), input.size(), dimensions.data(), dimensions.size());
    const char *inputs[] = {loaded.input.c_str()}, *outputs[] = {loaded.output.c_str()};
    const std::vector<Ort::Value> result = loaded.session.Run(Ort::RunOptions{nullptr}, inputs, &tensor, 1, outputs, 1);
    return SubjectRemoval::modelMask(result.front().GetTensorData<float>(), image.size());
}

// Swift's MaskCache: the last image's mask, by identity.
class MaskCache {
public:
    QImage mask(const QImage &image)
    {
        {
            const QMutexLocker lock(&m_lock);
            if (m_key == image.cacheKey())
                return m_value;
        }
        const QImage made = infer(image);
        const QMutexLocker lock(&m_lock);
        m_key = image.cacheKey();
        m_value = made;
        return made;
    }

private:
    QMutex m_lock;
    std::optional<qint64> m_key;
    QImage m_value;
};

MaskCache cache;

// The model's mask; slider moves only redo the refining.
QImage vision(const QImage &image)
{
    try {
        return cache.mask(image);
    } catch (const Ort::Exception &error) {
        qCWarning(lcApp) << "Remove Background's model failed:" << error.what();
        throw SubjectRemovalError(SubjectRemovalError::Kind::model, QString::fromUtf8(error.what()));
    } catch (const std::bad_alloc &) {
        // Its buffers, or ONNX Runtime's wrapper, found no memory.
        throw ExportError(ExportError::Kind::render);
    }
}

// Advanced refines, shifts the edge, then clears the haze.
QImage refined(const QImage &mask, const QImage &guide, const FilterSettings &settings, double limit)
{
    // Basic is the model's mask as it comes: quick.
    if (settings.backgroundQuality != BackgroundQuality::advanced)
        return mask;
    QImage image = settings.refineEdges > 0 ? GuidedMatte::refine(mask, guide, settings.refineEdges, limit) : mask;
    std::optional<float> level;
    if (settings.shiftEdge != 0) {
        // A blur, then a hard cut, moves the edge.
        image = PixelAdjust::gaussianBlur(image, std::abs(settings.shiftEdge) / 2, true);
        level = settings.shiftEdge < 0 ? 0.75f : 0.25f;
    }
    // 0 leaves the mask; 100 cuts hard at the middle.
    const float slope = 1 / std::max(0.02f, 1 - float(settings.matteContrast / 100) * 0.98f);
    QImage result = checked(QImage(image.size(), QImage::Format_Grayscale8));
    for (int y = 0; y < image.height(); ++y) {
        const uchar *in = image.constScanLine(y);
        uchar *out = result.scanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            float value = float(in[x]) / 255;
            // Swift's clamp first is the last one's: Contrast only steepens.
            if (level)
                value = (value - *level) * 1000;
            value = slope * value + (1 - slope) / 2;
            out[x] = uchar(std::lround(std::clamp(value, 0.0f, 1.0f) * 255));
        }
    }
    return result;
}
}

std::vector<float> SubjectRemoval::modelInput(const QImage &image)
{
    // Scaled premultiplied, so clear pixels read as black.
    const QImage small = checked(image.scaled(side, side, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGBA8888_Premultiplied));
    uchar brightest = 0;
    for (int y = 0; y < side; ++y) {
        const uchar *row = small.constScanLine(y);
        for (int x = 0; x < side; ++x)
            brightest = std::max({brightest, row[x * 4], row[x * 4 + 1], row[x * 4 + 2]});
    }
    const float peak = std::max(float(brightest), 1e-6f);
    constexpr std::array<float, 3> mean{0.485f, 0.456f, 0.406f}, spread{0.229f, 0.224f, 0.225f};
    std::vector<float> input(size_t(3) * side * side);
    for (int y = 0; y < side; ++y) {
        const uchar *row = small.constScanLine(y);
        for (int x = 0; x < side; ++x) {
            for (size_t channel = 0; channel < 3; ++channel)
                input[channel * side * side + size_t(y) * side + size_t(x)] = (float(row[x * 4 + int(channel)]) / peak - mean[channel]) / spread[channel];
        }
    }
    return input;
}

QImage SubjectRemoval::modelMask(const float *prediction, QSize size)
{
    const auto [low, high] = std::minmax_element(prediction, prediction + side * side);
    // Nothing the model takes for a subject: Swift's no instances.
    if (!(*high >= 0.5f))
        throw SubjectRemovalError(SubjectRemovalError::Kind::noSubject);
    // Stretched from low to high, truncated, as rembg does.
    const float range = std::max(*high - *low, std::numeric_limits<float>::epsilon());
    QImage mask = checked(QImage(side, side, QImage::Format_Grayscale8));
    for (int y = 0; y < side; ++y) {
        uchar *row = mask.scanLine(y);
        for (int x = 0; x < side; ++x)
            row[x] = uchar((prediction[size_t(y) * side + size_t(x)] - *low) / range * 255);
    }
    // Premultiplied, a smaller grid is area-filtered, as in `levels`.
    return checked(checked(mask.convertToFormat(QImage::Format_RGBA8888_Premultiplied))
                       .scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                       .convertToFormat(QImage::Format_Grayscale8));
}

QImage SubjectRemoval::subjectMask(const QImage &image, const std::optional<QImage> &existing, const FilterSettings &settings)
{
    if (existing && existing->size() != image.size())
        throw std::logic_error("a mask to keep must share the layer's grid");
    const QImage subject = refined(vision(image), image, settings, std::numeric_limits<double>::max());
    if (!existing)
        return subject;
    // Both masks hide: what either one hides stays hidden.
    const QImage base = checked(existing->convertToFormat(QImage::Format_Grayscale8));
    QImage combined = checked(QImage(subject.size(), QImage::Format_Grayscale8));
    for (int y = 0; y < subject.height(); ++y) {
        const uchar *top = subject.constScanLine(y), *under = base.constScanLine(y);
        uchar *out = combined.scanLine(y);
        for (int x = 0; x < subject.width(); ++x)
            out[x] = uchar((int(top[x]) * under[x] + 127) / 255);
    }
    return combined;
}

QImage SubjectRemoval::foreground(const QImage &image)
{
    return vision(image);
}

QImage SubjectRemoval::run(const QImage &image, const FilterSettings &settings)
{
    // The preview refines a copy 1400 long at most: quick.
    const QImage mask = refined(vision(image), image, settings, 1400);
    // Swift's blend over clear: each premultiplied byte times the mask.
    QImage result = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    // Its own pixels now, where a failed copy throws.
    result.detach();
    if (result.isNull())
        throw ExportError(ExportError::Kind::render);
    for (int y = 0; y < result.height(); ++y) {
        const uchar *coverage = mask.constScanLine(y);
        uchar *row = result.scanLine(y);
        for (int x = 0; x < result.width() * 4; ++x)
            row[x] = uchar((int(row[x]) * coverage[x / 4] + 127) / 255);
    }
    return result;
}

bool EditorSession::canSelectSubject() const
{
    // canEditLayers reads the document and the busy project.
    return canEditSelection();
}

void EditorSession::selectSubject(SelectionMode mode, std::function<void()> done)
{
    // The caller resumes from the event loop, as after await.
    const auto finish = [this, &done] {
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
    };
    if (!canSelectSubject()) {
        finish();
        return;
    }
    QImage shown;
    try {
        shown = BrushRaster::context(m_document->width, m_document->height, false);
        QPainter painter(&shown);
        drawLiveComposite(*m_document, painter);
    } catch (const ExportError &error) {
        qCWarning(lcApp) << "Select Subject cannot draw the canvas:" << error.what();
        finish();
        return;
    }
    setIsProjectBusy(true);
    m_subjecting = Subjecting{mode, m_document->id, std::move(done)};
    m_subject.setFuture(QtConcurrent::run([shown]() -> Subjected {
        try {
            return Subjected{SubjectRemoval::subjectMask(shown, std::nullopt, FilterSettings()), std::nullopt};
        } catch (const SubjectRemovalError &error) {
            return Subjected{std::nullopt, QString::fromUtf8(error.what())};
        } catch (const ExportError &error) {
            return Subjected{std::nullopt, QString::fromUtf8(error.what())};
        }
    }));
}

void EditorSession::finishSubject()
{
    const Subjected result = m_subject.result();
    const Subjecting pending = std::exchange(m_subjecting, std::nullopt).value();
    setIsProjectBusy(false);
    if (m_document && m_document->id == pending.documentID) {
        if (result.failure) {
            qCWarning(lcApp).noquote() << "Select Subject failed:" << *result.failure;
            setBrushError(result.failure);
        } else if (const std::optional<QPainterPath> traced = MaskTracing::whitePixels(*result.mask)) {
            // White where the subject is: its outline is the selection.
            const QTransform toDocument = BrushRaster::pixelToDocument(LayerTransform{.origin = QPointF(0, 0), .size = m_document->size()},
                                                                       result.mask->width(), result.mask->height());
            applySelection(toDocument.map(*traced), pending.mode, QStringLiteral("Select Subject"));
        } else {
            qCWarning(lcApp) << "Select Subject traced no outline";
        }
    }
    if (pending.done)
        QMetaObject::invokeMethod(this, pending.done, Qt::QueuedConnection);
}
