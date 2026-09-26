#include "UI/LayerAppearanceControls.h"
#include "UI/BlendModePicker.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QLabel>
#include <QVBoxLayout>
#include <cmath>

namespace {
QLabel *caption(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    QFont font = label->font();
    font.setPixelSize(10);
    label->setFont(font);
    return label;
}
}

LayerAppearanceControls::LayerAppearanceControls(EditorSession &session, QWidget *parent)
    : QWidget(parent), m_session(session), m_picker(new BlendModePicker(session, this)), m_slider(new QSlider(Qt::Horizontal, this)),
      m_percentage(new QLineEdit(this))
{
    setObjectName(QStringLiteral("layerAppearance"));
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(12, 12, 12, 12);
    column->setSpacing(8);
    auto *blend = new QHBoxLayout;
    m_blendCaption = caption(QStringLiteral("Blend"), this);
    blend->addWidget(m_blendCaption);
    blend->addWidget(m_picker, 1);
    column->addLayout(blend);
    auto *opacity = new QHBoxLayout;
    opacity->setSpacing(6);
    opacity->addWidget(caption(QStringLiteral("Opacity"), this));
    // A thousandth a step; a drag is one undo step.
    m_slider->setRange(0, 1000);
    m_slider->setObjectName(QStringLiteral("opacitySlider"));
    m_slider->setAccessibleName(QStringLiteral("Opacity"));
    opacity->addWidget(m_slider, 1);
    m_percentage->setObjectName(QStringLiteral("opacityPercent"));
    m_percentage->setAccessibleName(QStringLiteral("Opacity percent"));
    m_percentage->setFixedWidth(44);
    m_percentage->installEventFilter(this);
    auto *field = new QHBoxLayout;
    field->setSpacing(2);
    field->addWidget(m_percentage);
    field->addWidget(caption(QStringLiteral("%"), this));
    opacity->addLayout(field);
    column->addLayout(opacity);
    // A release in a popup opened mid-drag is watched application-wide.
    connect(m_slider, &QSlider::sliderPressed, this, [this] {
        m_session.beginOpacityEdit();
        qApp->installEventFilter(this);
    });
    connect(m_slider, &QSlider::sliderReleased, this, [this] {
        qApp->removeEventFilter(this);
        m_session.finishOpacityEdit();
    });
    connect(m_slider, &QSlider::valueChanged, this, [this](int value) {
        if (!m_syncing)
            m_session.setLayerOpacity(value / 1000.0);
    });
    connect(&m_session, &EditorSession::changed, this, &LayerAppearanceControls::synchronize);
    synchronize();
}

LayerAppearanceControls::~LayerAppearanceControls()
{
    // Swift's onDisappear: a drag never outlives its controls.
    m_session.finishOpacityEdit();
}

void LayerAppearanceControls::synchronize()
{
    // A folder takes an opacity; blending rests with the picker.
    setEnabled(m_session.canEditOpacity());
    m_blendCaption->setEnabled(m_session.canEditAppearance());
    const std::optional<ImageLayer> active = m_session.activeLayer();
    const double opacity = active ? active->opacity : 1;
    m_syncing = true;
    m_slider->setValue(int(std::lround(opacity * 1000)));
    m_syncing = false;
    // Another layer makes the controls anew, as Swift's `.id` does.
    if (m_layerID != m_session.activeLayerID()) {
        // Leaving applies to the old layer only; it is gone.
        m_percentage->clearFocus();
        m_layerID = m_session.activeLayerID();
        sync();
    } else if (!m_percentage->hasFocus()) {
        sync();
    }
}

void LayerAppearanceControls::step(double percent)
{
    if (m_session.activeLayerID() != m_layerID)
        return;
    // The session clamps; Swift's own clamp is left out.
    m_session.setLayerOpacity(percent / 100);
    sync();
}

// Leaving applies the text once, through the FocusOut.
void LayerAppearanceControls::releaseFocus()
{
    m_percentage->clearFocus();
    m_session.requestCanvasFocus();
}

void LayerAppearanceControls::sync()
{
    const std::optional<ImageLayer> active = m_session.activeLayer();
    m_percentage->setText(QString::number(std::lround((active ? active->opacity : 1) * 100)));
}

void LayerAppearanceControls::applyPercentage()
{
    if (m_session.activeLayerID() != m_layerID)
        return;
    bool number = false;
    const double value = m_percentage->text().toDouble(&number);
    // The session refuses a value that is no finite number.
    if (number)
        m_session.setLayerOpacity(value / 100);
    sync();
}

bool LayerAppearanceControls::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_percentage) {
        const bool release = event->type() == QEvent::MouseButtonRelease && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton;
        if (release && m_slider->isSliderDown())
            m_slider->setSliderDown(false);
        return false;
    }
    if (event->type() == QEvent::FocusOut) {
        // A menu borrows focus and gives it back: no leaving.
        const Qt::FocusReason reason = static_cast<QFocusEvent *>(event)->reason();
        if (reason != Qt::MenuBarFocusReason && reason != Qt::PopupFocusReason)
            applyPercentage();
        return false;
    }
    if (event->type() != QEvent::KeyPress)
        return QWidget::eventFilter(watched, event);
    const auto *key = static_cast<QKeyEvent *>(event);
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter || key->key() == Qt::Key_Escape) {
        releaseFocus();
        return true;
    }
    if (key->key() != Qt::Key_Up && key->key() != Qt::Key_Down)
        return QWidget::eventFilter(watched, event);
    // Up and Down nudge one percent, ten with Shift.
    const std::optional<ImageLayer> active = m_session.activeLayer();
    const double amount = (key->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1) * (key->key() == Qt::Key_Up ? 1 : -1);
    step(std::round((active ? active->opacity : 1) * 100) + amount);
    return true;
}
