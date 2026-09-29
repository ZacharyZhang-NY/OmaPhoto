#pragma once
#include "Document/EditorSession.h"
#include "UI/ToolHeaderStyle.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>
#include <functional>

// A bounded whole number, typed or stepped, clamped.
class SelectionAmountField : public QLineEdit {
    Q_OBJECT
public:
    // Swift's arrowSteps: arrows step from `value`, clamped, into `change`.
    SelectionAmountField(EditorSession &session, int low, int high, std::function<double()> value, std::function<void(double)> change,
                         QWidget *parent = nullptr);
    void sync(int amount);
    // The session's number, over any typing: a step or scrub.
    void showValue();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    EditorSession &m_session;
    const int m_low;
    const int m_high;
    const std::function<double()> m_value;
    const std::function<void(double)> m_change;
    int m_amount;
    // A menu or popup borrows the focus: the typing stays.
    bool m_borrowed = false;
};

// The selection tools' bar: kinds, mode, edges.
class LassoControls : public ToolHeaderBar {
    Q_OBJECT
public:
    explicit LassoControls(EditorSession &session, QWidget *parent = nullptr);

private:
    QToolButton *choice(QButtonGroup *group, const QString &name, const QString &text, const QString &tip, const std::function<void()> &pick);
    void changeWand(const std::function<void(WandSettings &)> &change);
    void changeObject(const std::function<void(ObjectSelectionSettings &)> &change);
    void synchronize();

    EditorSession &m_session;
    QButtonGroup *const m_marqueeKinds;
    QButtonGroup *const m_lassoKinds;
    QButtonGroup *const m_modes;
    QToolButton *const m_rectangle;
    QToolButton *const m_ellipse;
    QToolButton *const m_freehand;
    QToolButton *const m_polygonal;
    QToolButton *const m_replace;
    QToolButton *const m_add;
    QToolButton *const m_subtract;
    // The wand's own: tolerance, sample size, source, contiguous.
    QLabel *const m_toleranceLabel;
    SelectionAmountField *const m_tolerance;
    QComboBox *const m_sampleSize;
    QButtonGroup *const m_sources;
    QToolButton *const m_thisLayer;
    QToolButton *const m_allLayers;
    QCheckBox *const m_contiguous;
    // The Magic tool's modes, and Object mode's own settings.
    QButtonGroup *const m_wandModes;
    QToolButton *const m_wandMode;
    QToolButton *const m_objectMode;
    QButtonGroup *const m_objectSources;
    QToolButton *const m_objectThisLayer;
    QToolButton *const m_objectAllLayers;
    QLabel *const m_edgeLabel;
    SelectionAmountField *const m_edge;
    QLabel *const m_edgeUnit;
    QCheckBox *const m_antialias;
    QPushButton *const m_expand;
    SelectionAmountField *const m_expandAmount;
    // The units scrub their amounts, Swift's `unitSuffix`.
    QLabel *const m_expandUnit;
    QPushButton *const m_contract;
    SelectionAmountField *const m_contractAmount;
    QLabel *const m_contractUnit;
    QPushButton *const m_feather;
    SelectionAmountField *const m_featherAmount;
    QLabel *const m_featherUnit;
    QLabel *const m_empty;
    QPushButton *const m_deselect;
};

// Select's Expand, Contract or Feather amount, in a floating panel.
class SelectionAmountSheet : public QWidget {
    Q_OBJECT
public:
    SelectionAmountSheet(EditorSession &session, SelectionAmountOperation operation, QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;

private:
    // A whole number in range, else none; toInt trims spaces.
    std::optional<int> amount() const;
    void refresh();

    EditorSession &m_session;
    const int m_maximum;
    QSlider *const m_slider;
    QLineEdit *const m_input;
    QLabel *const m_note;
    QPushButton *const m_ok;
};
