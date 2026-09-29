#include "Rendering/EyedropperIcon.h"
#include "UI/CameraRawColorControls.h"
#include "UI/LayerIcons.h"
#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
const std::array<QString, 3> tabs{QStringLiteral("Hue"), QStringLiteral("Saturation"), QStringLiteral("Luminance")};

QColor hsb(double degrees, double saturation, double brightness)
{
    return QColor::fromHsvF(float(std::fmod(degrees, 360) / 360), float(saturation), float(brightness));
}

// Swift's familyTrack: the family's hue, strength or lightness.
CameraRawSliderTrack familyTrack(int index, int tab)
{
    const double hue = CameraRawMixerSettings::centers.at(size_t(index));
    using Kind = CameraRawSliderTrack::Kind;
    return {tab == 1 ? Kind::saturation : tab == 2 ? Kind::luminance : Kind::hue, hue};
}

// Swift's swatch: a filled circle, ringed white when chosen.
class Swatch : public QAbstractButton {
public:
    Swatch(double side, QWidget *parent) : QAbstractButton(parent), m_side(side) { setFixedSize(int(side) + 4, int(side) + 4); }
    QColor colour;
    bool chosen = false;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF circle(2, 2, m_side, m_side);
        painter.setPen(Qt::NoPen);
        painter.setBrush(colour);
        painter.drawEllipse(circle);
        if (chosen) {
            painter.setPen(QPen(Qt::white, 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(circle);
        }
    }

private:
    const double m_side;
};

void clear(QWidget *box)
{
    for (QWidget *child : box->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly))
        delete child;
}
}

