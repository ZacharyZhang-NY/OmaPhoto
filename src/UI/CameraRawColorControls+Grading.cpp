#include "UI/CameraRawColorControls.h"
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>
#include <cmath>
#include <numbers>

namespace {
CameraRawGradeWheel &wheelOf(CameraRawSettings &settings, int key)
{
    CameraRawGradingSettings &grading = settings.grading;
    return key == 0 ? grading.shadows : key == 1 ? grading.midtones : key == 2 ? grading.highlights : grading.global;
}

// Swift's GradeWheel: angle sets hue, distance saturation.
class GradeWheel : public QWidget {
public:
    GradeWheel(std::function<CameraRawGradeWheel()> value, std::function<void(double, double)> set, std::function<void()> reset, QWidget *parent)
        : QWidget(parent), m_value(std::move(value)), m_set(std::move(set)), m_reset(std::move(reset))
    {
        setFixedSize(86, 86);
        setToolTip(QStringLiteral("Drag to set hue and saturation. Double-click to reset this wheel."));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const double side = std::min(width(), height()), radius = side / 2 - 6;
        const QPointF centre(side / 2, side / 2);
        // Hue runs counterclockwise from red, as Qt's cone does.
        QConicalGradient hues(centre, 0);
        for (int degrees = 0; degrees <= 360; degrees += 30)
            hues.setColorAt(degrees / 360.0, QColor::fromHsvF(float(degrees % 360) / 360, 1, 1));
        painter.setOpacity(0.85);
        painter.setPen(Qt::NoPen);
        painter.setBrush(hues);
        painter.drawEllipse(centre, side / 2, side / 2);
        painter.setOpacity(1);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor::fromRgbF(1, 1, 1, 0.8f), 1));
        painter.drawEllipse(centre, side / 2 - 0.5, side / 2 - 0.5);
        const CameraRawGradeWheel wheel = m_value();
        const double angle = wheel.hue * std::numbers::pi / 180, distance = wheel.saturation / 100 * radius;
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        painter.drawEllipse(centre + QPointF(std::cos(angle) * distance, -std::sin(angle) * distance), 5, 5);
    }

    // Swift's contentShape: presses land inside the circle alone.
    bool inside(QPointF at) const
    {
        const double side = std::min(width(), height());
        return QLineF(QPointF(side / 2, side / 2), at).length() <= side / 2;
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        m_dragging = event->button() == Qt::LeftButton && inside(event->position());
        if (m_dragging)
            drag(event->position());
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_dragging && event->buttons().testFlag(Qt::LeftButton))
            drag(event->position());
    }

    void mouseReleaseEvent(QMouseEvent *) override { m_dragging = false; }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && inside(event->position()))
            m_reset();
    }

private:
    void drag(QPointF at)
    {
        const double side = std::min(width(), height()), radius = side / 2 - 6;
        const double dx = at.x() - side / 2, dy = side / 2 - at.y();
        double degrees = std::atan2(dy, dx) * 180 / std::numbers::pi;
        if (degrees < 0)
            degrees += 360;
        m_set(degrees, std::min(100.0, std::hypot(dx, dy) / radius * 100));
    }

    const std::function<CameraRawGradeWheel()> m_value;
    const std::function<void(double, double)> m_set;
    const std::function<void()> m_reset;
    bool m_dragging = false;
};

const std::array<QString, 5> pages{QStringLiteral("Three-Way"), QStringLiteral("Shadows"), QStringLiteral("Midtones"), QStringLiteral("Highlights"),
                                   QStringLiteral("Global")};

QLabel *small(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    QFont font = label->font();
    font.setPixelSize(10);
    label->setFont(font);
    return label;
}
}

CameraRawGradingControls::CameraRawGradingControls(EditorSession &session, QWidget *parent)
    : QWidget(parent), m_session(session), m_page(new QComboBox(this)), m_threeWay(new QWidget(this)), m_single(new QWidget(this))
{
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(8);
    // Five segments want more room than the panel: a menu.
    m_page->setObjectName(QStringLiteral("gradePage"));
    for (const QString &page : pages)
        m_page->addItem(page);
    m_page->setToolTip(QStringLiteral("Three-Way shows shadows, midtones, and highlights. The other choices show one wheel."));
    m_page->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    auto *hidden = new QLabel(QStringLiteral("Grading"), this);
    hidden->setBuddy(m_page);
    hidden->hide();
    connect(m_page, &QComboBox::activated, this, [this](int page) {
        if (!m_session.filterEdit())
            return;
        CameraRawPanel raw = m_session.filterEdit()->rawPanel;
        raw.gradePage = CameraRawGradePage(page);
        m_session.setCameraRawPanel(raw);
    });
    column->addWidget(m_page, 0, Qt::AlignLeft);
    auto *three = new QHBoxLayout(m_threeWay);
    three->setContentsMargins(0, 0, 0, 0);
    three->setSpacing(30);
    for (int key = 0; key < 3; ++key)
        three->addWidget(wheel(pages.at(size_t(key) + 1), key));
    three->addStretch(1);
    column->addWidget(m_threeWay);
    auto *single = new QHBoxLayout(m_single);
    single->setContentsMargins(0, 0, 0, 0);
    for (int key = 0; key < 4; ++key)
        single->addWidget(wheel(pages.at(size_t(key) + 1), key));
    single->addStretch(1);
    column->addWidget(m_single);
    const auto slider = [this, column](const QString &name, const QString &title, double low, double high, double reset, const QString &help,
                                       double CameraRawGradingSettings::*key) {
        const auto set = [this, key](double value) { update([key, value](CameraRawSettings &settings) { settings.grading.*key = value; }); };
        auto *row = new CameraRawRow({.name = name, .title = title, .help = help, .low = low, .high = high, .titleWidth = 78, .fixedTitle = true, .fieldWidth = 0},
                                     [this, key] { return raw().grading.*key; }, set, set, [set, reset] { set(reset); }, this);
        column->addWidget(row);
        m_rows.push_back(row);
    };
    slider(QStringLiteral("gradeBlending"), QStringLiteral("Blending"), 0, 100, 50, QStringLiteral("Controls how much the three tonal wheels overlap."),
           &CameraRawGradingSettings::blending);
    slider(QStringLiteral("gradeBalance"), QStringLiteral("Balance"), -100, 100, 0, QStringLiteral("Shifts the wheels toward shadows or highlights."),
           &CameraRawGradingSettings::balance);
    connect(&m_session, &EditorSession::changed, this, &CameraRawGradingControls::synchronize);
    synchronize();
}

