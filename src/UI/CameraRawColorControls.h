#pragma once
#include "Document/EditorSession.h"
#include "UI/CameraRawRow.h"
#include <QWidget>

class QAbstractButton;
class QButtonGroup;
class QComboBox;
class QLabel;
class QPushButton;
class QToolButton;
class QVBoxLayout;

// Swift's segmented pickers: touching checkable buttons, one chosen.
QButtonGroup *cameraRawSegments(const QStringList &titles, const QString &name, const QString &help, QVBoxLayout *column, std::function<void(int)> choose);

// Swift's CameraRawCurveControls: parametric regions or point curves.
class CameraRawCurveControls : public QWidget {
    Q_OBJECT
public:
    explicit CameraRawCurveControls(EditorSession &session, QWidget *parent = nullptr);
    ~CameraRawCurveControls() override;
    void synchronize();

private:
    CameraRawSettings raw() const;
    std::vector<CurvePoint> currentPoints() const;
    void update(const std::function<void(CameraRawSettings &)> &change);
    void store(const std::vector<CurvePoint> &points);
    void panel(const std::function<void(CameraRawPanel &)> &change);
    // Swift's Drag: a point, divider, or region.
    struct Drag {
        enum class Kind { point, divider, region } kind;
        size_t index = 0;
        double CameraRawCurveSettings::*key = nullptr;
        double start = 0;
        QPointF from;
    };
    std::optional<Drag> parametricDrag(QPointF at, QSizeF size) const;
    std::optional<Drag> pointDrag(QPointF at, QSizeF size);
    void beginDrag(QPointF at, QSizeF size);
    void continueDrag(QPointF at, QSizeF size);
    void removePoint(QPointF at, QSizeF size);
    friend class CameraRawCurveGraph;

    EditorSession &m_session;
    std::optional<Drag> m_drag;
    std::optional<size_t> m_selectedPoint;
    CameraRawPointChannel m_shownChannel = CameraRawPointChannel::rgb;
    QButtonGroup *m_page;
    QWidget *m_channelBox;
    QButtonGroup *m_channel;
    QWidget *const m_graph;
    QWidget *const m_amounts;
    QWidget *const m_points;
    QLabel *const m_selected;
    QComboBox *const m_preset;
    CameraRawRow *m_refine = nullptr;
    std::vector<CameraRawRow *> m_rows;
    QPushButton *const m_target;
};

// Swift's CameraRawMixerControls: HSL, one family, or picked colours.
class CameraRawMixerControls : public QWidget {
    Q_OBJECT
public:
    explicit CameraRawMixerControls(EditorSession &session, QWidget *parent = nullptr);
    ~CameraRawMixerControls() override;
    void synchronize();

private:
    CameraRawSettings raw() const;
    void update(const std::function<void(CameraRawSettings &)> &change);
    void updatePoint(const std::function<void(CameraRawPointColor &)> &change);
    void panel(const std::function<void(CameraRawPanel &)> &change);
    // Rows following a tab, a swatch or a point.
    void rebuildFamilies(int tab);
    void rebuildColor(int swatch);
    void rebuildPoint(int index, double hue);
    void row(QWidget *box, CameraRawRow::Spec spec, std::function<double(const CameraRawSettings &)> value,
             std::function<void(CameraRawSettings &, double)> write, double reset);

    EditorSession &m_session;
    QButtonGroup *m_page;
    QButtonGroup *m_tab;
    QWidget *const m_hsl;
    QWidget *const m_families;
    QWidget *const m_color;
    QWidget *const m_colorRows;
    std::vector<QAbstractButton *> m_swatches;
    QWidget *const m_point;
    QToolButton *const m_sampler;
    QWidget *const m_picked;
    QWidget *const m_pointControls;
    QWidget *const m_pointRows;
    QAbstractButton *const m_visualize;
    QPushButton *const m_target;
    // What the rebuilt rows were made for.
    std::optional<int> m_shownTab;
    std::optional<int> m_shownSwatch;
    std::optional<std::pair<int, double>> m_shownPoint;
    size_t m_shownPicked = 0;
};

// Swift's CameraRawGradingControls: the wheels, Blending, Balance.
class CameraRawGradingControls : public QWidget {
    Q_OBJECT
public:
    explicit CameraRawGradingControls(EditorSession &session, QWidget *parent = nullptr);
    void synchronize();

private:
    CameraRawSettings raw() const;
    void update(const std::function<void(CameraRawSettings &)> &change);
    QWidget *wheel(const QString &title, int key);

    EditorSession &m_session;
    QComboBox *const m_page;
    QWidget *const m_threeWay;
    QWidget *const m_single;
    std::vector<std::pair<int, QWidget *>> m_wheels;
    std::vector<CameraRawRow *> m_rows;
};
