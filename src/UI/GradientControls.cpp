#include "UI/GradientControls.h"
#include "Document/EditorSession.h"
#include "UI/LassoControls.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <cmath>

namespace {
// The session's gradient settings with one field rewritten.
void changeGradient(EditorSession &session, const std::function<void(GradientSettings &)> &change)
{
    GradientSettings settings = session.gradientSettings();
    change(settings);
    session.setGradientSettings(settings);
}
}

GradientSwatch::GradientSwatch(const EditorSession &session, QWidget *parent) : QWidget(parent), m_session(session)
{
    setObjectName(QStringLiteral("gradientSwatch"));
    setFixedSize(56, 18);
}

void GradientSwatch::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath shape;
    shape.addRoundedRect(QRectF(rect()), 3, 3);
    painter.setClipPath(shape);
    painter.fillRect(rect(), Qt::white);
    // Four-point squares: transparency reads as transparency.
    const QColor square = QColor::fromRgbF(0.5, 0.5, 0.5, 0.45);
    for (int row = 0; row * 4 < height(); ++row) {
        for (int column = 0; column * 4 < width(); ++column) {
            if ((row + column) % 2 == 0)
                painter.fillRect(QRectF(column * 4, row * 4, 4, 4), square);
        }
    }
    const std::array<QColor, 2> colors = m_session.gradientColors(false);
    QLinearGradient ramp(QPointF(0, 0), QPointF(width(), 0));
    ramp.setColorAt(0, colors[0]);
    ramp.setColorAt(1, colors[1]);
    painter.fillRect(rect(), ramp);
    painter.setClipping(false);
    painter.setPen(QPen(QColor(0, 0, 0, 128), 1));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 2.5, 2.5);
}

GradientControls::GradientControls(EditorSession &session, QWidget *parent)
    : ToolHeaderBar(QStringLiteral("Gradient"), parent), m_session(session), m_shapes(new QButtonGroup(this)),
      m_linear(shape(QStringLiteral("gradientLinear"), rawValue(GradientShape::linear), false)),
      m_radial(shape(QStringLiteral("gradientRadial"), rawValue(GradientShape::radial), true)), m_swatch(new GradientSwatch(session, this)),
      m_style(new QComboBox(this)), m_reverse(new QCheckBox(QStringLiteral("Reverse"), this)), m_opacitySlider(new QSlider(Qt::Horizontal, this)),
      m_opacity(new SelectionAmountField(session, 1, 100, [this] { return m_session.gradientSettings().opacity * 100; },
                                         [this](double percent) {
                                             changeGradient(m_session, [percent](GradientSettings &gradient) { gradient.opacity = percent / 100; });
                                         }, this)),
      m_mask(new QLabel(QStringLiteral("Mask"), this)), m_cancel(new QPushButton(QStringLiteral("Cancel"), this)),
      m_apply(new QPushButton(QStringLiteral("Apply"), this))
{
    m_style->setObjectName(QStringLiteral("gradientStyle"));
    for (const GradientStyle style : allGradientStyles)
        m_style->addItem(rawValue(style));
    connect(m_style, &QComboBox::activated, this,
            [this](int index) { changeGradient(m_session, [index](GradientSettings &gradient) { gradient.style = allGradientStyles[size_t(index)]; }); });
    m_reverse->setObjectName(QStringLiteral("gradientReverse"));
    connect(m_reverse, &QCheckBox::clicked, this, [this](bool on) { changeGradient(m_session, [on](GradientSettings &gradient) { gradient.reversed = on; }); });
    m_opacitySlider->setObjectName(QStringLiteral("gradientOpacitySlider"));
    m_opacitySlider->setRange(10, 1000);
    m_opacitySlider->setFixedWidth(100);
    connect(m_opacitySlider, &QSlider::valueChanged, this,
            [this](int value) { changeGradient(m_session, [value](GradientSettings &gradient) { gradient.opacity = value / 1000.0; }); });
    m_opacity->setObjectName(QStringLiteral("gradientOpacity"));
    m_opacity->setFixedWidth(42);
    m_opacity->setToolTip(QStringLiteral("Press 1–9 for 10–90%, 0 for 100%"));
    m_mask->setObjectName(QStringLiteral("gradientMaskNote"));
    m_mask->setForegroundRole(QPalette::PlaceholderText);
    m_cancel->setObjectName(QStringLiteral("gradientCancel"));
    m_apply->setObjectName(QStringLiteral("gradientApply"));
    connect(m_cancel, &QPushButton::clicked, this, [this] { m_session.cancelGradient(); });
    connect(m_apply, &QPushButton::clicked, this, [this] { m_session.commitGradient(); });
    for (QWidget *widget : std::initializer_list<QWidget *>{m_linear, m_radial, m_swatch, m_style, m_reverse, new QLabel(QStringLiteral("Opacity"), this),
                                                             m_opacitySlider, m_opacity, new QLabel(QStringLiteral("%"), this)})
        row->insertWidget(row->count() - 1, widget);
    for (QWidget *widget : std::initializer_list<QWidget *>{m_mask, m_cancel, m_apply})
        row->addWidget(widget);
    connect(&m_session, &EditorSession::changed, this, &GradientControls::synchronize);
    synchronize();
}

// A segment of Swift's shape picker.
QToolButton *GradientControls::shape(const QString &name, const QString &text, bool radial)
{
    auto *button = new QToolButton(this);
    button->setObjectName(name);
    button->setText(text);
    button->setCheckable(true);
    button->setToolTip(QStringLiteral("Linear runs along the line; Radial spreads out from the start point"));
    m_shapes->addButton(button);
    connect(button, &QToolButton::clicked, this, [this, radial] {
        changeGradient(m_session, [radial](GradientSettings &gradient) { gradient.shape = radial ? GradientShape::radial : GradientShape::linear; });
    });
    return button;
}

void GradientControls::synchronize()
{
    const GradientSettings &gradient = m_session.gradientSettings();
    (gradient.shape == GradientShape::radial ? m_radial : m_linear)->setChecked(true);
    m_style->setCurrentIndex(int(gradient.style));
    m_reverse->setChecked(gradient.reversed);
    // The slider shows the session's number without writing back.
    const QSignalBlocker blocker(m_opacitySlider);
    m_opacitySlider->setValue(int(std::lround(gradient.opacity * 1000)));
    m_opacity->sync(int(std::lround(gradient.opacity * 100)));
    m_mask->setVisible(m_session.isMaskSelected());
    m_cancel->setVisible(m_session.gradientEdit().has_value());
    m_apply->setVisible(m_session.gradientEdit().has_value());
    m_swatch->update();
    setEnabled(!m_session.showsBusy() && m_session.document().has_value());
}
