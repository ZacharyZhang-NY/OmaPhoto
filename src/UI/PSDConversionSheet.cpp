#include "UI/PSDConversionSheet.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {
QLabel *text(const QString &words, int pixels, QFont::Weight weight, QWidget *parent, bool secondary = false)
{
    auto *label = new QLabel(words, parent);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    QFont font = label->font();
    font.setPixelSize(pixels);
    font.setWeight(weight);
    label->setFont(font);
    if (secondary)
        label->setForegroundRole(QPalette::PlaceholderText);
    return label;
}

// Swift's `List`: each layer's name over its note.
QWidget *list(const std::vector<PSDConversion> &conversions, QWidget *parent)
{
    auto *scroll = new QScrollArea(parent);
    scroll->setObjectName(QStringLiteral("conversionList"));
    scroll->setWidgetResizable(true);
    auto *rows = new QWidget(scroll);
    auto *column = new QVBoxLayout(rows);
    for (const PSDConversion &item : conversions) {
        auto *row = new QVBoxLayout;
        row->setSpacing(4);
        row->setContentsMargins(8, 4, 8, 4);
        row->addWidget(text(item.layerName, 13, QFont::Bold, rows));
        row->addWidget(text(item.message, 13, QFont::Normal, rows));
        column->addLayout(row);
    }
    column->addStretch();
    scroll->setWidget(rows);
    scroll->setMinimumHeight(180);
    return scroll;
}
}

PSDConversionSheet::PSDConversionSheet(const PSDConversionRequest &request, std::function<void(bool)> finish, QWidget *parent) : QWidget(parent)
{
    setMinimumSize(520, 360);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(16);
    layout->addWidget(text(request.title, 17, QFont::Bold, this));
    layout->addWidget(text(request.isReading ? QStringLiteral("Reading the file to see what needs converting.")
                                             : QStringLiteral("OmaPhoto will convert these Photoshop features. Nothing is applied until you continue."),
                           13, QFont::Normal, this, true));
    if (request.isReading) {
        auto *reading = new QWidget(this);
        reading->setMinimumHeight(180);
        auto *row = new QHBoxLayout(reading);
        row->setSpacing(10);
        row->addStretch();
        auto *spinner = new QProgressBar(reading);
        spinner->setObjectName(QStringLiteral("conversionSpinner"));
        spinner->setRange(0, 0);
        spinner->setTextVisible(false);
        spinner->setFixedWidth(16);
        row->addWidget(spinner);
        row->addWidget(text(QStringLiteral("Reading the Photoshop file…"), 13, QFont::Normal, reading, true));
        row->addStretch();
        layout->addWidget(reading, 1);
    } else {
        layout->addWidget(list(request.conversions, this), 1);
    }
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    auto *cancel = new QPushButton(QStringLiteral("Cancel"), this);
    cancel->setObjectName(QStringLiteral("conversionCancel"));
    cancel->setAutoDefault(false);
    auto *confirm = new QPushButton(request.confirmTitle, this);
    confirm->setObjectName(QStringLiteral("conversionConfirm"));
    confirm->setDefault(true);
    confirm->setEnabled(!request.isReading);
    connect(cancel, &QPushButton::clicked, this, [finish] { finish(false); });
    connect(confirm, &QPushButton::clicked, this, [finish] { finish(true); });
    buttons->addWidget(cancel);
    buttons->addWidget(confirm);
    layout->addLayout(buttons);
}
