#pragma once
#include "SessionFixtures.h"
#include <QtTest>
#include <memory>

// Shared by the selection tests: a session, outlines, coverage.
inline std::unique_ptr<EditorSession> selectionSession(int width = 100, int height = 100)
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(width, height, true);
    session->selectTool(NavigationTool::lasso);
    return session;
}

inline void lasso(EditorSession &session, const std::vector<QPointF> &points, SelectionMode mode = SelectionMode::replace)
{
    session.beginLasso(points[0], mode);
    for (std::size_t index = 1; index < points.size(); ++index)
        session.extendLasso(points[index]);
    session.finishLasso();
}

inline std::vector<QPointF> square(double x, double y, double size)
{
    return {QPointF(x, y), QPointF(x + size, y), QPointF(x + size, y + size), QPointF(x, y + size)};
}

// Coverage 0–255 at a document pixel.
inline int coverage(const EditorSession &session, int x, int y)
{
    const CanvasDocument &document = session.document().value();
    const QImage image = session.selection().value().coverage(document.width, document.height);
    return image.constScanLine(y)[x];
}

inline QRectF bounds(const EditorSession &session)
{
    return session.selection().value().path.boundingRect();
}

// A 100×40 red image, the edit tests' whole layer.
inline ImportedImage filledRed()
{
    QImage image(100, 40, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::red);
    return ImportedImage(image, image, "Red");
}

inline QPainterPath rectPath(const QRectF &rect)
{
    QPainterPath path;
    path.addRect(rect);
    return path;
}
