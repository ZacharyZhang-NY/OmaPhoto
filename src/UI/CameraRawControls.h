#pragma once
#include "Document/EditorSession.h"
#include "UI/CameraRawSlider.h"
#include <QWidget>

class PickerField;
class QComboBox;
class QLabel;
class QToolButton;
class QVBoxLayout;

// Swift's CameraRawControls: the scope, then the ten groups.
class CameraRawControls : public QWidget {
    Q_OBJECT
public:
    explicit CameraRawControls(EditorSession &session, QWidget *parent = nullptr);
    // Shows the edit as it now stands.
    void synchronize();
    static constexpr int labelWidth = 96;

protected:
    // Swift's option monitor: Alt let go ends the views.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class Section { light, color, effects, curve, colorMixer, colorGrading, detail, optics, geometry, calibration };
    struct Group {
        Section section;
        QToolButton *disclosure;
        QToolButton *eye;
        QWidget *body;
    };
    struct Row {
        std::function<double &(CameraRawSettings &)> key;
        int decimals;
        CameraRawSlider *slider;
        PickerField *field;
    };
    QVBoxLayout *group(Section section, QVBoxLayout *column);
    // Swift's slider: a title, the slider and an exact field.
    void slider(QVBoxLayout *column, const QString &name, const QString &title, std::function<double &(CameraRawSettings &)> key, double low, double high, int decimals,
                std::optional<CameraRawClipping> clipping, const QString &help, CameraRawSliderTrack track = {}, double reset = 0);
    QLabel *subheadline(const QString &title, QVBoxLayout *column);
    void light(QVBoxLayout *column);
    void color(QVBoxLayout *column);
    void effects(QVBoxLayout *column);
    void assign(const std::function<double &(CameraRawSettings &)> &key, double value, std::optional<CameraRawClipping> clipping);
    void update(const std::function<void(CameraRawSettings &)> &change);
    void panel(const std::function<void(CameraRawPanel &)> &change);
    void toggleShown(Section section);
    CameraRawSettings raw() const;

    EditorSession &m_session;
    QWidget *const m_scope;
    QLabel *const m_readout;
    std::vector<Group> m_groups;
    std::vector<Row> m_rows;
    QComboBox *m_whiteBalance = nullptr;
    QToolButton *m_whiteBalanceSampler = nullptr;
    QLabel *m_whiteBalanceHint = nullptr;
    QComboBox *m_glowStyle = nullptr;
    QComboBox *m_vignetteStyle = nullptr;
};
