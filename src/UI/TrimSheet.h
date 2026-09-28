#pragma once
#include "Document/ImageTrim.h"
#include <QWidget>
#include <array>
#include <functional>

class QButtonGroup;
class QCheckBox;
class QPushButton;

// Swift's TrimSheet: the basis and the edges trimmed away.
class TrimSheet : public QWidget {
    Q_OBJECT
public:
    explicit TrimSheet(std::function<void(std::optional<TrimOptions>)> finish, QWidget *parent = nullptr);

private:
    const std::function<void(std::optional<TrimOptions>)> m_finish;
    QButtonGroup *const m_basedOn;
    // Top, bottom, left, right.
    std::array<QCheckBox *, 4> m_edges{};
    QPushButton *const m_ok;
};
