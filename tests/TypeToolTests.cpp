#include "BrushFixtures.h"
#include "SessionFixtures.h"
#include "Document/EditorSession.h"
#include "IO/ImageExporter.h"
#include <QTemporaryDir>

// Swift's TypeToolTests: text layers made, edited, saved, clipped.
namespace {
std::unique_ptr<EditorSession> makeSession()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(800, 600, true);
    session->selectTool(NavigationTool::type);
    return session;
}

// Swift writes `colorPicker.hsb` in place.
void pick(EditorSession &session, const PaletteColor &color)
{
    PickerHSB hsb = session.colorPicker().value().hsb;
    hsb.setRGB(color);
    session.setColorPickerHSB(hsb);
}

// Swift writes `session.textDraft?.style.content` in place.
void type(EditorSession &session, const QString &content)
{
    TextDraft draft = session.textDraft().value();
    draft.style.content = content;
    session.setTextDraft(draft);
}

// Opaque ink and clear pixels of an image, counted.
std::pair<int, int> inkAndClear(const QImage &image, const std::function<bool(const std::vector<int> &)> &inked)
{
    int ink = 0, clear = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const std::vector<int> at = pixel(image, x, y);
            if (at[3] == 0)
                ++clear;
            else if (inked(at))
                ++ink;
        }
    }
    return {ink, clear};
}
}

class TypeToolTests : public QObject {
    Q_OBJECT
private slots:
    void createEditCancelAndUndo();
    void transformsDuplicatesAndClippingKeepTextEditable();
    void saveReopenAndRasterize();
    void rasterHasTransparentBackgroundAndColoredGlyphs();
    void clippingToTextExportsColoredGlyphsOnTransparency();
    void paragraphBoxAndToolSwitchCommitEditableText();
    void emptyNewParagraphIsDiscarded();
    void invalidAndStaleDraftsDoNotChangeDocument();
    void textColorPickerPreviewsAndRestoresDraft();
    void theForegroundPickerPreviewsOpenTextToo();
};

void TypeToolTests::createEditCancelAndUndo()
{
    const auto session = makeSession();
    const int before = session->history.undoCount();
    session->beginText(QPointF(30, 40));
    type(*session, QStringLiteral("Text"));
    QCOMPARE(session->document().value().layers.size(), size_t(1));
    TextDraft draft = session->textDraft().value();
    draft.style.content = QStringLiteral("Hello\nCompositor");
    draft.style.fontSize = 48;
    QVERIFY(session->applyText(draft));
    QCOMPARE(session->activeLayer().value().liveText().value().style, draft.style);
    QCOMPARE(session->activeLayer().value().origin(), QPointF(30, 40));
    QCOMPARE(session->history.undoCount(), before + 1);
    session->editActiveText();
    session->setTextDraft(std::nullopt);
    QCOMPARE(session->history.undoCount(), before + 1);
    session->editActiveText();
    draft = session->textDraft().value();
    draft.style.content = QStringLiteral("Changed");
    QVERIFY(session->applyText(draft));
    session->undo();
    QCOMPARE(session->activeLayer().value().liveText().value().style.content, QString("Hello\nCompositor"));
    session->undo();
    QCOMPARE(session->document().value().layers.size(), size_t(1));
    session->redo();
    QVERIFY(session->activeLayer().value().liveText());
}

void TypeToolTests::transformsDuplicatesAndClippingKeepTextEditable()
{
    const auto session = makeSession();
    session->beginText(QPointF(20, 20));
    type(*session, QStringLiteral("Text"));
    QVERIFY(session->applyText(session->textDraft().value()));
    const QUuid id = session->activeLayerID().value();
    rewrite(*session, [&](ProjectSnapshot &snapshot) {
        record(snapshot, id).transform.rotation = 30;
        record(snapshot, id).transform.size.rwidth() *= 2;
    });
    const LayerTransform old = session->activeLayer().value().transform;
    session->editActiveText();
    TextDraft draft = session->textDraft().value();
    draft.style.content = QStringLiteral("Longer text");
    QVERIFY(session->applyText(draft));
    const LayerTransform updated = session->activeLayer().value().transform;
    QCOMPARE(updated.rotation, 30.0);
    QVERIFY(std::abs(updated.point(QPointF(0, 0)).x() - old.point(QPointF(0, 0)).x()) < 0.001);
    QVERIFY(std::abs(updated.point(QPointF(0, 0)).y() - old.point(QPointF(0, 0)).y()) < 0.001);
    session->duplicateActiveLayer();
    QCOMPARE(session->activeLayer().value().liveText().value().style.content, QString("Longer text"));
    const QUuid target = session->activeLayerID().value();
    QVERIFY(session->linkMask(id, target));
    QCOMPARE(session->activeLayer().value().maskSourceID, std::optional(id));
    QVERIFY(layerWith(*session, id).liveText());
}

