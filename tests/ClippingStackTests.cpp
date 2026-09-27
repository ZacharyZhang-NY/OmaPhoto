#include "LiveMaskFixtures.h"
#include <QtTest>

class ClippingStackTests : public QObject {
    Q_OBJECT
private slots:
    void init();
    void clippingColorKeepsTheBasesSoftAlphaWithoutABlackFringe();
    void stacksNeedNeighboursInOneFolder();
    void aStackBlendsOnceWithItsBasesMode();
    void laterChildrenCoverEarlierOnes();
    void aStackInheritsThePaintersOpacity();
};

void ClippingStackTests::init()
{
    QTest::failOnWarning(QRegularExpression("saved states|Unbalanced save/restore"));
}

void ClippingStackTests::clippingColorKeepsTheBasesSoftAlphaWithoutABlackFringe()
{
    Scene scene;
    const QUuid base = scene.add(placed(asset({255, 128, 32, 0}, 0)));
    const QUuid top = scene.add(placed(asset({255, 255, 255, 255})));
    scene.layers[top].source = base;
    const QImage clipped = scene.render({base, top});
    QCOMPARE(alphas(clipped), (std::array<int, 4>{255, 128, 32, 0}));
    // Full-strength red under the base's alpha: no black shows through.
    QCOMPARE(reds(clipped), (std::array<int, 4>{255, 128, 32, 0}));
    QCOMPARE(qGreen(clipped.pixel(1, 0)), 0);
    QCOMPARE(qBlue(clipped.pixel(1, 0)), 0);

    scene.layers[top].opacity = 0.5;
    const QImage translucent = scene.render({base, top});
    QCOMPARE(alphas(translucent), (std::array<int, 4>{255, 128, 32, 0}));
    QCOMPARE(reds(translucent), (std::array<int, 4>{127, 64, 16, 0}));
    scene.layers[top].opacity = 1;

    // A translucent child blends with the base's full-strength colour.
    Scene tinted;
    const QUuid red = tinted.add(placed(asset({255, 128, 32, 0})));
    const QUuid grey = tinted.add(placed(solid(2, 2, qRgba(128, 128, 128, 255))));
    tinted.layers[grey].source = red;
    tinted.layers[grey].opacity = 0.5;
    const QImage mixed = tinted.render({red, grey});
    QCOMPARE(mixed.pixel(0, 0), qRgba(192, 64, 64, 255));
    QCOMPARE(mixed.pixel(1, 0), qRgba(96, 32, 32, 128));
    QCOMPARE(mixed.pixel(0, 1), qRgba(24, 8, 8, 32));
    QCOMPARE(mixed.pixel(1, 1), qRgba(0, 0, 0, 0));

    const QUuid background = scene.add(placed(solid(2, 2, qRgba(255, 255, 255, 255))));
    const QImage flattened = scene.render({background, base, top});
    QCOMPARE(alphas(flattened), (std::array<int, 4>{255, 255, 255, 255}));
    QCOMPARE(reds(flattened), (std::array<int, 4>{255, 255, 255, 255}));
    QCOMPARE(qGreen(flattened.pixel(0, 0)), 0);
    QCOMPARE(qGreen(flattened.pixel(1, 0)), 127);
    QCOMPARE(qGreen(flattened.pixel(0, 1)), 223);
    QCOMPARE(qGreen(flattened.pixel(1, 1)), 255);
}

