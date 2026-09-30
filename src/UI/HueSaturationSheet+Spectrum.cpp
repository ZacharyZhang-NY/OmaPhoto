#include "UI/HueSaturationSheet.h"
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

// Swift's SpectrumEditor: the bars, their handles and the readout.
namespace {
// Swift's 72 hue slices, as they are or adjusted.
class Spectrum : public QWidget {
public:
    Spectrum(std::function<HueSaturationSettings()> value, bool after, QWidget *parent) : QWidget(parent), m_value(std::move(value)), m_after(after)
    {
        setFixedHeight(16);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        constexpr int slices = 72;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath rounded;
        rounded.addRoundedRect(QRectF(rect()), 3, 3);
        painter.setClipPath(rounded);
        const HueSaturationSettings settings = m_value();
        const double width = double(this->width()) / slices;
        for (int slice = 0; slice < slices; ++slice) {
            const double hue = double(slice) / slices * 360;
            const double shown = m_after ? HueSaturationFilter::shiftedHue(hue, settings) : hue;
            painter.fillRect(QRectF(slice * width, 0, width + 0.5, height()), QColor::fromHsvF(float(shown / 360), 1, 1));
        }
    }

private:
    const std::function<HueSaturationSettings()> m_value;
    const bool m_after;
};

// Swift's handles: shoulders as blocks, the core's ends as bars.
class SpectrumHandles : public QWidget {
public:
    SpectrumHandles(std::function<HueSaturationSettings()> value, std::function<void(double, double, bool)> drag, QWidget *parent)
        : QWidget(parent), m_value(std::move(value)), m_drag(std::move(drag))
    {
        setFixedHeight(12);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const std::array<double, 4> handles = m_value().band().handles();
        for (size_t index = 0; index < handles.size(); ++index) {
            const double x = handles[index] / 360 * width();
            const bool inner = index == 1 || index == 2;
            painter.fillRect(inner ? QRectF(x - 1, 0, 2, height()) : QRectF(x - 3.5, height() / 2.0 - 2.5, 7, 5), palette().color(QPalette::WindowText));
        }
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            m_drag(event->position().x(), width(), true);
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (event->buttons().testFlag(Qt::LeftButton))
            m_drag(event->position().x(), width(), false);
    }

private:
    const std::function<HueSaturationSettings()> m_value;
    const std::function<void(double, double, bool)> m_drag;
};

// The nearest handle round the circle; ties take the first.
int nearestHandle(const HueBand &band, double degrees)
{
    const std::array<double, 4> handles = band.handles();
    std::array<double, 4> distances{};
    for (size_t index = 0; index < handles.size(); ++index) {
        const double gap = std::fmod(std::abs(handles[index] - degrees), 360);
        distances[index] = std::min(gap, 360 - gap);
    }
    return int(std::min_element(distances.begin(), distances.end()) - distances.begin());
}
}

SpectrumEditor::SpectrumEditor(std::function<HueSaturationSettings()> value, std::function<void(const HueSaturationSettings &)> change, QWidget *parent)
    : QWidget(parent), m_value(std::move(value)), m_change(std::move(change)),
      m_handles(new SpectrumHandles(m_value, [this](double x, double width, bool pressed) { drag(x, width, pressed); }, this)),
      m_after(new Spectrum(m_value, true, this)), m_readout(new QLabel(this))
{
    auto *before = new Spectrum(m_value, false, this);
    before->setObjectName(QStringLiteral("spectrumBefore"));
    m_handles->setObjectName(QStringLiteral("spectrumHandles"));
    m_after->setObjectName(QStringLiteral("spectrumAfter"));
    m_readout->setObjectName(QStringLiteral("spectrumReadout"));
    // Swift's .caption in the secondary ink.
    QFont small = m_readout->font();
    small.setPixelSize(10);
    m_readout->setFont(small);
    m_readout->setForegroundRole(QPalette::PlaceholderText);
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(5);
    column->addWidget(before);
    column->addWidget(m_handles);
    column->addWidget(m_after);
    column->addWidget(m_readout, 0, Qt::AlignHCenter);
    synchronize();
}

void SpectrumEditor::synchronize()
{
    QStringList degrees;
    for (const double handle : m_value().band().handles())
        degrees << QStringLiteral("%1°").arg(std::lround(handle));
    m_readout->setText(degrees.join(QStringLiteral("   ")));
    m_handles->update();
    m_after->update();
}

void SpectrumEditor::drag(double x, double width, bool pressed)
{
    // A press starts afresh: Qt can lose a release.
    if (pressed)
        m_dragging.reset();
    const double degrees = std::clamp(x, 0.0, width) / width * 360;
    HueSaturationSettings settings = m_value();
    HueBand band = settings.band();
    const int index = m_dragging.value_or(nearestHandle(band, degrees));
    m_dragging = index;
    band.setHandle(index, degrees);
    settings.setBand(band);
    m_change(settings);
}
