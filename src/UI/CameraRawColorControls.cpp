#include "UI/CameraRawColorControls.h"
#include "UI/CameraRawRow.h"
#include "UI/LayerIcons.h"
#include <QButtonGroup>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

QButtonGroup *cameraRawSegments(const QStringList &titles, const QString &name, const QString &help, QVBoxLayout *column, std::function<void(int)> choose)
{
    auto *box = new QWidget;
    auto *row = new QHBoxLayout(box);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    auto *group = new QButtonGroup(box);
    group->setExclusive(true);
    for (int index = 0; index < titles.size(); ++index) {
        auto *button = new QToolButton(box);
        button->setText(titles[index]);
        button->setCheckable(true);
        button->setToolTip(help);
        button->setObjectName(name + QString::number(index));
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        group->addButton(button, index);
        row->addWidget(button);
    }
    QObject::connect(group, &QButtonGroup::idClicked, box, std::move(choose));
    column->addWidget(box);
    return group;
}

// Swift's curve graph: diagonal, curve, dividers or points.
class CameraRawCurveGraph : public QWidget {
public:
    explicit CameraRawCurveGraph(CameraRawCurveControls &owner) : QWidget(&owner), m_owner(owner)
    {
        setObjectName(QStringLiteral("cameraRawCurveGraph"));
        setFixedHeight(150);
    }

protected:
    bool parametric() const
    {
        return !m_owner.m_session.filterEdit() || m_owner.m_session.filterEdit()->rawPanel.curvePage == CameraRawCurvePage::parametric;
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath frame;
        frame.addRoundedRect(QRectF(rect()), 4, 4);
        painter.setClipPath(frame);
        painter.fillRect(rect(), QColor::fromRgbF(0, 0, 0, 0.35f));
        const double w = width(), h = height();
        painter.setPen(QPen(QColor::fromRgbF(1, 1, 1, 0.25f), 1));
        painter.drawLine(QPointF(0, h), QPointF(w, 0));
        const CameraRawCurveSettings curve = m_owner.raw().curve;
        std::vector<double> samples;
        if (parametric()) {
            for (int index = 0; index < 64; ++index)
                samples.push_back(curve.parametric(index / 63.0));
        } else {
            for (const float value : curve.channelTable(m_owner.currentPoints()))
                samples.push_back(value);
        }
        QPainterPath line;
        for (size_t index = 0; index < samples.size(); ++index) {
            const QPointF point(double(index) / double(std::max<size_t>(1, samples.size() - 1)) * w, (1 - samples[index]) * h);
            if (index == 0)
                line.moveTo(point);
            else
                line.lineTo(point);
        }
        painter.strokePath(line, QPen(Qt::white, 1.5));
        if (parametric()) {
            painter.setPen(QPen(Qt::white, 3));
            for (const double split : {curve.shadowSplit, curve.darkSplit, curve.lightSplit})
                painter.drawLine(QPointF(split / 100 * w, h - 8), QPointF(split / 100 * w, h));
            return;
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        for (const CurvePoint &point : m_owner.currentPoints())
            painter.drawEllipse(QRectF(point.x * w - 4, (1 - point.y) * h - 4, 8, 8));
    }

    // Swift's two drags: still, a press adds; moved, it drags.
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            m_press = event->position();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_press || !event->buttons().testFlag(Qt::LeftButton))
            return;
        if (!m_moved && QLineF(*m_press, event->position()).length() < 2)
            return;
        m_moved = true;
        if (parametric())
            moveDivider(event->position().x());
        else
            movePoint(event->position());
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_press && !m_moved && event->position() == *m_press && !parametric())
            addPoint(event->position());
        m_press.reset();
        m_moved = false;
    }

    // The double click's own release must add nothing back.
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && !parametric())
            removePoint(event->position());
    }

