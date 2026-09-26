#pragma once
#include "Document/Curves.h"
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QWidget>
#include <functional>
#include <optional>

// Swift's CurvesControls: a channel's curve, its points and buttons.
class CurvesControls : public QWidget {
    Q_OBJECT
public:
    // Swift's binding: the settings read, and written back.
    CurvesControls(std::function<CurvesSettings()> value, std::function<void(const CurvesSettings &)> change, QWidget *parent = nullptr);
    // Shows the settings as they now stand.
    void synchronize();

private:
    friend class CurveGraph;
    std::vector<CurvePoint> points() const;
    void setPoints(const std::vector<CurvePoint> &points);
    // Swift's drag: take a near point or add one.
    void drag(QPointF at, QSizeF size);

    const std::function<CurvesSettings()> m_value;
    const std::function<void(const CurvesSettings &)> m_change;
    std::optional<size_t> m_selected;
    std::optional<size_t> m_dragging;
    std::optional<LevelsChannel> m_shownChannel;
    QComboBox *const m_channel;
    QWidget *const m_graph;
    QLabel *const m_readout;
    QPushButton *const m_remove;
    QPushButton *const m_reset;
};
