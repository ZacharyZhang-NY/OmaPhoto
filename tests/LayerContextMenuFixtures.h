#pragma once
#include "UI/NativeLayerList.h"
#include <QMenu>
#include <QtTest>
#include <memory>
#include <stdexcept>

// What the context menu tests share.
namespace {
// Two red pixel layers, the top one with a stroke.
inline std::unique_ptr<EditorSession> paintedPair()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(8, 8);
    for (int layer = 0; layer < 2; ++layer) {
        QImage pixels(8, 8, QImage::Format_RGBA8888_Premultiplied);
        pixels.fill(Qt::red);
        session->insert(ImportedImage(pixels, pixels, QStringLiteral("Red")));
    }
    session->setEffects(LayerEffects{.stroke = StrokeEffect()});
    return session;
}

// Swift's sessionWithThreeLayers: three blank layers, 800 by 600.
inline std::unique_ptr<EditorSession> threeLayers()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(800, 600);
    for (int layer = 0; layer < 3; ++layer)
        session->addBlankLayer();
    return session;
}

struct Shown {
    NativeLayerList list;
    explicit Shown(EditorSession &session) : list(session)
    {
        list.resize(252, 400);
        list.show();
        if (!QTest::qWaitForWindowActive(&list))
            throw std::runtime_error("the list never became active");
    }
    LayerCell &row(int index) { return *list.cells().at(size_t(index)); }
    // A press on the row's name, away from every control.
    std::unique_ptr<QMenu> menu(int index) { return list.menuFor(row(index), QPoint(row(index).width() - 20, 20)); }
};

inline QAction &item(QMenu &menu, const char *name)
{
    auto *found = menu.findChild<QAction *>(QString::fromLatin1(name));
    if (!found)
        throw std::runtime_error(name);
    return *found;
}

inline QStringList titles(const QMenu &menu)
{
    QStringList result;
    for (const QAction *action : menu.actions())
        result << (action->isSeparator() ? QStringLiteral("-") : action->text());
    return result;
}
}