private:
    void moveDivider(double x)
    {
        const double value = std::min(98.0, std::max(2.0, x / std::max(double(width()), 1.0) * 100));
        const CameraRawCurveSettings curve = m_owner.raw().curve;
        const std::array<double, 3> splits{curve.shadowSplit, curve.darkSplit, curve.lightSplit};
        size_t nearest = 0;
        for (size_t index = 1; index < 3; ++index)
            if (std::abs(splits[index] - value) < std::abs(splits[nearest] - value))
                nearest = index;
        m_owner.update([nearest, value](CameraRawSettings &settings) {
            (nearest == 0 ? settings.curve.shadowSplit : nearest == 1 ? settings.curve.darkSplit : settings.curve.lightSplit) = value;
        });
    }

    void addPoint(QPointF at)
    {
        std::vector<CurvePoint> points = m_owner.currentPoints();
        points.push_back({std::min(0.99, std::max(0.01, at.x() / width())), std::min(1.0, std::max(0.0, 1 - at.y() / height()))});
        m_owner.store(points);
    }

    // The nearest inner point by x; ends stay.
    std::optional<size_t> nearestInner(const std::vector<CurvePoint> &points, double x) const
    {
        std::optional<size_t> nearest;
        for (size_t index = 1; index + 1 < points.size(); ++index)
            if (!nearest || std::abs(points[index].x - x) < std::abs(points[*nearest].x - x))
                nearest = index;
        return nearest;
    }

    void movePoint(QPointF at)
    {
        std::vector<CurvePoint> points = m_owner.currentPoints();
        const double x = at.x() / width();
        const std::optional<size_t> index = nearestInner(points, x);
        if (!index)
            return;
        points[*index] = {std::min(0.98, std::max(0.02, x)), std::min(1.0, std::max(0.0, 1 - at.y() / height()))};
        m_owner.store(points);
    }

    void removePoint(QPointF at)
    {
        std::vector<CurvePoint> points = m_owner.currentPoints();
        const double x = at.x() / width();
        const std::optional<size_t> index = nearestInner(points, x);
        if (!index || !(std::abs(points[*index].x - x) < 0.04))
            return;
        points.erase(points.begin() + std::ptrdiff_t(*index));
        m_owner.store(points);
    }

    CameraRawCurveControls &m_owner;
    std::optional<QPointF> m_press;
    bool m_moved = false;
};

namespace {
QLabel *caption(const QString &text, int pixels)
{
    auto *label = new QLabel(text);
    QFont font = label->font();
    font.setPixelSize(pixels);
    label->setFont(font);
    label->setForegroundRole(QPalette::PlaceholderText);
    return label;
}
}