void ClippingStackTests::stacksNeedNeighboursInOneFolder()
{
    // Outside a stack, base and child both paint soft alpha.
    const std::array<int, 4> shared{255, 128, 32, 0}, thickened{255, 192, 60, 0};
    Scene scene;
    const QUuid folder = QUuid::createUuid();
    const QUuid base = scene.add(placed(asset({255, 128, 32, 0}, 0)));
    const QUuid top = scene.add(placed(asset({255, 255, 255, 255})));
    const QUuid other = scene.add(placed(BrushRaster::context(2, 2, false)));
    scene.layers[top].source = base;
    QCOMPARE(alphas(scene.render({base, top})), shared);
    QCOMPARE(alphas(scene.render({base, other, top})), thickened);
    scene.layers[top].parent = folder;
    QCOMPARE(alphas(scene.render({base, top})), thickened);
    scene.layers[base].parent = folder;
    QCOMPARE(alphas(scene.render({base, top})), shared);

    // Clipped neighbours all join; a base clipped itself starts none.
    const QUuid third = scene.add(placed(asset({255, 255, 255, 255})));
    scene.layers[third].source = base;
    scene.layers[third].parent = folder;
    scene.layers[third].opacity = 0.5;
    QCOMPARE(alphas(scene.render({base, top, third})), shared);
    QCOMPARE(scene.drawn[third] > 0, true);
    scene.layers[base].source = other;
    QCOMPARE(alphas(scene.render({base, top, third})), (std::array<int, 4>{0, 0, 0, 0}));
}

void ClippingStackTests::aStackBlendsOnceWithItsBasesMode()
{
    Scene scene;
    const QUuid background = scene.add(placed(solid(2, 2, qRgba(200, 100, 50, 255))));
    const QUuid base = scene.add(placed(asset({255, 128, 32, 0}, 0)));
    const QUuid top = scene.add(placed(solid(2, 2, qRgba(128, 128, 128, 255))));
    scene.layers[top].source = base;
    scene.layers[base].blendMode = LayerBlendMode::multiply;
    const QImage drawn = scene.render({background, base, top});
    // Grey covers the base; the group multiplies through its alpha.
    QCOMPARE(drawn.pixel(0, 0), qRgba(100, 50, 25, 255));
    QCOMPARE(drawn.pixel(1, 1), qRgba(200, 100, 50, 255));
    QCOMPARE(drawn.pixel(1, 0), qRgba(150, 75, 37, 255));
    // A hand-blended mode too, where Swift's group falls to Normal.
    scene.layers[base].blendMode = LayerBlendMode::linearBurn;
    const QImage burnt = scene.render({background, base, top});
    QCOMPARE(burnt.pixel(0, 0), qRgba(73, 0, 0, 255));
    QCOMPARE(burnt.pixel(1, 1), qRgba(200, 100, 50, 255));
    QCOMPARE(qRed(burnt.pixel(1, 0)), 136);
    QCOMPARE(qGreen(burnt.pixel(1, 0)), 50);
}

void ClippingStackTests::laterChildrenCoverEarlierOnes()
{
    Scene scene;
    const QUuid base = scene.add(placed(asset({255, 128, 32, 0}, 0)));
    const QUuid red = scene.add(placed(solid(2, 2, qRgba(255, 0, 0, 255))));
    const QUuid blue = scene.add(placed(solid(2, 2, qRgba(0, 0, 255, 255))));
    scene.layers[red].source = base;
    scene.layers[blue].source = base;
    QCOMPARE(scene.render({base, red, blue}).pixel(1, 0), qRgba(0, 0, 128, 128));
    QCOMPARE(scene.render({base, blue, red}).pixel(1, 0), qRgba(128, 0, 0, 128));
}

void ClippingStackTests::aStackInheritsThePaintersOpacity()
{
    Scene scene;
    const QUuid base = scene.add(placed(asset({255, 128, 32, 0}, 0)));
    const QUuid top = scene.add(placed(asset({255, 255, 255, 255})));
    scene.layers[top].source = base;
    LiveMaskRenderer live = scene.renderer();
    live.prepareStacks({base, top}, [](QUuid) { return std::nullopt; }, [](QUuid) { return LayerBlendMode::normal; });
    QImage surface = BrushRaster::context(2, 2, false);
    QPainter painter(&surface);
    painter.setOpacity(0.5);
    live.drawComposite(base, painter);
    QCOMPARE(painter.opacity(), 0.5);
    painter.end();
    QCOMPARE(alphas(surface), (std::array<int, 4>{127, 64, 16, 0}));
    QCOMPARE(reds(surface), (std::array<int, 4>{127, 64, 16, 0}));
}

QTEST_MAIN(ClippingStackTests)
#include "ClippingStackTests.moc"
