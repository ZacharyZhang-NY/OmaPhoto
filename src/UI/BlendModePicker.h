#pragma once
#include "Document/EditorSession.h"
#include <QComboBox>

// The blend menu: hovering tries a mode, choosing keeps it.
class BlendModePicker : public QComboBox {
    Q_OBJECT
public:
    explicit BlendModePicker(EditorSession &session, QWidget *parent = nullptr);

    void showPopup() override;
    void hidePopup() override;

private:
    void synchronize();
    void highlight(int index);
    void choose(int index);

    EditorSession &m_session;
    bool m_tracking = false;
    std::optional<QUuid> m_layerID;
    std::optional<LayerBlendMode> m_highlighted;
};