void TypeToolTests::saveReopenAndRasterize()
{
    const auto session = makeSession();
    session->beginText(QPointF(0, 0));
    type(*session, QStringLiteral("Text"));
    TextDraft draft = session->textDraft().value();
    draft.style.content = QStringLiteral("Café 日本語\nSecond line");
    draft.style.alignment = TextAlignment::right;
    draft.style.tracking = 3;
    QVERIFY(session->applyText(draft));
    QTemporaryDir folder;
    const QString path = folder.filePath(QStringLiteral("text.comp"));
    ProjectStore::save(session->projectSnapshot().value(), path);
    const auto reopened = makeSession();
    reopened->installProject(ProjectStore::load(path), path);
    QCOMPARE(reopened->activeLayer().value().liveText().value().style, draft.style);
    // Other pixels in its place: no longer text.
    ImageLayer painted = reopened->activeLayer().value();
    const QImage replacement = EditorSession::shapeImage(ShapeKind::rectangle, QSizeF(10, 10), PaletteColor{1, 0, 0});
    painted.asset = ImportedImage(replacement, replacement, QStringLiteral("Painted"));
    QVERIFY(!painted.liveText());
    QVERIFY(!painted.hierarchyRecord().text);
}

void TypeToolTests::rasterHasTransparentBackgroundAndColoredGlyphs()
{
    LayerTextStyle style;
    style.content = QStringLiteral("TYPE");
    style.red = 1;
    const QImage image = EditorSession::textImage(style);
    bool pure = true;
    const auto [ink, clear] = inkAndClear(image, [&](const std::vector<int> &at) {
        pure = pure && at[0] > 0 && at[1] == 0 && at[2] == 0;
        return true;
    });
    QVERIFY(pure);
    QVERIFY(ink > 100 && clear > 100);
}

void TypeToolTests::clippingToTextExportsColoredGlyphsOnTransparency()
{
    const auto session = makeSession();
    session->beginText(QPointF(0, 0));
    type(*session, QStringLiteral("Text"));
    QVERIFY(session->applyText(session->textDraft().value()));
    const QUuid source = session->activeLayerID().value();
    const QSizeF size = session->activeLayer().value().size();
    const QImage fill = EditorSession::shapeImage(ShapeKind::rectangle, size, PaletteColor{1, 0, 0});
    session->addPixelLayer(fill, QPointF(0, 0), QStringLiteral("Clipped color"), QStringLiteral("Fill"));
    QVERIFY(session->linkMask(source, session->activeLayerID().value()));
    const QImage exported = ImageExporter::render(session->projectSnapshot().value()).image;
    bool red = true;
    const auto [ink, clear] = inkAndClear(exported, [&](const std::vector<int> &at) {
        if (at[3] != 255)
            return false;
        red = red && at[0] == 255 && at[1] == 0;
        return true;
    });
    QVERIFY(red);
    QVERIFY(ink > 100 && clear > 100);
}

void TypeToolTests::paragraphBoxAndToolSwitchCommitEditableText()
{
    const auto session = makeSession();
    session->beginText(QRectF(40, 60, 200, 120));
    QCOMPARE(session->textDraft().value().style.content, QString());
    type(*session, QStringLiteral("Text that wraps inside its paragraph box"));
    session->selectTool(NavigationTool::brush);
    QVERIFY(!session->textDraft() && session->tool() == NavigationTool::brush);
    QCOMPARE(session->activeLayer().value().size(), QSizeF(200, 120));
    QCOMPARE(session->activeLayer().value().liveText().value().style.boxSize, std::optional(QSizeF(200, 120)));
    session->selectTool(NavigationTool::type);
    session->editActiveText();
    type(*session, QStringLiteral("Edited on canvas"));
    session->cancelText();
    QCOMPARE(session->activeLayer().value().liveText().value().style.content, QString("Text that wraps inside its paragraph box"));
}

void TypeToolTests::emptyNewParagraphIsDiscarded()
{
    const auto session = makeSession();
    const size_t count = session->document().value().layers.size();
    session->beginText(QRectF(0, 0, 100, 100));
    session->selectTool(NavigationTool::brush);
    QCOMPARE(session->document().value().layers.size(), count);
    QVERIFY(!session->textDraft());
}

