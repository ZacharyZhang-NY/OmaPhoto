#pragma once
#include <QAbstractButton>
#include <QApplication>
#include <QFileDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QDir>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <functional>

// Answers, in order, the dialogs the code under test opens.
class DialogDesk : public QObject {
public:
    // A button's text, a path, "<cancel>", "<escape>" or "<return>".
    QStringList replies;
    // What each dialog showed when it was answered.
    QStringList seen;
    // A state worth noting at that moment; may stay empty.
    std::function<QString()> note;
    // Typed into a panel's name field before "<cancel>".
    QString typeBeforeCancel;

    DialogDesk()
    {
        // Panels start in their own folder, never the tree.
        QDir::setCurrent(m_folder.path());
        connect(&m_timer, &QTimer::timeout, this, &DialogDesk::look);
        m_timer.start(0);
    }
    ~DialogDesk() override { QDir::setCurrent(QDir::tempPath()); }

private:
    // One dialog a tick: an answer may delete listed widgets.
    void look()
    {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            auto *alert = qobject_cast<QMessageBox *>(widget);
            auto *panel = qobject_cast<QFileDialog *>(widget);
            if ((!alert && !panel) || !widget->isVisible() || widget->property("answered").toBool())
                continue;
            widget->setProperty("answered", true);
            const QString reply = replies.isEmpty() ? QStringLiteral("<unexpected>") : replies.takeFirst();
            const QString state = note ? QStringLiteral("|") + note() : QString();
            if (alert)
                answer(alert, reply, state);
            else
                answer(panel, reply, state);
            return;
        }
    }

    void answer(QMessageBox *alert, const QString &reply, const QString &state)
    {
        QStringList buttons;
        for (const QAbstractButton *button : alert->buttons())
            buttons << button->text();
        seen << QStringLiteral("alert|%1|%2|%3|%4").arg(alert->icon()).arg(alert->text(), alert->informativeText(), buttons.join(',')) + state;
        // Keys reach the button whose role takes them.
        if (reply == QLatin1String("<escape>") || reply == QLatin1String("<return>")) {
            QTest::keyClick(alert, reply == QLatin1String("<escape>") ? Qt::Key_Escape : Qt::Key_Return);
            return;
        }
        for (QAbstractButton *button : alert->buttons()) {
            if (button->text() == reply) {
                button->click();
                return;
            }
        }
        seen << QStringLiteral("no button named ") + reply;
        alert->reject();
    }

    void answer(QFileDialog *panel, const QString &reply, const QString &state)
    {
        auto *name = panel->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"));
        seen << QStringLiteral("panel|%1|%2|%3|%4|%5")
                    .arg(panel->windowTitle(), panel->acceptMode() == QFileDialog::AcceptSave ? "save" : "open",
                         panel->fileMode() == QFileDialog::Directory ? "folder" : "file", name ? name->text() : QStringLiteral("<no field>"),
                         panel->defaultSuffix())
                + state;
        if (!name || reply == QLatin1String("<cancel>") || reply == QLatin1String("<unexpected>")) {
            if (name && !typeBeforeCancel.isEmpty())
                name->setText(typeBeforeCancel);
            panel->reject();
            return;
        }
        // Typed like a user; several files quoted, as panels read.
        const QStringList paths = reply.split(QLatin1Char('|'));
        name->setText(paths.size() == 1 ? reply : QStringLiteral("\"") + paths.join(QStringLiteral("\" \"")) + QStringLiteral("\""));
        static_cast<QDialog *>(panel)->accept();
    }

    QTemporaryDir m_folder;
    QTimer m_timer;
};
