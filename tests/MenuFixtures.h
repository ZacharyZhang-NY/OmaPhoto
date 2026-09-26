#pragma once
#include "UI/CompositorMenus.h"
#include "UI/ProjectWorkspaceView.h"
#include <QtTest>

// Shared by the menu tests: a window and its bar.
struct Bar {
    ProjectWorkspace workspace;
    ProjectWorkspaceView window{workspace};
    CompositorMenus *menus = window.findChild<CompositorMenus *>();
    EditorSession &session() { return workspace.current().session; }
    QAction &action(const char *name)
    {
        QAction *found = menus->action(QString::fromLatin1(name));
        if (!found)
            throw std::runtime_error(std::string("no entry named ") + name);
        return *found;
    }
};

inline ImportedImage white()
{
    QImage image(1, 1, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::white);
    return ImportedImage(image, QImage(), "White");
}
