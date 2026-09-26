#pragma once
#include "LevelsFixtures.h"
#include "UI/LevelsSheet.h"
#include <QAccessible>
#include <QApplication>
#include <QLineEdit>

// Shared by the sheet's tests: a sheet and its parts.
namespace {
// Gray 64 twice, a warm pixel and 65: known peaks.
struct Sheet {
    EditorSession session;
    std::unique_ptr<LevelsSheet> sheet;
    Sheet()
    {
        session.createDocument(4, 1);
        const QImage source = image({{64, 64, 64, 255}, {64, 64, 64, 255}, {192, 96, 0, 255}, {65, 65, 65, 255}});
        session.insert(ImportedImage(source, source, QStringLiteral("Four")));
        session.beginLevels();
        sheet = std::make_unique<LevelsSheet>(session);
    }
    bool show()
    {
        sheet->show();
        return QTest::qWaitForWindowActive(sheet.get()) && QTest::qWaitFor([this] { return session.levels().value().histogramReady; });
    }
    template <typename Widget> Widget &child(const char *name) const
    {
        auto *found = sheet->findChild<Widget *>(QString::fromLatin1(name));
        if (!found)
            throw std::runtime_error(name);
        return *found;
    }
    QPushButton &button(const QString &text) const
    {
        for (QPushButton *button : sheet->findChildren<QPushButton *>()) {
            if (button->text() == text)
                return *button;
        }
        throw std::runtime_error(text.toStdString());
    }
    QLabel &label(const QString &text) const
    {
        for (QLabel *label : sheet->findChildren<QLabel *>()) {
            if (label->text() == text)
                return *label;
        }
        throw std::runtime_error(text.toStdString());
    }
    // A handle sits on the sheet itself, over its track.
    QWidget &handle(const QString &name) const
    {
        for (QWidget *widget : sheet->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
            if (widget->accessibleName() == name)
                return *widget;
        }
        throw std::runtime_error(name.toStdString());
    }
    LevelRange range() const { return session.levels().value().settings.current(); }
    void set(const LevelRange &range)
    {
        LevelsSettings settings = session.levels().value().settings;
        settings.setCurrent(range);
        session.updateLevels(settings, false);
    }
};

// Moves a pressed handle to a tone along its track.
inline void drag(QWidget &handle, const QWidget &track, double tone, Qt::MouseButtons buttons = Qt::LeftButton)
{
    const QPointF at(track.x() + tone / 255 * track.width() - handle.x(), 10);
    QMouseEvent event(QEvent::MouseMove, at, at, handle.mapToGlobal(at), Qt::NoButton, buttons, Qt::NoModifier);
    QApplication::sendEvent(&handle, &event);
}

inline void type(QLineEdit &field, const QString &text)
{
    field.setFocus();
    field.selectAll();
    QTest::keyClicks(&field, text);
}

// The strongest pixel of an icon, and its colour.
inline QColor ink(const QIcon &icon)
{
    const QImage drawn = icon.pixmap(QSize(14, 14)).toImage();
    QColor strongest(Qt::transparent);
    for (int y = 0; y < drawn.height(); ++y) {
        for (int x = 0; x < drawn.width(); ++x) {
            if (drawn.pixelColor(x, y).alpha() > strongest.alpha())
                strongest = drawn.pixelColor(x, y);
        }
    }
    return strongest;
}

inline QString name(QWidget &widget)
{
    return QAccessible::queryAccessibleInterface(&widget)->text(QAccessible::Name);
}

// Unites the regions a widget repaints.
class Repaints : public QObject {
public:
    explicit Repaints(QWidget &widget) { widget.installEventFilter(this); }
    QRegion painted;

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::Paint)
            painted += static_cast<QPaintEvent *>(event)->region();
        return false;
    }
};

// The window's colour under a black quarter, as Qt blends.
inline QColor shaded(const QColor &window)
{
    QImage blended(1, 1, QImage::Format_ARGB32_Premultiplied);
    blended.fill(window);
    QPainter(&blended).fillRect(0, 0, 1, 1, QColor(0, 0, 0, 64));
    return blended.pixelColor(0, 0);
}
}
