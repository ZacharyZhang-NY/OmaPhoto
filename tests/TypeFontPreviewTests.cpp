#include "InlineTextDrawFixtures.h"
#include "Rendering/TextLayout.h"
#include "UI/TypeControls.h"
#include <QAbstractItemView>
#include <QFontInfo>

// Swift 1.3.5's font previews: faces in themselves, tried on text.
namespace {
const QString sans = QStringLiteral("DejaVuSans");
const QString mono = QStringLiteral("DejaVuSansMono");
const QString serif = QStringLiteral("DejaVuSerif");

struct Typed : DrawnText {
    TypeControls *const bar = new TypeControls(session, &window);
    TypeFontPicker &menu() const { return *bar->findChild<TypeFontPicker *>(QStringLiteral("typeFont")); }
    Typed()
    {
        bar->setGeometry(0, 300, 400, 40);
        bar->show();
        session.changeTextStyle([](LayerTextStyle &style) { style.fontName = sans; });
        canvas->setFocus();
        beginTextAt(session, QPointF(20, 30));
        QTest::keyClicks(canvas, QStringLiteral("Hello"));
        QTest::keyClick(canvas, Qt::Key_A, Qt::ControlModifier);
    }
    QString face() const { return session.textDraft().value().style.uniformFontName({0, 5}).value_or(QString()); }
    void hover(const QString &name) { emit menu().highlighted(menu().findText(name)); }
};
}

class TypeFontPreviewTests : public QObject {
    Q_OBJECT
private slots:
    void theMenuDrawsEachFaceInItself();
    void hoveringTriesAFaceAndClosingPutsItBack();
    void aChoiceKeepsWhatShows();
    void onlyOpenTextPreviews();
};

void TypeFontPreviewTests::theMenuDrawsEachFaceInItself()
{
    Typed typed;
    typed.session.changeTextStyle([](LayerTextStyle &style) { style.setFont(mono, {0, 2}); });
    // A desktop font in points: the shown pixel size counts.
    QFont points = typed.menu().font();
    points.setPointSize(11);
    typed.menu().setFont(points);
    QCOMPARE(typed.menu().font().pixelSize(), -1);
    typed.menu().showPopup();
    const int pixels = QFontInfo(typed.menu().font()).pixelSize();
    QVERIFY(pixels > 0);
    QVERIFY(typed.menu().isMultiple(0) && !typed.menu().itemData(0, Qt::FontRole).isValid());
    QVERIFY(typed.menu().count() > 3);
    for (int index = 1; index < typed.menu().count(); ++index) {
        const QString name = typed.menu().itemText(index);
        const QFont face = typed.menu().itemData(index, Qt::FontRole).value<QFont>();
        QCOMPARE(face.family(), TextLayout::font(LayerTextStyle(), name).family());
        QCOMPARE(face.styleName(), TextLayout::font(LayerTextStyle(), name).styleName());
        QCOMPARE(face.pixelSize(), pixels);
    }
    typed.menu().hidePopup();
}

void TypeFontPreviewTests::hoveringTriesAFaceAndClosingPutsItBack()
{
    Typed typed;
    typed.menu().showPopup();
    typed.hover(mono);
    QCOMPARE(typed.face(), mono);
    // Each face from the text as it was, never stacked.
    typed.hover(serif);
    QCOMPARE(typed.face(), serif);
    typed.menu().hidePopup();
    QCOMPARE(typed.face(), serif);
    QTRY_COMPARE(typed.face(), sans);
    // The next preview starts from the text as it stands.
    QTest::keyClick(typed.canvas, Qt::Key_End);
    QTest::keyClicks(typed.canvas, QStringLiteral("!"));
    QTest::keyClick(typed.canvas, Qt::Key_A, Qt::ControlModifier);
    typed.menu().showPopup();
    typed.hover(mono);
    QCOMPARE(typed.session.textDraft().value().style.content, QString("Hello!"));
    typed.menu().hidePopup();
    QTRY_COMPARE(typed.face(), sans);
    // Only the selected letters take the face tried.
    typed.session.setTextDraft([&] {
        TextDraft draft = typed.session.textDraft().value();
        draft.selection = {0, 2};
        return draft;
    }());
    typed.session.previewFont(mono);
    QVERIFY(typed.session.textDraft().value().style.uniformFontName({0, 2}) == mono && typed.session.textDraft().value().style.fontNameAt(3) == sans);
    // Tried and put back unchanged, the text is no news.
    typed.session.previewFont(sans);
    typed.session.endFontPreview();
    QSignalSpy changed(&typed.session, &EditorSession::changed);
    typed.session.previewFont(sans);
    typed.session.endFontPreview();
    QVERIFY(changed.isEmpty());
}

void TypeFontPreviewTests::aChoiceKeepsWhatShows()
{
    Typed typed;
    typed.menu().showPopup();
    typed.hover(mono);
    // Qt closes the menu, then reports the choice.
    typed.menu().hidePopup();
    emit typed.menu().activated(typed.menu().findText(mono));
    QCoreApplication::processEvents();
    QCOMPARE(typed.face(), mono);
    // Kept: the preview's original is gone, so no later revert.
    QSignalSpy changed(&typed.session, &EditorSession::changed);
    typed.session.endFontPreview();
    QVERIFY(changed.isEmpty() && typed.face() == mono);
    // The face already showing changes nothing, silently.
    typed.session.previewFont(mono);
    QVERIFY(changed.isEmpty());
    typed.session.keepFontPreview();
    typed.session.endFontPreview();
    QVERIFY(changed.isEmpty() && typed.face() == mono);
}

void TypeFontPreviewTests::onlyOpenTextPreviews()
{
    Typed typed;
    QVERIFY(typed.session.finishText());
    const LayerTextStyle kept = typed.session.activeLayer().value().liveText().value().style;
    QSignalSpy changed(&typed.session, &EditorSession::changed);
    typed.session.previewFont(mono);
    typed.session.endFontPreview();
    QVERIFY(changed.isEmpty() && !typed.session.textDraft());
    QCOMPARE(typed.session.activeLayer().value().liveText().value().style, kept);
}

QTEST_MAIN(TypeFontPreviewTests)
#include "TypeFontPreviewTests.moc"
