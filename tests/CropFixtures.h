#pragma once
#include "Document/EditorSession.h"
#include <QtTest>
#include <stdexcept>

// Commits the frame and waits: the canvas the crop leaves.
inline QRectF committed(EditorSession &session)
{
    bool done = false;
    session.commitCrop([&] { done = true; });
    if (!QTest::qWaitFor([&] { return done; }))
        throw std::runtime_error("the crop never finished");
    return QRectF(QPointF(0, 0), session.document().value().size());
}