CameraRawCurveControls::CameraRawCurveControls(EditorSession &session, QWidget *parent)
    : QWidget(parent), m_session(session), m_graph(new CameraRawCurveGraph(*this)), m_amounts(new QWidget(this)), m_points(new QWidget(this)),
      m_selected(caption(QString(), 10)), m_preset(new QComboBox(m_points)), m_target(new QPushButton(QStringLiteral("Targeted Adjustment"), this))
{
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(8);
    m_page = cameraRawSegments({QStringLiteral("Parametric"), QStringLiteral("Point")}, QStringLiteral("curvePage"),
                               QStringLiteral("Parametric lifts tonal regions. Point places anchors on the curve."), column,
                               [this](int page) { panel([page](CameraRawPanel &raw) { raw.curvePage = CameraRawCurvePage(page); }); });
    m_channelBox = new QWidget(this);
    auto *channelColumn = new QVBoxLayout(m_channelBox);
    channelColumn->setContentsMargins(0, 0, 0, 0);
    m_channel = cameraRawSegments({QStringLiteral("RGB"), QStringLiteral("Red"), QStringLiteral("Green"), QStringLiteral("Blue")},
                                  QStringLiteral("pointChannel"), QStringLiteral("RGB changes brightness. Red, green, and blue also shift the color."),
                                  channelColumn, [this](int channel) { panel([channel](CameraRawPanel &raw) { raw.pointChannel = CameraRawPointChannel(channel); }); });
    column->addWidget(m_channelBox);
    column->addWidget(m_graph);
    auto *amounts = new QVBoxLayout(m_amounts);
    amounts->setContentsMargins(0, 0, 0, 0);
    amounts->setSpacing(8);
    const auto amount = [this, amounts](const QString &name, const QString &title, double CameraRawCurveSettings::*key, const QString &help) {
        auto *row = new CameraRawRow({.name = name, .title = title, .help = help, .titleWidth = 88, .fixedTitle = true, .fieldWidth = 48},
                                     [this, key] { return raw().curve.*key; },
                                     [this, key](double value) { update([key, value](CameraRawSettings &settings) { settings.curve.*key = value; }); },
                                     [this, key](double value) { update([key, value](CameraRawSettings &settings) { settings.curve.*key = value; }); },
                                     [this, key] { update([key](CameraRawSettings &settings) { settings.curve.*key = 0; }); });
        amounts->addWidget(row);
        m_rows.push_back(row);
    };
    amount(QStringLiteral("curveHighlights"), QStringLiteral("Highlights"), &CameraRawCurveSettings::highlights, QStringLiteral("Lifts or lowers the brightest tones."));
    amount(QStringLiteral("curveLights"), QStringLiteral("Lights"), &CameraRawCurveSettings::lights, QStringLiteral("Lifts or lowers the light tones."));
    amount(QStringLiteral("curveDarks"), QStringLiteral("Darks"), &CameraRawCurveSettings::darks, QStringLiteral("Lifts or lowers the dark tones."));
    amount(QStringLiteral("curveShadows"), QStringLiteral("Shadows"), &CameraRawCurveSettings::shadows, QStringLiteral("Lifts or lowers the darkest tones."));
    column->addWidget(m_amounts);
    auto *points = new QVBoxLayout(m_points);
    points->setContentsMargins(0, 0, 0, 0);
    points->setSpacing(8);
    m_selected->setObjectName(QStringLiteral("curveSelectedPoint"));
    m_selected->setToolTip(QStringLiteral("Input and output of the selected curve point."));
    points->addWidget(m_selected);
    m_preset->setObjectName(QStringLiteral("curvePreset"));
    m_preset->addItems({QStringLiteral("Custom"), QStringLiteral("Linear"), QStringLiteral("Medium Contrast"), QStringLiteral("Strong Contrast")});
    m_preset->setToolTip(QStringLiteral("Replaces this curve with a straight line or a contrast curve."));
    auto *presetRow = new QHBoxLayout;
    auto *presetTitle = new QLabel(QStringLiteral("Preset"), m_points);
    presetTitle->setBuddy(m_preset);
    presetRow->addWidget(presetTitle);
    presetRow->addWidget(m_preset);
    presetRow->addStretch(1);
    points->addLayout(presetRow);
    connect(m_preset, &QComboBox::activated, this, [this](int preset) {
        if (preset == 1)
            store(CameraRawCurveSettings::linear());
        else if (preset == 2)
            store(CameraRawCurveSettings::mediumContrast());
        else if (preset == 3)
            store(CameraRawCurveSettings::strongContrast());
    });
    m_refine = new CameraRawRow({.name = QStringLiteral("refineSaturation"), .title = QStringLiteral("Refine Saturation"),
                                 .help = QStringLiteral("How much the RGB curve also changes color strength. Zero keeps it to brightness."), .titleWidth = 88,
                                 .fixedTitle = true, .fieldWidth = 48},
                                [this] { return raw().curve.refineSaturation; },
                                [this](double value) { update([value](CameraRawSettings &settings) { settings.curve.refineSaturation = value; }); },
                                [this](double value) { update([value](CameraRawSettings &settings) { settings.curve.refineSaturation = value; }); },
                                [this] { update([](CameraRawSettings &settings) { settings.curve.refineSaturation = 0; }); });
    points->addWidget(m_refine);
    m_rows.push_back(m_refine);
    column->addWidget(m_points);
    m_target->setObjectName(QStringLiteral("curveTargeted"));
    m_target->setCheckable(true);
    m_target->setAutoDefault(false);
    m_target->setToolTip(QStringLiteral("Drag on the picture to move the curve for the tone under the pointer."));
    connect(m_target, &QPushButton::clicked, this, [this] {
        panel([](CameraRawPanel &raw) {
            raw.targetsMixer = false;
            raw.targetsCurve = !raw.targetsCurve;
        });
    });
    column->addWidget(m_target, 0, Qt::AlignLeft);
    connect(&m_session, &EditorSession::changed, this, &CameraRawCurveControls::synchronize);
    synchronize();
}

