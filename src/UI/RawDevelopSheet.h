#pragma once
#include "IO/RawImporter.h"
#include <QWidget>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QLabel;
class QProgressBar;
class QPushButton;
class QSlider;
class QTimer;

// Swift's RawDevelopSheet: a screen-sized preview under four sliders.
class RawDevelopSheet : public QWidget {
    Q_OBJECT
public:
    RawDevelopSheet(RawDevelopRequest request, std::function<void(std::optional<RawDevelopSettings>)> finish, QWidget *parent = nullptr);
    ~RawDevelopSheet() override;

private:
    class Preview;
    struct Row {
        QSlider *slider;
        QLabel *value;
        float RawDevelopSettings::*field;
        double scale;
        int precision;
        QString unit;
    };
    void addRow(const QString &title, float RawDevelopSettings::*field, double low, double high, double scale, int precision, const QString &unit);
    void changed();
    void develop();
    void synchronize();

    const QString m_path;
    const std::function<void(std::optional<RawDevelopSettings>)> m_finish;
    RawDevelopSettings m_settings;
    // The newest develop's flag, Swift's task cancellation.
    std::shared_ptr<std::atomic_bool> m_cancelled;
    QTimer *const m_wait;
    Preview *const m_preview;
    QProgressBar *const m_spinner;
    std::vector<Row> m_rows;
    QPushButton *const m_reset;
};
