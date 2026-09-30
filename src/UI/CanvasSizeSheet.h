#pragma once
#include "Document/CanvasSize.h"
#include "Document/ColorPalette.h"
#include "Document/EditorSession+Model.h"
#include <QWidget>
#include <array>
#include <functional>

class NumericScrub;
class PickerField;
class QButtonGroup;
class QComboBox;
class QLabel;
class QPushButton;
class SwatchButton;

// Swift's CanvasSizeSheet: the new size, the point kept, the extension.
class CanvasSizeSheet : public QWidget {
    Q_OBJECT
public:
    CanvasSizeSheet(const CanvasDocument &document, PaletteColor foreground, PaletteColor background,
                    std::function<void(std::optional<CanvasSizeOptions>)> finish, QWidget *parent = nullptr);
    ~CanvasSizeSheet() override;

private:
    PickerField *dimension(bool widthAxis);
    // Swift's scrubbable Width or Height title.
    NumericScrub *scrub(QLabel *title, bool widthAxis);
    std::optional<CanvasExtensionColor> fill() const;
    void pickCustomColor();
    void synchronize();

    const PaletteColor m_foreground;
    const PaletteColor m_background;
    const std::function<void(std::optional<CanvasSizeOptions>)> m_finish;
    CanvasSizeDraft m_draft;
    int m_anchor = 4;
    PaletteColor m_custom = PaletteColor::white();
    PickerField *const m_width;
    PickerField *const m_height;
    // Their limits follow the unit, Relative and Lock.
    std::array<NumericScrub *, 2> m_scrubs{};
    QLabel *const m_note;
    QButtonGroup *const m_anchors;
    QLabel *const m_anchorName;
    QComboBox *const m_extension;
    QWidget *const m_customRow;
    SwatchButton *const m_customSwatch;
    QPushButton *const m_ok;
};
