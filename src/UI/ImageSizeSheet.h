#pragma once
#include "Document/EditorSession+Model.h"
#include "IO/ImageResizer.h"
#include <QWidget>
#include <functional>

class PickerField;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

// Swift's ImageSizeSheet: pixels resampled, or print size alone.
class ImageSizeSheet : public QWidget {
    Q_OBJECT
public:
    ImageSizeSheet(const CanvasDocument &document, std::function<void(std::optional<ImageSizeOptions>)> finish, QWidget *parent = nullptr);

private:
    bool valid() const;
    double display(double pixels, qint64 original) const;
    PickerField *dimension(bool isWidth);
    void setDimension(double value, bool isWidth);
    void setResample(bool enabled);
    void synchronize();

    const qint64 m_originalWidth;
    const qint64 m_originalHeight;
    const std::function<void(std::optional<ImageSizeOptions>)> m_finish;
    double m_width;
    double m_height;
    double m_resolution;
    bool m_locked = true;
    bool m_resample = true;
    QString m_unit = QStringLiteral("Pixels");
    LayerSampling m_sampling = LayerSampling::high;
    QComboBox *const m_units;
    PickerField *const m_widthField;
    PickerField *const m_heightField;
    QCheckBox *const m_lock;
    PickerField *const m_resolutionField;
    QWidget *const m_samplingRow;
    QComboBox *const m_samplingChoice;
    QLabel *const m_explanation;
    QLabel *const m_result;
    QPushButton *const m_resize;
};
