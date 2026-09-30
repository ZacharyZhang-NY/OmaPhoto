#include "MenuFixtures.h"
#include "Document/BrushStroke.h"
#include "SelectionFixtures.h"

// Swift's d448625: filters, adjustments and Invert wait for open text.
class TextEditingGatesTests : public QObject {
    Q_OBJECT
private slots:
    void colourEditsWaitForOpenText();
    void existingTextAndAnEmptyLayerWaitToo();
};

void TextEditingGatesTests::colourEditsWaitForOpenText()
{
    Bar bar;
    EditorSession &session = bar.session();
    session.createDocument(40, 30);
    QImage image = BrushRaster::context(40, 30, false);
    image.fill(QColor(120, 160, 200));
    session.insert(ImportedImage(image, image, QStringLiteral("Colour")));
    const QStringList entries{"levels", "curves", "hueSaturation", "invert", "gaussianBlur", "vignette", "exposure"};
    const auto enabled = [&] {
        QStringList on;
        for (const QString &name : entries) {
            if (bar.action(qPrintable(name)).isEnabled())
                on << name;
        }
        return on;
    };
    QVERIFY(session.canAdjustColors() && session.canInvert() && session.canVignette());
    QCOMPARE(enabled(), entries);
    // Open text is drawn by its editor: colour edits wait.
    session.selectTool(NavigationTool::type);
    session.beginText(QPointF(5, 5));
    QVERIFY(session.textDraft());
    QVERIFY(!session.canAdjustColors() && !session.canInvert() && !session.canVignette());
    QCOMPARE(enabled(), QStringList());
    session.beginLevels();
    session.beginFilter(FilterKind::gaussianBlur);
    session.invertPixels();
    QVERIFY(!session.levels() && !session.filterEdit());
    // Closed, the text lets them go on.
    session.cancelText();
    QVERIFY(session.canAdjustColors() && session.canInvert() && session.canVignette());
    QCOMPARE(enabled(), entries);
}

void TextEditingGatesTests::existingTextAndAnEmptyLayerWaitToo()
{
    Bar bar;
    EditorSession &session = bar.session();
    session.createDocument(200, 100);
    session.selectTool(NavigationTool::type);
    session.beginText(QPointF(10, 10));
    TextDraft draft = session.textDraft().value();
    draft.style.content = QStringLiteral("Words");
    session.setTextDraft(draft);
    QVERIFY(session.finishText());
    session.applySelection(rectPath(QRectF(0, 0, 50, 50)), SelectionMode::replace, "Select");
    const QStringList entries{"levels", "hueSaturation", "invert", "gaussianBlur", "cameraRawFilter", "contentAwareFill"};
    const auto enabled = [&] {
        QStringList on;
        for (const QString &name : entries) {
            if (bar.action(qPrintable(name)).isEnabled())
                on << name;
        }
        return on;
    };
    QCOMPARE(enabled(), entries);
    QVERIFY(session.canContentAwareFill());
    // Reopened, the layer's text is its editor's: every edit waits.
    session.editActiveText();
    QVERIFY(session.textDraft().value().layerID.has_value());
    QVERIFY(!session.canAdjustColors() && !session.canInvert() && !session.canContentAwareFill());
    QCOMPARE(enabled(), QStringList());
    const QImage pixels = session.activeLayer().value().asset.value().image();
    const int steps = session.history.undoCount();
    session.beginHueSaturation();
    session.beginFilter(FilterKind::cameraRaw);
    bool inverted = false;
    session.invertPixels([&inverted] { inverted = true; });
    QTRY_VERIFY(inverted);
    QVERIFY(!session.hueSaturation() && !session.filterEdit());
    QCOMPARE(session.history.undoCount(), steps);
    QCOMPARE(session.activeLayer().value().asset.value().image(), pixels);
    QCOMPARE(session.textDraft().value().style.content, QString("Words"));
    QVERIFY(session.finishText());
    QCOMPARE(enabled(), entries);
    // Vignette paints an empty layer, but not under new text.
    session.deselect();
    session.addBlankLayer();
    QVERIFY(!session.activeLayer().value().asset && session.canVignette());
    session.beginText(QPointF(120, 60), true);
    QVERIFY(!session.textDraft().value().layerID && !session.activeLayer().value().asset);
    QVERIFY(!session.canVignette() && !bar.action("vignette").isEnabled());
    session.cancelText();
    QVERIFY(session.canVignette() && bar.action("vignette").isEnabled());
}

QTEST_MAIN(TextEditingGatesTests)
#include "TextEditingGatesTests.moc"
