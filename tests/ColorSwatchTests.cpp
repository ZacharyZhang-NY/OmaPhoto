#include "SelectionCanvasFixtures.h"
#include "UI/BrushControls.h"
#include "UI/ColorPaletteControls.h"
#include "UI/NativeLayerList.h"
#include "UI/ShapeControls.h"
#include "UI/TypeControls.h"
#include <QLabel>

// The bars' swatches; X and D on canvas and list.
namespace {
QAbstractButton &swatch(QWidget &bar, const char *name)
{
    auto *found = bar.findChild<QAbstractButton *>(QString::fromLatin1(name));
    if (!found)
        throw std::runtime_error(name);
    return *found;
}

QColor middle(QWidget &widget)
{
    return widget.grab().toImage().pixelColor(widget.width() / 2, widget.height() / 2);
}

std::unique_ptr<EditorSession> sessionWithLayer(NavigationTool tool)
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(400, 300, true);
    session->selectTool(tool);
    return session;
}
}

class ColorSwatchTests : public QObject {
    Q_OBJECT
private slots:
    void theBrushBarPicksTheForeground();
    void theShapeBarFillsWithTheForeground();
    void theTypeBarShowsAndPicksTheTextColour();
    void xAndDSwapAndRestoreThePalette();
};

void ColorSwatchTests::theBrushBarPicksTheForeground()
{
    const std::unique_ptr<EditorSession> session = sessionWithLayer(NavigationTool::brush);
    BrushControls bar(*session);
    bar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&bar));
    QAbstractButton &colour = swatch(bar, "brushColor");
    QVERIFY(colour.isVisibleTo(&bar));
    QCOMPARE(colour.toolTip(), QString("Foreground color"));
    QCOMPARE(colour.size(), QSize(34, 18));
    QCOMPARE(middle(colour), QColor(Qt::black));
    // A white ring inside the black rim, as Swift's.
    QVERIFY(colour.grab().toImage().pixelColor(17, 1).lightness() > 200);
    QCOMPARE(colour.grab().toImage().pixelColor(17, 0).lightness(), 0);
    QCoreApplication::processEvents();
    PaintSpy repaint(colour);
    session->setForegroundColor(PaletteColor{1, 0, 0});
    QTRY_VERIFY(repaint.count > 0);
    QCOMPARE(middle(colour), QColor(Qt::red));
    QTest::mouseClick(&colour, Qt::LeftButton);
    QCOMPARE(session->colorPicker().value().target.title(), QString("Color Picker (Foreground Color)"));
    session->closeColorPicker(false);
    // Clone Stamp, the Smear and masks paint no own colour.
    QLabel *label = nullptr;
    for (QLabel *each : bar.findChildren<QLabel *>()) {
        if (each->text() == QString("Color"))
            label = each;
    }
    QVERIFY(label);
    for (const auto &[tool, shows] : {std::pair(NavigationTool::cloneStamp, false), std::pair(NavigationTool::blur, false), std::pair(NavigationTool::spotHealing, true)}) {
        session->selectTool(tool);
        QCOMPARE(colour.isVisibleTo(&bar), shows);
        QCOMPARE(label->isVisibleTo(&bar), shows);
    }
    session->selectTool(NavigationTool::brush);
    session->addMask();
    QVERIFY(session->isMaskSelected());
    QVERIFY(!colour.isVisibleTo(&bar));
    session->selectLayerTarget(session->activeLayerID().value(), false);
    // A stroke under way rests it.
    session->beginBrush(QPointF(10, 10));
    QVERIFY(session->brushStroke());
    QVERIFY(!colour.isEnabled());
    session->cancelBrush();
    QVERIFY(colour.isEnabled());
}

