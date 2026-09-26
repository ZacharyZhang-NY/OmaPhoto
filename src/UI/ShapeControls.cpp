#include "UI/ShapeControls.h"
#include "Document/EditorSession.h"
#include "UI/ColorPaletteControls.h"
#include "UI/LassoControls.h"
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <cmath>

ShapeControls::ShapeControls(EditorSession &session, QWidget *parent)
    : ToolHeaderBar(QStringLiteral("Shape"), parent), m_session(session), m_kinds(new QButtonGroup(this)),
      m_kindButtons{kind(ShapeKind::rectangle), kind(ShapeKind::ellipse), kind(ShapeKind::line)},
      m_widthSlider(new QSlider(Qt::Horizontal, this)),
      m_widthField(new SelectionAmountField(session, 1, 5000, [this] { return m_session.shapeLineWidth(); },
                                            [this](double width) { m_session.setShapeLineWidth(width); }, this)),
      m_width(amount(QStringLiteral("Width"), m_widthSlider, m_widthField)), m_radiusSlider(new QSlider(Qt::Horizontal, this)),
      m_radiusField(new SelectionAmountField(session, 0, 5000, [this] { return m_session.shapeCornerRadius(); },
                                             [this](double radius) { m_session.setShapeCornerRadius(radius); }, this)),
      m_radius(amount(QStringLiteral("Radius"), m_radiusSlider, m_radiusField)),
      m_fill(new SwatchButton([&session] { return session.foregroundColor(); }, 3, 0, 0.5, this))
{
    m_widthSlider->setObjectName(QStringLiteral("shapeWidthSlider"));
    m_widthSlider->setRange(1, 100);
    connect(m_widthSlider, &QSlider::valueChanged, this, [this](int value) { m_session.setShapeLineWidth(value); });
    m_widthField->setObjectName(QStringLiteral("shapeWidth"));
    m_width->setObjectName(QStringLiteral("shapeWidthGroup"));
    m_radiusSlider->setObjectName(QStringLiteral("shapeRadiusSlider"));
    m_radiusSlider->setRange(0, 200);
    connect(m_radiusSlider, &QSlider::valueChanged, this, [this](int value) { m_session.setShapeCornerRadius(value); });
    m_radiusField->setObjectName(QStringLiteral("shapeRadius"));
    m_radius->setObjectName(QStringLiteral("shapeRadiusGroup"));
    m_radius->setToolTip(QStringLiteral("Round the rectangle's corners by this many pixels; 0 keeps them square"));
    m_fill->setObjectName(QStringLiteral("shapeFill"));
    m_fill->setFixedSize(36, 18);
    m_fill->setToolTip(QStringLiteral("Shapes fill with the foreground color; click to change it"));
    connect(m_fill, &QAbstractButton::clicked, this, [this] { m_session.openColorPicker(false); });
    auto *fill = new QWidget(this);
    auto *fillRow = new QHBoxLayout(fill);
    fillRow->setContentsMargins(0, 0, 0, 0);
    fillRow->setSpacing(6);
    fillRow->addWidget(new QLabel(QStringLiteral("Fill"), fill));
    fillRow->addWidget(m_fill);
    for (QWidget *widget : std::initializer_list<QWidget *>{m_kindButtons[0], m_kindButtons[1], m_kindButtons[2], m_width, m_radius, fill})
        row->insertWidget(row->count() - 1, widget);
    connect(&m_session, &EditorSession::changed, this, &ShapeControls::synchronize);
    synchronize();
}

// A segment of Swift's picker; choosing drops a draft first.
QToolButton *ShapeControls::kind(ShapeKind value)
{
    auto *button = new QToolButton(this);
    button->setObjectName(QStringLiteral("shape") + rawValue(value));
    button->setText(rawValue(value));
    button->setCheckable(true);
    button->setToolTip(QStringLiteral("Shift-U (or Tab) steps through Rectangle, Ellipse and Line"));
    m_kinds->addButton(button);
    connect(button, &QToolButton::clicked, this, [this, value] {
        m_session.cancelShape();
        m_session.setShapeKind(value);
    });
    return button;
}

QWidget *ShapeControls::amount(const QString &name, QSlider *slider, SelectionAmountField *field)
{
    auto *group = new QWidget(this);
    auto *layout = new QHBoxLayout(group);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    slider->setFixedWidth(100);
    field->setFixedWidth(48);
    for (QWidget *widget : std::initializer_list<QWidget *>{new QLabel(name, group), slider, field, new QLabel(QStringLiteral("px"), group)})
        layout->addWidget(widget);
    return group;
}

void ShapeControls::synchronize()
{
    const ShapeKind kind = m_session.shapeKind();
    m_kindButtons[size_t(kind)]->setChecked(true);
    m_width->setVisible(kind == ShapeKind::line);
    m_radius->setVisible(kind == ShapeKind::rectangle);
    // The sliders show the session's number without writing back.
    const QSignalBlocker widthBlocker(m_widthSlider), radiusBlocker(m_radiusSlider);
    m_widthSlider->setValue(int(std::lround(std::min(100.0, m_session.shapeLineWidth()))));
    m_radiusSlider->setValue(int(std::lround(std::min(200.0, m_session.shapeCornerRadius()))));
    m_widthField->sync(int(std::lround(m_session.shapeLineWidth())));
    m_radiusField->sync(int(std::lround(m_session.shapeCornerRadius())));
    m_fill->update();
    setEnabled(!m_session.showsBusy() && m_session.document().has_value());
}