CameraRawMixerControls::CameraRawMixerControls(EditorSession &session, QWidget *parent)
    : QWidget(parent), m_session(session), m_hsl(new QWidget(this)), m_families(new QWidget(m_hsl)), m_color(new QWidget(this)),
      m_colorRows(new QWidget(m_color)), m_point(new QWidget(this)), m_sampler(new QToolButton(m_point)), m_picked(new QWidget(m_point)),
      m_pointControls(new QWidget(m_point)), m_pointRows(new QWidget(m_pointControls)),
      m_visualize(new QCheckBox(QStringLiteral("Visualize Range"), m_pointControls)), m_target(new QPushButton(QStringLiteral("Targeted Adjustment"), this))
{
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(8);
    m_page = cameraRawSegments({QStringLiteral("HSL"), QStringLiteral("Color"), QStringLiteral("Point Color")}, QStringLiteral("mixerPage"),
                               QStringLiteral("HSL lists every color. Color edits one family. Point Color adjusts a color you pick."), column,
                               [this](int page) { panel([page](CameraRawPanel &raw) { raw.mixerPage = CameraRawMixerPage(page); }); });
    auto *hsl = new QVBoxLayout(m_hsl);
    hsl->setContentsMargins(0, 0, 0, 0);
    hsl->setSpacing(8);
    m_tab = cameraRawSegments({tabs[0], tabs[1], tabs[2]}, QStringLiteral("mixerTab"),
                              QStringLiteral("Hue shifts the color, Saturation its strength, and Luminance its brightness."), hsl,
                              [this](int tab) { panel([tab](CameraRawPanel &raw) { raw.mixerTab = CameraRawMixerTab(tab); }); });
    new QVBoxLayout(m_families);
    m_families->layout()->setContentsMargins(0, 0, 0, 0);
    m_families->layout()->setSpacing(8);
    hsl->addWidget(m_families);
    column->addWidget(m_hsl);
    auto *color = new QVBoxLayout(m_color);
    color->setContentsMargins(0, 0, 0, 0);
    color->setSpacing(8);
    auto *swatches = new QHBoxLayout;
    for (int index = 0; index < 8; ++index) {
        auto *swatch = new Swatch(18, m_color);
        swatch->setObjectName(QStringLiteral("mixerSwatch%1").arg(index));
        swatch->colour = hsb(CameraRawMixerSettings::centers.at(size_t(index)), 0.8, 0.9);
        swatch->setToolTip(QStringLiteral("Edit %1.").arg(CameraRawMixerSettings::names.at(size_t(index))));
        connect(swatch, &QAbstractButton::clicked, this, [this, index] { panel([index](CameraRawPanel &raw) { raw.mixerSwatch = index; }); });
        swatches->addWidget(swatch);
        m_swatches.push_back(swatch);
    }
    swatches->addStretch(1);
    color->addLayout(swatches);
    new QVBoxLayout(m_colorRows);
    m_colorRows->layout()->setContentsMargins(0, 0, 0, 0);
    m_colorRows->layout()->setSpacing(8);
    color->addWidget(m_colorRows);
    column->addWidget(m_color);
    auto *point = new QVBoxLayout(m_point);
    point->setContentsMargins(0, 0, 0, 0);
    point->setSpacing(8);
    auto *pickRow = new QHBoxLayout;
    m_sampler->setObjectName(QStringLiteral("pointColorSampler"));
    m_sampler->setAutoRaise(true);
    m_sampler->setToolTip(QStringLiteral("Click the picture to save a color. Up to eight colors."));
    connect(m_sampler, &QToolButton::clicked, this, [this] { panel([](CameraRawPanel &raw) { raw.samplesPointColor = !raw.samplesPointColor; }); });
    pickRow->addWidget(m_sampler);
    new QHBoxLayout(m_picked);
    m_picked->layout()->setContentsMargins(0, 0, 0, 0);
    pickRow->addWidget(m_picked);
    pickRow->addStretch(1);
    point->addLayout(pickRow);
    auto *controls = new QVBoxLayout(m_pointControls);
    controls->setContentsMargins(0, 0, 0, 0);
    controls->setSpacing(8);
    new QVBoxLayout(m_pointRows);
    m_pointRows->layout()->setContentsMargins(0, 0, 0, 0);
    m_pointRows->layout()->setSpacing(8);
    controls->addWidget(m_pointRows);
    m_visualize->setObjectName(QStringLiteral("visualizeRange"));
    m_visualize->setToolTip(QStringLiteral("Dims the picture outside this color's range. It is not kept when you press OK."));
    connect(m_visualize, &QAbstractButton::clicked, this, [this](bool on) { updatePoint([on](CameraRawPointColor &color) { color.visualize = on; }); });
    controls->addWidget(m_visualize);
    point->addWidget(m_pointControls);
    column->addWidget(m_point);
    m_target->setObjectName(QStringLiteral("mixerTargeted"));
    m_target->setCheckable(true);
    m_target->setAutoDefault(false);
    m_target->setToolTip(QStringLiteral("Drag a color in the picture. Nearby color families move together."));
    connect(m_target, &QPushButton::clicked, this, [this] {
        panel([](CameraRawPanel &raw) {
            raw.targetsCurve = false;
            raw.targetsMixer = !raw.targetsMixer;
        });
    });
    column->addWidget(m_target, 0, Qt::AlignLeft);
    connect(&m_session, &EditorSession::changed, this, &CameraRawMixerControls::synchronize);
    synchronize();
}

void CameraRawMixerControls::row(QWidget *box, CameraRawRow::Spec spec, std::function<double(const CameraRawSettings &)> value,
                                 std::function<void(CameraRawSettings &, double)> write, double reset)
{
    const auto set = [this, write](double number) { update([write, number](CameraRawSettings &settings) { write(settings, number); }); };
    box->layout()->addWidget(new CameraRawRow(std::move(spec), [this, value] { return value(raw()); }, set, set, [set, reset] { set(reset); }, box));
}

void CameraRawMixerControls::rebuildFamilies(int tab)
{
    clear(m_families);
    for (int index = 0; index < 8; ++index) {
        const QString &name = CameraRawMixerSettings::names.at(size_t(index));
        const auto family = [tab](CameraRawSettings &settings) -> std::array<double, 8> & {
            return tab == 1 ? settings.mixer.saturation : tab == 2 ? settings.mixer.luminance : settings.mixer.hue;
        };
        row(m_families,
            {.name = QStringLiteral("family") + name, .title = name, .help = QStringLiteral("%1 of %2.").arg(tabs.at(size_t(tab)), name),
             .track = familyTrack(index, tab), .titleWidth = 78, .fixedTitle = true, .fieldWidth = 48, .scrub = 1},
            [family, index](const CameraRawSettings &settings) {
                CameraRawSettings copy = settings;
                return family(copy).at(size_t(index));
            },
            [family, index](CameraRawSettings &settings, double value) { family(settings).at(size_t(index)) = value; }, 0);
    }
}

