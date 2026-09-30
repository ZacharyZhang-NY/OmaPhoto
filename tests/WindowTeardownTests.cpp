#include "AcceptanceFixtures.h"
#include "UI/ColorPickerSheet.h"

// The window goes while a panel's field holds typing.
class WindowTeardownTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void theWindowGoesWhileAPanelFieldHoldsTyping_data();
    void theWindowGoesWhileAPanelFieldHoldsTyping();
};

namespace {
// The shown panel field to type into.
PickerField &field(const QString &name)
{
    for (QWidget *window : QApplication::topLevelWidgets())
        if (window->isVisible())
            for (PickerField *found : window->findChildren<PickerField *>())
                if (found->isVisible() && (name.isEmpty() || found->objectName() == name))
                    return *found;
    throw std::runtime_error("no shown panel field");
}
}

void WindowTeardownTests::theWindowGoesWhileAPanelFieldHoldsTyping_data()
{
    QTest::addColumn<QString>("entry");
    QTest::addColumn<QString>("name");
    QTest::newRow("colour picker") << QString() << QStringLiteral("r");
    for (const char *entry : {"levels", "hueSaturation", "gaussianBlur"})
        QTest::newRow(entry) << QString::fromLatin1(entry) << QString();
}

void WindowTeardownTests::theWindowGoesWhileAPanelFieldHoldsTyping()
{
    QFETCH(QString, entry);
    QFETCH(QString, name);
    auto app = std::make_unique<App>();
    QVERIFY(scene(120, 80).save(app->path("photo.png")));
    app->session().createDocument(120, 80);
    app->importFile(app->path("photo.png"));
    if (entry.isEmpty())
        app->session().openColorPicker(false);
    else
        app->action(entry.toLatin1().constData()).trigger();
    QTRY_VERIFY(QApplication::activeWindow() != &app->window);
    PickerField &typed = field(name);
    typed.setFocus();
    QTRY_VERIFY(typed.hasFocus());
    typed.selectAll();
    QTest::keyClicks(&typed, QStringLiteral("12"));
    QVERIFY(typed.isModified());
    // The panel commits as it hides, into a going window.
    app.reset();
    for (QWidget *window : QApplication::topLevelWidgets())
        QVERIFY(!window->isVisible());
}

QTEST_MAIN(WindowTeardownTests)
#include "WindowTeardownTests.moc"
