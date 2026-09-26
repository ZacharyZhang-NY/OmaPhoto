#include "Document/BrushStroke.h"
#include "ContentView.h"
#include "SelectionFixtures.h"
#include "UI/FilterSheet.h"
#include <QCheckBox>
#include <QDialog>
#include <QProgressBar>
#include <QPushButton>
#include <QtTest>

// The filter panel, driven as a user drives it.
namespace {
template <typename Widget> Widget &find(QWidget &root, const char *name)
{
    Widget *found = root.findChild<Widget *>(QString::fromLatin1(name));
    if (!found)
        throw std::runtime_error(std::string("no widget named ") + name);
    return *found;
}

// A blue field, a red object, a selection, an editor.
struct Editor {
    EditorSession session;
    ContentView view{session};
    Editor()
    {
        session.createDocument(64, 48, true);
        QImage image = BrushRaster::context(64, 48, false);
        image.fill(QColor(51, 153, 204));
        QPainter painter(&image);
        painter.fillRect(QRect(20, 16, 12, 10), Qt::red);
        painter.end();
        session.insert(ImportedImage(image, image, "Object"));
        session.applySelection(rectPath(QRectF(20, 16, 12, 10)), SelectionMode::replace, "Select");
        view.show();
    }
    // The panel's shown sheet; a closed panel's goes later.
    FilterSheet *sheet()
    {
        for (FilterSheet *each : view.findChildren<FilterSheet *>()) {
            if (each->isVisible())
                return each;
        }
        return nullptr;
    }
    QDialog &panel() { return *qobject_cast<QDialog *>(sheet()->window()); }
    bool prepared() { return QTest::qWaitFor([&] { return session.filterEdit().has_value() && !session.filterEdit().value().preparing; }, 20000); }
};
}

class FilterSheetTests : public QObject {
    Q_OBJECT
private slots:
    void thePanelShowsWithTheEditAndSaysWhatItWaitsFor();
    void previewCancelAndOKReachTheSession();
    void escapeAndClosingCancelReturnCommits();
    void noSourceShowsTheErrorAndHoldsOK();
};

void FilterSheetTests::thePanelShowsWithTheEditAndSaysWhatItWaitsFor()
{
    Editor editor;
    QVERIFY(!editor.sheet());
    editor.session.beginFilter(FilterKind::contentAwareFill);
    FilterSheet &sheet = *editor.sheet();
    QCOMPARE(editor.panel().objectName(), QString("filterPanel"));
    QCOMPARE(editor.panel().windowTitle(), QString("Content-Aware Fill"));
    QCOMPARE(sheet.width(), 380);
    QCOMPARE(find<QLabel>(sheet, "filterWords").text(), QString("Fill the selection using surrounding pixels from this layer."));
    QVERIFY(find<QCheckBox>(sheet, "filterPreview").isChecked());
    QVERIFY(find<QLabel>(sheet, "filterLimited").isVisible());
    QVERIFY(!find<QLabel>(sheet, "filterError").isVisible());
    // Working: OK waits with the spinner.
    QVERIFY(!find<QPushButton>(sheet, "filterOK").isEnabled());
    QVERIFY(find<QProgressBar>(sheet, "filterSpinner").isVisible());
    QCOMPARE(find<QProgressBar>(sheet, "filterSpinner").maximum(), 0);
    QCOMPARE(find<QLabel>(sheet, "filterActivity").text(), QString("Working…"));
    QVERIFY(editor.prepared());
    QVERIFY(find<QPushButton>(sheet, "filterOK").isEnabled());
    QVERIFY(!find<QProgressBar>(sheet, "filterSpinner").isVisible());
    QVERIFY(!find<QLabel>(sheet, "filterActivity").isVisible());
    // Applying: everything rests until the result lands.
    bool done = false;
    editor.session.commitFilter([&] { done = true; });
    QVERIFY(!sheet.isEnabled());
    QCOMPARE(find<QLabel>(sheet, "filterActivity").text(), QString("Applying…"));
    QVERIFY(find<QProgressBar>(sheet, "filterSpinner").isVisible());
    QTRY_VERIFY(done);
    QVERIFY(!editor.sheet() && !editor.session.filterEdit().has_value());
    QCOMPARE(editor.session.history.undoName(), QString("Content-Aware Fill"));
}

