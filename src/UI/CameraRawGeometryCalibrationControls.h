#pragma once
#include "Document/EditorSession.h"
#include "UI/CameraRawRow.h"
#include <QWidget>

class QAbstractButton;
class QButtonGroup;
class QComboBox;
class QLabel;
class QPushButton;

// Swift's CameraRawGeometryControls: Upright, guides, the transform.
class CameraRawGeometryControls : public QWidget {
    Q_OBJECT
public:
    explicit CameraRawGeometryControls(EditorSession &session, QWidget *parent = nullptr);
    void synchronize();

private:
    void update(const std::function<void(CameraRawGeometrySettings &)> &change);

    EditorSession &m_session;
    QButtonGroup *m_upright = nullptr;
    QWidget *const m_guided;
    QPushButton *const m_draw;
    QLabel *const m_drawHint;
    QPushButton *const m_clear;
    QComboBox *m_projection = nullptr;
    QAbstractButton *const m_constrain;
    std::vector<CameraRawRow *> m_rows;
};

// Swift's CameraRawCalibrationControls: the process and the primaries.
class CameraRawCalibrationControls : public QWidget {
    Q_OBJECT
public:
    explicit CameraRawCalibrationControls(EditorSession &session, QWidget *parent = nullptr);
    void synchronize();

private:
    EditorSession &m_session;
    QComboBox *const m_process;
    QLabel *const m_summary;
    std::vector<CameraRawRow *> m_rows;
};
