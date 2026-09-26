#pragma once
#include "Document/EditorSession.h"
#include "UI/HueSaturationSheet.h"
#include <QAccessible>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>
#include <QtTest>

// Shared by the sheet's tests: a sheet and its parts.
namespace {
// Red, yellow, green and blue: a hue for each range.
struct Sheet {
    EditorSession session;
    std::unique_ptr<HueSaturationSheet> sheet;
    Sheet()
    {
        session.createDocument(4, 1);
        QImage source(4, 1, QImage::Format_RGBA8888_Premultiplied);
        const QColor hues[] = {Qt::red, Qt::yellow, Qt::green, Qt::blue};
        for (int x = 0; x < 4; ++x)
            source.setPixelColor(x, 0, hues[x]);
        session.insert(ImportedImage(source, source, QStringLiteral("Hues")));
        session.beginHueSaturation();
        sheet = std::make_unique<HueSaturationSheet>(session);
    }
    template <typename Widget> Widget &child(const char *name) const
    {
        auto *found = sheet->findChild<Widget *>(QString::fromLatin1(name));
        if (!found)
            throw std::runtime_error(name);
        return *found;
    }
    template <typename Widget> Widget &titled(const QString &text) const
    {
        for (Widget *widget : sheet->findChildren<Widget *>()) {
            if (widget->text() == text)
                return *widget;
        }
        throw std::runtime_error(text.toStdString());
    }
    const HueSaturationSettings &settings() const { return session.hueSaturation().value().settings; }
    void set(const HueSaturationSettings &settings) { session.updateHueSaturation(settings, true); }
    bool show()
    {
        sheet->show();
        return QTest::qWaitForWindowActive(sheet.get());
    }
};

inline QString name(QWidget &widget)
{
    return QAccessible::queryAccessibleInterface(&widget)->text(QAccessible::Name);
}

// The name a label's relation gives, as Linux reads it.
inline QString labelled(QWidget &widget)
{
    const auto labels = QAccessible::queryAccessibleInterface(&widget)->relations(QAccessible::Label);
    return labels.size() == 1 ? labels.first().first->text(QAccessible::Name) : QString();
}

inline void type(QLineEdit &field, const QString &text)
{
    field.setFocus();
    field.selectAll();
    QTest::keyClicks(&field, text);
}

// A widget's drawing over white, without its parent's.
inline QImage drawn(QWidget &widget)
{
    QImage canvas(widget.size(), QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::white);
    widget.render(&canvas, QPoint(), QRegion(), QWidget::DrawChildren);
    return canvas;
}

// White under a quarter of the highlight, as Qt blends.
inline QColor tinted(const QColor &highlight)
{
    QImage blended(1, 1, QImage::Format_ARGB32_Premultiplied);
    blended.fill(Qt::white);
    QColor quarter = highlight;
    quarter.setAlphaF(0.25f);
    QPainter(&blended).fillRect(0, 0, 1, 1, quarter);
    return blended.pixelColor(0, 0);
}

inline void send(QWidget &widget, double x, QEvent::Type type, Qt::MouseButtons held, Qt::MouseButton button = Qt::LeftButton)
{
    const QPointF at(x, 6);
    QMouseEvent event(type, at, widget.mapToGlobal(at), type == QEvent::MouseMove ? Qt::NoButton : button, held, Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
}

// Counts a widget's paint events.
class Paints : public QObject {
public:
    explicit Paints(QWidget &widget) { widget.installEventFilter(this); }
    int count = 0;

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        count += event->type() == QEvent::Paint;
        return false;
    }
};
}
