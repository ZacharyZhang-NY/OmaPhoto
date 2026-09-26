#pragma once
#include "Document/ShapeTool.h"
#include "UI/ToolHeaderStyle.h"
#include <array>

class EditorSession;
class QButtonGroup;
class QSlider;
class QToolButton;
class SelectionAmountField;
class SwatchButton;

// Swift's ShapeControls: the kind, a line's width, a rectangle's radius.
class ShapeControls : public ToolHeaderBar {
    Q_OBJECT
public:
    explicit ShapeControls(EditorSession &session, QWidget *parent = nullptr);

private:
    QToolButton *kind(ShapeKind value);
    // A label, slider, field and "px", as Swift's HStack.
    QWidget *amount(const QString &name, QSlider *slider, SelectionAmountField *field);
    void synchronize();

    EditorSession &m_session;
    QButtonGroup *const m_kinds;
    const std::array<QToolButton *, 3> m_kindButtons;
    QSlider *const m_widthSlider;
    SelectionAmountField *const m_widthField;
    QWidget *const m_width;
    QSlider *const m_radiusSlider;
    SelectionAmountField *const m_radiusField;
    QWidget *const m_radius;
    // Shapes fill with the foreground colour.
    SwatchButton *const m_fill;
};
