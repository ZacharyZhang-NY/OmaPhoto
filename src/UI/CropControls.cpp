#include "UI/CropControls.h"
#include "Document/EditorSession.h"
#include <QComboBox>
#include <QLocale>
#include <QPushButton>

CropControls::CropControls(EditorSession &session, QWidget *parent)
    : ToolHeaderBar(QStringLiteral("Crop"), parent), m_session(session), m_ratio(new QComboBox(this)), m_size(new QLabel(this)),
      m_cancel(new QPushButton(QStringLiteral("Cancel"), this)), m_apply(new QPushButton(QStringLiteral("Apply Crop"), this))
{
    row->setSpacing(14);
    // Swift's labelled picker: label and menu share 170 points.
    auto *picker = new QWidget(this);
    picker->setFixedWidth(170);
    auto *pickerRow = new QHBoxLayout(picker);
    pickerRow->setContentsMargins(0, 0, 0, 0);
    auto *label = new QLabel(QStringLiteral("Ratio"), picker);
    label->setBuddy(m_ratio);
    pickerRow->addWidget(label);
    pickerRow->addWidget(m_ratio, 1);
    m_ratio->setObjectName(QStringLiteral("cropRatio"));
    for (const QString &choice : {QStringLiteral("Free"), QStringLiteral("Original"), QStringLiteral("1:1"), QStringLiteral("4:3"), QStringLiteral("16:9")})
        m_ratio->addItem(choice);
    // Swift's onChange: the frame takes a ratio that changed.
    connect(m_ratio, &QComboBox::activated, this, [this](int index) {
        if (m_ratio->itemText(index) == m_session.cropRatioChoice())
            return;
        m_session.setCropRatioChoice(m_ratio->itemText(index));
        m_session.changeCropRatio();
    });
    m_size->setObjectName(QStringLiteral("cropSize"));
    m_cancel->setObjectName(QStringLiteral("cropCancel"));
    m_apply->setObjectName(QStringLiteral("cropApply"));
    connect(m_cancel, &QPushButton::clicked, this, [this] { m_session.cancelCrop(); });
    connect(m_apply, &QPushButton::clicked, this, [this] { m_session.commitCrop(); });
    row->insertWidget(row->count() - 1, picker);
    row->insertWidget(row->count() - 1, m_size);
    row->addWidget(m_cancel);
    row->addWidget(m_apply);
    connect(&m_session, &EditorSession::changed, this, &CropControls::synchronize);
    synchronize();
}

void CropControls::synchronize()
{
    m_ratio->setCurrentText(m_session.cropRatioChoice());
    const std::optional<QRectF> &rect = m_session.cropRect();
    // Whole pixels, grouped as the status bar prints them.
    if (rect) {
        const QLocale english(QLocale::English, QLocale::UnitedStates);
        m_size->setText(QStringLiteral("%1 × %2 px").arg(english.toString(qint64(rect->width())), english.toString(qint64(rect->height()))));
    }
    m_size->setVisible(rect.has_value());
    m_cancel->setEnabled(rect.has_value());
    m_apply->setEnabled(rect.has_value());
    setEnabled(!m_session.showsBusy() && m_session.document().has_value());
}