CameraRawSettings CameraRawCurveControls::raw() const
{
    return m_session.filterEdit() ? m_session.filterEdit()->settings.cameraRaw : CameraRawSettings();
}

std::vector<CurvePoint> CameraRawCurveControls::currentPoints() const
{
    const CameraRawCurveSettings curve = raw().curve;
    switch (m_session.filterEdit() ? m_session.filterEdit()->rawPanel.pointChannel : CameraRawPointChannel::rgb) {
    case CameraRawPointChannel::rgb: return curve.rgb;
    case CameraRawPointChannel::red: return curve.red;
    case CameraRawPointChannel::green: return curve.green;
    case CameraRawPointChannel::blue: return curve.blue;
    }
    throw std::logic_error("unknown point channel");
}

void CameraRawCurveControls::update(const std::function<void(CameraRawSettings &)> &change)
{
    FilterSettings settings = m_session.filterEdit() ? m_session.filterEdit()->settings : FilterSettings();
    change(settings.cameraRaw);
    m_session.updateFilter(settings, m_session.filterEdit() ? m_session.filterEdit()->preview : true);
}

void CameraRawCurveControls::store(const std::vector<CurvePoint> &points)
{
    const CameraRawPointChannel channel = m_session.filterEdit() ? m_session.filterEdit()->rawPanel.pointChannel : CameraRawPointChannel::rgb;
    update([&points, channel](CameraRawSettings &settings) {
        (channel == CameraRawPointChannel::rgb     ? settings.curve.rgb
         : channel == CameraRawPointChannel::red   ? settings.curve.red
         : channel == CameraRawPointChannel::green ? settings.curve.green
                                                   : settings.curve.blue) = points;
    });
}

void CameraRawCurveControls::panel(const std::function<void(CameraRawPanel &)> &change)
{
    if (!m_session.filterEdit())
        return;
    CameraRawPanel raw = m_session.filterEdit()->rawPanel;
    change(raw);
    m_session.setCameraRawPanel(raw);
}

void CameraRawCurveControls::synchronize()
{
    const std::optional<FilterEdit> &edit = m_session.filterEdit();
    const bool point = edit && edit->rawPanel.curvePage == CameraRawCurvePage::point;
    m_page->button(point ? 1 : 0)->setChecked(true);
    m_channelBox->setVisible(point);
    const CameraRawPointChannel channel = edit ? edit->rawPanel.pointChannel : CameraRawPointChannel::rgb;
    m_channel->button(int(channel))->setChecked(true);
    m_graph->setToolTip(point ? QStringLiteral("Drag a point. Click the curve to add one. Double-click a point to remove it.")
                              : QStringLiteral("Drag a divider to change which tones the neighboring sliders affect."));
    m_graph->update();
    m_amounts->setVisible(!point);
    m_points->setVisible(point);
    const std::vector<CurvePoint> points = currentPoints();
    // Swift's selectedPoint: the last inner point, else the last.
    const CurvePoint selected = points.size() > 2 ? points[points.size() - 2] : points.back();
    m_selected->setText(QStringLiteral("In %1   Out %2").arg(std::lround(selected.x * 255)).arg(std::lround(selected.y * 255)));
    const QSignalBlocker quiet(m_preset);
    m_preset->setCurrentIndex(points == CameraRawCurveSettings::linear()           ? 1
                              : points == CameraRawCurveSettings::mediumContrast() ? 2
                              : points == CameraRawCurveSettings::strongContrast() ? 3
                                                                                  : 0);
    m_refine->setVisible(channel == CameraRawPointChannel::rgb);
    for (CameraRawRow *row : m_rows)
        row->synchronize();
    m_target->setChecked(edit && edit->rawPanel.targetsCurve);
    m_target->setIcon(QIcon(LayerIcons::pixmap(LayerIcon::scope, 14, palette().color(QPalette::ButtonText), devicePixelRatioF())));
}
