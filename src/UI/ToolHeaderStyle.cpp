#include "UI/ToolHeaderStyle.h"
#include <QApplication>

QFont ToolHeaderStyle::titleFont()
{
    QFont font = QApplication::font();
    font.setPixelSize(13);
    font.setWeight(QFont::DemiBold);
    return font;
}

QFont ToolHeaderStyle::controlFont()
{
    QFont font = QApplication::font();
    font.setPixelSize(12);
    return font;
}

ToolHeaderBar::ToolHeaderBar(const QString &text, QWidget *parent)
    : QWidget(parent), title(new QLabel(text, this)), row(new QHBoxLayout(this))
{
    setFixedHeight(ToolHeaderStyle::height);
    setFont(ToolHeaderStyle::controlFont());
    title->setFont(ToolHeaderStyle::titleFont());
    row->setContentsMargins(18, 0, 18, 0);
    row->setSpacing(12);
    row->addWidget(title);
    row->addStretch(1);
}
