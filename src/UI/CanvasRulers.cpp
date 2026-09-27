#include "UI/CanvasRulers.h"
#include "Rendering/EditorCanvas.h"
#include <QMouseEvent>
#include <QPainter>
#include <array>
#include <cmath>

CanvasRulerCorner::CanvasRulerCorner(QWidget *parent) : QWidget(parent)
{
    setFixedSize(CanvasRuler::thickness, CanvasRuler::thickness);
}

void CanvasRulerCorner::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor::fromRgbF(0.2, 0.2, 0.2));
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor::fromRgbF(1, 1, 1, 0.28), 1));
    const double side = CanvasRuler::thickness;
    painter.drawLine(QPointF(5, side - 4), QPointF(side - 4, 5));
}

CanvasRulerView::CanvasRulerView(EditorSession &session, CanvasGuide::Axis axis, CanvasView &canvas, QWidget *parent)
    : QWidget(parent), m_session(session), m_axis(axis), m_canvas(canvas)
{
    const bool vertical = axis == CanvasGuide::Axis::vertical;
    setAccessibleName(vertical ? QStringLiteral("Vertical ruler") : QStringLiteral("Horizontal ruler"));
    setCursor(vertical ? Qt::SizeHorCursor : Qt::SizeVerCursor);
    if (vertical)
        setFixedWidth(CanvasRuler::thickness);
    else
        setFixedHeight(CanvasRuler::thickness);
    connect(&session, &EditorSession::changed, this, qOverload<>(&QWidget::update));
}

double CanvasRulerView::majorStep(double pointsPerPixel)
{
    const double target = 70 / std::max(pointsPerPixel, 0.0001);
    static constexpr std::array<double, 18> nice{1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1'000, 2'000, 2'500, 5'000, 10'000, 20'000, 25'000};
    for (const double step : nice) {
        if (step >= target)
            return step;
    }
    return 50'000;
}

QString CanvasRulerView::label(double value)
{
    return QString::number(qint64(std::round(value)));
}

void CanvasRulerView::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor::fromRgbF(0.2, 0.2, 0.2));
    const std::optional<CanvasDocument> &document = m_session.document();
    if (!document)
        return;
    const bool horizontal = m_axis == CanvasGuide::Axis::horizontal;
    const QSizeF size = document->size();
    const CanvasViewport &viewport = m_session.viewport;
    const double step = majorStep(viewport.pointsPerPixel());
    const double minor = step / 10;
    const double hairline = 1 / std::max(devicePixelRatioF(), 1.0);
    // The canvas's own points, wherever the ruler sits beside it.
    const QPointF offset = inCanvas(QPointF(0, 0));
    const QPointF from = viewport.documentPoint(offset, size), to = viewport.documentPoint(offset + QPointF(width(), height()), size);
    const double start = horizontal ? from.x() : from.y(), end = horizontal ? to.x() : to.y();
    const double first = std::floor(std::min(start, end) / minor) * minor, last = std::ceil(std::max(start, end) / minor) * minor;
    if (!(minor > 0) || !std::isfinite(first) || !std::isfinite(last))
        return;
    QFont font = painter.font();
    font.setPixelSize(8);
    painter.setFont(font);
    const QColor tick = QColor::fromRgbF(0.62, 0.62, 0.62), words = QColor::fromRgbF(0.78, 0.78, 0.78);
    for (double value = first; value <= last + 0.001; value += minor) {
        const QPointF at = viewport.viewPoint(QPointF(value, value), size) - offset;
        const double view = horizontal ? at.x() : at.y();
        const double remainder = std::abs(std::remainder(value, step));
        const bool major = remainder < 0.001 || std::abs(remainder - step) < 0.001;
        const bool middle = !major && std::abs(std::remainder(value, step / 2)) < 0.001;
        const double length = major ? 8 : middle ? 5 : 3;
        painter.fillRect(horizontal ? QRectF(view - hairline / 2, height() - length, hairline, length)
                                    : QRectF(width() - length, view - hairline / 2, length, hairline),
                         tick);
        if (!major)
            continue;
        const QString text = label(value);
        const QSizeF drawn = painter.fontMetrics().size(Qt::TextSingleLine, text);
        painter.setPen(words);
        if (horizontal) {
            painter.drawText(QRectF(QPointF(view + 2, 0), drawn), Qt::AlignLeft | Qt::AlignTop, text);
        } else {
            // Turned along the tick, as Swift draws it.
            painter.save();
            painter.translate(1, view + 2);
            painter.rotate(-90);
            painter.drawText(QRectF(QPointF(-drawn.width(), 0), drawn), Qt::AlignLeft | Qt::AlignTop, text);
            painter.restore();
        }
    }
    const QColor edge = QColor::fromRgbF(0.08, 0.08, 0.08);
    painter.fillRect(horizontal ? QRectF(0, height() - hairline, width(), hairline) : QRectF(width() - hairline, 0, hairline, height()), edge);
}

// The canvas is a sibling: points go by the screen.
QPointF CanvasRulerView::inCanvas(QPointF point) const
{
    return m_canvas.mapFromGlobal(mapToGlobal(point));
}

std::optional<double> CanvasRulerView::documentPosition(QPointF point) const
{
    return m_canvas.documentPosition(m_axis, inCanvas(point));
}

void CanvasRulerView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_canvas.setFocus(Qt::MouseFocusReason);
    const std::optional<double> position = documentPosition(event->position());
    if (!m_session.canEditGuides() || !position)
        return;
    m_session.beginGuideCreation(m_axis, *position);
}

void CanvasRulerView::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_session.guideDrag())
        return;
    if (const std::optional<double> position = documentPosition(event->position()))
        m_session.moveGuideDrag(*position);
}

void CanvasRulerView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_session.guideDrag())
        return;
    m_session.finishGuideDrag(m_canvas.isOverRuler(inCanvas(event->position())));
}
