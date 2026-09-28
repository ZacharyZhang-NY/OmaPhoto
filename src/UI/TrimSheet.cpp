#include "UI/TrimSheet.h"
#include "UI/KeyboardShortcuts.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>
#include <algorithm>

namespace {
QLabel *text(const QString &words, int pixels, QFont::Weight weight, QWidget *parent)
{
    auto *label = new QLabel(words, parent);
    label->setTextFormat(Qt::PlainText);
    QFont font = label->font();
    font.setPixelSize(pixels);
    font.setWeight(weight);
    label->setFont(font);
    return label;
}

QFrame *divider(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setForegroundRole(QPalette::Mid);
    return line;
}
}

TrimSheet::TrimSheet(std::function<void(std::optional<TrimOptions>)> finish, QWidget *parent)
    : QWidget(parent), m_finish(std::move(finish)), m_basedOn(new QButtonGroup(this)), m_ok(new QPushButton(QStringLiteral("OK"), this))
{
    setFixedWidth(320);
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(24, 24, 24, 24);
    column->setSpacing(18);
    column->addWidget(text(QStringLiteral("Trim"), 17, QFont::Bold, this));

    // Swift's radio group, its label hidden behind the headline.
    auto *basis = new QVBoxLayout;
    basis->setSpacing(8);
    basis->addWidget(text(QStringLiteral("Based On"), 13, QFont::DemiBold, this));
    for (const TrimBasedOn option : allTrimBasedOn) {
        auto *radio = new QRadioButton(rawValue(option), this);
        radio->setObjectName(QStringLiteral("trimBasedOn%1").arg(int(option)));
        radio->setChecked(option == TrimBasedOn::transparentPixels);
        m_basedOn->addButton(radio, int(option));
        basis->addWidget(radio);
    }
    column->addLayout(basis);
    column->addWidget(divider(this));

    auto *away = new QVBoxLayout;
    away->setSpacing(8);
    away->addWidget(text(QStringLiteral("Trim Away"), 13, QFont::DemiBold, this));
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(24);
    grid->setVerticalSpacing(8);
    const std::array<QString, 4> names = {QStringLiteral("Top"), QStringLiteral("Bottom"), QStringLiteral("Left"), QStringLiteral("Right")};
    for (size_t index = 0; index < names.size(); ++index) {
        m_edges[index] = new QCheckBox(names[index], this);
        m_edges[index]->setObjectName(QStringLiteral("trim") + names[index]);
        m_edges[index]->setChecked(true);
        grid->addWidget(m_edges[index], int(index / 2), int(index % 2));
        // OK rests while no edge is checked.
        connect(m_edges[index], &QCheckBox::toggled, this, [this] {
            m_ok->setEnabled(std::any_of(m_edges.begin(), m_edges.end(), [](const QCheckBox *edge) { return edge->isChecked(); }));
        });
    }
    grid->setColumnStretch(2, 1);
    away->addLayout(grid);
    column->addLayout(away);
    column->addWidget(divider(this));

    auto *cancel = new QPushButton(QStringLiteral("Cancel"), this);
    cancel->setObjectName(QStringLiteral("trimCancel"));
    cancel->setAutoDefault(false);
    connect(cancel, &QPushButton::clicked, this, [this] { m_finish(std::nullopt); });
    m_ok->setObjectName(QStringLiteral("trimOK"));
    m_ok->setDefault(true);
    // Swift's configuredNativeShortcut: Return and Escape, as remapped.
    NativeShortcut::bind(*this, m_ok, cancel);
    connect(m_ok, &QPushButton::clicked, this, [this] {
        m_finish(TrimOptions{.basedOn = TrimBasedOn(m_basedOn->checkedId()), .top = m_edges[0]->isChecked(), .bottom = m_edges[1]->isChecked(),
                             .left = m_edges[2]->isChecked(), .right = m_edges[3]->isChecked()});
    });
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(cancel);
    buttons->addStretch(1);
    buttons->addWidget(m_ok);
    column->addLayout(buttons);
}
