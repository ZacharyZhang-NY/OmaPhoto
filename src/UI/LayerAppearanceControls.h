#pragma once
#include "Document/EditorSession.h"
#include <QLineEdit>
#include <QSlider>
#include <QWidget>

class BlendModePicker;

// The active layer's blend mode and opacity.
class LayerAppearanceControls : public QWidget {
    Q_OBJECT
public:
    explicit LayerAppearanceControls(EditorSession &session, QWidget *parent = nullptr);
    ~LayerAppearanceControls() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void synchronize();
    void step(double percent);
    void releaseFocus();
    void sync();
    void applyPercentage();

    EditorSession &m_session;
    BlendModePicker *const m_picker;
    QSlider *const m_slider;
    QLineEdit *const m_percentage;
    // The layer the controls were last made for.
    std::optional<QUuid> m_layerID;
    bool m_syncing = false;
};
