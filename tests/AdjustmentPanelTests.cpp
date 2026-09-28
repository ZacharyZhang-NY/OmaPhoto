#include "ContentView.h"
#include "MenuFixtures.h"
#include "UI/LayerIcons.h"
#include "UI/LayersPanel.h"
#include "UI/NativeLayerList.h"
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QToolButton>
#include <QtTest>

// Adjustment layers in the window: footer, rows, menu, sheets.
namespace {
template <typename Widget> Widget &find(QWidget &root, const char *name)
{
    Widget *found = root.findChild<Widget *>(QString::fromLatin1(name));
    if (!found)
        throw std::runtime_error(std::string("no widget named ") + name);
    return *found;
}

ImportedImage whiteImage(int width, int height)
{
    QImage pixels(width, height, QImage::Format_RGBA8888_Premultiplied);
    pixels.fill(Qt::white);
    return ImportedImage(pixels, pixels, QStringLiteral("White"));
}

// A shown label with these words, if any.
QLabel *shownLabel(QWidget &root, const QString &words)
{
    for (QLabel *label : root.findChildren<QLabel *>()) {
        if (label->text() == words && label->isVisible())
            return label;
    }
    return nullptr;
}

bool inked(const QImage &image, int x, int y)
{
    return qAlpha(image.pixel(x, y)) > 128;
}
}

class AdjustmentPanelTests : public QObject {
    Q_OBJECT
private slots:
    void theFooterAddsEachKindAndItsEditorOpens();
    void rowsShowTheirGlyphAndOpenOnADoubleClick();
    void theLayerMenuAddsAndEditsAdjustments();
    void theSheetsSayTheyReadWhatLiesBeneath();
};

void AdjustmentPanelTests::theFooterAddsEachKindAndItsEditorOpens()
{
    EditorSession session;
    LayersPanel panel(session);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    auto &button = find<QToolButton>(panel, "addAdjustment");
    QCOMPARE(button.toolTip(), QString("New adjustment layer"));
    QCOMPARE(button.accessibleName(), QString("New adjustment layer"));
    QCOMPARE(button.popupMode(), QToolButton::InstantPopup);
    QVERIFY(!button.isEnabled());
    // Swift's circle.lefthalf.filled, in the footer's ink.
    const QImage icon = LayerIcons::pixmap(LayerIcon::halfFilledCircle, 16, panel.palette().color(QPalette::PlaceholderText), panel.devicePixelRatio()).toImage();
    QCOMPARE(button.icon().pixmap(QSize(16, 16), panel.devicePixelRatio()).toImage().convertToFormat(icon.format()), icon);
    // Swift's Menu: the kinds by name, no ellipsis, & doubled.
    const QList<QAction *> entries = button.menu()->actions();
    QCOMPARE(entries.size(), qsizetype(allAdjustmentKinds.size()));
    for (size_t index = 0; index < allAdjustmentKinds.size(); ++index)
        QCOMPARE(entries[qsizetype(index)]->text(), rawValue(allAdjustmentKinds[index]).replace("&", "&&"));
    QCOMPARE(entries[10]->text(), QString("Black && White"));
    session.createDocument(4, 4);
    session.insert(whiteImage(4, 4));
    QVERIFY(button.isEnabled());
    entries[1]->trigger();
    QCOMPARE(session.activeLayer().value().adjustment.value().kind, AdjustmentKind::levels);
    // The panel's task opens the editor; the footer rests meanwhile.
    QTRY_VERIFY(session.levels().has_value());
    QVERIFY(session.adjustmentOriginal() && !button.isEnabled());
    session.cancelLevels();
    QVERIFY(button.isEnabled() && !session.adjustmentEditingID());
    // A request made again begins again.
    session.setAdjustmentEditingID(session.activeLayerID());
    QTRY_VERIFY(session.levels().has_value());
    session.commitLevels();
    QTRY_VERIFY(!session.adjustmentEditingID());
}

