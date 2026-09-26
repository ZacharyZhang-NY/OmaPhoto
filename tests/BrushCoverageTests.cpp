#include "BrushFixtures.h"
#include <cmath>

// The CPU coverage against the shader's own falloff, in float.
namespace {
// One dab's alpha as the shader computes it, capped.
int shaderAlpha(float distanceSquared, float radius, float hardness)
{
    const float t = std::clamp((std::sqrt(distanceSquared) / radius - hardness) / (1.0f - hardness), 0.0f, 1.0f);
    const float coverage = std::max(0.0f, (std::exp(-2.5f * t * t) - std::exp(-2.5f)) / (1.0f - std::exp(-2.5f)));
    return int(std::lround(255 * std::min(coverage, 0.999f)));
}

// The shader's optical density at d², float for float.
float shaderDensity(float distanceSquared, float radius, float hardness)
{
    const float t = std::clamp((std::sqrt(distanceSquared) / radius - hardness) / (1.0f - hardness), 0.0f, 1.0f);
    const float coverage = std::max(0.0f, (std::exp(-2.5f * t * t) - std::exp(-2.5f)) / (1.0f - std::exp(-2.5f)));
    return -std::log(std::max(1.0f - coverage, 0.001f));
}

// The shader's quadrature of one level segment, float for float.
float segmentDensity(float px, float py, float x0, float x1, float y, float radius, float hardness, float spacing)
{
    const float projection = px - x0, perpendicularSquared = (py - y) * (py - y);
    if (perpendicularSquared >= radius * radius)
        return 0;
    const float reach = std::sqrt(radius * radius - perpendicularSquared);
    const float lo = std::max(0.0f, projection - reach), hi = std::min(x1 - x0, projection + reach);
    if (hi <= lo)
        return 0;
    const float midpoint = (lo + hi) * 0.5f, half = (hi - lo) * 0.5f;
    constexpr float nodes[4] = {0.1834346425f, 0.5255324099f, 0.7966664774f, 0.9602898565f};
    constexpr float weights[4] = {0.3626837834f, 0.3137066459f, 0.2223810345f, 0.1012285363f};
    float integral = 0;
    for (int i = 0; i < 4; ++i) {
        const float a = midpoint - half * nodes[i] - projection, b = midpoint + half * nodes[i] - projection;
        integral += weights[i] * (shaderDensity(perpendicularSquared + a * a, radius, hardness) + shaderDensity(perpendicularSquared + b * b, radius, hardness));
    }
    return integral * half / spacing;
}

// A lone dab at (x, 0.5), read at (pixel, 0).
int dabAlpha(double diameter, double hardness, double x, int pixel)
{
    const auto paint = stroke(pixel + 24, 4, red(diameter, hardness));
    paint->append(QPointF(x, 0.5));
    return alpha(preview(*paint, QSizeF(pixel + 24, 4)), pixel, 0);
}
}

class BrushCoverageTests : public QObject {
    Q_OBJECT
private slots:
    void aSoftDabFollowsItsFalloffWithinAStep();
    void nearFullHardnessTheFormulaReplacesTheTable();
    void aStrokesSegmentsFollowTheShaderEitherSideOfTheSwitch();
    void farAlongAStrokeTheNodesRoundAsTheShaders();
};

void BrushCoverageTests::aSoftDabFollowsItsFalloffWithinAStep()
{
    // One dab on a pixel's centre: the shader's falloff, capped.
    for (const auto &[diameter, hardness] : {std::pair{100.0, 0.0}, std::pair{60.0, 0.5}}) {
        const auto paint = stroke(200, 200, red(diameter, hardness));
        paint->append(QPointF(100.5, 100.5));
        const QImage live = preview(*paint, QSizeF(200, 200));
        for (const int distance : {0, 10, 20, 25, 29, 38, 45, 49}) {
            const int expected = shaderAlpha(float(distance * distance), float(diameter / 2), float(hardness));
            const int found = alpha(live, 100 + distance, 100);
            QVERIFY2(std::abs(found - expected) <= 1, qPrintable(QStringLiteral("%1 px at %2: %3, not %4").arg(diameter).arg(distance).arg(found).arg(expected)));
        }
    }
}