void CameraRawMixerControls::rebuildColor(int swatch)
{
    clear(m_colorRows);
    const std::array<QString, 3> help{QStringLiteral("Shifts the selected color family around the wheel."),
                                      QStringLiteral("Makes the selected color family stronger or quieter."),
                                      QStringLiteral("Makes the selected color family lighter or darker.")};
    for (int tab = 0; tab < 3; ++tab) {
        const auto family = [tab](CameraRawSettings &settings) -> std::array<double, 8> & {
            return tab == 1 ? settings.mixer.saturation : tab == 2 ? settings.mixer.luminance : settings.mixer.hue;
        };
        row(m_colorRows,
            {.name = QStringLiteral("color") + tabs.at(size_t(tab)), .title = tabs.at(size_t(tab)), .help = help.at(size_t(tab)),
             .track = familyTrack(swatch, tab), .titleWidth = 88, .fixedTitle = true, .fieldWidth = 0},
            [family, swatch](const CameraRawSettings &settings) {
                CameraRawSettings copy = settings;
                return family(copy).at(size_t(swatch));
            },
            [family, swatch](CameraRawSettings &settings, double value) { family(settings).at(size_t(swatch)) = value; }, 0);
    }
}

void CameraRawMixerControls::rebuildPoint(int index, double hue)
{
    clear(m_pointRows);
    using Kind = CameraRawSliderTrack::Kind;
    const auto point = [this, index](const QString &name, const QString &title, const QString &help, double CameraRawPointColor::*key, double low,
                                     double high, double reset, CameraRawSliderTrack track) {
        row(m_pointRows,
            {.name = name, .title = title, .help = help, .low = low, .high = high, .track = track, .titleWidth = 110, .fixedTitle = true, .fieldWidth = 0},
            [index, key](const CameraRawSettings &settings) {
                const std::vector<CameraRawPointColor> &points = settings.mixer.points;
                return size_t(index) < points.size() ? points[size_t(index)].*key : 0.0;
            },
            [index, key](CameraRawSettings &settings, double value) {
                if (size_t(index) < settings.mixer.points.size())
                    settings.mixer.points[size_t(index)].*key = value;
            },
            reset);
    };
    point(QStringLiteral("hueShift"), QStringLiteral("Hue Shift"), QStringLiteral("Shifts the picked color around the color wheel."),
          &CameraRawPointColor::hueShift, -100, 100, 0, {Kind::hue, hue});
    point(QStringLiteral("saturationShift"), QStringLiteral("Saturation Shift"), QStringLiteral("Makes the picked color stronger or quieter."),
          &CameraRawPointColor::saturationShift, -100, 100, 0, {Kind::saturation, hue});
    point(QStringLiteral("luminanceShift"), QStringLiteral("Luminance Shift"), QStringLiteral("Makes the picked color lighter or darker."),
          &CameraRawPointColor::luminanceShift, -100, 100, 0, {Kind::luminance, hue});
    point(QStringLiteral("hueRange"), QStringLiteral("Hue Range"), QStringLiteral("How far in hue the adjustment reaches."), &CameraRawPointColor::hueRange,
          5, 180, 30, {});
    point(QStringLiteral("saturationRange"), QStringLiteral("Saturation Range"), QStringLiteral("How far in saturation the adjustment reaches."),
          &CameraRawPointColor::saturationRange, 0.05, 1, 0.4, {});
    point(QStringLiteral("luminanceRange"), QStringLiteral("Luminance Range"), QStringLiteral("How far in brightness the adjustment reaches."),
          &CameraRawPointColor::luminanceRange, 0.05, 1, 0.4, {});
}

CameraRawSettings CameraRawMixerControls::raw() const
{
    return m_session.filterEdit() ? m_session.filterEdit()->settings.cameraRaw : CameraRawSettings();
}

void CameraRawMixerControls::update(const std::function<void(CameraRawSettings &)> &change)
{
    FilterSettings settings = m_session.filterEdit() ? m_session.filterEdit()->settings : FilterSettings();
    change(settings.cameraRaw);
    m_session.updateFilter(settings, m_session.filterEdit() ? m_session.filterEdit()->preview : true);
}

