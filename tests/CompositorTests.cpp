#include "Document/EditorSession.h"
#include "Rendering/CanvasViewport.h"
#include <QtTest>

class CompositorTests : public QObject {
    Q_OBJECT
    const QSizeF document{1920, 1080};
private slots:
    void dimensionValidation();
    void actualPixelsAndRoundTrip_data();
    void actualPixelsAndRoundTrip();
    void zoomKeepsCursorPixelFixed();
    void fitAndResizeModes();
    void limitsAndNewDocumentReset();
    void fitWaitsForAViewSize();
    void backingScaleNeverDropsBelowOne();
    void viewportsCompareExactly();
};

void CompositorTests::dimensionValidation()
{
    QCOMPARE(CanvasDocument::validDimension("0"), std::nullopt);
    QCOMPARE(CanvasDocument::validDimension("-1"), std::nullopt);
    QCOMPARE(CanvasDocument::validDimension("1.5"), std::nullopt);
    QCOMPARE(CanvasDocument::validDimension("30001"), std::nullopt);
    QCOMPARE(CanvasDocument::validDimension("9999999999999999999999"), std::nullopt);
    QCOMPARE(CanvasDocument::validDimension(" 1920 "), std::optional<int>(1920));
    QCOMPARE(CanvasDocument::validDimension("30000"), std::optional<int>(30000));
    QCOMPARE(CanvasDocument::validDimension("\t+64\u00a0"), std::optional<int>(64));
    QCOMPARE(CanvasDocument::validDimension("1920\n"), std::nullopt);
    QCOMPARE(CanvasDocument::validDimension(QString::fromUtf8("1920\0px", 7)), std::nullopt);
    QCOMPARE(CanvasDocument::validDimension("19 20"), std::nullopt);
    QCOMPARE(CanvasDocument::validDimension("+"), std::nullopt);
    QCOMPARE(CanvasDocument::validDimension(""), std::nullopt);
    QCOMPARE(CanvasDocument::validDimension(QString::fromUtf8("\u0661\u0662")), std::nullopt);
}

void CompositorTests::actualPixelsAndRoundTrip_data()
{
    QTest::addColumn<double>("backing");
    QTest::newRow("1x") << 1.0;
    QTest::newRow("2x") << 2.0;
}

void CompositorTests::actualPixelsAndRoundTrip()
{
    QFETCH(double, backing);
    CanvasViewport viewport;
    viewport.resize(QSizeF(800, 600), backing, std::nullopt);
    for (double zoom : {0.25, 1.0, 3.75}) {
        viewport.setZoom(zoom, viewport.center(), document);
        viewport.translate(QSizeF(73.5, -44.25));
        const QPointF pixel(183.25, 837.5);
        const QPointF viewPoint = viewport.viewPoint(pixel, document);
        const QPointF result = viewport.documentPoint(viewPoint, document);
        QVERIFY(std::abs(result.x() - pixel.x()) < 0.000001);
        QVERIFY(std::abs(result.y() - pixel.y()) < 0.000001);
        QVERIFY(std::abs(viewport.documentRect(document).width() * backing - document.width() * zoom) < 0.000001);
    }
}

void CompositorTests::zoomKeepsCursorPixelFixed()
{
    CanvasViewport viewport;
    viewport.resize(QSizeF(1000, 700), 2, document);
    const QPointF anchor(157, 221);
    const QPointF before = viewport.documentPoint(anchor, document);
    QVERIFY(viewport.followsFit());
    viewport.setZoom(4, anchor, document);
    QCOMPARE(viewport.zoom(), 4.0);
    QVERIFY(!viewport.followsFit());
    const QPointF after = viewport.documentPoint(anchor, document);
    QVERIFY(std::abs(before.x() - after.x()) < 0.000001);
    QVERIFY(std::abs(before.y() - after.y()) < 0.000001);
}

void CompositorTests::fitAndResizeModes()
{
    CanvasViewport viewport;
    viewport.resize(QSizeF(800, 600), 2, document);
    const QRectF rect = viewport.documentRect(document);
    QVERIFY(rect.width() <= 704.000001);
    QVERIFY(rect.height() <= 504.000001);
    QCOMPARE(rect.center().x(), 400.0);
    QCOMPARE(rect.center().y(), 300.0);
    viewport.translate(QSizeF(60, -35));
    QCOMPARE(viewport.pan, QSizeF(60, -35));
    QVERIFY(!viewport.followsFit());
    const QPointF before = viewport.documentPoint(viewport.center(), document);
    const double zoom = viewport.zoom();
    viewport.resize(QSizeF(1200, 800), 1, document);
    const QPointF after = viewport.documentPoint(viewport.center(), document);
    QCOMPARE(viewport.zoom(), zoom);
    QVERIFY(std::abs(before.x() - after.x()) < 0.000001);
    QVERIFY(std::abs(before.y() - after.y()) < 0.000001);
    viewport.fit(document);
    QCOMPARE(viewport.pan, QSizeF(0, 0));
    QVERIFY(viewport.followsFit());
}

