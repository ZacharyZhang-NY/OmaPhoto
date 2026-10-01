#include "Document/EditorSession.h"
#include "UI/ColorPickerSheet.h"
#include "UI/ColorRangeSheet.h"
#include "UI/SampleButton.h"
#include <QCheckBox>
#include <QDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QtTest>

// Swift 1.3.4's ColorRangeSheet: its layout, field, preview, words.
namespace {
// Red left, blue right; the sheet over an open edit.
struct Sheet {
    EditorSession session;
    std::unique_ptr<ColorRangeSheet> sheet;
    Sheet(QSize size = QSize(400, 300))
    {
        QImage image(size, QImage::Format_RGBA8888_Premultiplied);
        image.fill(QColor(200, 30, 30));
        for (int y = 0; y < size.height(); ++y)
            for (int x = size.width() / 2; x < size.width(); ++x)
                image.setPixelColor(x, y, QColor(30, 40, 210));
        session.createDocument(size.width(), size.height());
        session.insert(ImportedImage(image, image, QStringLiteral("Halves")));
        session.beginColorRange();
        sheet = std::make_unique<ColorRangeSheet>(session);
        sheet->show();
        if (!QTest::qWaitForWindowActive(sheet.get()))
            throw std::runtime_error("the sheet never became active");
    }
    const ColorRangeEdit &edit() const { return session.colorRange().value(); }
    template <typename Widget> Widget &child(const char *name) const
    {
        Widget *found = sheet->findChild<Widget *>(QString::fromLatin1(name));
        if (!found)
            throw std::runtime_error(std::string("no child ") + name);
        return *found;
    }
    // A click on the left half, and its match landing.
    void sampleRed()
    {
        QSignalSpy changed(&session, &EditorSession::changed);
        session.sampleColorRange(QPointF(10, 10), false, false);
        QVERIFY(QTest::qWaitFor([&changed] { return changed.count() >= 2; }));
    }
};
}

class ColorRangeSheetTests : public QObject {
    Q_OBJECT
private slots:
    void theSheetLaysOutAsSwifts();
    void theFieldTakesTypingAsSwiftsBinding();
    void thePreviewPaintsTheMatch();
    void thePreviewShrinksSmoothly();
    void invertAndErrorsFollowTheEdit();
};