void BrushCoverageTests::nearFullHardnessTheFormulaReplacesTheTable()
{
    // The reviewers' two pixels: the shader gives 247 and 188.
    QCOMPARE(dabAlpha(2000, 0.9995, 0.94505, 1000), 247);
    const float probe = 5.5f - float(0.500033379);
    QCOMPARE(shaderAlpha(probe * probe, 5, 0.99999f), 188);
    QVERIFY(std::abs(dabAlpha(10, 0.99999, 0.500033379, 5) - 188) <= 1);
    // Across the fade, small and large tips, around the switch.
    for (const double hardness : {0.999, 0.9995, 0.9996, 0.9999, 0.99999, 0.999999}) {
        for (const auto &[diameter, pixel] : {std::pair{10.0, 5}, std::pair{2000.0, 1000}}) {
            for (const double fade : {0.1, 0.3, 0.5, 0.7, 0.9}) {
                const double radius = diameter / 2, x = pixel + 0.5 - radius * (hardness + fade * (1 - hardness));
                const float dx = float(pixel + 0.5) - float(x);
                const int expected = shaderAlpha(dx * dx, float(radius), float(hardness)), found = dabAlpha(diameter, hardness, x, pixel);
                QVERIFY2(std::abs(found - expected) <= 1,
                         qPrintable(QStringLiteral("%1 px, %2 hard, dab at %3: %4, not %5").arg(diameter).arg(hardness).arg(x, 0, 'g', 12).arg(found).arg(expected)));
            }
        }
    }
}

void BrushCoverageTests::aStrokesSegmentsFollowTheShaderEitherSideOfTheSwitch()
{
    // A dab where the stroke starts, then one level segment.
    for (const double hardness : {0.5, 0.9995, 0.99999, 0.999999}) {
        for (const double diameter : {10.0, 400.0}) {
            const float radius = float(diameter / 2), spacing = float(std::max(0.25, diameter * BrushStroke::spacingFraction(hardness)));
            const int width = int(diameter) + 40;
            for (const double fade : {0.1, 0.5, 0.9}) {
                const double y = 5.5 - radius * (hardness + fade * (1 - hardness)), x0 = -radius - 5.0, x1 = width + radius + 5.0;
                const auto paint = stroke(width, 12, red(diameter, hardness));
                paint->append(QPointF(x0, y));
                paint->append(QPointF(x1, y));
                const QImage live = preview(*paint, QSizeF(width, 12));
                for (int x = 0; x < width; x += 7) {
                    const float px = float(x) + 0.5f, dx = px - float(x0), dy = 5.5f - float(y);
                    const float total = shaderDensity(dx * dx + dy * dy, radius, float(hardness))
                        + segmentDensity(px, 5.5f, float(x0), float(x1), float(y), radius, float(hardness), spacing);
                    const int expected = int(std::round(255.0f * (1.0f - std::exp(-std::min(total, 20.0f))))), found = alpha(live, x, 5);
                    QVERIFY2(std::abs(found - expected) <= 1,
                             qPrintable(QStringLiteral("%1 px, %2 hard, fade %3, x %4: %5, not %6").arg(diameter).arg(hardness).arg(fade).arg(x).arg(found).arg(expected)));
                }
            }
        }
    }
}

void BrushCoverageTests::farAlongAStrokeTheNodesRoundAsTheShaders()
{
    // 8192 pixels along, node offsets round as the shader's do.
    const auto paint = stroke(30000, 16, red(10, 0.999999));
    paint->append(QPointF(10, 5.5000045));
    paint->append(QPointF(29990, 5.5000045));
    const float spacing = float(std::max(0.25, 10 * BrushStroke::spacingFraction(0.999999)));
    const float y = float(5.5000045), total = segmentDensity(8202.5f, 10.5f, 10, 29990, y, 5, float(0.999999), spacing);
    const int expected = int(std::round(255.0f * (1.0f - std::exp(-std::min(total, 20.0f)))));
    QCOMPARE(expected, 17);
    QCOMPARE(alpha(preview(*paint, QSizeF(30000, 16)), 8202, 10), 17);
    paint->flush();
    QCOMPARE(alpha(preview(*paint, QSizeF(30000, 16)), 8202, 10), 17);
}

QTEST_GUILESS_MAIN(BrushCoverageTests)
#include "BrushCoverageTests.moc"
