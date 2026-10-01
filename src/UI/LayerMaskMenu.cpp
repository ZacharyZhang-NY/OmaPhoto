#include "UI/LayerMaskMenu.h"
#include "UI/LayerIcons.h"
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QPainter>

LayerMaskMenu::LayerMaskMenu(EditorSession &session, QWidget *parent) : QToolButton(parent), m_session(session)
{
    setObjectName(QStringLiteral("addLayerMask"));
    setAccessibleName(QStringLiteral("Add layer mask"));
    setAutoRaise(true);
    setFixedSize(32, 40);
    // Swift reads Option from the event that clicked.
    connect(this, &QToolButton::clicked, this, [this] { m_session.addMask(!QGuiApplication::keyboardModifiers().testFlag(Qt::AltModifier)); });
    connect(&m_session, &EditorSession::changed, this, &LayerMaskMenu::synchronize);
    synchronize();
}

void LayerMaskMenu::synchronize()
{
    const std::optional<ImageLayer> active = m_session.activeLayer();
    setEnabled(m_session.canEditMask() && !(active && active->mask));
    setToolTip(m_session.selection() ? QStringLiteral("Add layer mask revealing the selection (Alt-click to hide it)")
                                     : QStringLiteral("Add layer mask (Alt-click for a black mask)"));
    setIcon(LayerIcons::pixmap(LayerIcon::addMask, 16, palette().color(QPalette::PlaceholderText), devicePixelRatio()));
}

void LayerMaskMenu::changeEvent(QEvent *event)
{
    QToolButton::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        synchronize();
}

MaskAloneBadge::MaskAloneBadge(EditorSession &session, QWidget *parent) : QWidget(parent), m_name(new QLabel(this))
{
    setObjectName(QStringLiteral("maskAloneBadge"));
    setFixedHeight(26);
    const auto font = [](QLabel *label, bool semibold, double alpha) {
        QFont face = label->font();
        face.setPixelSize(12);
        face.setWeight(semibold ? QFont::DemiBold : QFont::Normal);
        label->setFont(face);
        QPalette colours = label->palette();
        colours.setColor(QPalette::WindowText, QColor::fromRgbF(1, 1, 1, alpha));
        label->setPalette(colours);
    };
    auto *icon = new QLabel(this);
    icon->setPixmap(LayerIcons::pixmap(LayerIcon::addMask, 11, Qt::white, devicePixelRatioF()));
    auto *title = new QLabel(QStringLiteral("Layer Mask"), this);
    title->setObjectName(QStringLiteral("maskAloneTitle"));
    font(title, true, 1);
    m_name->setObjectName(QStringLiteral("maskAloneName"));
    m_name->setTextFormat(Qt::PlainText);
    m_name->setMaximumWidth(220);
    font(m_name, false, 0.6);
    auto *close = new QToolButton(this);
    close->setObjectName(QStringLiteral("maskAloneClose"));
    close->setAutoRaise(true);
    close->setIcon(LayerIcons::pixmap(LayerIcon::xmark, 9, Qt::white, devicePixelRatioF()));
    close->setToolTip(QStringLiteral("Show the image again (or Alt-click the mask thumbnail)"));
    close->setAccessibleName(QStringLiteral("Stop viewing the mask"));
    connect(close, &QToolButton::clicked, this, [&session] { session.setViewsMaskAlone(false); });
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(11, 0, 8, 0);
    row->setSpacing(7);
    for (QWidget *widget : std::initializer_list<QWidget *>{icon, title, m_name, close})
        row->addWidget(widget);
}

// Swift's byTruncatingTail within 220 points.
void MaskAloneBadge::setLayerName(const QString &name)
{
    m_name->setText(m_name->fontMetrics().elidedText(name, Qt::ElideRight, 220));
}

void MaskAloneBadge::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor::fromRgbF(1, 1, 1, 0.14), 1));
    painter.setBrush(QColor::fromRgbF(0, 0, 0, 0.75));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 13, 13);
}
