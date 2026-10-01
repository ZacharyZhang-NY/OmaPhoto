#include "UI/JPEGExportSheet.h"
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <algorithm>
#include <cmath>

double JPEGPreview::fitZoom(int width, int height, QSizeF frame, double displayScale)
{
    const double scale = std::max(1.0, displayScale);
    return std::min(frame.width() / (width / scale), frame.height() / (height / scale));
}

std::optional<double> JPEGPreview::step(double zoom, int direction)
{
    if (direction > 0) {
        const auto next = std::find_if(steps.begin(), steps.end(), [zoom](double step) { return step > zoom * 1.001; });
        return next == steps.end() ? std::nullopt : std::optional(*next);
    }
    const auto next = std::find_if(steps.rbegin(), steps.rend(), [zoom](double step) { return step < zoom * 0.999; });
    return next == steps.rend() ? std::nullopt : std::optional(*next);
}

JPEGPreview::JPEGPreview(QSize pixels, std::function<void()> zoomChanged, QWidget *parent)
    : QAbstractScrollArea(parent), m_pixels(pixels), m_zoomChanged(std::move(zoomChanged))
{
    setFixedSize(frame);
    setFrameShape(QFrame::NoFrame);
    setToolTip(QStringLiteral("Drag or scroll to move around; double-click switches between Fit and 100%"));
    horizontalScrollBar()->setSingleStep(20);
    verticalScrollBar()->setSingleStep(20);
}

void JPEGPreview::setImage(const QImage &image, bool updating)
{
    // A new image only: the averaged copy still holds.
    if (image.cacheKey() != m_image.cacheKey())
        m_reduced = QImage();
    m_image = image;
    m_updating = updating;
    viewport()->update();
}

double JPEGPreview::shownZoom() const
{
    return m_zoom.value_or(fitZoom(m_pixels.width(), m_pixels.height(), QSizeF(frame), devicePixelRatioF()));
}

void JPEGPreview::zoomBy(int direction)
{
    if (const std::optional<double> next = step(shownZoom(), direction))
        setZoom(next);
}

// Swift's keepCentered: the middle stays on the same part.
void JPEGPreview::setZoom(std::optional<double> zoom)
{
    if (zoom == m_zoom)
        return;
    const QSizeF view(viewport()->size());
    QPointF fraction(0.5, 0.5);
    if (m_zoom) {
        const QSizeF before = shownSize(*m_zoom);
        fraction = QPointF((horizontalScrollBar()->value() + std::min(view.width(), before.width()) / 2) / before.width(),
                           (verticalScrollBar()->value() + std::min(view.height(), before.height()) / 2) / before.height());
    }
    m_zoom = zoom;
    updateRanges();
    if (m_zoom) {
        const QSizeF size = shownSize(*m_zoom);
        const QSizeF shown(viewport()->size());
        const double x = std::clamp(fraction.x() * size.width() - shown.width() / 2, 0.0, std::max(0.0, size.width() - shown.width()));
        const double y = std::clamp(fraction.y() * size.height() - shown.height() / 2, 0.0, std::max(0.0, size.height() - shown.height()));
        horizontalScrollBar()->setValue(int(std::lround(x)));
        verticalScrollBar()->setValue(int(std::lround(y)));
    }
    m_drag.reset();
    showCursor(false);
    viewport()->update();
    m_zoomChanged();
}

// The image's size on screen at `zoom`, in points.
QSizeF JPEGPreview::shownSize(double zoom) const
{
    return QSizeF(m_pixels) / std::max(1.0, devicePixelRatioF()) * zoom;
}

void JPEGPreview::updateRanges()
{
    const QSizeF size = m_zoom ? shownSize(*m_zoom) : QSizeF(0, 0);
    const QSize view = viewport()->size();
    horizontalScrollBar()->setRange(0, std::max(0, int(std::ceil(size.width() - view.width()))));
    horizontalScrollBar()->setPageStep(view.width());
    verticalScrollBar()->setRange(0, std::max(0, int(std::ceil(size.height() - view.height()))));
    verticalScrollBar()->setPageStep(view.height());
}

void JPEGPreview::showCursor(bool grabbing)
{
    viewport()->setCursor(!m_zoom ? Qt::ArrowCursor : grabbing ? Qt::ClosedHandCursor : Qt::OpenHandCursor);
}

void JPEGPreview::paintEvent(QPaintEvent *)
{
    QPainter painter(viewport());
    const QRectF view(viewport()->rect());
    painter.fillRect(view, QColor::fromRgbF(0.12f, 0.12f, 0.12f));
    if (!m_image.isNull()) {
        QRectF target;
        if (m_zoom) {
            // Smaller than the view, it sits in the middle.
            const QSizeF size = shownSize(*m_zoom);
            target = QRectF(QPointF(size.width() < view.width() ? (view.width() - size.width()) / 2 : -horizontalScrollBar()->value(),
                                    size.height() < view.height() ? (view.height() - size.height()) / 2 : -verticalScrollBar()->value()),
                            size);
        } else {
            target = QRectF(QPointF(), QSizeF(m_image.size()).scaled(view.size(), Qt::KeepAspectRatio));
            target.moveCenter(view.center());
        }
        // Nearest from 100% up: each JPEG pixel as it is.
        if (m_zoom && *m_zoom >= 1) {
            painter.drawImage(target, m_image);
        } else {
            // Swift's high quality: an area average, kept per size.
            const QSize device = (target.size() * devicePixelRatioF()).toSize().expandedTo(QSize(1, 1));
            if (m_reduced.size() != device)
                m_reduced = m_image.scaled(device, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            painter.drawImage(target, m_reduced);
        }
    }
    if (m_updating) {
        // Swift's regular material behind the spinner.
        QColor material = palette().color(QPalette::Window);
        material.setAlphaF(0.85f);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(material);
        QRectF plate(0, 0, 64, 64);
        plate.moveCenter(view.center());
        painter.drawRoundedRect(plate, 8, 8);
    }
}

void JPEGPreview::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateRanges();
}

void JPEGPreview::mousePressEvent(QMouseEvent *event)
{
    if (m_zoom && event->button() == Qt::LeftButton)
        m_drag = {event->position().toPoint(), QPoint(horizontalScrollBar()->value(), verticalScrollBar()->value())};
}

void JPEGPreview::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_drag)
        return;
    const QPoint moved = event->position().toPoint() - m_drag->first;
    horizontalScrollBar()->setValue(m_drag->second.x() - moved.x());
    verticalScrollBar()->setValue(m_drag->second.y() - moved.y());
    showCursor(true);
}

void JPEGPreview::mouseReleaseEvent(QMouseEvent *)
{
    m_drag.reset();
    showCursor(false);
}

// Double-click switches between Fit and 100%.
void JPEGPreview::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        setZoom(m_zoom ? std::nullopt : std::optional(1.0));
}
