#include "UI/CurvesControls.h"
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

// The curve over its grid; presses and drags edit points.
class CurveGraph : public QWidget {
public:
    explicit CurveGraph(CurvesControls &controls) : QWidget(&controls), m_controls(controls) { setFixedHeight(260); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(0, 0, 0, 89));
        const CurvesSettings settings = m_controls.m_value();
        const auto position = [&](double x, double y) { return QPointF(x / 255 * width(), (1 - y / 255) * height()); };
        QPainterPath grid;
        for (int index = 0; index <= 4; ++index) {
            const double f = index / 4.0;
            grid.moveTo(f * width(), 0);
            grid.lineTo(f * width(), height());
            grid.moveTo(0, f * height());
            grid.lineTo(width(), f * height());
        }
        painter.setRenderHint(QPainter::Antialiasing);
        painter.strokePath(grid, QPen(QColor(255, 255, 255, 31), 1));
        QPainterPath line;
        for (int x = 0; x <= 255; ++x) {
            const QPointF point = position(x, settings.value(x, size_t(settings.channel)));
            if (x == 0)
                line.moveTo(point);
            else
                line.lineTo(point);
        }
        painter.strokePath(line, QPen(Qt::white, 2));
        const std::vector<CurvePoint> &points = settings.channels.at(size_t(settings.channel));
        for (size_t index = 0; index < points.size(); ++index) {
            const QPointF at = position(points[index].x, points[index].y);
            QPainterPath dot;
            dot.addEllipse(QRectF(at.x() - 4, at.y() - 4, 8, 8));
            painter.fillPath(dot, m_controls.m_selected == index ? palette().color(QPalette::Highlight) : QColor(Qt::white));
        }
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            m_controls.drag(event->position(), size());
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (event->buttons().testFlag(Qt::LeftButton))
            m_controls.drag(event->position(), size());
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            m_controls.m_dragging.reset();
    }

private:
    CurvesControls &m_controls;
};

CurvesControls::CurvesControls(std::function<CurvesSettings()> value, std::function<void(const CurvesSettings &)> change, QWidget *parent)
    : QWidget(parent), m_value(std::move(value)), m_change(std::move(change)), m_channel(new QComboBox(this)), m_graph(new CurveGraph(*this)),
      m_readout(new QLabel(this)), m_remove(new QPushButton(QStringLiteral("Remove point"), this)),
      m_reset(new QPushButton(QStringLiteral("Reset curve"), this))
{
    for (const LevelsChannel channel : allLevelsChannels)
        m_channel->addItem(rawValue(channel));
    connect(m_channel, &QComboBox::activated, this, [this](int index) {
        CurvesSettings settings = m_value();
        settings.channel = allLevelsChannels[size_t(index)];
        m_change(settings);
        synchronize();
    });
    m_graph->setObjectName(QStringLiteral("curvesGraph"));
    m_readout->setObjectName(QStringLiteral("curvesReadout"));
    auto *title = new QLabel(QStringLiteral("Channel"), this);
    title->setBuddy(m_channel);
    auto *picker = new QHBoxLayout;
    picker->addWidget(title);
    picker->addWidget(m_channel, 1);
    auto *hint = new QLabel(QStringLiteral("Click to add a point. Drag to adjust."), this);
    QFont small = hint->font();
    small.setPixelSize(10);
    hint->setFont(small);
    hint->setForegroundRole(QPalette::PlaceholderText);
    m_remove->setAutoDefault(false);
    m_reset->setAutoDefault(false);
    connect(m_remove, &QPushButton::clicked, this, [this] {
        std::vector<CurvePoint> points = this->points();
        // Swift's guard: an inner point that is still there.
        if (!m_selected || *m_selected == 0 || *m_selected >= points.size() - 1)
            return;
        points.erase(points.begin() + qsizetype(*m_selected));
        m_selected.reset();
        setPoints(points);
    });
    connect(m_reset, &QPushButton::clicked, this, [this] {
        m_selected.reset();
        setPoints({{0, 0}, {255, 255}});
    });
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(12);
    column->addLayout(picker);
    column->addWidget(m_graph);
    column->addWidget(hint);
    auto *selection = new QHBoxLayout;
    selection->addWidget(m_readout);
    selection->addStretch(1);
    selection->addWidget(m_remove);
    column->addLayout(selection);
    column->addWidget(m_reset, 0, Qt::AlignLeft);
    synchronize();
}

std::vector<CurvePoint> CurvesControls::points() const
{
    const CurvesSettings settings = m_value();
    return settings.channels.at(size_t(settings.channel));
}

void CurvesControls::setPoints(const std::vector<CurvePoint> &points)
{
    CurvesSettings settings = m_value();
    settings.channels.at(size_t(settings.channel)) = points;
    m_change(settings);
    synchronize();
}

void CurvesControls::drag(QPointF at, QSizeF size)
{
    const double x = std::min(255.0, std::max(0.0, at.x() / size.width() * 255));
    const double y = std::min(255.0, std::max(0.0, 255 - at.y() / size.height() * 255));
    std::vector<CurvePoint> p = points();
    if (!m_dragging) {
        const auto distance = [&](const CurvePoint &point) { return std::hypot(point.x - x, point.y - y); };
        const auto nearest = std::min_element(p.begin(), p.end(), [&](const CurvePoint &a, const CurvePoint &b) { return distance(a) < distance(b); });
        if (distance(*nearest) < 14) {
            m_dragging = size_t(nearest - p.begin());
        } else if (p.size() < 32 && x > 1 && x < 254 && std::all_of(p.begin(), p.end(), [x](const CurvePoint &point) { return std::abs(point.x - x) > 1; })) {
            p.push_back({x, y});
            std::sort(p.begin(), p.end(), [](const CurvePoint &a, const CurvePoint &b) { return a.x < b.x; });
            m_dragging = size_t(std::find_if(p.begin(), p.end(), [x](const CurvePoint &point) { return point.x == x; }) - p.begin());
        }
    }
    if (!m_dragging || *m_dragging >= p.size())
        return;
    const size_t i = *m_dragging;
    m_selected = i;
    p[i].y = y;
    if (i > 0 && i < p.size() - 1)
        p[i].x = std::min(p[i + 1].x - 1, std::max(p[i - 1].x + 1, x));
    setPoints(p);
}

void CurvesControls::synchronize()
{
    const CurvesSettings settings = m_value();
    // Another channel drops the selection, as Swift's onChange.
    if (m_shownChannel != settings.channel) {
        m_shownChannel = settings.channel;
        m_selected.reset();
        m_dragging.reset();
    }
    m_channel->setCurrentIndex(int(settings.channel));
    const std::vector<CurvePoint> &points = settings.channels.at(size_t(settings.channel));
    const bool shown = m_selected && *m_selected < points.size();
    m_readout->setVisible(shown);
    if (shown)
        m_readout->setText(QStringLiteral("Input %1 · Output %2").arg(int(points[*m_selected].x)).arg(int(points[*m_selected].y)));
    m_remove->setEnabled(m_selected && *m_selected != 0 && *m_selected != points.size() - 1);
    m_graph->update();
}
