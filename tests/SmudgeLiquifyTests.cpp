#include "BrushFixtures.h"
#include "Document/EditorSession.h"
#include "Document/PixelAdjust.h"
#include "IO/ImageExporter.h"
#include "SessionFixtures.h"
#include <cmath>

// The Smear's warps against Swift's own arithmetic, replayed here.
namespace {
// Swift's WarpStroke, written again from SmudgeLiquify.swift.
struct Oracle {
    std::vector<uchar> pixels;
    int width, height;
    double diameter, hardness, strength;
    bool smudging;
    std::vector<float> carried, scratch;
    std::optional<QPointF> last;

    int radius() const { return int(std::ceil(diameter / 2)); }
    float weight(float u) const
    {
        if (!(u < 1))
            return 0;
        const float h = float(hardness);
        if (!(u > h))
            return 1;
        const float t = (1 - u) / (1 - h);
        return t * t * (3 - 2 * t);
    }
    void pickUp(QPointF c)
    {
        const int r = radius(), side = 2 * r + 1;
        carried.assign(size_t(side * side * 4), 0);
        const int cx = int(std::round(c.x())), cy = int(std::round(c.y()));
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                const int y = cy + dy, x = cx + dx;
                if (y < 0 || y >= height || x < 0 || x >= width)
                    continue;
                for (int k = 0; k < 4; ++k)
                    carried[size_t(((dy + r) * side + dx + r) * 4 + k)] = float(pixels[size_t((y * width + x) * 4 + k)]);
            }
    }
    void smudge(QPointF c)
    {
        const int r = radius(), side = 2 * r + 1;
        const int cx = int(std::round(c.x())), cy = int(std::round(c.y()));
        const float keep = float(strength), invR = 1 / float(diameter / 2);
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                const int y = cy + dy, x = cx + dx;
                if (y < 0 || y >= height || x < 0 || x >= width)
                    continue;
                const float w = weight(std::sqrt(float(dx * dx + dy * dy)) * invR);
                if (!(w > 0))
                    continue;
                for (int k = 0; k < 4; ++k) {
                    uchar &p = pixels[size_t((y * width + x) * 4 + k)];
                    float &carry = carried[size_t(((dy + r) * side + dx + r) * 4 + k)];
                    const float under = float(p), painted = under + (carry - under) * w;
                    p = uchar(std::max(0.0f, std::min(255.0f, std::round(painted))));
                    carry = painted + (carry - painted) * keep;
                }
            }
    }
    void push(QPointF a, QPointF b)
    {
        const int r = radius();
        const float mx = float(b.x() - a.x()) * float(strength), my = float(b.y() - a.y()) * float(strength);
        const int margin = int(std::ceil(std::max(std::abs(mx), std::abs(my)))) + 2;
        const int cx = int(std::round(b.x())), cy = int(std::round(b.y()));
        const int x0 = std::max(0, cx - r - margin), x1 = std::min(width - 1, cx + r + margin);
        const int y0 = std::max(0, cy - r - margin), y1 = std::min(height - 1, cy + r + margin);
        if (x0 > x1 || y0 > y1)
            return;
        const int cw = x1 - x0 + 1, ch = y1 - y0 + 1;
        scratch.assign(size_t(cw * ch * 4), 0);
        for (int y = 0; y < ch; ++y)
            for (int x = 0; x < cw; ++x)
                for (int k = 0; k < 4; ++k)
                    scratch[size_t((y * cw + x) * 4 + k)] = float(pixels[size_t(((y + y0) * width + x + x0) * 4 + k)]);
        const float invR = 1 / float(diameter / 2);
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                const int y = cy + dy, x = cx + dx;
                if (y < y0 || y > y1 || x < x0 || x > x1)
                    continue;
                const float w = weight(std::sqrt(float(dx * dx + dy * dy)) * invR);
                if (!(w > 0))
                    continue;
                const float sx = std::min(float(cw - 1), std::max(0.0f, float(x - x0) - mx * w));
                const float sy = std::min(float(ch - 1), std::max(0.0f, float(y - y0) - my * w));
                const int ix = std::min(cw - 2, int(sx)), iy = std::min(ch - 2, int(sy));
                if (ix < 0 || iy < 0)
                    continue;
                const float fx = sx - float(ix), fy = sy - float(iy);
                const auto at = [&](int ox, int oy, int k) { return scratch[size_t(((iy + oy) * cw + ix + ox) * 4 + k)]; };
                for (int k = 0; k < 4; ++k) {
                    const float top = at(0, 0, k) + (at(1, 0, k) - at(0, 0, k)) * fx;
                    const float bottom = at(0, 1, k) + (at(1, 1, k) - at(0, 1, k)) * fx;
                    pixels[size_t((y * width + x) * 4 + k)] = uchar(std::max(0.0f, std::min(255.0f, std::round(top + (bottom - top) * fy))));
                }
            }
    }
    void append(QPointF point)
    {
        if (!last) {
            last = point;
            if (smudging)
                pickUp(point);
            return;
        }
        const QPointF from = *last;
        const double distance = std::hypot(point.x() - from.x(), point.y() - from.y());
        const double spacing = std::max(1.0, diameter * (smudging ? 0.08 : 0.025));
        if (distance < spacing)
            return;
        const int steps = int(std::ceil(distance / spacing));
        QPointF previous = from;
        for (int step = 1; step <= steps; ++step) {
            const double t = double(step) / double(steps);
            const QPointF next(from.x() + (point.x() - from.x()) * t, from.y() + (point.y() - from.y()) * t);
            if (smudging)
                smudge(next);
            else
                push(previous, next);
            previous = next;
        }
        last = point;
    }
};