// Swift's wheel: its title, the wheel, the readout and brightness.
QWidget *CameraRawGradingControls::wheel(const QString &title, int key)
{
    auto *box = new QWidget;
    box->setObjectName(QStringLiteral("gradeWheel") + title);
    auto *column = new QVBoxLayout(box);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(4);
    QLabel *heading = small(title, box);
    heading->setToolTip(QStringLiteral("Drag inside the wheel. Angle sets hue, distance sets saturation."));
    column->addWidget(heading, 0, Qt::AlignHCenter);
    column->addWidget(new GradeWheel([this, key] {
        CameraRawSettings settings = raw();
        return wheelOf(settings, key);
    },
                                     [this, key](double hue, double saturation) {
                                         update([key, hue, saturation](CameraRawSettings &settings) {
                                             wheelOf(settings, key).hue = hue;
                                             wheelOf(settings, key).saturation = saturation;
                                         });
                                     },
                                     [this, key] {
                                         update([key](CameraRawSettings &settings) {
                                             wheelOf(settings, key).hue = 0;
                                             wheelOf(settings, key).saturation = 0;
                                         });
                                     },
                                     box),
                      0, Qt::AlignHCenter);
    QLabel *readout = small(QString(), box);
    readout->setObjectName(QStringLiteral("gradeReadout"));
    readout->setToolTip(QStringLiteral("Hue and saturation of this wheel."));
    column->addWidget(readout, 0, Qt::AlignHCenter);
    const auto set = [this, key](double value) { update([key, value](CameraRawSettings &settings) { wheelOf(settings, key).luminance = value; }); };
    // Three columns fit when each slider takes 96.
    auto *luminance = new CameraRawSlider(-100, 100, {}, QStringLiteral("Brightness added by this wheel."), set, [set] { set(0); }, box);
    luminance->setObjectName(QStringLiteral("gradeLuminance"));
    luminance->setFixedWidth(96);
    column->addWidget(luminance, 0, Qt::AlignHCenter);
    m_wheels.push_back({key, box});
    return box;
}

CameraRawSettings CameraRawGradingControls::raw() const
{
    return m_session.filterEdit() ? m_session.filterEdit()->settings.cameraRaw : CameraRawSettings();
}

void CameraRawGradingControls::update(const std::function<void(CameraRawSettings &)> &change)
{
    FilterSettings settings = m_session.filterEdit() ? m_session.filterEdit()->settings : FilterSettings();
    change(settings.cameraRaw);
    m_session.updateFilter(settings, m_session.filterEdit() ? m_session.filterEdit()->preview : true);
}

void CameraRawGradingControls::synchronize()
{
    const CameraRawGradePage page = m_session.filterEdit() ? m_session.filterEdit()->rawPanel.gradePage : CameraRawGradePage::threeWay;
    {
        const QSignalBlocker quiet(m_page);
        m_page->setCurrentIndex(int(page));
    }
    m_threeWay->setVisible(page == CameraRawGradePage::threeWay);
    m_single->setVisible(page != CameraRawGradePage::threeWay);
    CameraRawSettings settings = raw();
    for (const auto &[key, box] : m_wheels) {
        // A single page shows the wheel it names.
        if (box->parentWidget() == m_single)
            box->setVisible(int(page) == key + 1);
        const CameraRawGradeWheel &wheel = wheelOf(settings, key);
        box->findChild<QLabel *>(QStringLiteral("gradeReadout"))->setText(QStringLiteral("%1°  %2").arg(std::lround(wheel.hue)).arg(std::lround(wheel.saturation)));
        box->findChild<CameraRawSlider *>(QStringLiteral("gradeLuminance"))->display(wheel.luminance);
        box->update();
    }
    for (CameraRawRow *row : m_rows)
        row->synchronize();
}
