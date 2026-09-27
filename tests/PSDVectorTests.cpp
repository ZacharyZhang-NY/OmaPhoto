#include "IO/PSD/PSDDocumentBuilder.h"
#include "IO/PSD/PSDVector.h"
#include "PSDFixture.h"
#include "PSDVectorBuilders.h"
#include "PSDVectorFixtures.h"
#include <QtTest>

// Swift's vector tests: shapes live, masks rasterized, sizes refused.
using namespace PSDVectorBuilders;

namespace {
std::vector<QPointF> box()
{
    return {QPointF(120, 30), QPointF(120, 80), QPointF(20, 80), QPointF(20, 30)};
}

std::optional<ImageImportError::Kind> refusal(const std::function<void()> &run)
{
    try {
        run();
    } catch (const ImageImportError &error) {
        return error.kind;
    }
    return std::nullopt;
}

PSDRecord shapeRecord(const QString &name, const PSDVector::Live &live)
{
    PSDRecord record = PSDFixture::record(name, live.image, live.bounds);
    record.kind = PSDLayerKind::vector;
    record.shape = live.style;
    record.shapeNotes = live.notes;
    return record;
}
}

class PSDVectorTests : public QObject {
    Q_OBJECT
private slots:
    void vectorMaskIsRasterizedWithFillAndStroke();
    void photoshopShapeExtrasRasterizeInPlace();
    void fillEllipseImportsAsALiveShape();
    void strokedRectangleImportsAsALiveShapeAndReportsTheStroke();
    void fourSharpCornersInferARectangleWithoutOrigination();
    void hugeOriginationSizeIsRejectedWithoutTrapping();
    void nonFiniteOriginationSizeIsIgnored();
    void hugeStrokeWidthIsRejectedWithoutTrapping();
    void sidesAndBudgetsRefuseAtTheirEdges();
    void knotCountsAndCurvesDecideTheShape();
    void subpathsCloseOpenAndStandApart();
    void fillsWindAntialiasAndMitersKeepTheirTips();
};

void PSDVectorTests::vectorMaskIsRasterizedWithFillAndStroke()
{
    const QSizeF canvas(200, 200);
    PSDVector::Extra extra{{"vmsk", vectorMask(canvas, box())},
                           {"SoCo", colorDescriptor(0, 110, 255)},
                           {"vstk", strokeStyle(true, false, 1, 255, 255, 0)}};
    const PSDVector::Raster raster = PSDVector::raster(extra, canvas).value();
    // Fractions of 2^24 fall short: the box floors outward.
    QCOMPARE(raster.bounds, QRectF(19, 29, 101, 51));
    QCOMPARE(raster.image.size(), QSize(101, 51));
    QCOMPARE(raster.image.pixel(50, 25), qRgb(0, 110, 255));
    extra["vstk"] = strokeStyle(true, true, 10, 255, 255, 0);
    extra["SoCo"] = colorDescriptor(0, 0, 0);
    const PSDVector::Raster stroked = PSDVector::raster(extra, canvas).value();
    // Half the width and a pixel more on each side.
    QCOMPARE(stroked.bounds, QRectF(13, 23, 113, 63));
    QCOMPARE(stroked.image.pixel(56, 31), qRgb(0, 0, 0));
    QCOMPARE(stroked.image.pixel(56, 7), qRgb(255, 255, 0));
    // A miter keeps the square corner; a bevel would not.
    QCOMPARE(stroked.image.pixel(2, 2), qRgb(255, 255, 0));
}

void PSDVectorTests::photoshopShapeExtrasRasterizeInPlace()
{
    const PSDVector::Raster circle = PSDVector::raster(PSDVectorFixtures::circle(), PSDVectorFixtures::canvas).value();
    QVERIFY(std::abs(circle.bounds.center().x() - 618) < 8);
    QVERIFY(std::abs(circle.bounds.center().y() - 677) < 8);
    QVERIFY(std::abs(circle.bounds.width() - 328) < 12);
    QVERIFY(std::abs(circle.bounds.height() - 328) < 12);
    const PSDVector::Raster rectangle = PSDVector::raster(PSDVectorFixtures::rectangle(), PSDVectorFixtures::canvas).value();
    QVERIFY(rectangle.bounds.width() > 640);
    QVERIFY(rectangle.bounds.height() > 170);
    QVERIFY(std::abs(rectangle.bounds.center().x() - 1268) < 20);
    QVERIFY(std::abs(rectangle.bounds.center().y() - 244) < 20);
}