void ColorRangeSheetTests::theSheetLaysOutAsSwifts()
{
    Sheet shown;
    auto *column = qobject_cast<QVBoxLayout *>(shown.sheet->layout());
    QVERIFY(column && shown.sheet->width() == 340 && column->spacing() == 16);
    QCOMPARE(column->contentsMargins(), QMargins(24, 24, 24, 24));
    // The eyedroppers in Swift's order, 6 apart, then a stretch.
    auto *droppers = qobject_cast<QHBoxLayout *>(column->itemAt(0)->layout());
    QVERIFY(droppers && droppers->spacing() == 6 && droppers->count() == 4 && droppers->itemAt(3)->spacerItem());
    QCOMPARE(droppers->itemAt(0)->widget(), &shown.child<SampleButton>("colorRangeSample"));
    QCOMPARE(droppers->itemAt(2)->widget(), &shown.child<SampleButton>("colorRangeRemove"));
    // The preview, centred, the canvas's shape within 292 by 200.
    QWidget *preview = column->itemAt(1)->widget();
    QVERIFY(preview && column->itemAt(1)->alignment() == Qt::AlignHCenter);
    QCOMPARE(preview->size(), QSize(267, 200));
    QCOMPARE(preview->mapTo(shown.sheet.get(), QPoint()).x(), (340 - 267) / 2);
    // The caption: wrapped, callout-sized, secondary.
    auto &caption = shown.child<QLabel>("colorRangeCaption");
    QVERIFY(column->itemAt(2)->widget() == &caption && caption.wordWrap() && caption.font().pixelSize() == 12);
    QCOMPARE(caption.foregroundRole(), QPalette::PlaceholderText);
    // Fuzziness: title, slider stretching, a 48-point field.
    QWidget *row = column->itemAt(3)->widget();
    QCOMPARE(row->toolTip(), QStringLiteral("How far a color may be from the picked ones and still be selected"));
    auto *fuzziness = qobject_cast<QHBoxLayout *>(row->layout());
    QVERIFY(fuzziness->spacing() == 10 && fuzziness->contentsMargins() == QMargins() && fuzziness->count() == 3);
    QCOMPARE(qobject_cast<QLabel *>(fuzziness->itemAt(0)->widget())->text(), QStringLiteral("Fuzziness"));
    QVERIFY(fuzziness->itemAt(1)->widget() == &shown.child<QSlider>("fuzzinessSlider") && fuzziness->stretch(1) == 1);
    auto &field = shown.child<PickerField>("fuzzinessField");
    QVERIFY(fuzziness->itemAt(2)->widget() == &field && field.width() == 48 && field.alignment() == Qt::AlignRight);
    QVERIFY(field.accessibleName() == QStringLiteral("Fuzziness") && field.placeholderText() == QStringLiteral("Fuzziness"));
    // Invert, the error, a rule, then Cancel and OK apart.
    auto &invert = shown.child<QCheckBox>("colorRangeInvert");
    QVERIFY(column->itemAt(4)->widget() == &invert);
    QCOMPARE(invert.toolTip(), QStringLiteral("Select everything except those colors, such as all but a green screen"));
    auto &error = shown.child<QLabel>("colorRangeError");
    QVERIFY(column->itemAt(5)->widget() == &error && error.wordWrap());
    auto *rule = qobject_cast<QFrame *>(column->itemAt(6)->widget());
    QVERIFY(rule && rule->frameShape() == QFrame::HLine && rule->foregroundRole() == QPalette::Mid);
    auto *buttons = qobject_cast<QHBoxLayout *>(column->itemAt(7)->layout());
    QVERIFY(buttons && buttons->count() == 3 && buttons->itemAt(1)->spacerItem());
    auto *cancel = qobject_cast<QPushButton *>(buttons->itemAt(0)->widget());
    auto *ok = qobject_cast<QPushButton *>(buttons->itemAt(2)->widget());
    QVERIFY(cancel->text() == QStringLiteral("Cancel") && ok->text() == QStringLiteral("OK") && ok->isDefault());
    // In a panel, a dialog, only OK takes Return.
    QDialog panel;
    shown.sheet->setParent(&panel);
    QVERIFY(!cancel->autoDefault() && ok->autoDefault());
    shown.sheet->setParent(nullptr);
    // Cancel puts the old selection back and closes the edit.
    cancel->click();
    QVERIFY(!shown.session.colorRange());
}

