#include "Document/ProjectWorkspace.h"
#include "Logging.h"
#include "UI/OmarchyTheme.h"
#include "UI/ProjectWorkspaceView.h"
#include "UI/SliderSnap.h"
#include <QApplication>
#include <QUrl>

int main(int argc, char **argv)
{
    qSetMessagePattern(QStringLiteral("%{time yyyy-MM-dd hh:mm:ss.zzz} %{type} %{category}: %{message}"));
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("OmaPhoto"));
    QApplication::setApplicationVersion(QStringLiteral(OMAPHOTO_VERSION));
    // The desktop entry's name: icons and windows find each other.
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.ZacharyZhang_NY.OmaPhoto"));
    qCInfo(lcApp).noquote() << "OmaPhoto" << OMAPHOTO_VERSION << "on Qt" << qVersion()
                  << "platform" << QGuiApplication::platformName();
    // The desktop's colours, retinted when the theme switches.
    OmarchyTheme theme;
    // Slider knobs snap to a click on the track.
    SliderSnap::install();
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    window.show();
    // Files named at launch, as Finder's Open With gives them.
    QList<QUrl> files;
    for (const QString &argument : QApplication::arguments().mid(1))
        files << QUrl::fromLocalFile(argument);
    if (!files.isEmpty())
        workspace.receive(files);
    return QApplication::exec();
}
