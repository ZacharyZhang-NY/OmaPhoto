#include "UI/CameraRawControls.h"
#include "UI/ColorPickerSheet.h"
#include "UI/CameraRawColorControls.h"
#include "UI/CameraRawDetailOpticsControls.h"
#include "UI/CameraRawGeometryCalibrationControls.h"
#include "UI/CameraRawRow.h"
#include "UI/LayerIcons.h"
#include "Rendering/EyedropperIcon.h"
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
QString title(int section)
{
    static const std::array<QString, 10> titles{QStringLiteral("Light"),  QStringLiteral("Color"),       QStringLiteral("Color Grading"),
                                               QStringLiteral("Effects"), QStringLiteral("Curve"),       QStringLiteral("Color Mixer"),
                                               QStringLiteral("Detail"),  QStringLiteral("Optics"),      QStringLiteral("Geometry"),
                                               QStringLiteral("Calibration")};
    return titles.at(size_t(section));
}

// The group's eye in CameraRawGroups, by section.
bool &shows(CameraRawGroups &groups, int section)
{
    const std::array fields{&CameraRawGroups::light,    &CameraRawGroups::color,  &CameraRawGroups::grading, &CameraRawGroups::effects,
                            &CameraRawGroups::curve,    &CameraRawGroups::mixer,  &CameraRawGroups::detail,  &CameraRawGroups::optics,
                            &CameraRawGroups::geometry, &CameraRawGroups::calibration};
    return groups.*fields.at(size_t(section));
}

bool adjusts(const CameraRawSettings &raw, int section)
{
    switch (section) {
    case 0: return raw.adjustsLight();
    case 1: return raw.adjustsColor();
    case 2: return raw.grading.adjusts();
    case 3: return raw.adjustsEffects();
    case 4: return raw.curve.adjusts();
    case 5: return raw.mixer.adjusts();
    case 6: return raw.detail.adjusts();
    case 7: return raw.optics.adjusts();
    case 8: return raw.geometry.adjusts();
    case 9: return raw.calibration.adjusts();
    }
    throw std::logic_error("unknown Camera Raw section");
}

// Swift's graph: the three ribbons, or the vectorscope's cells.
class ScopeGraph : public QWidget {
public:
    ScopeGraph(EditorSession &session, QWidget *parent) : QWidget(parent), m_session(session) { setFixedHeight(110); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath frame;
        frame.addRoundedRect(QRectF(rect()), 4, 4);
        painter.setClipPath(frame);
        painter.fillRect(rect(), QColor::fromRgbF(0, 0, 0, 0.35f));
        const std::optional<FilterEdit> &edit = m_session.filterEdit();
        if (!edit || !edit->rawPanel.scope)
            return;
        const CameraRawScope &scope = *edit->rawPanel.scope;
        const QSizeF size = QSizeF(this->size());
        if (edit->rawPanel.scopeMode == CameraRawScopeMode::histogram) {
            const double peak = scope.peak();
            if (!(peak > 0))
                return;
            // SwiftUI's red, green and blue, as the Levels histogram.
            ribbon(painter, scope.red, QColor(255, 66, 69), peak, size);
            ribbon(painter, scope.green, QColor(48, 209, 88), peak, size);
            ribbon(painter, scope.blue, QColor(0, 145, 255), peak, size);
            return;
        }
        const double peak = *std::max_element(scope.vectorscope.begin(), scope.vectorscope.end());
        if (!(peak > 0))
            return;
        const double cell = size.width() / CameraRawScope::scopeSide;
        for (size_t index = 0; index < scope.vectorscope.size(); ++index) {
            if (!(scope.vectorscope[index] > 0))
                continue;
            const int column = int(index) % CameraRawScope::scopeSide, row = int(index) / CameraRawScope::scopeSide;
            const double amount = std::min(1.0, scope.vectorscope[index] / peak);
            painter.fillRect(QRectF(column * cell, size.height() - (row + 1) * cell, cell + 0.2, cell + 0.2),
                             QColor::fromRgbF(1, 1, 1, float(0.15 + 0.85 * amount)));
        }
    }

    void contextMenuEvent(QContextMenuEvent *event) override
    {
        QMenu menu(this);
        menu.addAction(QStringLiteral("Histogram"), this, [this] { mode(CameraRawScopeMode::histogram); });
        menu.addAction(QStringLiteral("Vectorscope"), this, [this] { mode(CameraRawScopeMode::vectorscope); });
        menu.exec(event->globalPos());
    }

private:
    static void ribbon(QPainter &painter, const std::array<double, 256> &bins, QColor colour, double peak, QSizeF size)
    {
        QPainterPath path(QPointF(0, size.height()));
        for (size_t index = 0; index < bins.size(); ++index) {
            const double height = size.height() * std::min(1.0, std::max(0.0, bins[index] / peak));
            path.lineTo(double(index) * size.width() / double(bins.size()), size.height() - height);
        }
        path.lineTo(size.width(), size.height());
        path.closeSubpath();
        colour.setAlphaF(0.55f);
        painter.fillPath(path, colour);
    }