void PSDVectorTests::fillEllipseImportsAsALiveShape()
{
    PSDVector::Extra extra = PSDVectorFixtures::circle();
    extra["vogk"] = originationData(5, QRectF(454, 513, 328, 328));
    const PSDVector::Live live = PSDVector::live(extra, PSDVectorFixtures::canvas).value();
    QVERIFY(live.style.kind == ShapeKind::ellipse);
    QVERIFY(std::abs(live.style.green - 110 / 255.0) < 0.01);
    QVERIFY(std::abs(live.style.blue - 1) < 0.01);
    QVERIFY(live.notes.empty());
    QCOMPARE(live.bounds, QRectF(454, 513, 328, 328));
    QCOMPARE(live.image.size(), QSize(328, 328));
    const PSDImport imported = PSDDocumentBuilder::makeImport(PSDDocument{1920, 1080, 72, {shapeRecord("cercle-bleu", live)}});
    QVERIFY(imported.layers[0].liveShape().value().style.kind == ShapeKind::ellipse);
    QVERIFY(imported.conversions.empty());
    QCOMPARE(imported.layers[0].liveShape().value().image, imported.layers[0].asset.value().identity());
}

void PSDVectorTests::strokedRectangleImportsAsALiveShapeAndReportsTheStroke()
{
    PSDVector::Extra extra = PSDVectorFixtures::rectangle();
    extra["vogk"] = originationData(2, QRectF(945, 153, 646, 182), {0, 0, 0, 0});
    const PSDVector::Live live = PSDVector::live(extra, PSDVectorFixtures::canvas).value();
    QVERIFY(live.style.kind == ShapeKind::rectangle);
    QCOMPARE(live.style.cornerRadius, 0.0);
    QCOMPARE(live.notes, std::vector<QString>{"The Photoshop stroke isn’t supported on shape layers and was omitted."});
    const PSDImport imported = PSDDocumentBuilder::makeImport(PSDDocument{1920, 1080, 72, {shapeRecord("rectangle-contour-jaune", live)}});
    QVERIFY(imported.layers[0].liveShape().value().style.kind == ShapeKind::rectangle);
    QCOMPARE(imported.conversions.size(), size_t(1));
    QCOMPARE(imported.conversions[0].layerName, QString("rectangle-contour-jaune"));
    QVERIFY(imported.conversions[0].message.contains("stroke"));
    // Radii within half a pixel round by the largest.
    extra["vogk"] = originationData(2, QRectF(945, 153, 646, 182), {12, 12.4, 12, 12});
    QCOMPARE(PSDVector::live(extra, PSDVectorFixtures::canvas).value().style.cornerRadius, 12.4);
    // Wider apart they refuse; the four sharp anchors decide instead.
    extra["vogk"] = originationData(2, QRectF(945, 153, 646, 182), {12, 12.6, 12, 12});
    const PSDVector::Live sharp = PSDVector::live(extra, PSDVectorFixtures::canvas).value();
    QCOMPARE(sharp.style.cornerRadius, 0.0);
    QVERIFY(sharp.bounds != QRectF(945, 153, 646, 182));
}

void PSDVectorTests::fourSharpCornersInferARectangleWithoutOrigination()
{
    const PSDVector::Extra extra{{"vmsk", vectorMask(QSizeF(200, 200), box())},
                                 {"SoCo", colorDescriptor(0, 110, 255)},
                                 {"vstk", strokeStyle(true, false, 1, 255, 255, 0)}};
    const PSDVector::Live live = PSDVector::live(extra, QSizeF(200, 200)).value();
    QVERIFY(live.style.kind == ShapeKind::rectangle);
    QVERIFY(live.notes.empty());
    QCOMPARE(live.bounds, QRectF(19, 29, 101, 51));
}