void TypeToolTests::invalidAndStaleDraftsDoNotChangeDocument()
{
    const auto session = makeSession();
    session->beginText(QPointF(0, 0));
    type(*session, QStringLiteral("Text"));
    TextDraft draft = session->textDraft().value();
    draft.style.fontSize = NAN;
    QVERIFY(!session->applyText(draft));
    draft.style.fontSize = 72;
    draft.style.boxSize = QSizeF(0, 100);
    QVERIFY(!session->applyText(draft));
    draft.style.boxSize = QSizeF(360, 160);
    draft.style.content = QStringLiteral("Valid");
    session->setTextDraft(std::nullopt);
    session->createDocument(100, 100, true);
    QVERIFY(!session->applyText(draft));
    QCOMPARE(session->document().value().layers.size(), size_t(1));
}

void TypeToolTests::textColorPickerPreviewsAndRestoresDraft()
{
    const auto session = makeSession();
    session->beginText(QPointF(30, 40));
    const LayerTextStyle original = session->textDraft().value().style;
    session->openTextColorPicker();
    pick(*session, PaletteColor{1, 0, 0});
    session->previewTextColor();
    QCOMPARE(session->textDraft().value().style.red, 1.0);
    QCOMPARE(session->textDraft().value().style.green, 0.0);
    QCOMPARE(session->foregroundColor(), PaletteColor::black());
    session->closeColorPicker(false);
    QCOMPARE(session->textDraft().value().style, original);
    QCOMPARE(session->foregroundColor(), PaletteColor::black());
    session->openTextColorPicker();
    pick(*session, PaletteColor{0, 0, 1});
    session->previewTextColor();
    session->closeColorPicker(true);
    QCOMPARE(session->textDraft().value().style.blue, 1.0);
    QCOMPARE(session->foregroundColor(), (PaletteColor{0, 0, 1}));
    // With no text open, Cancel leaves the next text's colour.
    session->setTextDraft(std::nullopt);
    session->openTextColorPicker();
    pick(*session, PaletteColor{1, 0, 0});
    session->closeColorPicker(true);
    session->setForegroundColor(PaletteColor{0, 1, 0});
    session->openTextColorPicker();
    session->closeColorPicker(false);
    QCOMPARE(session->textDefaults().red, 1.0);
    QCOMPARE(session->textDefaults().green, 0.0);
}

void TypeToolTests::theForegroundPickerPreviewsOpenTextToo()
{
    // Swift 1.2.5 (4e5f4ec): open text follows the foreground picker.
    const auto session = makeSession();
    session->beginText(QPointF(30, 40));
    const auto paint = [&](const PaletteColor &color) {
        session->changeTextStyle([&](LayerTextStyle &style) {
            style.red = color.red;
            style.green = color.green;
            style.blue = color.blue;
        });
    };
    const auto colour = [&] {
        const LayerTextStyle style = session->textDraft().value().style;
        return PaletteColor{style.red, style.green, style.blue};
    };
    // Blue text, a black foreground: Cancel restores the text's own.
    paint(PaletteColor{0, 0, 1});
    session->openColorPicker(false);
    pick(*session, PaletteColor{0, 1, 0});
    session->previewTextColor();
    QCOMPARE(colour(), (PaletteColor{0, 1, 0}));
    session->closeColorPicker(false);
    QCOMPARE(colour(), (PaletteColor{0, 0, 1}));
    QCOMPARE(session->foregroundColor(), PaletteColor::black());
    // Another draft set meanwhile is neither previewed nor restored.
    session->openColorPicker(false);
    TextDraft other = session->textDraft().value();
    other.id = QUuid::createUuid();
    session->setTextDraft(other);
    paint(PaletteColor{1, 0, 0});
    pick(*session, PaletteColor{0, 1, 0});
    session->previewTextColor();
    QCOMPARE(colour(), (PaletteColor{1, 0, 0}));
    session->closeColorPicker(false);
    QCOMPARE(colour(), (PaletteColor{1, 0, 0}));
    // OK keeps the colour, and the foreground with it.
    session->openColorPicker(false);
    pick(*session, PaletteColor{0, 1, 0});
    session->previewTextColor();
    session->closeColorPicker(true);
    QCOMPARE(colour(), (PaletteColor{0, 1, 0}));
    QCOMPARE(session->foregroundColor(), (PaletteColor{0, 1, 0}));
    // The background picker leaves text alone.
    session->openColorPicker(true);
    pick(*session, PaletteColor{1, 0, 0});
    session->previewTextColor();
    QCOMPARE(colour(), (PaletteColor{0, 1, 0}));
    session->closeColorPicker(false);
    // The Type bar's picker: a later draft keeps its colour.
    session->openTextColorPicker();
    other.id = QUuid::createUuid();
    session->setTextDraft(other);
    paint(PaletteColor{1, 0, 0});
    session->closeColorPicker(false);
    QCOMPARE(colour(), (PaletteColor{1, 0, 0}));
}

QTEST_MAIN(TypeToolTests)
#include "TypeToolTests.moc"
