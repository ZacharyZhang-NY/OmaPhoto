#pragma once
#include "Document/ProjectWorkspace.h"
#include "IO/ImageExporter.h"
#include "Rendering/EditorCanvas.h"
#include "UI/CompositorMenus.h"
#include "UI/OmarchyTheme.h"
#include "UI/ProjectWorkspaceView.h"
#include <QDir>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>

// The acceptance run drives the app's own window.
struct App {
    QTemporaryDir files;
    // As the app starts: the theme before the window.
    OmarchyTheme theme{files.filePath(QStringLiteral("omarchy"))};
    ProjectWorkspace workspace;
    ProjectWorkspaceView window{workspace};
    App()
    {
        window.resize(1180, 780);
        window.show();
        if (!QTest::qWaitForWindowActive(&window))
            throw std::runtime_error("the window never became active");
    }
    EditorSession &session() { return workspace.current().session; }
    CanvasView &canvas() { return *window.findChild<CanvasView *>(); }
    QAction &action(const char *name)
    {
        QAction *found = window.findChild<CompositorMenus *>()->action(QString::fromLatin1(name));
        if (!found)
            throw std::runtime_error(std::string("no entry named ") + name);
        return *found;
    }
    // A document point where the canvas shows it.
    QPoint at(QPointF point)
    {
        return session().viewport.viewPoint(point, session().document().value().size()).toPoint();
    }
    void press(QPointF point, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QTest::mousePress(&canvas(), Qt::LeftButton, modifiers, at(point));
    }
    void move(QPointF point)
    {
        const QPointF view = at(point);
        QMouseEvent event(QEvent::MouseMove, view, view, canvas().mapToGlobal(view.toPoint()), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&canvas(), &event);
    }
    void release(QPointF point) { QTest::mouseRelease(&canvas(), Qt::LeftButton, Qt::NoModifier, at(point)); }
    // A drag in eight steps, as a hand makes it.
    void drag(QPointF from, QPointF to)
    {
        press(from);
        for (int step = 1; step <= 8; ++step)
            move(from + (to - from) * (step / 8.0));
        release(to);
    }
    // Waits for the project to be free again.
    void settle() { QTRY_VERIFY_WITH_TIMEOUT(!session().isProjectBusy(), 30000); }
    QString path(const QString &name) const { return files.filePath(name); }
    // The window and its shown panels, saved when asked for.
    void shot(const QString &name)
    {
        const QByteArray folder = qgetenv("ACCEPTANCE_SHOTS");
        if (folder.isEmpty())
            return;
        // Posted geometry and previews land first.
        QTest::qWait(300);
        QImage image = window.grab().toImage();
        QPainter painter(&image);
        for (QWidget *panel : QApplication::topLevelWidgets())
            if (panel != &window && panel->isVisible() && panel->parentWidget() && panel->parentWidget()->window() == &window)
                painter.drawPixmap(panel->geometry().topLeft() - window.geometry().topLeft(), panel->grab());
        painter.end();
        QDir().mkpath(QString::fromLocal8Bit(folder));
        if (!image.save(QDir(QString::fromLocal8Bit(folder)).filePath(name + QStringLiteral(".png"))))
            throw std::runtime_error("the screenshot could not be written");
    }
    // The document as an export draws it.
    QColor shown(int x, int y) { return ImageExporter::render(session().projectSnapshot().value()).image.pixelColor(x, y); }
    void importFile(const QString &file)
    {
        bool done = false;
        session().importImages({QUrl::fromLocalFile(file)}, std::nullopt, [&done] { done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 30000);
    }
};

// A photograph's stand-in: sky, ground and a red disc.
inline QImage scene(int width, int height)
{
    QImage image(width, height, QImage::Format_RGB32);
    QPainter painter(&image);
    QLinearGradient sky(0, 0, 0, height);
    sky.setColorAt(0, QColor(40, 90, 200));
    sky.setColorAt(1, QColor(170, 210, 250));
    painter.fillRect(image.rect(), sky);
    painter.fillRect(QRect(0, height * 3 / 4, width, height / 4), QColor(60, 140, 70));
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(220, 30, 30));
    painter.drawEllipse(QPointF(width / 2.0, height / 2.0), height / 4.0, height / 4.0);
    return image;
}

// A layer's pixel at a document point; clear outside it.
inline QColor pixel(const EditorSession &session, QUuid id, int x, int y)
{
    for (const ImageLayer &layer : session.document().value().layers) {
        if (layer.id != id)
            continue;
        const QImage image = layer.asset.value().image();
        const QPointF unit = (QPointF(x + 0.5, y + 0.5) - layer.transform.origin);
        const QPoint at(int(std::floor(unit.x() * image.width() / layer.transform.size.width())),
                        int(std::floor(unit.y() * image.height() / layer.transform.size.height())));
        return image.rect().contains(at) ? image.pixelColor(at) : QColor(0, 0, 0, 0);
    }
    throw std::runtime_error("no such layer");
}