void PSDVectorTests::hugeOriginationSizeIsRejectedWithoutTrapping()
{
    const PSDVector::Extra extra{{"vogk", originationData(5, QRectF(0, 0, 1e20, 1e20))},
                                 {"SoCo", colorDescriptor(0, 110, 255)},
                                 {"vstk", strokeStyle(true, false, 1, 255, 255, 0)}};
    QCOMPARE(refusal([&] { PSDVector::live(extra, PSDVectorFixtures::canvas); }), std::optional(ImageImportError::Kind::tooLarge));
}

void PSDVectorTests::nonFiniteOriginationSizeIsIgnored()
{
    const PSDVector::Extra extra{{"vogk", originationData(5, QRectF(10, 10, qInf(), 100))},
                                 {"SoCo", colorDescriptor(0, 110, 255)},
                                 {"vstk", strokeStyle(true, false, 1, 255, 255, 0)}};
    QVERIFY(!PSDVector::live(extra, PSDVectorFixtures::canvas));
}

void PSDVectorTests::hugeStrokeWidthIsRejectedWithoutTrapping()
{
    const PSDVector::Extra extra{{"vmsk", vectorMask(QSizeF(200, 200), box())},
                                 {"SoCo", colorDescriptor(0, 0, 0)},
                                 {"vstk", strokeStyle(true, true, 1e20, 255, 255, 0)}};
    QCOMPARE(refusal([&] { PSDVector::raster(extra, QSizeF(200, 200)); }), std::optional(ImageImportError::Kind::tooLarge));
}

