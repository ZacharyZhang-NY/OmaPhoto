#pragma once
#include "Document/HueSaturation.h"
#include <QToolButton>
#include <functional>

// Swift's plain sampling buttons: a glyph, tinted when chosen.
class SampleButton : public QToolButton {
    Q_OBJECT
public:
    SampleButton(std::function<void(QPainter &, const QColor &)> glyph, QWidget *parent);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    const std::function<void(QPainter &, const QColor &)> m_glyph;
};

// Swift's eyedropper, badged plus or minus for Add and Remove.
void sampleEyedropper(QPainter &painter, HueSampleMode mode, const QColor &ink);
