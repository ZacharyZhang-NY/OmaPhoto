#pragma once
#include "Document/EditorSession.h"
#include "UI/CameraRawRow.h"
#include "UI/ColorPickerSheet.h"
#include <QLabel>
#include <QStyleOptionSlider>
#include <QtTest>

// One Camera Raw row as Swift's view builds it.
struct RowCase {
    const char *name;
    const char *title;
    const char *help;
    double low;
    double high;
    double reset;
    // Swift's slider rounds to whole numbers.
    bool whole;
    // The field's width, none at zero.
    int field;
    int titleWidth;
    bool fixedTitle;
    bool coloured;
    std::function<double(const CameraRawSettings &)> read;
};

// The knob's middle, where a double click resets.
inline QPoint knob(const CameraRawSlider &slider)
{
    QStyleOptionSlider option;
    option.initFrom(&slider);
    option.orientation = Qt::Horizontal;
    option.minimum = slider.minimum();
    option.maximum = slider.maximum();
    option.sliderPosition = slider.sliderPosition();
    option.sliderValue = slider.value();
    return slider.style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, &slider).center();
}

// Checks a row's words, layout, writes, reset, typing and track.
inline void checkRow(QWidget &root, const EditorSession &session, const RowCase &row)
{
    const QString name = QString::fromLatin1(row.name);
    auto *slider = root.findChild<CameraRawSlider *>(name + QStringLiteral("Slider"));
    QVERIFY2(slider, row.name);
    auto *shown = qobject_cast<CameraRawRow *>(slider->parentWidget());
    QVERIFY2(shown, row.name);
    auto *title = shown->findChild<QLabel *>();
    const auto read = [&] { return row.read(session.filterEdit().value().settings.cameraRaw); };
    QCOMPARE(title->text(), QString::fromUtf8(row.title));
    QCOMPARE(title->toolTip(), QString::fromUtf8(row.help));
    QCOMPARE(slider->toolTip(), QString::fromUtf8(row.help));
    QCOMPARE(shown->accessibleName(), QString::fromUtf8(row.title));
    QCOMPARE(title->minimumWidth(), row.titleWidth);
    QVERIFY2(row.fixedTitle == (title->maximumWidth() == row.titleWidth), row.name);
    auto *field = shown->findChild<PickerField *>(name + QStringLiteral("Field"));
    QVERIFY2(bool(field) == (row.field > 0), row.name);
    if (field) {
        QCOMPARE(field->width(), row.field);
        QCOMPARE(field->placeholderText(), QString::fromUtf8(row.title));
        QCOMPARE(field->accessibleName(), QString::fromUtf8(row.title));
        QCOMPARE(field->toolTip(), QString::fromUtf8(row.help));
        QVERIFY(field->alignment().testFlag(Qt::AlignRight));
    }
    // A third of the travel, rounded where Swift rounds.
    slider->setValue(333);
    const double third = row.low + (row.high - row.low) * 0.333;
    QVERIFY2(std::abs(read() - (row.whole ? std::round(third) : third)) < 1e-9, qPrintable(name + QStringLiteral(" slides to ") + QString::number(read())));
    QTest::mouseDClick(slider, Qt::LeftButton, {}, knob(*slider));
    QVERIFY2(std::abs(read() - row.reset) < 1e-9, qPrintable(name + QStringLiteral(" resets to ") + QString::number(read())));
    // The row follows the model in its knob and field.
    QVERIFY2(std::abs(slider->shown() - row.reset) <= (row.high - row.low) / 1000, qPrintable(name + QStringLiteral(" shows ") + QString::number(slider->shown())));
    if (field)
        QCOMPARE(field->text(), field->locale().toString(row.reset, 'f', 0));
    if (field) {
        // A typed number applies as typed, unrounded.
        const double typed = row.low + (row.high - row.low) * 0.625 + 0.25;
        field->setFocus();
        field->selectAll();
        QTest::keyClicks(field, field->locale().toString(typed, 'f', 2));
        QTest::keyClick(field, Qt::Key_Return);
        QVERIFY2(std::abs(read() - typed) < 1e-9, qPrintable(name + QStringLiteral(" types ") + QString::number(read())));
    }
    // A coloured track shows colour past the knob, plain gray.
    if (row.reset < row.high) {
        QTest::mouseDClick(slider, Qt::LeftButton, {}, knob(*slider));
        const QImage drawn = slider->grab().toImage();
        const QColor end = drawn.pixelColor(drawn.width() - 6, drawn.height() / 2);
        const bool gray = std::abs(end.red() - end.green()) < 12 && std::abs(end.green() - end.blue()) < 12;
        QVERIFY2(gray != row.coloured, qPrintable(name + QStringLiteral(" track ") + end.name()));
    }
}