void FilterSheetTests::previewCancelAndOKReachTheSession()
{
    Editor editor;
    editor.session.beginFilter(FilterKind::contentAwareFill);
    QVERIFY(editor.prepared());
    const QUuid id = editor.session.activeLayerID().value();
    QVERIFY(editor.session.filterEdit().value().previewImage(id).has_value());
    QCheckBox &preview = find<QCheckBox>(*editor.sheet(), "filterPreview");
    preview.click();
    QVERIFY(!editor.session.filterEdit().value().preview);
    QVERIFY(!editor.session.filterEdit().value().previewImage(id).has_value());
    preview.click();
    QVERIFY(editor.session.filterEdit().value().previewImage(id).has_value());
    find<QPushButton>(*editor.sheet(), "filterCancel").click();
    QVERIFY(!editor.session.filterEdit().has_value() && !editor.sheet());
    editor.session.beginFilter(FilterKind::contentAwareFill);
    QVERIFY(editor.prepared());
    const int count = editor.session.history.undoCount();
    find<QPushButton>(*editor.sheet(), "filterOK").click();
    QTRY_VERIFY(!editor.session.filterEdit().has_value());
    QCOMPARE(editor.session.history.undoCount(), count + 1);
}

void FilterSheetTests::escapeAndClosingCancelReturnCommits()
{
    Editor editor;
    editor.session.beginFilter(FilterKind::contentAwareFill);
    QVERIFY(editor.prepared());
    QTest::keyClick(editor.sheet(), Qt::Key_Escape);
    QVERIFY(!editor.session.filterEdit().has_value() && !editor.sheet());
    editor.session.beginFilter(FilterKind::contentAwareFill);
    editor.panel().close();
    QVERIFY(!editor.session.filterEdit().has_value() && !editor.sheet());
    // Return is OK while it is enabled, nothing while waiting.
    editor.session.beginFilter(FilterKind::contentAwareFill);
    QTest::keyClick(editor.sheet(), Qt::Key_Return);
    QVERIFY(editor.session.filterEdit().has_value() && !editor.session.filterEdit().value().committing);
    QVERIFY(editor.prepared());
    QTest::keyClick(editor.sheet(), Qt::Key_Return);
    QVERIFY(editor.session.filterEdit().value().committing);
    QTRY_VERIFY(!editor.session.filterEdit().has_value());
    QCOMPARE(editor.session.history.undoName(), QString("Content-Aware Fill"));
    // Return on Cancel is still OK's: nothing while OK waits.
    const int count = editor.session.history.undoCount();
    editor.session.beginFilter(FilterKind::contentAwareFill);
    QPushButton &cancel = find<QPushButton>(*editor.sheet(), "filterCancel");
    cancel.setFocus();
    QTest::keyClick(&cancel, Qt::Key_Return);
    QVERIFY(editor.session.filterEdit().has_value() && !editor.session.filterEdit().value().committing);
    QVERIFY(editor.prepared());
    QTest::keyClick(&cancel, Qt::Key_Return);
    QVERIFY(editor.session.filterEdit().value().committing);
    QTRY_VERIFY(!editor.session.filterEdit().has_value());
    QCOMPARE(editor.session.history.undoCount(), count + 1);
}

void FilterSheetTests::noSourceShowsTheErrorAndHoldsOK()
{
    Editor editor;
    editor.session.selectAll();
    editor.session.beginFilter(FilterKind::contentAwareFill);
    QVERIFY(editor.prepared());
    QLabel &error = find<QLabel>(*editor.sheet(), "filterError");
    QVERIFY(error.isVisible());
    QCOMPARE(error.text(), QString("Not enough unselected, opaque image pixels to synthesize a fill. Use a smaller selection with some surrounding image."));
    QCOMPARE(error.foregroundRole(), QPalette::BrightText);
    QVERIFY(!find<QPushButton>(*editor.sheet(), "filterOK").isEnabled());
    QTest::keyClick(editor.sheet(), Qt::Key_Return);
    QVERIFY(editor.session.filterEdit().has_value() && !editor.session.filterEdit().value().committing);
    // Without a selection, others open without the note.
    find<QPushButton>(*editor.sheet(), "filterCancel").click();
    editor.session.deselect();
    QVERIFY(!editor.session.canContentAwareFill());
    editor.session.beginFilter(FilterKind::gaussianBlur);
    QVERIFY(!find<QLabel>(*editor.sheet(), "filterLimited").isVisible());
    // Only an automatic kind holds OK while it prepares.
    QVERIFY(editor.session.filterEdit().value().preparing && find<QPushButton>(*editor.sheet(), "filterOK").isEnabled());
}

QTEST_MAIN(FilterSheetTests)
#include "FilterSheetTests.moc"
