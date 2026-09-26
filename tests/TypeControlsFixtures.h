#pragma once
#include "SessionFixtures.h"
#include "UI/TypeControls.h"
#include <QComboBox>
#include <QPushButton>
#include <QToolButton>
#include <QtTest>

// Shared by the Type bar's tests: the bar, a document.
namespace {
template <typename Widget> Widget &find(QWidget &parent, const char *name)
{
    auto *found = parent.findChild<Widget *>(QString::fromLatin1(name));
    if (!found)
        throw std::runtime_error(name);
    return *found;
}

// A new text layer saying `content`, left active.
QUuid addText(EditorSession &session, const QString &content)
{
    session.selectTool(NavigationTool::type);
    session.beginText(QPointF(20, 30), true);
    TextDraft draft = session.textDraft().value();
    draft.style.content = content;
    if (!session.applyText(draft))
        throw std::runtime_error("the text was refused");
    return session.activeLayerID().value();
}

// The bar shown in an active window, over a document.
struct Bar {
    EditorSession session;
    TypeControls bar;
    Bar() : bar((session.createDocument(400, 300, true), session.selectTool(NavigationTool::type), session))
    {
        bar.show();
        if (!QTest::qWaitForWindowActive(&bar))
            throw std::runtime_error("the bar never became active");
    }
    TextStyleField &field(const char *name) { return find<TextStyleField>(bar, name); }
    QToolButton &align(const char *name) { return find<QToolButton>(bar, name); }
    QPushButton &button(const char *name) { return find<QPushButton>(bar, name); }
    // Replaces a field's text key by key, as typed.
    void type(TextStyleField &field, const QString &text)
    {
        field.setFocus();
        if (!QTest::qWaitFor([&] { return field.hasFocus(); }))
            throw std::runtime_error("the field never took the focus");
        field.selectAll();
        QTest::keyClicks(&field, text);
    }
    void restyle(const std::function<void(LayerTextStyle &)> &change) { session.changeTextStyle(change); }
};

}