// Red left, blue right, a green block below.
QImage scene(int width, int height)
{
    QImage image = BrushRaster::context(width, height, false);
    QPainter painter(&image);
    painter.fillRect(QRect(0, 0, width / 2, height), QColor(255, 0, 0));
    painter.fillRect(QRect(width / 2, 0, width - width / 2, height), QColor(0, 0, 255));
    painter.fillRect(QRect(width / 4, height * 3 / 4, width / 2, height / 4), QColor(0, 160, 0, 200));
    return image;
}

Oracle oracle(const QImage &image, const BrushSettings &settings, bool smudging)
{
    std::vector<uchar> bytes(size_t(image.width() * image.height() * 4));
    for (int y = 0; y < image.height(); ++y)
        std::memcpy(bytes.data() + size_t(y * image.width() * 4), image.constScanLine(y), size_t(image.width() * 4));
    return Oracle{bytes, image.width(), image.height(), std::max(2.0, settings.diameter), std::min(0.98, std::max(0.0, settings.hardness)),
                  std::min(1.0, std::max(0.01, settings.opacity)), smudging, {}, {}, std::nullopt};
}

// Every byte the stroke made, against the oracle's.
int differences(const WarpStroke &stroke, const Oracle &expected)
{
    int found = 0;
    for (int y = 0; y < stroke.height; ++y)
        found += std::memcmp(stroke.image().constScanLine(y), expected.pixels.data() + size_t(y * stroke.width * 4), size_t(stroke.width * 4)) != 0;
    return found;
}

std::vector<int> exported(const EditorSession &session, int x, int y)
{
    return pixel(ImageExporter::render(session.projectSnapshot().value()).image, x, y);
}

// The scene as one layer, the Smear chosen in `mode`.
void smearing(EditorSession &session, BlurToolMode mode, const BrushSettings &settings)
{
    session.createDocument(80, 40);
    const QImage image = scene(80, 40);
    session.insert(ImportedImage(image, image, QStringLiteral("Scene")));
    session.selectTool(NavigationTool::blur);
    session.setBlurMode(mode);
    session.setBrushSettings(settings);
}
}