void AdjustmentPanelTests::rowsShowTheirGlyphAndOpenOnADoubleClick()
{
    EditorSession session;
    session.createDocument(40, 30);
    session.insert(whiteImage(40, 30));
    const QUuid pixels = session.activeLayerID().value();
    session.addMask();
    std::vector<QUuid> ids;
    for (const AdjustmentKind kind : allAdjustmentKinds) {
        session.addAdjustment(kind);
        session.setAdjustmentEditingID(std::nullopt);
        ids.push_back(session.activeLayerID().value());
    }
    session.addMask();
    NativeLayerList list(session);
    list.resize(252, 720);
    list.show();
    QVERIFY(QTest::qWaitForWindowActive(&list));
    // Top first: the last kind added heads the list.
    QCOMPARE(list.cells().size(), size_t(13));
    for (size_t index = 0; index < allAdjustmentKinds.size(); ++index) {
        LayerCell &row = *list.cells().at(allAdjustmentKinds.size() - 1 - index);
        const QColor ink = row.palette().color(QPalette::WindowText);
        QCOMPARE(row.findChild<QLabel *>("layerDimensions")->text(), QString("Adjustment · Double-click to edit"));
        QCOMPARE(row.thumbnail().size(), QSize(36, 36));
        const QImage expected = LayerIcons::adjustmentThumbnail(allAdjustmentKinds[index], ink, 1).toImage();
        QCOMPARE(row.thumbnail().icon().pixmap(QSize(36, 36), 1).toImage().convertToFormat(expected.format()), expected);
    }
    // Each kind its own glyph, as Swift's symbols.
    const std::pair<AdjustmentKind, LayerIcon> symbols[] = {{AdjustmentKind::hsv, LayerIcon::halfFilledCircle}, {AdjustmentKind::levels, LayerIcon::sliders},
                                                            {AdjustmentKind::curves, LayerIcon::curvePath}, {AdjustmentKind::exposure, LayerIcon::plusMinusCircle},
                                                            {AdjustmentKind::gradientMap, LayerIcon::paintPalette}, {AdjustmentKind::grain, LayerIcon::circleGrid},
                                                            {AdjustmentKind::addNoise, LayerIcon::dottedCircle}, {AdjustmentKind::gaussianBlur, LayerIcon::drop},
                                                            {AdjustmentKind::motionBlur, LayerIcon::wind},
                                                            {AdjustmentKind::invert, LayerIcon::rightHalfCircle}, {AdjustmentKind::blackWhite, LayerIcon::hatchedCircle},
                                                            {AdjustmentKind::colorBalance, LayerIcon::axes}};
    QList<QImage> glyphs;
    for (const auto &[kind, symbol] : symbols) {
        QVERIFY(LayerIcons::symbol(kind) == symbol);
        glyphs << LayerIcons::adjustmentThumbnail(kind, Qt::black, 1).toImage();
    }
    for (qsizetype first = 0; first < glyphs.size(); ++first) {
        for (qsizetype second = first + 1; second < glyphs.size(); ++second)
            QVERIFY(glyphs[first] != glyphs[second]);
    }
    // The glyph is 1.2 times the rail's, centred: 21.6 points.
    const QImage circle = glyphs.front();
    QVERIFY(inked(circle, 9, 18) && inked(circle, 26, 18) && !inked(circle, 5, 18) && !inked(circle, 30, 18));
    // The curve turns a quarter clockwise: its dot moves right.
    const QImage curve = LayerIcons::adjustmentThumbnail(AdjustmentKind::curves, Qt::black, 1).toImage();
    const QImage upright = LayerIcons::pixmap(LayerIcon::curvePath, 36, Qt::black, 1).toImage();
    QVERIFY(inked(curve, 24, 11) && !inked(curve, 11, 11));
    QVERIFY(inked(upright, 7, 7) && !inked(upright, 29, 7));
    // The drop fills; the wind strokes across; dots ring emptiness.
    const auto glyph = [](AdjustmentKind kind) { return LayerIcons::adjustmentThumbnail(kind, Qt::black, 1).toImage(); };
    QVERIFY(inked(glyph(AdjustmentKind::gaussianBlur), 18, 20) && !inked(glyph(AdjustmentKind::gaussianBlur), 11, 11));
    QVERIFY(inked(glyph(AdjustmentKind::motionBlur), 11, 18) && inked(glyph(AdjustmentKind::motionBlur), 24, 18));
    QVERIFY(!inked(glyph(AdjustmentKind::motionBlur), 16, 16));
    QVERIFY(inked(glyph(AdjustmentKind::addNoise), 26, 18) && !inked(glyph(AdjustmentKind::addNoise), 18, 18));
    // The masks: an adjustment's shows no link, the pixels' does.
    QVERIFY(list.cells().front()->findChild<QToolButton *>("maskLink")->isHidden());
    QVERIFY(!list.cells().back()->findChild<QToolButton *>("maskLink")->isHidden());
    // A double click on the glyph asks for its editor.
    session.selectLayer(pixels);
    LayerCell &levels = *list.cells().at(allAdjustmentKinds.size() - 2);
    QTest::mouseDClick(&levels.thumbnail(), Qt::LeftButton);
    QCOMPARE(session.activeLayerID(), std::optional(ids[1]));
    QCOMPARE(session.adjustmentEditingID(), std::optional(ids[1]));
    QVERIFY(!session.renamingLayerID());
    // While one is asked for, rows refuse the gesture.
    QTest::mouseDClick(&levels, Qt::LeftButton, Qt::NoModifier, QPoint(levels.width() - 20, 20));
    QVERIFY(!session.renamingLayerID());
    // On the name it renames, as every layer.
    session.setAdjustmentEditingID(std::nullopt);
    QTest::mouseDClick(&levels, Qt::LeftButton, Qt::NoModifier, QPoint(levels.width() - 20, 20));
    QCOMPARE(session.renamingLayerID(), std::optional(ids[1]));
    QVERIFY(!session.adjustmentEditingID());
    QTest::keyClick(levels.findChild<QLineEdit *>("layerNameEditor"), Qt::Key_Escape);
    QVERIFY(!session.renamingLayerID());
    // Invert has no editor: its glyph renames, as Swift's row.
    LayerCell &invert = *list.cells().at(allAdjustmentKinds.size() - 10);
    QTest::mouseDClick(&invert.thumbnail(), Qt::LeftButton);
    QCOMPARE(session.activeLayerID(), std::optional(ids[9]));
    QVERIFY(!session.adjustmentEditingID());
    QCOMPARE(session.renamingLayerID(), std::optional(ids[9]));
    QCOMPARE(invert.findChild<QLabel *>("layerDimensions")->text(), QString("Adjustment · Double-click to edit"));
}