void CompositorTests::limitsAndNewDocumentReset()
{
    EditorSession session;
    session.viewport.resize(QSizeF(800, 600), 2, std::nullopt);
    session.zoom(5);
    QCOMPARE(session.viewport.zoom(), 1.0);
    session.createDocument(1920, 1080);
    session.zoom(1000);
    QCOMPARE(session.viewport.zoom(), CanvasViewport::maximumZoom);
    session.zoom(0);
    QCOMPARE(session.viewport.zoom(), CanvasViewport::minimumZoom);
    session.zoom(std::nan(""));
    QCOMPARE(session.viewport.zoom(), CanvasViewport::minimumZoom);
    session.viewport.translate(QSizeF(999, 888));
    session.createDocument(400, 300);
    QVERIFY(session.viewport.followsFit());
    QCOMPARE(session.viewport.pan, QSizeF(0, 0));
    QCOMPARE(session.document().value().width, 400);
    session.createDocument(0, 200);
    session.createDocument(200, 0);
    session.createDocument(30'001, 200);
    session.createDocument(200, 30'001);
    QCOMPARE(session.document().value().width, 400);
    session.createDocument(30'000, 1);
    QCOMPARE(session.document().value().size(), QSizeF(30'000, 1));
    session.createDocument(1, 30'000);
    QCOMPARE(session.document().value().size(), QSizeF(1, 30'000));
    // Zooming about a point keeps that document pixel under it.
    session.createDocument(1920, 1080);
    const QPointF anchor(157, 221);
    const QPointF before = session.viewport.documentPoint(anchor, session.document().value().size());
    session.zoom(4, anchor);
    const QPointF after = session.viewport.documentPoint(anchor, session.document().value().size());
    QVERIFY(std::abs(before.x() - after.x()) < 0.000001 && std::abs(before.y() - after.y()) < 0.000001);
    session.viewport.translate(QSizeF(10, 10));
    session.fit();
    QVERIFY(session.viewport.followsFit());
}

void CompositorTests::fitWaitsForAViewSize()
{
    CanvasViewport viewport;
    viewport.setZoom(2, QPointF(0, 0), document);
    viewport.fit(document);
    QVERIFY(viewport.followsFit());
    QCOMPARE(viewport.zoom(), 2.0);
    viewport.resize(QSizeF(1056, 636), 1, document);
    QCOMPARE(viewport.zoom(), 0.5);
    QCOMPARE(viewport.pan, QSizeF(0, 0));
}

void CompositorTests::backingScaleNeverDropsBelowOne()
{
    CanvasViewport viewport;
    viewport.resize(QSizeF(800, 600), 0.5, std::nullopt);
    QCOMPARE(viewport.backingScale, 1.0);
    QCOMPARE(viewport.pointsPerPixel(), 1.0);
}

void CompositorTests::viewportsCompareExactly()
{
    CanvasViewport viewport;
    viewport.resize(QSizeF(800, 600), 2, std::nullopt);
    viewport.setZoom(1.5, viewport.center(), document);
    QVERIFY(viewport == CanvasViewport(viewport));

    CanvasViewport other = viewport;
    other.viewSize = QSizeF(800 + 1e-10, 600);
    QVERIFY(!(viewport == other));
    other = viewport;
    other.viewSize = QSizeF(800, 600 + 1e-10);
    QVERIFY(!(viewport == other));
    other = viewport;
    other.backingScale = 2 + 1e-13;
    QVERIFY(!(viewport == other));
    other = viewport;
    other.pan = QSizeF(1e-13, 0);
    QVERIFY(!(viewport == other));
    other = viewport;
    other.pan = QSizeF(0, 1e-13);
    QVERIFY(!(viewport == other));

    CanvasViewport zoomed = viewport;
    zoomed.setZoom(1.5 + 1e-13, zoomed.center(), document);
    QVERIFY(zoomed.pan.width() == viewport.pan.width() && zoomed.pan.height() == viewport.pan.height());
    QCOMPARE(zoomed.followsFit(), viewport.followsFit());
    QVERIFY(!(viewport == zoomed));

    CanvasViewport unsized;
    unsized.translate(QSizeF(0, 0));
    CanvasViewport following = unsized;
    following.fit(document);
    QCOMPARE(following.zoom(), unsized.zoom());
    QVERIFY(following.followsFit() && !unsized.followsFit());
    QVERIFY(!(unsized == following));
}

QTEST_GUILESS_MAIN(CompositorTests)
#include "CompositorTests.moc"
