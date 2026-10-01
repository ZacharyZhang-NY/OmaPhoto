#pragma once
#include "Document/EditorSession.h"
#include <QWidget>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

class DialogColorSwatch;
class PickerField;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;

// Swift's GridSettingsSheet: each change previews; Cancel puts back.
class GridSettingsSheet : public QWidget {
    Q_OBJECT
public:
    using Settings = std::pair<LayoutGrid, GridAppearance>;
    GridSettingsSheet(EditorSession &session, LayoutGrid grid, GridAppearance appearance, std::function<void(const Settings &)> preview,
                      std::function<void(std::optional<Settings>)> finish, QWidget *parent = nullptr);
    ~GridSettingsSheet() override;

private:
    bool valid() const;
    LayoutGrid grid() const { return LayoutGrid(m_spacing, m_subdivisions); }
    // Swift's onChange: the grid follows, a preset ends a pick.
    void setAppearance(const GridAppearance &appearance);
    void setSpacing(int spacing, int subdivisions);
    void setOpacity(int opacity);
    // The swatch's binding: a picked colour becomes Custom's.
    void pick(const PaletteColor &picked);
    PickerField *field(const QString &name, std::function<void(int)> apply, std::function<void(int)> step);
    void synchronize();

    EditorSession &m_session;
    const std::function<void(const Settings &)> m_preview;
    const std::function<void(std::optional<Settings>)> m_finish;
    int m_spacing;
    int m_subdivisions;
    GridAppearance m_appearance;
    // The preset a pick started from; picking its colour returns.
    std::optional<GridAppearance::Preset> m_pickedFrom;
    QComboBox *const m_preset;
    DialogColorSwatch *const m_swatch;
    QComboBox *const m_style;
    QSlider *const m_opacitySlider;
    PickerField *const m_opacity;
    PickerField *const m_spacingField;
    PickerField *const m_subdivisionField;
    QLabel *const m_note;
    QPushButton *const m_ok;
};
