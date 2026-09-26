#pragma once
#include "Document/ProjectWorkspace.h"
#include "IO/ProjectStore.h"
#include "Rendering/EditorCanvas.h"
#include "UI/NativeLayerList.h"
#include "SessionFixtures.h"
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMenu>
#include <QMimeData>
#include <QScrollBar>
#include <QtTest>

// Shared by the drag tests: a shown list, its drops.
struct Shown {
    EditorSession &session;
    NativeLayerList list;
    explicit Shown(EditorSession &session) : session(session), list(session)
    {
        list.resize(252, 400);
        list.show();
        if (!QTest::qWaitForWindowActive(&list))
            throw std::runtime_error("the list never became active");
    }
    QUuid id(int row) { return list.cells().at(size_t(row))->layerID(); }
    // A row's top edge, its middle, or below every row.
    QPoint top(int row) { return list.cells().at(size_t(row))->mapTo(&list, QPoint(100, 2)); }
    QPoint middle(int row) { return list.cells().at(size_t(row))->mapTo(&list, QPoint(100, LayerCell::rowHeight / 2)); }
    QPoint below() { return list.cells().back()->mapTo(&list, QPoint(100, LayerCell::rowHeight + 30)); }
    QMimeData *rows(std::initializer_list<int> which)
    {
        QStringList lines;
        for (const int row : which)
            lines << uuidString(id(row));
        auto *data = new QMimeData;
        data->setData(ProjectWorkspace::layerType, lines.join('\n').toUtf8());
        data->setData(NativeLayerList::sourceType, list.dragToken().toUtf8());
        return data;
    }
    // The drag events a drop brings, offering as the source.
    bool drop(QMimeData *data, Qt::DropActions offered, QPoint listPoint, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        const QPointF inViewport = list.viewport()->mapFromParent(listPoint);
        QDragEnterEvent enter(inViewport.toPoint(), offered, data, Qt::LeftButton, modifiers);
        QApplication::sendEvent(list.viewport(), &enter);
        QDropEvent drop(inViewport, offered, data, Qt::LeftButton, modifiers);
        QApplication::sendEvent(list.viewport(), &drop);
        delete data;
        lastAction = drop.dropAction();
        return drop.isAccepted();
    }
    Qt::DropAction lastAction = Qt::IgnoreAction;
    std::vector<QString> names()
    {
        std::vector<QString> result;
        for (const LayerHierarchy::Entry &entry : session.layerRows())
            result.push_back(entry.layer.name);
        return result;
    }
};

inline std::unique_ptr<EditorSession> sessionWithLayers(int count)
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(800, 600);
    for (int each = 0; each < count; ++each)
        session->addBlankLayer();
    return session;
}

inline QImage image(const QCursor &cursor)
{
    return cursor.pixmap().toImage();
}