void PSDVectorTests::sidesAndBudgetsRefuseAtTheirEdges()
{
    const auto filled = [](QSizeF canvas, const std::vector<QPointF> &corners) {
        return PSDVector::Extra{{"vmsk", vectorMask(canvas, corners)}, {"SoCo", colorDescriptor(0, 0, 0)}};
    };
    // A side past 30,000 pixels, however few pixels in all.
    const QSizeF wide(40'000, 10);
    const PSDVector::Extra long_ = filled(wide, {QPointF(0, 0), QPointF(30'001, 0), QPointF(30'001, 1), QPointF(0, 1)});
    QCOMPARE(refusal([&] { PSDVector::raster(long_, wide); }), std::optional(ImageImportError::Kind::tooLarge));
    // The budget holds at its edge: 101 by 51 pixels.
    const PSDVector::Extra extra = filled(QSizeF(200, 200), box());
    QCOMPARE(PSDVector::raster(extra, QSizeF(200, 200), 5151).value().image.size(), QSize(101, 51));
    QCOMPARE(refusal([&] { PSDVector::raster(extra, QSizeF(200, 200), 5150); }), std::optional(ImageImportError::Kind::tooLarge));
    // A lone anchor still spends one pixel of the budget.
    const PSDVector::Extra dot = filled(QSizeF(200, 200), {QPointF(10.5, 10.5)});
    QCOMPARE(PSDVector::raster(dot, QSizeF(200, 200), 1).value().image.size(), QSize(1, 1));
    QCOMPARE(refusal([&] { PSDVector::raster(dot, QSizeF(200, 200), 0); }), std::optional(ImageImportError::Kind::tooLarge));
    // A stroke narrower than nothing is refused.
    const PSDVector::Extra negative{{"vmsk", vectorMask(QSizeF(200, 200), box())}, {"vstk", strokeStyle(false, true, -5, 255, 0, 0)}};
    QCOMPARE(refusal([&] { PSDVector::raster(negative, QSizeF(200, 200)); }), std::optional(ImageImportError::Kind::tooLarge));
}

void PSDVectorTests::knotCountsAndCurvesDecideTheShape()
{
    const QSizeF canvas(200, 200);
    // A knot past the declared count stays out.
    std::vector<QPointF> five = box();
    five.push_back(QPointF(190, 190));
    const PSDVector::Extra counted{{"vmsk", vectorMask(canvas, {Subpath{five, true, 4}})}, {"SoCo", colorDescriptor(0, 110, 255)}};
    QCOMPARE(PSDVector::live(counted, canvas).value().bounds, QRectF(19, 29, 101, 51));
    QCOMPARE(PSDVector::raster(counted, canvas).value().bounds, QRectF(19, 29, 101, 51));
    // Handles off their anchors make curves, which infer no rectangle.
    const PSDVector::Extra curved{{"vmsk", vectorMask(canvas, {Subpath{box(), true, std::nullopt, QPointF(5, 0)}})}, {"SoCo", colorDescriptor(0, 110, 255)}};
    QVERIFY(!PSDVector::live(curved, canvas));
    QVERIFY(PSDVector::raster(curved, canvas).value().bounds.width() > 101);
}

void PSDVectorTests::subpathsCloseOpenAndStandApart()
{
    const QSizeF canvas(200, 200);
    const std::vector<QPointF> far{QPointF(190, 150), QPointF(190, 190), QPointF(150, 190), QPointF(150, 150)};
    const auto stroked = [&](const std::vector<Subpath> &subpaths) {
        return PSDVector::raster({{"vmsk", vectorMask(canvas, subpaths)}, {"vstk", strokeStyle(false, true, 4, 255, 0, 0)}}, canvas).value();
    };
    const auto ink = [](const PSDVector::Raster &raster, QPointF at) { return qAlpha(raster.image.pixel((at - raster.bounds.topLeft()).toPoint())); };
    // A closed subpath strokes its closing edge; open ones not.
    const PSDVector::Raster both = stroked({Subpath{box()}, Subpath{far}});
    QCOMPARE(ink(both, QPointF(70, 30)), 255);
    QCOMPARE(ink(both, QPointF(170, 150)), 255);
    const PSDVector::Raster open = stroked({Subpath{box(), false}, Subpath{far}});
    QCOMPARE(ink(open, QPointF(70, 30)), 0);
    QCOMPARE(ink(open, QPointF(120, 55)), 255);
    // Each subpath begins anew: nothing joins them.
    QCOMPARE(ink(both, QPointF(105, 90)), 0);
}

void PSDVectorTests::fillsWindAntialiasAndMitersKeepTheirTips()
{
    const QSizeF canvas(200, 200);
    // Two squares turning the same way: their overlap stays filled.
    const std::vector<QPointF> first{QPointF(20, 20), QPointF(80, 20), QPointF(80, 80), QPointF(20, 80)};
    const std::vector<QPointF> second{QPointF(50, 50), QPointF(110, 50), QPointF(110, 110), QPointF(50, 110)};
    const PSDVector::Raster overlap =
        PSDVector::raster({{"vmsk", vectorMask(canvas, {Subpath{first}, Subpath{second}})}, {"SoCo", colorDescriptor(0, 0, 0)}}, canvas).value();
    QCOMPARE(qAlpha(overlap.image.pixel((QPointF(65, 65) - overlap.bounds.topLeft()).toPoint())), 255);
    // A circle's rim takes partial coverage.
    const QImage circle = PSDVector::raster(PSDVectorFixtures::circle(), PSDVectorFixtures::canvas).value().image;
    int partial = 0;
    for (int y = 0; y < circle.height(); ++y)
        for (int x = 0; x < circle.width(); ++x)
            partial += qAlpha(circle.pixel(x, y)) > 0 && qAlpha(circle.pixel(x, y)) < 255;
    QVERIFY2(partial > 200, qPrintable(QString::number(partial)));
    // A 30° apex keeps its miter at limit 10.
    const double reach = 100 * std::tan(15 * M_PI / 180);
    const std::vector<QPointF> apex{QPointF(100, 20), QPointF(100 + reach, 120), QPointF(100 - reach, 120)};
    const std::vector<QPointF> spot{QPointF(100, 0), QPointF(101, 0), QPointF(101, 1), QPointF(100, 1)};
    const PSDVector::Raster tip =
        PSDVector::raster({{"vmsk", vectorMask(canvas, {Subpath{apex}, Subpath{spot}})}, {"vstk", strokeStyle(false, true, 4, 255, 0, 0)}}, canvas).value();
    // The tip's edge crosses this pixel; a bevel clears it.
    QVERIFY(qAlpha(tip.image.pixel((QPointF(100, 14) - tip.bounds.topLeft()).toPoint())) > 100);
}

QTEST_GUILESS_MAIN(PSDVectorTests)
#include "PSDVectorTests.moc"
