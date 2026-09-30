#pragma once
#include "Document/EditorSession.h"
#include "UI/CameraRawRow.h"
#include <QWidget>

class QAbstractButton;
class QLabel;
class QToolButton;
class QVBoxLayout;

// Swift's CameraRawDetailControls: sharpening and noise reduction.
class CameraRawDetailControls : public QWidget {
    Q_OBJECT
public:
    explicit CameraRawDetailControls(EditorSession &session, QWidget *parent = nullptr);
    ~CameraRawDetailControls() override;
    void synchronize();

private:
    // Swift's sharpenSlider; Masking shows its mask while Alt is held.
    void row(QVBoxLayout *column, const QString &name, const QString &title, double CameraRawDetailSettings::*key, double high, double reset,
                      bool maskingPreview, const QString &help);
    void assign(double CameraRawDetailSettings::*key, double value, bool maskingPreview);

    EditorSession &m_session;
    std::vector<CameraRawRow *> m_rows;
    QWidget *m_luminanceDetail = nullptr;
    QWidget *m_colorDetail = nullptr;
};

// Swift's CameraRawOpticsControls: profile, distortion, defringe, vignetting.
class CameraRawOpticsControls : public QWidget {
    Q_OBJECT
public:
    explicit CameraRawOpticsControls(EditorSession &session, QWidget *parent = nullptr);
    ~CameraRawOpticsControls() override;
    void synchronize();

private:
    void row(QVBoxLayout *column, const QString &name, const QString &title, double CameraRawOpticsSettings::*key, double low, double high, double reset,
             const QString &help);
    QWidget *hueRange(const QString &name, const QString &title, double CameraRawOpticsSettings::*low, double CameraRawOpticsSettings::*high,
                      double lowReset, double highReset, const QString &help);
    void update(const std::function<void(CameraRawOpticsSettings &)> &change);

    EditorSession &m_session;
    std::vector<CameraRawRow *> m_rows;
    std::vector<std::pair<CameraRawSlider *, double CameraRawOpticsSettings::*>> m_hueSliders;
    QAbstractButton *m_chromatic = nullptr;
    QAbstractButton *m_profile = nullptr;
    QWidget *m_profileRows = nullptr;
    QToolButton *m_defringe = nullptr;
    QLabel *m_defringeHint = nullptr;
};