class SmudgeLiquifyTests : public QObject {
    Q_OBJECT
private slots:
    void liquifyAndSmudgeFollowSwiftsArithmetic_data();
    void liquifyAndSmudgeFollowSwiftsArithmetic();
    void theSettingsAreClampedAsSwiftClampsThem();
    void liquifyCommitsAlongThePathAsOneStep();
    void theCommitLandsAllTheWarpMoved();
    void smudgeCarriesColourAndNamesItsStep();
    void aWarpReplacesSoClearPixelsClear();
    void aMaskIsNeverWarped();
    void aWarpHoldsTheSessionUntilItEnds();
    void aLayerChangedUnderTheWarpTakesNothing();
    void tabStepsTheSmearsModes();
};

void SmudgeLiquifyTests::liquifyAndSmudgeFollowSwiftsArithmetic_data()
{
    QTest::addColumn<bool>("smudging");
    QTest::addColumn<double>("diameter");
    QTest::addColumn<double>("hardness");
    QTest::addColumn<double>("strength");
    QTest::newRow("liquify") << false << 20.0 << 0.5 << 1.0;
    QTest::newRow("liquify weak soft") << false << 13.0 << 0.0 << 0.35;
    QTest::newRow("liquify wide") << false << 60.0 << 0.3 << 0.8;
    QTest::newRow("smudge") << true << 20.0 << 0.5 << 1.0;
    QTest::newRow("smudge weak hard") << true << 9.0 << 0.9 << 0.2;
}

void SmudgeLiquifyTests::liquifyAndSmudgeFollowSwiftsArithmetic()
{
    QFETCH(bool, smudging);
    QFETCH(double, diameter);
    QFETCH(double, hardness);
    QFETCH(double, strength);
    const QImage image = scene(80, 40);
    const BrushSettings settings = brush(diameter, hardness, 0, 0, 0, strength);
    WarpStroke stroke(ImageLayer(ImportedImage(image, image, QStringLiteral("Scene")), QPointF(0, 0)), image,
                      ImageLayer(ImportedImage(image, image, QStringLiteral("Scene")), QPointF(0, 0)).transform, QSizeF(80, 40),
                      smudging ? BlurToolMode::smudge : BlurToolMode::liquify, settings);
    Oracle expected = oracle(image, settings, smudging);
    // Along, back across, off the canvas and in again.
    for (const QPointF point : {QPointF(30.6, 20.2), QPointF(47.7, 23.1), QPointF(47.9, 23.3), QPointF(48.4, 23.3), QPointF(20, 31.5), QPointF(-6, 37),
                                QPointF(12, 9.5)}) {
        stroke.append(point);
        expected.append(point);
        QCOMPARE(differences(stroke, expected), 0);
    }
    QVERIFY(stroke.image() != image);
    QVERIFY(stroke.points().size() > 10);
}

void SmudgeLiquifyTests::theSettingsAreClampedAsSwiftClampsThem()
{
    const QImage image = scene(20, 20);
    const ImageLayer layer(ImportedImage(image, image, QStringLiteral("Scene")), QPointF(0, 0));
    const WarpStroke low(layer, image, layer.transform, QSizeF(20, 20), BlurToolMode::liquify, brush(1, -1, 0, 0, 0, 0));
    QCOMPARE(low.diameter, 2.0);
    QCOMPARE(low.hardness, 0.0);
    QCOMPARE(low.strength, 0.01);
    const WarpStroke high(layer, image, layer.transform, QSizeF(20, 20), BlurToolMode::smudge, brush(300, 1, 0, 0, 0, 2));
    QCOMPARE(high.hardness, 0.98);
    QCOMPARE(high.strength, 1.0);
    QCOMPARE(high.width, 20);
    QCOMPARE(high.image(), image);
}

