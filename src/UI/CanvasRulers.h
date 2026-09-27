#pragma once
#include "Document/EditorSession.h"
#include <QWidget>

class CanvasView;

namespace CanvasRuler {
inline constexpr int thickness = 18;
}

// The square where the two rulers meet.
class CanvasRulerCorner : public QWidget {
    Q_OBJECT
public:
    explicit CanvasRulerCorner(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
};

// Swift's CanvasRulerNSView: ticks in document pixels; drag out a guide.
class CanvasRulerView : public QWidget {
    Q_OBJECT
public:
    CanvasRulerView(EditorSession &session, CanvasGuide::Axis axis, CanvasView &canvas, QWidget *parent = nullptr);
    // Numbered ticks about 70 points apart, 1-2-5 steps.
    static double majorStep(double pointsPerPixel);
    static QString label(double value);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QPointF inCanvas(QPointF point) const;
    std::optional<double> documentPosition(QPointF point) const;

    EditorSession &m_session;
    const CanvasGuide::Axis m_axis;
    CanvasView &m_canvas;
};
