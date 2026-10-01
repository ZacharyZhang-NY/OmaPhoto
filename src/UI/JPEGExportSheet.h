#pragma once
#include "Document/ColorPalette.h"
#include "IO/ImageExporter.h"
#include <QWidget>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>

class QLabel;
class QProgressBar;
class QPushButton;
class QSlider;
class QTimer;
class DialogColorSwatch;
class EditorSession;

// Swift's JPEGExportSheet: quality, matte and the encoded preview.
class JPEGExportSheet : public QWidget {
    Q_OBJECT
public:
    // The last export's quality, where the next one starts.
    static const QString qualityKey;

    JPEGExportSheet(ExportRaster raster, EditorSession &session, std::function<void(std::optional<QByteArray>)> finish, QWidget *parent = nullptr);
    ~JPEGExportSheet() override;

private:
    class Preview;
    void request();
    void encode();
    void setMatte(const PaletteColor &matte);
    void synchronize();

    const ExportRaster m_raster;
    EditorSession &m_session;
    const std::function<void(std::optional<QByteArray>)> m_finish;
    JPEGOptions m_options;
    std::optional<JPEGResult> m_result;
    std::optional<JPEGOptions> m_readyOptions;
    std::optional<QString> m_error;
    // The newest request's flag, Swift's task cancellation.
    std::shared_ptr<std::atomic_bool> m_cancelled;
    QTimer *const m_wait;
    Preview *const m_preview;
    QProgressBar *const m_spinner;
    QSlider *const m_quality;
    QLabel *const m_percent;
    DialogColorSwatch *const m_matteSwatch;
    QLabel *const m_failure;
    QLabel *const m_bytes;
    QLabel *const m_note;
    QPushButton *const m_export;
};
