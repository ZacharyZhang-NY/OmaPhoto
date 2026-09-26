#include "TypeControlsFixtures.h"
#include "Rendering/TextLayout.h"
#include "UI/TypeControls.h"
#include <QAbstractItemView>
#include <QComboBox>
#include <QStyleOptionComboBox>
#include <QtTest>

// The Type bar's font menu: loaded late, cut short.
class TypeFontPickerTests : public QObject {
    Q_OBJECT
private slots:
    void aFaceIsChosenKeptAndCutShort();
    void theFaceShownChangesNothing();
};

void TypeFontPickerTests::aFaceIsChosenKeptAndCutShort()
{
    Bar shown;
    auto &font = find<QComboBox>(shown.bar, "typeFont");
    // Open text on a layer: a face restyles its draft.
    addText(shown.session, QStringLiteral("Face"));
    shown.session.editActiveText();
    QVERIFY(shown.session.textDraft().has_value());
    font.showPopup();
    font.hidePopup();
    // A face writes its name; the loaded list keeps Helvetica.
    const int bold = font.findText(QStringLiteral("DejaVuSans-Bold"));
    QVERIFY(bold >= 0);
    emit font.activated(bold);
    QCOMPARE(shown.session.textDraft().value().style.fontName, QString("DejaVuSans-Bold"));
    QCOMPARE(font.currentText(), QString("DejaVuSans-Bold"));
    QVERIFY(font.findText(QStringLiteral("Helvetica")) >= 0);
    QCOMPARE(font.count(), TextLayout::availableFonts().size() + 1);
    // A missing face named elsewhere joins the list's end.
    shown.restyle([](LayerTextStyle &style) { style.fontName = QStringLiteral("Zapfino"); });
    QCOMPARE(font.currentText(), QString("Zapfino"));
    QCOMPARE(font.count(), TextLayout::availableFonts().size() + 2);
    QCOMPARE(font.itemText(font.count() - 1), QString("Zapfino"));
    shown.restyle([](LayerTextStyle &style) { style.fontName = QStringLiteral("Aachen"); });
    QVERIFY(font.currentText() == QString("Aachen") && font.itemText(font.count() - 1) == QString("Aachen") && font.findText("Zapfino") >= 0);
    // An open menu is left alone; closed, it follows.
    font.showPopup();
    QVERIFY(font.findText("Zapfino") >= 0);
    shown.restyle([](LayerTextStyle &style) { style.fontName = QStringLiteral("Zapfino"); });
    QCOMPARE(font.currentText(), QString("Aachen"));
    font.hidePopup();
    QTRY_VERIFY(!font.view()->isVisible());
    shown.restyle([](LayerTextStyle &style) { style.fontName = QStringLiteral("Zapfino"); });
    QCOMPARE(font.currentText(), QString("Zapfino"));
    // A long name is cut, the width kept.
    QStyleOptionComboBox option;
    option.initFrom(&font);
    const int room = font.style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, &font).width() - 2;
    const auto image = [](QWidget &widget) { return widget.grab().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied); };
    // One name far past the room, one just past.
    QString nearly;
    for (int wide = 0; wide < 40 && nearly.isEmpty(); ++wide) {
        for (int thin = 0; thin < 40 && nearly.isEmpty(); ++thin) {
            const QString name = QString(wide, QLatin1Char('W')) + QString(thin, QLatin1Char('i'));
            const int advance = font.fontMetrics().horizontalAdvance(name);
            if (advance > room && advance <= room + 2)
                nearly = name;
        }
    }
    QVERIFY(!nearly.isEmpty());
    for (const QString &name : {QString(120, QLatin1Char('W')), nearly}) {
        shown.restyle([&](LayerTextStyle &style) { style.fontName = name; });
        QCOMPARE(font.width(), 210);
        QComboBox plain;
        plain.setFixedSize(font.size());
        plain.setPalette(font.palette());
        plain.setFont(font.font());
        plain.addItem(font.fontMetrics().elidedText(name, Qt::ElideRight, room));
        QVERIFY(plain.currentText().endsWith(QChar(0x2026)));
        QCOMPARE(image(font), image(plain));
    }
}

void TypeFontPickerTests::theFaceShownChangesNothing()
{
    Bar shown;
    auto &font = find<QComboBox>(shown.bar, "typeFont");
    addText(shown.session, QStringLiteral("Face"));
    shown.session.selectTool(NavigationTool::move);
    QVERIFY(!shown.session.textDraft());
    font.showPopup();
    font.hidePopup();
    QSignalSpy changes(&shown.session, &EditorSession::changed);
    emit font.activated(font.findText(QStringLiteral("Helvetica")));
    QVERIFY(!shown.session.textDraft());
    QCOMPARE(shown.session.tool(), NavigationTool::move);
    QCOMPARE(int(changes.count()), 0);
}

QTEST_MAIN(TypeFontPickerTests)
#include "TypeFontPickerTests.moc"
