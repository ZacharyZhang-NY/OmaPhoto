#include "Document/EditorSession.h"
#include <QtTest>

// Swift's Smoothing: the brush trails the pointer on a string.
namespace {
std::unique_ptr<EditorSession> painting(double smoothing, NavigationTool tool = NavigationTool::brush)
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(200, 200, true);
    session->selectTool(tool);
    BrushSettings brush = session->brushSettings();
    brush.diameter = 4;
    brush.hardness = 1;
    brush.smoothing = smoothing;
    session->setBrushSettings(brush);
    return session;
}

QPointF painted(const EditorSession &session)
{
    return session.shiftLineStart().value();
}

// The committed pixels at a document point, clear outside them.
int ink(const EditorSession &session, int x, int y)
{
    const ImageLayer &layer = session.document().value().layers.back();
    const QImage image = layer.asset.value().image();
    const QPoint at = QPoint(x, y) - layer.origin().toPoint();
    if (layer.size() != QSizeF(image.size()))
        throw std::runtime_error("the layer is scaled");
    return image.rect().contains(at) ? qAlpha(image.pixel(at)) : 0;
}
}

class BrushSmoothingTests : public QObject {
    Q_OBJECT
private slots:
    void theBrushWaitsUntilTheStringIsTaut();
    void theStrokeEndsWhereTheHandDid();
    void theStringIsMeasuredOnScreen();
    void onlyTheBrushSmoothsAndACancelLetsGo();
};

void BrushSmoothingTests::theBrushWaitsUntilTheStringIsTaut()
{
    const auto session = painting(10);
    session->beginBrush(QPointF(50, 50));
    session->continueBrush(QPointF(55, 50));
    QCOMPARE(painted(*session), QPointF(50, 50));
    // Taut at exactly its length, the string still holds.
    session->continueBrush(QPointF(60, 50));
    QCOMPARE(painted(*session), QPointF(50, 50));
    session->continueBrush(QPointF(70, 50));
    QCOMPARE(painted(*session), QPointF(60, 50));
    // Pulled at an angle, the brush moves along the string.
    session->continueBrush(QPointF(70, 60));
    const double reach = 10 * std::sqrt(0.5);
    QVERIFY(std::abs(painted(*session).x() - (70 - reach)) < 1e-9 && std::abs(painted(*session).y() - (60 - reach)) < 1e-9);
    // Without Smoothing the brush is the pointer.
    const auto exact = painting(0);
    exact->beginBrush(QPointF(50, 50));
    exact->continueBrush(QPointF(51, 50));
    QCOMPARE(painted(*exact), QPointF(51, 50));
}

void BrushSmoothingTests::theStrokeEndsWhereTheHandDid()
{
    const auto session = painting(10);
    session->beginBrush(QPointF(50, 50));
    session->continueBrush(QPointF(80, 50));
    QVERIFY(session->finishBrushImmediately());
    QVERIFY(ink(*session, 79, 50) > 0 && ink(*session, 70, 50) > 0);
    QCOMPARE(ink(*session, 86, 50), 0);
    // A string never pulled taut still ends at the pointer.
    const auto still = painting(10);
    still->beginBrush(QPointF(50, 50));
    still->continueBrush(QPointF(57, 50));
    QVERIFY(still->finishBrushImmediately());
    QVERIFY(ink(*still, 56, 50) > 0);
    QCOMPARE(ink(*still, 62, 50), 0);
}

void BrushSmoothingTests::theStringIsMeasuredOnScreen()
{
    const auto session = painting(10);
    session->viewport.setZoom(2, QPointF(), session->document().value().size());
    session->beginBrush(QPointF(50, 50));
    session->continueBrush(QPointF(60, 50));
    QCOMPARE(painted(*session), QPointF(55, 50));
}

void BrushSmoothingTests::onlyTheBrushSmoothsAndACancelLetsGo()
{
    const auto healing = painting(10, NavigationTool::spotHealing);
    healing->beginBrush(QPointF(50, 50));
    healing->continueBrush(QPointF(52, 50));
    QCOMPARE(painted(*healing), QPointF(52, 50));
    // The eraser is the Brush in another mode: it smooths.
    const auto erasing = painting(10);
    erasing->setBrushMode(BrushToolMode::erase);
    erasing->beginBrush(QPointF(50, 50));
    erasing->continueBrush(QPointF(55, 50));
    QCOMPARE(painted(*erasing), QPointF(50, 50));
    // A cancelled stroke leaves no anchor for the next.
    const auto session = painting(10);
    session->beginBrush(QPointF(50, 50));
    session->continueBrush(QPointF(80, 50));
    session->cancelBrush();
    session->beginBrush(QPointF(100, 100));
    session->continueBrush(QPointF(115, 100));
    QCOMPARE(painted(*session), QPointF(105, 100));
    // A cancel drops the hand's point: nothing lands.
    session->cancelBrush();
    QVERIFY(session->finishBrushImmediately());
}

QTEST_GUILESS_MAIN(BrushSmoothingTests)
#include "BrushSmoothingTests.moc"