void AdjustmentPanelTests::theLayerMenuAddsAndEditsAdjustments()
{
    Bar bar;
    QAction &adjustments = bar.action("newAdjustmentLayer"), &edit = bar.action("editAdjustment");
    QCOMPARE(adjustments.text(), QString("New Adjustment Layer"));
    QCOMPARE(edit.text(), QString("Edit Adjustment…"));
    QVERIFY(!adjustments.isEnabled() && !edit.isEnabled());
    const std::pair<const char *, const char *> entries[] = {{"newHueSaturationAdjustment", "Hue/Saturation…"}, {"newLevelsAdjustment", "Levels…"},
                                                             {"newCurvesAdjustment", "Curves…"}, {"newExposureAdjustment", "Exposure…"},
                                                             {"newGradientMapAdjustment", "Gradient Map…"}, {"newGrainAdjustment", "Grain…"},
                                                             {"newAddNoiseAdjustment", "Add Noise…"}, {"newGaussianBlurAdjustment", "Gaussian Blur…"},
                                                             {"newMotionBlurAdjustment", "Motion Blur…"},
                                                             {"newInvertAdjustment", "Invert"}, {"newBlackWhiteAdjustment", "Black && White…"},
                                                             {"newColorBalanceAdjustment", "Color Balance…"}};
    const QList<QAction *> listed = adjustments.menu()->actions();
    QCOMPARE(listed.size(), qsizetype(std::size(entries)));
    for (qsizetype index = 0; index < listed.size(); ++index) {
        QCOMPARE(listed[index]->objectName(), QString(entries[index].first));
        QCOMPARE(listed[index]->text(), QString(entries[index].second));
    }
    bar.session().createDocument(4, 4);
    bar.session().insert(whiteImage(4, 4));
    const QUuid pixels = bar.session().activeLayerID().value();
    QVERIFY(adjustments.isEnabled() && !edit.isEnabled());
    bar.action("newCurvesAdjustment").trigger();
    const QUuid curves = bar.session().activeLayerID().value();
    // The window's panel opens the filter sheet on it.
    QTRY_VERIFY(bar.session().filterEdit().has_value());
    QVERIFY(bar.session().filterEdit().value().kind == FilterKind::curves);
    QVERIFY(!adjustments.isEnabled() && !edit.isEnabled());
    bar.session().cancelFilter();
    QVERIFY(adjustments.isEnabled() && edit.isEnabled());
    edit.trigger();
    QCOMPARE(bar.session().adjustmentEditingID(), std::optional(curves));
    QTRY_VERIFY(bar.session().filterEdit().has_value());
    bar.session().cancelFilter();
    bar.session().selectLayer(pixels);
    QVERIFY(adjustments.isEnabled() && !edit.isEnabled());
    // Invert applies at once; beyond Swift, nothing to edit.
    bar.action("newInvertAdjustment").trigger();
    QVERIFY(bar.session().activeLayer().value().adjustment.value().kind == AdjustmentKind::invert);
    QVERIFY(!bar.session().adjustmentEditingID() && adjustments.isEnabled() && !edit.isEnabled());
    QCOMPARE(bar.session().history.undoName(), QString("New Invert Adjustment"));
    // Undo and Redo double a step's ampersand, no mnemonic.
    bar.action("newBlackWhiteAdjustment").trigger();
    QTRY_VERIFY(bar.session().filterEdit().has_value());
    bar.session().cancelFilter();
    QCOMPARE(bar.action("undo").text(), QString("Undo New Black && White Adjustment"));
    bar.session().undo();
    QCOMPARE(bar.action("redo").text(), QString("Redo New Black && White Adjustment"));
}

void AdjustmentPanelTests::theSheetsSayTheyReadWhatLiesBeneath()
{
    EditorSession session;
    ContentView view(session);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    session.createDocument(20, 20);
    session.insert(whiteImage(20, 20));
    session.selectAll();
    for (const AdjustmentKind kind : {AdjustmentKind::levels, AdjustmentKind::hsv, AdjustmentKind::grain}) {
        session.addAdjustment(kind);
        QTRY_VERIFY(session.adjustmentOriginal().has_value());
        QTest::qWait(50);
        const auto shown = [&](const char *words) {
            for (QWidget *top : QApplication::topLevelWidgets()) {
                if (top->isVisible() && shownLabel(*top, QString::fromUtf8(words)))
                    return true;
            }
            return false;
        };
        // No selection limits a layer's settings.
        QVERIFY(!shown("Limited to the selection"));
        if (kind == AdjustmentKind::levels)
            QVERIFY(shown("Underlying pixels · alpha-weighted histogram"));
        session.finishAdjustmentEditing(false);
    }
}

QTEST_MAIN(AdjustmentPanelTests)
#include "AdjustmentPanelTests.moc"
