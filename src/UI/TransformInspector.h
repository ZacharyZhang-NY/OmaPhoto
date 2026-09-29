#pragma once
#include "Document/EditorSession.h"
#include "UI/ToolHeaderStyle.h"
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <functional>

class NumericScrub;

// A labelled number, typed at once, stepped by arrows.
class TransformValueField : public QWidget {
    Q_OBJECT
public:
    // The label scrubs within low and high, in whole steps.
    TransformValueField(const QString &label, const QString &suffix, double low, double high, EditorSession &session,
                        std::function<void(double)> change, QWidget *parent = nullptr);

    // Shown when the field is not being edited.
    void sync(double value);
    // Swift's `.id` drops a scrub under way with its layer.
    void endScrub();
    // No idle zeros on a whole number, two decimals otherwise.
    static QString formatted(double value);

    QLineEdit *const field;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    EditorSession &m_session;
    const std::function<void(double)> m_change;
    NumericScrub *m_scrub = nullptr;
    double m_value = 0;
    // A menu or popup borrows the focus: the typing stays.
    bool m_borrowed = false;
};

// The Move tool's bar: numbers, settings, Apply and Cancel.
class TransformInspector : public ToolHeaderBar {
    Q_OBJECT
public:
    explicit TransformInspector(EditorSession &session, QWidget *parent = nullptr);

protected:
    // Return applies and Escape cancels outside the fields.
    void keyPressEvent(QKeyEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void applyLockIcon();
    LayerTransform value() const;
    QSizeF pixelSize() const;
    void change(const std::function<void(LayerTransform &)> &update);
    // Swift's `resize`; the name would hide QWidget's.
    void resizeBox(double number, bool width);
    void synchronize();

    EditorSession &m_session;
    // The layer the fields were built for, as Swift's `.id`.
    std::optional<QUuid> m_layerID;
    QCheckBox *const m_autoSelect;
    QCheckBox *const m_showControls;
    QScrollArea *const m_fields;
    TransformValueField *const m_x;
    TransformValueField *const m_y;
    TransformValueField *const m_width;
    TransformValueField *const m_height;
    QToolButton *const m_lock;
    TransformValueField *const m_scale;
    TransformValueField *const m_rotation;
    QComboBox *const m_sampling;
    QPushButton *const m_cancel;
    QPushButton *const m_apply;
};
