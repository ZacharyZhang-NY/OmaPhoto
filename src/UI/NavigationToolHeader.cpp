#include "UI/NavigationToolHeader.h"
#include <QKeyEvent>
#include <QRegularExpression>
#include <cmath>

NavigationToolHeader::NavigationToolHeader(EditorSession &session, QWidget *parent)
    : ToolHeaderBar(QString(), parent), m_session(session), m_zoom(new QLineEdit(this)), m_unit(new QLabel(QStringLiteral("%"), this))
{
    m_zoom->setObjectName(QStringLiteral("zoomPercentage"));
    m_zoom->setAccessibleName(QStringLiteral("Zoom percentage"));
    m_zoom->setToolTip(QStringLiteral("Zoom percentage (0.1–3200%). Press Return to apply."));
    m_zoom->setFixedWidth(72);
    m_zoom->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_zoom->installEventFilter(this);
    // The unit sits tight against its field: one value.
    auto *value = new QHBoxLayout;
    value->setSpacing(2);
    value->addWidget(m_zoom);
    value->addWidget(m_unit);
    row->insertLayout(1, value);
    connect(&m_session, &EditorSession::changed, this, &NavigationToolHeader::synchronize);
    synchronize();
}

void NavigationToolHeader::synchronize()
{
    const bool zooms = m_session.tool() == NavigationTool::zoom;
    title->setText(zooms ? QStringLiteral("Zoom") : QStringLiteral("Pan"));
    m_zoom->setVisible(zooms);
    m_unit->setVisible(zooms);
    m_zoom->setEnabled(m_session.document().has_value() && !m_session.showsBusy());
    if (!m_zoom->hasFocus())
        syncZoom();
}

void NavigationToolHeader::syncZoom()
{
    static const QRegularExpression trailingZeros(QStringLiteral("\\.?0+$"));
    m_displayed = QString::number(m_session.viewport.zoom() * 100, 'f', 2).remove(trailingZeros);
    m_zoom->setText(m_displayed);
}

void NavigationToolHeader::applyZoom()
{
    if (m_zoom->text() != m_displayed) {
        // No number reads as zero; "inf" the viewport refuses.
        const double value = m_zoom->text().trimmed().remove(QLatin1Char('%')).toDouble();
        if (value > 0 && !m_session.isProjectBusy())
            m_session.zoom(value / 100);
    }
    syncZoom();
}

void NavigationToolHeader::step(double percent)
{
    m_zoom->setText(QString::number(std::clamp(percent, 0.1, 3200.0), 'g', 6));
    applyZoom();
}

bool NavigationToolHeader::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::FocusOut)
        applyZoom();
    if (event->type() != QEvent::KeyPress)
        return ToolHeaderBar::eventFilter(watched, event);
    const auto *key = static_cast<QKeyEvent *>(event);
    // Return and Escape apply, then hand the canvas the keys.
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter || key->key() == Qt::Key_Escape) {
        applyZoom();
        m_zoom->clearFocus();
        m_session.requestCanvasFocus();
        return true;
    }
    if (key->key() != Qt::Key_Up && key->key() != Qt::Key_Down)
        return ToolHeaderBar::eventFilter(watched, event);
    // Up and Down step one percent, ten with Shift.
    static const QRegularExpression notNumeric(QStringLiteral("[^0-9.]"));
    bool isNumber = false;
    const double typed = m_zoom->text().remove(notNumeric).toDouble(&isNumber);
    const double amount = (key->modifiers() & Qt::ShiftModifier ? 10 : 1) * (key->key() == Qt::Key_Up ? 1 : -1);
    step((isNumber ? typed : m_session.viewport.zoom() * 100) + amount);
    return true;
}