    void mode(CameraRawScopeMode mode)
    {
        if (!m_session.filterEdit())
            return;
        CameraRawPanel panel = m_session.filterEdit()->rawPanel;
        panel.scopeMode = mode;
        m_session.setCameraRawPanel(panel);
    }

    EditorSession &m_session;
};

// Swift's clip button: a small triangle, lit when on.
class ClipButton : public QToolButton {
public:
    ClipButton(bool shadows, QWidget *parent) : QToolButton(parent), m_shadows(shadows)
    {
        setFixedSize(14, 14);
        setAutoRaise(true);
        setObjectName(shadows ? QStringLiteral("shadowClipping") : QStringLiteral("highlightClipping"));
        setToolTip(shadows ? QStringLiteral("Show clipped shadows in blue on the preview.") : QStringLiteral("Show clipped highlights in red on the preview."));
        setAccessibleName(shadows ? QStringLiteral("Shadow Clipping Indicator") : QStringLiteral("Highlight Clipping Indicator"));
    }
    bool on = false;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(on ? (m_shadows ? QColor(0, 145, 255) : QColor(255, 66, 69)) : QColor::fromRgbF(1, 1, 1, 0.55f));
        painter.drawPolygon(QPolygonF({QPointF(7, 3), QPointF(12, 11), QPointF(2, 11)}));
    }

private:
    const bool m_shadows;
};
}

