#include "Document/EditorSession.h"
#include "UI/CropControls.h"
#include <QComboBox>
#include <QPushButton>
#include <QtTest>

// Swift's CropControls: ratio, size, Cancel and Apply Crop.
class CropControlsTests : public QObject {
    Q_OBJECT
private slots:
    void theBarShowsTheFrameAndItsRatio();
    void itsButtonsCancelAndCrop();
    void itRestsWhileBusyOrEmpty();
};

namespace {
template <typename Widget> Widget &control(const QWidget &bar, const char *name)
{
    Widget *found = bar.findChild<Widget *>(QString::fromLatin1(name));
    if (!found)
        throw std::runtime_error(std::string("no control named ") + name);
    return *found;
}
}

void CropControlsTests::theBarShowsTheFrameAndItsRatio()
{
    EditorSession session;
    session.createDocument(1200, 800);
    session.selectTool(NavigationTool::crop);
    CropControls bar(session);
    bar.show();
    QCOMPARE(bar.title->text(), QString("Crop"));
    QComboBox &ratio = control<QComboBox>(bar, "cropRatio");
    QStringList choices;
    for (int index = 0; index < ratio.count(); ++index)
        choices << ratio.itemText(index);
    QCOMPARE(choices, (QStringList{"Free", "Original", "1:1", "4:3", "16:9"}));
    QCOMPARE(ratio.currentText(), QString("Free"));
    // Labelled as Swift's picker, the two 170 points wide.
    const QList<QLabel *> labels = bar.findChildren<QLabel *>();
    const auto named = std::find_if(labels.begin(), labels.end(), [](const QLabel *label) { return label->text() == "Ratio"; });
    QVERIFY(named != labels.end());
    QCOMPARE((*named)->buddy(), &ratio);
    QCOMPARE(ratio.parentWidget()->width(), 170);
    QCOMPARE((*named)->x(), 0);
    QCOMPARE((*named)->width(), (*named)->sizeHint().width());
    QCOMPARE(ratio.geometry().right(), 169);
    QCOMPARE(bar.row->spacing(), 14);
    QLabel &size = control<QLabel>(bar, "cropSize");
    QCOMPARE(size.text(), QString("1,200 × 800 px"));
    QVERIFY(size.isVisible());
    // A new ratio takes the frame about its middle, once.
    emit ratio.activated(2);
    QCOMPARE(session.cropRatioChoice(), QString("1:1"));
    QCOMPARE(session.cropRect(), std::optional(QRectF(0, -200, 1200, 1200)));
    QCOMPARE(size.text(), QString("1,200 × 1,200 px"));
    session.setCropRect(QRectF(0, 0, 1200, 600));
    emit ratio.activated(2);
    QCOMPARE(session.cropRect(), std::optional(QRectF(0, 0, 1200, 600)));
    // The session's choice shows; whole pixels, truncated.
    session.setCropRatioChoice("16:9");
    QCOMPARE(ratio.currentText(), QString("16:9"));
    session.setCropRect(QRectF(0.4, 0, 99.9, 50.2));
    QCOMPARE(size.text(), QString("99 × 50 px"));
    session.cancelCrop();
    QVERIFY(!size.isVisible());
}

void CropControlsTests::itsButtonsCancelAndCrop()
{
    EditorSession session;
    session.createDocument(100, 100);
    session.selectTool(NavigationTool::crop);
    CropControls bar(session);
    bar.show();
    QPushButton &cancel = control<QPushButton>(bar, "cropCancel");
    QPushButton &apply = control<QPushButton>(bar, "cropApply");
    QCOMPARE(apply.text(), QString("Apply Crop"));
    QVERIFY(cancel.x() < apply.x());
    session.setCropRect(QRectF(10, 10, 40, 30));
    QTest::mouseClick(&cancel, Qt::LeftButton);
    QCOMPARE(session.cropRect(), std::nullopt);
    // Without a frame drawn, neither has work to do.
    QVERIFY(!cancel.isEnabled() && !apply.isEnabled());
    session.setCropRect(QRectF(10, 10, 40, 30));
    QVERIFY(cancel.isEnabled() && apply.isEnabled());
    QTest::mouseClick(&apply, Qt::LeftButton);
    QTRY_COMPARE(session.document().value().width, 40);
    QCOMPARE(session.document().value().height, 30);
    QVERIFY(!apply.isEnabled());
}

void CropControlsTests::itRestsWhileBusyOrEmpty()
{
    EditorSession session;
    CropControls bar(session);
    QVERIFY(!bar.isEnabled());
    session.createDocument(100, 100);
    QVERIFY(bar.isEnabled());
    // Busy past its delay, the bar rests with the rest.
    session.setIsProjectBusy(true);
    QVERIFY(bar.isEnabled());
    QTRY_VERIFY(session.showsBusy());
    QVERIFY(!bar.isEnabled());
    session.setIsProjectBusy(false);
    QVERIFY(bar.isEnabled());
}

QTEST_MAIN(CropControlsTests)
#include "CropControlsTests.moc"