void ColorSwatchTests::theShapeBarFillsWithTheForeground()
{
    const std::unique_ptr<EditorSession> session = sessionWithLayer(NavigationTool::shape);
    ShapeControls bar(*session);
    bar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&bar));
    QAbstractButton &fill = swatch(bar, "shapeFill");
    QCOMPARE(fill.toolTip(), QString("Shapes fill with the foreground color; click to change it"));
    QCOMPARE(fill.size(), QSize(36, 18));
    QCoreApplication::processEvents();
    PaintSpy repaint(fill);
    session->setForegroundColor(PaletteColor{0, 1, 0});
    QTRY_VERIFY(repaint.count > 0);
    QCOMPARE(middle(fill), QColor(Qt::green));
    // Its rim is half black, no white ring.
    const QColor rim = fill.grab().toImage().pixelColor(0, 9);
    QVERIFY2(rim.red() == 0 && std::abs(rim.green() - 128) <= 1, qPrintable(rim.name()));
    QCOMPARE(fill.grab().toImage().pixelColor(18, 1), QColor(Qt::green));
    QTest::mouseClick(&fill, Qt::LeftButton);
    QCOMPARE(session->colorPicker().value().target.title(), QString("Color Picker (Foreground Color)"));
}

void ColorSwatchTests::theTypeBarShowsAndPicksTheTextColour()
{
    const std::unique_ptr<EditorSession> session = sessionWithLayer(NavigationTool::type);
    TypeControls bar(*session);
    // Wide enough that the swatch shows in its scroll area.
    bar.resize(1400, bar.sizeHint().height());
    bar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&bar));
    QAbstractButton &colour = swatch(bar, "typeColor");
    QCOMPARE(colour.toolTip(), QString("Text color"));
    // No text open: the foreground, the next text's colour.
    session->setForegroundColor(PaletteColor{1, 0, 0});
    QCOMPARE(middle(colour), QColor(Qt::red));
    session->beginText(QPointF(10, 10), true);
    QCoreApplication::processEvents();
    PaintSpy repaint(colour);
    session->changeTextStyle([](LayerTextStyle &style) { style.blue = 1; style.red = 0; });
    QTRY_VERIFY(repaint.count > 0);
    QCOMPARE(middle(colour), QColor(Qt::blue));
    QTest::mouseClick(&colour, Qt::LeftButton);
    QCOMPARE(session->colorPicker().value().target.title(), QString("Color Picker (Text Color)"));
    QCOMPARE(session->colorPicker().value().original, (PaletteColor{0, 0, 1}));
}

void ColorSwatchTests::xAndDSwapAndRestoreThePalette()
{
    Canvas shown;
    NativeLayerList list(shown.session, &shown.window);
    list.setGeometry(0, 0, 100, 100);
    list.show();
    for (QWidget *target : {static_cast<QWidget *>(shown.canvas), static_cast<QWidget *>(&list)}) {
        shown.session.setForegroundColor(PaletteColor{1, 0, 0});
        QTest::keyClick(target, Qt::Key_X);
        QCOMPARE(shown.session.foregroundColor(), PaletteColor::white());
        QCOMPARE(shown.session.backgroundColor(), (PaletteColor{1, 0, 0}));
        // Shift adds nothing; Ctrl and Alt make other keys.
        QTest::keyClick(target, Qt::Key_X, Qt::ShiftModifier);
        QCOMPARE(shown.session.foregroundColor(), (PaletteColor{1, 0, 0}));
        for (const Qt::KeyboardModifier modifier : {Qt::ControlModifier, Qt::AltModifier, Qt::MetaModifier}) {
            QTest::keyClick(target, Qt::Key_X, modifier);
            QTest::keyClick(target, Qt::Key_D, modifier);
            QCOMPARE(shown.session.foregroundColor(), (PaletteColor{1, 0, 0}));
        }
        QTest::keyClick(target, Qt::Key_D);
        QCOMPARE(shown.session.foregroundColor(), PaletteColor::black());
        QCOMPARE(shown.session.backgroundColor(), PaletteColor::white());
    }
}

QTEST_MAIN(ColorSwatchTests)
#include "ColorSwatchTests.moc"