void ColorRangeSheetTests::theFieldTakesTypingAsSwiftsBinding()
{
    Sheet shown;
    auto &field = shown.child<PickerField>("fuzzinessField");
    auto &slider = shown.child<QSlider>("fuzzinessSlider");
    QVERIFY(field.text() == QStringLiteral("40") && slider.value() == 40);
    // Typed, then Return: whole numbers, shown whole.
    field.setFocus();
    QTRY_VERIFY(field.hasFocus());
    field.selectAll();
    QTest::keyClicks(&field, QStringLiteral("40.4"));
    QTest::keyClick(&field, Qt::Key_Return);
    QVERIFY(shown.edit().fuzziness == 40 && field.text() == QStringLiteral("40"));
    // The slider replaces typing, as the title's scrub does.
    field.selectAll();
    QTest::keyClicks(&field, QStringLiteral("77"));
    slider.setValue(90);
    QVERIFY(shown.edit().fuzziness == 90 && field.text() == QStringLiteral("90"));
    field.selectAll();
    QTest::keyClicks(&field, QStringLiteral("77"));
    QLabel *title = qobject_cast<QLabel *>(qobject_cast<QHBoxLayout *>(field.parentWidget()->layout())->itemAt(0)->widget());
    QTest::mousePress(title, Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));
    QMouseEvent move(QEvent::MouseMove, QPointF(12, 2), title->mapToGlobal(QPointF(12, 2)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(title, &move);
    QTest::mouseRelease(title, Qt::LeftButton, Qt::NoModifier, QPoint(12, 2));
    QVERIFY(shown.edit().fuzziness == 100 && field.text() == QStringLiteral("100"));
    // Text that is no number goes back.
    field.selectAll();
    QTest::keyClicks(&field, QStringLiteral("lots"));
    QTest::keyClick(&field, Qt::Key_Return);
    QVERIFY(shown.edit().fuzziness == 100 && field.text() == QStringLiteral("100"));
    // The sheet going commits what its field holds.
    field.selectAll();
    QTest::keyClicks(&field, QStringLiteral("123"));
    shown.sheet.reset();
    QCOMPARE(shown.edit().fuzziness, 123.0);
}

void ColorRangeSheetTests::thePreviewPaintsTheMatch()
{
    Sheet shown;
    QWidget *preview = shown.sheet->layout()->itemAt(1)->widget();
    // Black before a colour, inside a faint white rim.
    QImage drawn = preview->grab().toImage();
    QCOMPARE(drawn.pixelColor(100, 100), QColor(Qt::black));
    QCOMPARE(qGray(drawn.pixel(0, 100)), 51);
    // A match repaints it: white over the red half.
    int painted = 0;
    struct Counter : QObject {
        int &count;
        explicit Counter(int &count) : count(count) {}
        bool eventFilter(QObject *, QEvent *event) override
        {
            count += event->type() == QEvent::Paint;
            return false;
        }
    } counter(painted);
    preview->installEventFilter(&counter);
    shown.sampleRed();
    QTRY_VERIFY(painted > 0);
    drawn = preview->grab().toImage();
    QVERIFY(qGray(drawn.pixel(60, 100)) > 240 && qGray(drawn.pixel(200, 100)) < 15);
}

void ColorRangeSheetTests::thePreviewShrinksSmoothly()
{
    // Red then stripes a pixel wide: the preview halves them.
    QImage image(584, 400, QImage::Format_RGBA8888_Premultiplied);
    for (int y = 0; y < 400; ++y)
        for (int x = 0; x < 584; ++x)
            image.setPixelColor(x, y, x < 40 || x % 2 ? QColor(200, 30, 30) : QColor(30, 40, 210));
    EditorSession session;
    session.createDocument(584, 400);
    session.insert(ImportedImage(image, image, QStringLiteral("Stripes")));
    session.beginColorRange();
    ColorRangeSheet sheet(session);
    QWidget *preview = sheet.layout()->itemAt(1)->widget();
    QCOMPARE(preview->size(), QSize(292, 200));
    QSignalSpy changed(&session, &EditorSession::changed);
    session.sampleColorRange(QPointF(10, 10), false, false);
    QVERIFY(QTest::qWaitFor([&changed] { return changed.count() >= 2; }));
    const QImage drawn = preview->grab().toImage();
    QVERIFY(qGray(drawn.pixel(10, 100)) > 240);
    QVERIFY(std::abs(qGray(drawn.pixel(150, 100)) - 128) < 20);
}

void ColorRangeSheetTests::invertAndErrorsFollowTheEdit()
{
    Sheet shown;
    auto &invert = shown.child<QCheckBox>("colorRangeInvert");
    shown.session.setColorRangeInvert(true);
    QVERIFY(invert.isChecked());
    shown.session.setColorRangeInvert(false);
    QVERIFY(!invert.isChecked());
    // A checkerboard past the edge limit: the error shows.
    QImage board(2100, 2100, QImage::Format_RGBA8888_Premultiplied);
    for (int y = 0; y < board.height(); ++y) {
        auto *row = reinterpret_cast<quint32 *>(board.scanLine(y));
        for (int x = 0; x < board.width(); ++x)
            row[x] = (x + y) % 2 && (x > 2 || y > 2) ? 0xFFFFFFFF : 0xFF000000;
    }
    EditorSession session;
    session.createDocument(2100, 2100);
    session.insert(ImportedImage(board, board, QStringLiteral("Board")));
    session.beginColorRange();
    ColorRangeSheet sheet(session);
    auto *error = sheet.findChild<QLabel *>(QStringLiteral("colorRangeError"));
    QVERIFY(error->isHidden());
    session.setColorRangeFuzziness(0);
    session.sampleColorRange(QPointF(1, 1), false, false);
    QTRY_VERIFY_WITH_TIMEOUT(!error->isHidden(), 30000);
    QCOMPARE(error->text(), QString::fromUtf8(MagicWandError(MagicWandError::Kind::tooDetailed).what()));
    QCOMPARE(error->foregroundRole(), QPalette::BrightText);
}

QTEST_MAIN(ColorRangeSheetTests)
#include "ColorRangeSheetTests.moc"