void SmudgeLiquifyTests::liquifyCommitsAlongThePathAsOneStep()
{
    EditorSession session;
    smearing(session, BlurToolMode::liquify, BrushSettings{.diameter = 16, .hardness = 0.5});
    const int steps = session.history.undoCount();
    session.beginBrush(QPointF(30, 12));
    QVERIFY(session.warpStroke() && !session.brushStroke());
    session.continueBrush(QPointF(52, 12));
    // The copy shows the push before anything is committed.
    const QImage live = session.warpStroke()->image();
    QCOMPARE(exported(session, 44, 12), (std::vector<int>{0, 0, 255, 255}));
    QVERIFY(pixel(live, 44, 12)[0] > 200);
    session.finishBrush();
    QVERIFY(!session.warpStroke());
    QCOMPARE(session.history.undoCount(), steps + 1);
    QCOMPARE(session.history.undoName(), QString("Liquify"));
    QCOMPARE(exported(session, 44, 12), pixel(live, 44, 12));
    // Away from the path, nothing moved.
    QCOMPARE(exported(session, 70, 30), (std::vector<int>{0, 0, 255, 255}));
    QCOMPARE(exported(session, 5, 2), (std::vector<int>{255, 0, 0, 255}));
    session.undo();
    QCOMPARE(exported(session, 44, 12), (std::vector<int>{0, 0, 255, 255}));
}

void SmudgeLiquifyTests::theCommitLandsAllTheWarpMoved()
{
    // Dabs round past an off-grid path; short strokes show rims.
    for (const auto &[from, to] : {std::pair(QPointF(24, 11.6), QPointF(56, 11.6)), std::pair(QPointF(37, 11.6), QPointF(41, 11.6))}) {
        EditorSession session;
        smearing(session, BlurToolMode::liquify, BrushSettings{.diameter = 16, .hardness = 0});
        session.beginBrush(from);
        session.continueBrush(to);
        const QImage live = session.warpStroke()->image().copy();
        session.finishBrush();
        const QImage landed = ImageExporter::render(session.projectSnapshot().value()).image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
        int apart = 0;
        for (int y = 0; y < 40; ++y)
            apart += std::memcmp(landed.constScanLine(y), live.constScanLine(y), 80 * 4) != 0;
        QCOMPARE(apart, 0);
        QVERIFY(pixel(live, 41, 19) != pixel(scene(80, 40), 41, 19));
        // Shift's line starts where the warp ended, on the pixels.
        QCOMPARE(session.shiftLineStart().value(), to);
    }
}

void SmudgeLiquifyTests::smudgeCarriesColourAndNamesItsStep()
{
    EditorSession session;
    smearing(session, BlurToolMode::smudge, BrushSettings{.diameter = 12, .hardness = 0, .opacity = 0.5});
    session.beginBrush(QPointF(30, 12));
    session.continueBrush(QPointF(50, 12));
    session.finishBrush();
    QCOMPARE(session.history.undoName(), QString("Smudge"));
    // The core carries red all the way; its flanks fade.
    QCOMPARE(exported(session, 49, 12), (std::vector<int>{255, 0, 0, 255}));
    const std::vector<int> near = exported(session, 42, 15), far = exported(session, 49, 15);
    QVERIFY2(near[0] > far[0] + 20 && far[0] > 0 && far[2] > 0, qPrintable(QString("%1 %2 %3").arg(near[0]).arg(far[0]).arg(far[2])));
    QCOMPARE(exported(session, 75, 35), (std::vector<int>{0, 0, 255, 255}));
}

void SmudgeLiquifyTests::aWarpReplacesSoClearPixelsClear()
{
    EditorSession session;
    session.createDocument(60, 20);
    QImage half = BrushRaster::context(60, 20, false);
    QPainter(&half).fillRect(QRect(30, 0, 30, 20), QColor(255, 0, 0));
    session.insert(ImportedImage(half, half, QStringLiteral("Half")));
    session.selectTool(NavigationTool::blur);
    session.setBrushSettings(BrushSettings{.diameter = 16, .hardness = 0.5});
    // Clear pushed over red leaves the red clear there.
    session.beginBrush(QPointF(22, 10));
    session.continueBrush(QPointF(42, 10));
    session.finishBrush();
    QVERIFY2(exported(session, 36, 10)[3] < 60, qPrintable(QString::number(exported(session, 36, 10)[3])));
    QCOMPARE(exported(session, 50, 2)[3], 255);
}