CameraRawControls::CameraRawControls(EditorSession &session, QWidget *parent)
    : QWidget(parent), m_session(session), m_scope(new ScopeGraph(session, this)), m_readout(new QLabel(this))
{
    m_scope->setObjectName(QStringLiteral("cameraRawScope"));
    m_readout->setObjectName(QStringLiteral("cameraRawReadout"));
    m_readout->setToolTip(QStringLiteral("Red, green, and blue of the pixel under the pointer."));
    m_readout->setForegroundRole(QPalette::PlaceholderText);
    QFont caption = m_readout->font();
    caption.setPixelSize(10);
    m_readout->setFont(caption);
    auto *clips = new QHBoxLayout(m_scope);
    clips->setContentsMargins(4, 4, 4, 4);
    for (const bool shadows : {true, false}) {
        auto *button = new ClipButton(shadows, m_scope);
        connect(button, &QToolButton::clicked, this, [this, shadows] {
            panel([shadows](CameraRawPanel &raw) {
                bool &shown = shadows ? raw.showsShadowClipping : raw.showsHighlightClipping;
                shown = !shown;
            });
            m_session.updateFilter(m_session.filterEdit().value().settings, m_session.filterEdit()->preview);
        });
        clips->addWidget(button);
        if (shadows)
            clips->addStretch(1);
    }
    auto *scopeColumn = new QVBoxLayout;
    scopeColumn->setSpacing(4);
    scopeColumn->addWidget(m_scope);
    scopeColumn->addWidget(m_readout);
    auto *content = new QWidget;
    auto *groups = new QVBoxLayout(content);
    groups->setContentsMargins(0, 0, 0, 0);
    groups->setSpacing(12);
    light(group(Section::light, groups));
    color(group(Section::color, groups));
    group(Section::colorGrading, groups)->addWidget(new CameraRawGradingControls(session));
    effects(group(Section::effects, groups));
    group(Section::curve, groups)->addWidget(new CameraRawCurveControls(session));
    group(Section::colorMixer, groups)->addWidget(new CameraRawMixerControls(session));
    group(Section::detail, groups)->addWidget(new CameraRawDetailControls(session));
    group(Section::optics, groups)->addWidget(new CameraRawOpticsControls(session));
    group(Section::geometry, groups)->addWidget(new CameraRawGeometryControls(session));
    group(Section::calibration, groups)->addWidget(new CameraRawCalibrationControls(session));
    groups->addStretch(1);
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("cameraRawGroups"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(10);
    column->addLayout(scopeColumn);
    column->addWidget(scroll, 1);
    qApp->installEventFilter(this);
    connect(&m_session, &EditorSession::changed, this, &CameraRawControls::synchronize);
    synchronize();
}

QVBoxLayout *CameraRawControls::group(Section section, QVBoxLayout *column)
{
    const QString name = title(int(section));
    auto *box = new QWidget;
    auto *header = new QHBoxLayout;
    header->setSpacing(6);
    auto *disclosure = new QToolButton(box);
    disclosure->setObjectName(QString(name).remove(QLatin1Char(' ')) + QStringLiteral("Section"));
    disclosure->setText(name);
    disclosure->setAccessibleName(name);
    disclosure->setAutoRaise(true);
    disclosure->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    QFont headline = disclosure->font();
    headline.setPixelSize(13);
    headline.setWeight(QFont::DemiBold);
    disclosure->setFont(headline);
    auto *eye = new QToolButton(box);
    eye->setObjectName(QString(name).remove(QLatin1Char(' ')) + QStringLiteral("Eye"));
    eye->setAutoRaise(true);
    connect(eye, &QToolButton::clicked, this, [this, section] { toggleShown(section); });
    header->addWidget(disclosure);
    header->addStretch(1);
    header->addWidget(eye);
    auto *body = new QWidget(box);
    auto *rows = new QVBoxLayout(body);
    rows->setContentsMargins(18, 0, 0, 0);
    rows->setSpacing(8);
    // Light, Color and Color Grading open first, Swift's `expanded`.
    body->setVisible(section == Section::light || section == Section::color || section == Section::colorGrading);
    connect(disclosure, &QToolButton::clicked, this, [this, body] {
        body->setVisible(body->isHidden());
        synchronize();
    });
    auto *stack = new QVBoxLayout(box);
    stack->setContentsMargins(0, 0, 0, 0);
    stack->setSpacing(8);
    stack->addLayout(header);
    stack->addWidget(body);
    column->addWidget(box);
    m_groups.push_back(Group{section, disclosure, eye, body});
    return rows;
}

void CameraRawControls::slider(QVBoxLayout *column, const QString &name, const QString &title, std::function<double &(CameraRawSettings &)> key,
                               double low, double high, int decimals, std::optional<CameraRawClipping> clipping, const QString &help,
                               CameraRawSliderTrack track, double reset)
{
    const double step = std::pow(10.0, decimals);
    auto *row = new CameraRawRow(
        {.name = name, .title = title, .help = help, .low = low, .high = high, .decimals = decimals, .track = track, .titleResets = true, .scrub = 1 / step},
        [this, key] {
            CameraRawSettings settings = raw();
            return key(settings);
        },
        [this, key, step, clipping](double value) { assign(key, std::round(value * step) / step, clipping); },
        [this, key](double typed) { assign(key, typed, std::nullopt); }, [this, key, reset] { assign(key, reset, std::nullopt); });
    column->addWidget(row);
    m_rows.push_back(row);
}

QLabel *CameraRawControls::subheadline(const QString &title, QVBoxLayout *column)
{
    auto *label = new QLabel(title);
    QFont font = label->font();
    font.setPixelSize(11);
    label->setFont(font);
    column->addWidget(label);
    return label;
}

// Swift's assign: Alt on a Light slider shows its clipping.
void CameraRawControls::assign(const std::function<double &(CameraRawSettings &)> &key, double value, std::optional<CameraRawClipping> clipping)
{
    if (!m_session.filterEdit())
        return;
    const bool showClipping = clipping && QGuiApplication::keyboardModifiers().testFlag(Qt::AltModifier);
    panel([&](CameraRawPanel &raw) { raw.clipping = showClipping ? clipping : std::nullopt; });
    update([&](CameraRawSettings &settings) {
        double &field = key(settings);
        field = value;
        if (&field == &settings.temperature || &field == &settings.tint)
            settings.whiteBalance = CameraRawWhiteBalance::custom;
    });
}

void CameraRawControls::update(const std::function<void(CameraRawSettings &)> &change)
{
    FilterSettings settings = m_session.filterEdit() ? m_session.filterEdit()->settings : FilterSettings();
    change(settings.cameraRaw);
    m_session.updateFilter(settings, m_session.filterEdit() ? m_session.filterEdit()->preview : true);
}

void CameraRawControls::panel(const std::function<void(CameraRawPanel &)> &change)
{
    if (!m_session.filterEdit())
        return;
    CameraRawPanel raw = m_session.filterEdit()->rawPanel;
    change(raw);
    m_session.setCameraRawPanel(raw);
}

void CameraRawControls::toggleShown(Section section)
{
    if (!m_session.filterEdit())
        return;
    panel([section](CameraRawPanel &raw) {
        bool &shown = shows(raw.shows, int(section));
        shown = !shown;
    });
    m_session.updateFilter(m_session.filterEdit()->settings, m_session.filterEdit()->preview);
}

CameraRawSettings CameraRawControls::raw() const
{
    return m_session.filterEdit() ? m_session.filterEdit()->settings.cameraRaw : CameraRawSettings();
}

bool CameraRawControls::eventFilter(QObject *watched, QEvent *event)
{
    // Alt let go ends the clipping and sharpen-mask views.
    if (event->type() == QEvent::KeyRelease && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Alt && !static_cast<QKeyEvent *>(event)->isAutoRepeat()
        && m_session.filterEdit()) {
        const CameraRawPanel &raw = m_session.filterEdit()->rawPanel;
        if (raw.clipping || raw.sharpenMask) {
            panel([](CameraRawPanel &shown) {
                shown.clipping = std::nullopt;
                shown.sharpenMask = false;
            });
            m_session.updateFilter(m_session.filterEdit()->settings, m_session.filterEdit()->preview);
        }
    }
    return QWidget::eventFilter(watched, event);
}

void CameraRawControls::synchronize()
{
    const CameraRawSettings settings = raw();
    const std::optional<FilterEdit> &edit = m_session.filterEdit();
    const qreal ratio = devicePixelRatioF();
    const QColor ink = palette().color(QPalette::WindowText), secondary = palette().color(QPalette::PlaceholderText);
    for (const Group &group : m_groups) {
        group.disclosure->setIcon(QIcon(LayerIcons::pixmap(!group.body->isHidden() ? LayerIcon::chevronDown : LayerIcon::chevronRight, 12, ink, ratio)));
        const bool adjusted = adjusts(settings, int(group.section));
        group.eye->setVisible(adjusted);
        CameraRawGroups groups = edit ? edit->rawPanel.shows : CameraRawGroups();
        const bool shown = shows(groups, int(group.section));
        const QString name = title(int(group.section));
        group.eye->setIcon(QIcon(LayerIcons::pixmap(shown ? LayerIcon::eye : LayerIcon::eyeSlash, 14, ink, ratio)));
        group.eye->setToolTip((shown ? QStringLiteral("Hide %1 in the preview") : QStringLiteral("Show %1 in the preview")).arg(name));
        group.eye->setAccessibleName((shown ? QStringLiteral("Hide %1") : QStringLiteral("Show %1")).arg(name));
    }
    for (CameraRawRow *row : m_rows)
        row->synchronize();
    const std::optional<std::array<int, 3>> readout = edit ? edit->rawPanel.readout : std::nullopt;
    m_readout->setText(readout ? QStringLiteral("R %1   G %2   B %3").arg((*readout)[0]).arg((*readout)[1]).arg((*readout)[2])
                               : QStringLiteral("R —   G —   B —"));
    const bool histogram = !edit || edit->rawPanel.scopeMode == CameraRawScopeMode::histogram;
    m_scope->setToolTip(histogram ? QStringLiteral("Tones from black on the left to white on the right: blacks, shadows, midtones, highlights, whites. "
                                                   "Right-click to show the vectorscope.")
                                  : QStringLiteral("Hue around the wheel, saturation outward from the center. Right-click to show the histogram."));
    m_scope->setAccessibleName(histogram ? QStringLiteral("RGB histogram") : QStringLiteral("Vectorscope"));
    // Found as QToolButtons: newer Qt finds only Q_OBJECT classes.
    for (const bool shadows : {true, false}) {
        auto *clip = static_cast<ClipButton *>(m_scope->findChild<QToolButton *>(shadows ? QStringLiteral("shadowClipping") : QStringLiteral("highlightClipping")));
        clip->on = edit && (shadows ? edit->rawPanel.showsShadowClipping : edit->rawPanel.showsHighlightClipping);
        clip->update();
    }
    m_scope->update();
    if (m_whiteBalance) {
        const QSignalBlocker quiet(m_whiteBalance);
        m_whiteBalance->setCurrentIndex(int(settings.whiteBalance));
        const bool sampling = edit && edit->rawPanel.samplesWhiteBalance;
        m_whiteBalanceSampler->setIcon(EyedropperIcon::icon(sampling ? palette().color(QPalette::Highlight) : secondary, ratio));
        m_whiteBalanceHint->setVisible(sampling);
    }
    if (m_glowStyle) {
        const QSignalBlocker glow(m_glowStyle), vignette(m_vignetteStyle);
        m_glowStyle->setCurrentIndex(int(settings.glowStyle));
        m_vignetteStyle->setCurrentIndex(int(settings.vignetteStyle));
    }
}

CameraRawControls::~CameraRawControls()
{
    releaseFocus(*this);
}
