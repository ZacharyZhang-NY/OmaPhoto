#pragma once
#include "Document/ColorPalette.h"
#include "IO/ImageExporter.h"
#include <QAbstractScrollArea>
#include <QWidget>
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>

class QLabel;
class QProgressBar;
class QShortcut;
class QPushButton;
class QSlider;
class QTimer;
class DialogColorSwatch;
class EditorSession;

// Swift's JPEGPreview: the encoded JPEG, fitted or zoomed, dragged.
class JPEGPreview : public QAbstractScrollArea {
    Q_OBJECT
public:
    static constexpr QSize frame{560, 330};
    static constexpr std::array<double, 6> steps{0.25, 0.5, 1, 2, 4, 8};
    // The zoom at which the whole image fits `frame`.
    static double fitZoom(int width, int height, QSizeF frame, double displayScale);
    // The next step past `zoom`: 1 in, −1 out.
    static std::optional<double> step(double zoom, int direction);

    // `pixels`: the export's size, which the image may undercut.
    JPEGPreview(QSize pixels, std::function<void()> zoomChanged, QWidget *parent);
    void setImage(const QImage &image, bool updating);
    // None fits; 1 is a screen pixel a pixel.
    std::optional<double> zoom() const { return m_zoom; }
    void setZoom(std::optional<double> zoom);
    double shownZoom() const;
    void zoomBy(int direction);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    QSizeF shownSize(double zoom) const;
    void updateRanges();
    void showCursor(bool grabbing);

    const QSize m_pixels;
    // Each change of zoom, for the sheet's buttons.
    const std::function<void()> m_zoomChanged;
    QImage m_image;
    // The image shrunk to the size last drawn.
    QImage m_reduced;
    bool m_updating = false;
    std::optional<double> m_zoom;
    // A drag's press and the scroll it began from.
    std::optional<std::pair<QPoint, QPoint>> m_drag;
};

// Swift's JPEGExportSheet: quality, matte and the encoded preview.
class JPEGExportSheet : public QWidget {
    Q_OBJECT
public:
    // The last export's quality, where the next one starts.
    static const QString qualityKey;

    JPEGExportSheet(ExportRaster raster, EditorSession &session, std::function<void(std::optional<QByteArray>)> finish, QWidget *parent = nullptr);
    ~JPEGExportSheet() override;

protected:
    // The zoom glyphs follow the theme.
    void changeEvent(QEvent *event) override;

private:
    void request();
    void encode();
    void setMatte(const PaletteColor &matte);
    void synchronize();
    // The View menu's zoom keys, as remapped, zoom the preview.
    void bindZoomKeys();

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
    JPEGPreview *const m_preview;
    QProgressBar *const m_spinner;
    QSlider *const m_quality;
    QLabel *const m_percent;
    DialogColorSwatch *const m_matteSwatch;
    QLabel *const m_failure;
    QLabel *const m_bytes;
    QLabel *const m_note;
    QPushButton *const m_export;
    QPushButton *const m_fit;
    QPushButton *const m_zoomIn;
    QPushButton *const m_zoomOut;
    // Fit, Actual Pixels, Zoom In and Zoom Out.
    std::array<QShortcut *, 4> m_zoomKeys{};
};
