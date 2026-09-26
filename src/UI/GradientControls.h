#pragma once
#include "UI/ToolHeaderStyle.h"
#include <functional>

class EditorSession;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QPushButton;
class QSlider;
class QToolButton;
class SelectionAmountField;

// Swift's gradient swatch: the colours over a checkerboard.
class GradientSwatch : public QWidget {
public:
    GradientSwatch(const EditorSession &session, QWidget *parent);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    const EditorSession &m_session;
};

// Swift's GradientControls: shape, colours, reverse, opacity, apply.
class GradientControls : public ToolHeaderBar {
    Q_OBJECT
public:
    explicit GradientControls(EditorSession &session, QWidget *parent = nullptr);

private:
    QToolButton *shape(const QString &name, const QString &text, bool radial);
    void synchronize();

    EditorSession &m_session;
    QButtonGroup *const m_shapes;
    QToolButton *const m_linear;
    QToolButton *const m_radial;
    GradientSwatch *const m_swatch;
    QComboBox *const m_style;
    QCheckBox *const m_reverse;
    // The slider steps in thousandths, the field in whole percents.
    QSlider *const m_opacitySlider;
    SelectionAmountField *const m_opacity;
    QLabel *const m_mask;
    QPushButton *const m_cancel;
    QPushButton *const m_apply;
};