// Swift's updatePoint: the chosen point, when it still exists.
void CameraRawMixerControls::updatePoint(const std::function<void(CameraRawPointColor &)> &change)
{
    const int index = m_session.filterEdit() ? m_session.filterEdit()->rawPanel.pointIndex : 0;
    update([index, &change](CameraRawSettings &settings) {
        if (index >= 0 && size_t(index) < settings.mixer.points.size())
            change(settings.mixer.points[size_t(index)]);
    });
}

void CameraRawMixerControls::panel(const std::function<void(CameraRawPanel &)> &change)
{
    if (!m_session.filterEdit())
        return;
    CameraRawPanel raw = m_session.filterEdit()->rawPanel;
    change(raw);
    m_session.setCameraRawPanel(raw);
}

void CameraRawMixerControls::synchronize()
{
    const std::optional<FilterEdit> &edit = m_session.filterEdit();
    const CameraRawPanel shown = edit ? edit->rawPanel : CameraRawPanel();
    const CameraRawSettings settings = raw();
    m_page->button(int(shown.mixerPage))->setChecked(true);
    m_tab->button(int(shown.mixerTab))->setChecked(true);
    m_hsl->setVisible(shown.mixerPage == CameraRawMixerPage::hsl);
    m_color->setVisible(shown.mixerPage == CameraRawMixerPage::color);
    m_point->setVisible(shown.mixerPage == CameraRawMixerPage::point);
    if (m_shownTab != int(shown.mixerTab)) {
        m_shownTab = int(shown.mixerTab);
        rebuildFamilies(*m_shownTab);
    }
    const int swatch = std::min(7, shown.mixerSwatch);
    if (m_shownSwatch != swatch) {
        m_shownSwatch = swatch;
        rebuildColor(swatch);
    }
    for (size_t index = 0; index < m_swatches.size(); ++index) {
        static_cast<Swatch *>(m_swatches[index])->chosen = int(index) == shown.mixerSwatch;
        m_swatches[index]->update();
    }
    const std::vector<CameraRawPointColor> &points = settings.mixer.points;
    if (m_shownPicked != points.size()) {
        m_shownPicked = points.size();
        clear(m_picked);
        for (size_t index = 0; index < points.size(); ++index) {
            auto *pick = new Swatch(16, m_picked);
            pick->setObjectName(QStringLiteral("pickedColor%1").arg(index));
            pick->setToolTip(QStringLiteral("Select this picked color."));
            connect(pick, &QAbstractButton::clicked, this, [this, index] { panel([index](CameraRawPanel &raw) { raw.pointIndex = int(index); }); });
            m_picked->layout()->addWidget(pick);
            // Shown now: a layout shows late children only later.
            pick->show();
        }
    }
    for (size_t index = 0; index < points.size(); ++index) {
        // Newer Qt finds only Q_OBJECT classes: found by base.
        auto *pick = static_cast<Swatch *>(m_picked->findChild<QAbstractButton *>(QStringLiteral("pickedColor%1").arg(index)));
        pick->colour = hsb(points[index].hue, points[index].saturation, points[index].luminance);
        pick->chosen = int(index) == shown.pointIndex;
        pick->update();
    }
    const bool chosen = shown.pointIndex >= 0 && size_t(shown.pointIndex) < points.size();
    m_pointControls->setVisible(chosen);
    if (chosen && m_shownPoint != std::pair(shown.pointIndex, points[size_t(shown.pointIndex)].hue)) {
        m_shownPoint = std::pair(shown.pointIndex, points[size_t(shown.pointIndex)].hue);
        rebuildPoint(shown.pointIndex, m_shownPoint->second);
    }
    {
        const QSignalBlocker quiet(m_visualize);
        m_visualize->setChecked(chosen && points[size_t(shown.pointIndex)].visualize);
    }
    m_sampler->setIcon(EyedropperIcon::icon(palette().color(shown.samplesPointColor ? QPalette::Highlight : QPalette::PlaceholderText), devicePixelRatioF()));
    for (CameraRawRow *each : findChildren<CameraRawRow *>())
        each->synchronize();
    m_target->setChecked(shown.targetsMixer);
    m_target->setIcon(QIcon(LayerIcons::pixmap(LayerIcon::scope, 14, palette().color(QPalette::ButtonText), devicePixelRatioF())));
}
