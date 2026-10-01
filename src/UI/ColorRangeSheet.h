#pragma once
#include "Document/EditorSession.h"
#include <QWidget>
#include <array>

class QCheckBox;
class QLabel;
class QPushButton;
class QSlider;
class PickerField;
class SampleButton;

// Swift's ColorRangeSheet: eyedroppers, preview, Fuzziness, Invert.
class ColorRangeSheet : public QWidget {
    Q_OBJECT
public:
    explicit ColorRangeSheet(EditorSession &session, QWidget *parent = nullptr);
    ~ColorRangeSheet() override;

private:
    void synchronize();

    EditorSession &m_session;
    std::array<SampleButton *, 3> m_eyedroppers{};
    QWidget *m_preview;
    QLabel *m_caption;
    QSlider *m_slider;
    PickerField *m_field;
    QCheckBox *m_invert;
    QLabel *m_error;
    QPushButton *m_ok;
};
