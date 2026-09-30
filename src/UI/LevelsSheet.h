#pragma once
#include "Document/EditorSession.h"
#include "UI/ColorPickerSheet.h"
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QWidget>
#include <array>
#include <functional>

// Swift's LevelsSheet: channel, histogram, handles, entries, samples, auto.
class LevelsSheet : public QWidget {
    Q_OBJECT
public:
    explicit LevelsSheet(EditorSession &session, QWidget *parent = nullptr);
    ~LevelsSheet() override;

protected:
    // A new theme recolours the eyedroppers' ink.
    void changeEvent(QEvent *event) override;

private:
    // Swift's update: the settings changed and handed to the session.
    void change(const std::function<void(LevelsSettings &)> &edit);
    // A handle's tone, which replaces its entry's typing.
    void dragTo(bool output, size_t index, double tone);
    // A labelled entry of the five, in Swift's order.
    PickerField *field(size_t index);
    void synchronize();

    EditorSession &m_session;
    QComboBox *const m_channel;
    QWidget *const m_histogram;
    QWidget *const m_input;
    // Made before the output handles, which lie on top.
    QWidget *const m_gradient;
    QWidget *const m_output;
    // Input black, gamma, input white, output black, output white.
    const std::array<PickerField *, 5> m_fields;
    const std::array<QPushButton *, 3> m_samples;
    QLabel *const m_sampleHint;
    const std::array<QPushButton *, 3> m_autos;
    QCheckBox *const m_preview;
    QPushButton *const m_reset;
    QLabel *const m_caption;
    QPushButton *const m_cancel;
    QProgressBar *const m_spinner;
    QPushButton *const m_ok;
};