void SmudgeLiquifyTests::aMaskIsNeverWarped()
{
    EditorSession session;
    smearing(session, BlurToolMode::liquify, BrushSettings{.diameter = 16, .hardness = 0.5});
    session.addLayerMask(true);
    QVERIFY(session.isMaskSelected());
    session.beginBrush(QPointF(30, 12));
    QVERIFY(!session.warpStroke() && !session.brushStroke());
    QCOMPARE(session.brushError().value(), QString("Smudge and Liquify work on a layer's pixels, not its mask."));
    // Blur still paints the mask, softened.
    session.setBrushError(std::nullopt);
    session.setBlurMode(BlurToolMode::blur);
    session.beginBrush(QPointF(30, 12));
    QVERIFY(session.brushStroke() && session.brushStroke()->isMask && session.brushStroke()->isBlur);
    session.cancelBrush();
}

void SmudgeLiquifyTests::aWarpHoldsTheSessionUntilItEnds()
{
    EditorSession session;
    smearing(session, BlurToolMode::liquify, BrushSettings{.diameter = 16, .hardness = 0.5});
    session.addBlankLayer();
    const QUuid blank = session.activeLayerID().value();
    session.selectLayer(session.document().value().layers.front().id);
    session.selectTool(NavigationTool::blur);
    session.beginBrush(QPointF(30, 12));
    QVERIFY(session.warpStroke());
    QVERIFY(!session.canEditLayers() && !session.canUseHistory() && !session.canStartProjectOperation());
    session.selectTool(NavigationTool::brush);
    QCOMPARE(session.tool(), NavigationTool::blur);
    session.selectLayer(blank);
    QVERIFY(session.activeLayerID() != blank);
    session.cycleToolMode();
    QCOMPARE(session.blurMode(), BlurToolMode::liquify);
    // Busy holds the release; Escape's cancel drops it all.
    session.continueBrush(QPointF(50, 12));
    session.setIsProjectBusy(true);
    QVERIFY(!session.finishBrushImmediately());
    QVERIFY(session.warpStroke());
    session.setIsProjectBusy(false);
    const int steps = session.history.undoCount();
    session.cancelBrush();
    QVERIFY(!session.warpStroke());
    QCOMPARE(session.history.undoCount(), steps);
    QVERIFY(session.canEditLayers());
}

void SmudgeLiquifyTests::aLayerChangedUnderTheWarpTakesNothing()
{
    EditorSession session;
    smearing(session, BlurToolMode::liquify, BrushSettings{.diameter = 16, .hardness = 0.5});
    const QUuid id = session.activeLayerID().value();
    session.beginBrush(QPointF(30, 12));
    session.continueBrush(QPointF(52, 12));
    // Swift's guard: the same pixels and placement, or nothing lands.
    rewrite(session, [&](ProjectSnapshot &snapshot) { record(snapshot, id).transform.origin = QPointF(1, 0); });
    const int steps = session.history.undoCount();
    session.finishBrush();
    QVERIFY(!session.warpStroke());
    QCOMPARE(session.history.undoCount(), steps);
}

void SmudgeLiquifyTests::tabStepsTheSmearsModes()
{
    EditorSession session;
    session.createDocument(8, 8, true);
    session.selectTool(NavigationTool::blur);
    QCOMPARE(session.blurMode(), BlurToolMode::liquify);
    session.cycleToolMode();
    QCOMPARE(session.blurMode(), BlurToolMode::blur);
    session.cycleToolMode();
    QCOMPARE(session.blurMode(), BlurToolMode::smudge);
    session.cycleToolMode();
    QCOMPARE(session.blurMode(), BlurToolMode::liquify);
    QCOMPARE(rawValue(BlurToolMode::liquify), QString("Liquify"));
    QCOMPARE(rawValue(BlurToolMode::blur), QString("Blur"));
    QCOMPARE(rawValue(BlurToolMode::smudge), QString("Smudge"));
}

QTEST_GUILESS_MAIN(SmudgeLiquifyTests)
#include "SmudgeLiquifyTests.moc"
