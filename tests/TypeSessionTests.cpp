#include "Document/DocumentLimits.h"
#include "BrushFixtures.h"
#include "SelectionFixtures.h"
#include "SessionRecord.h"
#include "Document/EditorSession.h"
#include <QSignalSpy>

// The Type tool in the session: gates, hooks, colours, names.
namespace {
std::unique_ptr<EditorSession> makeSession()
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(800, 600, true);
    session->selectTool(NavigationTool::type);
    return session;
}

// New text saying `content` at `at`, applied.
QUuid textLayer(EditorSession &session, QPointF at, const QString &content)
{
    session.selectTool(NavigationTool::type);
    session.beginText(at, true);
    TextDraft draft = session.textDraft().value();
    draft.style.content = content;
    if (!session.applyText(draft))
        throw std::runtime_error("text was refused");
    return session.activeLayerID().value();
}

// Swift's `await`: true once the callback ran.
bool landed(const std::function<void(std::function<void()>)> &start)
{
    bool done = false;
    start([&] { done = true; });
    return QTest::qWaitFor([&] { return done; }, 20000);
}
}

class TypeSessionTests : public QObject {
    Q_OBJECT
private slots:
    void aDraftHoldsTheGatesAndFileRequests();
    void switchingFinishesTheDraft();
    void theSwatchColoursOpenTextOnly();
    void fillRecoloursLiveText();
    void styleChangesEditTextOrSetDefaults();
    void namesAreFirstWordsOnOneLine();
    void aClickFindsTheTextUnderIt();
    void applyingKeepsTheCornerAndPinsTheMask();
    void aDraggedBoxIsRoundedAndBounded();
    void aStyleIsValidWithinSwiftsBounds();
    void everyChangeIsAnnouncedAndRefusalsAreSilent();
    void applyingTakesWhatSwiftTakes();
    void recolouringNeedsANewColour();
};

void TypeSessionTests::aDraftHoldsTheGatesAndFileRequests()
{
    const auto session = makeSession();
    QVERIFY(session->canEditLayers() && session->canUseHistory() && session->canStartProjectOperation());
    session->beginText(QPointF(10, 10));
    QVERIFY(!session->canEditLayers() && !session->canUseHistory() && !session->canStartProjectOperation());
    bool ran = false;
    session->waitForFileRequest([&] { ran = true; });
    QCoreApplication::processEvents();
    QVERIFY(!ran);
    const int focus = session->canvasFocusRequest();
    session->cancelText();
    QCOMPARE(session->canvasFocusRequest(), focus + 1);
    QTRY_VERIFY(ran);
    QVERIFY(session->canEditLayers());
    // Landing the draft frees a waiting request too.
    session->beginText(QPointF(10, 10));
    ran = false;
    session->waitForFileRequest([&] { ran = true; });
    QCoreApplication::processEvents();
    QVERIFY(!ran);
    session->changeTextStyle([](LayerTextStyle &style) { style.content = QStringLiteral("Landed"); });
    // The draft's own change looked, found the draft, and waits.
    QCoreApplication::processEvents();
    QVERIFY(!ran);
    QVERIFY(session->finishText());
    QTRY_VERIFY(ran);
}

void TypeSessionTests::switchingFinishesTheDraft()
{
    const auto session = makeSession();
    const QUuid blank = session->activeLayerID().value();
    session->beginText(QPointF(10, 10));
    TextDraft draft = session->textDraft().value();
    draft.style.content = QStringLiteral("Tool");
    session->setTextDraft(draft);
    // Busy: the draft cannot land, so the tool stays.
    session->setIsProjectBusy(true);
    session->selectTool(NavigationTool::brush);
    QVERIFY(session->tool() == NavigationTool::type && session->textDraft());
    session->setIsProjectBusy(false);
    // What is chosen already leaves the draft open.
    session->selectTool(NavigationTool::type);
    session->selectLayer(blank);
    session->selectLayers({blank}, blank);
    QCOMPARE(session->textDraft().value().style.content, QString("Tool"));
    session->selectTool(NavigationTool::brush);
    QVERIFY(session->tool() == NavigationTool::brush && !session->textDraft());
    QCOMPARE(session->activeLayer().value().name, QString("Tool"));
    const QUuid layer = textLayer(*session, QPointF(10, 200), QStringLiteral("Layer"));
    session->beginText(QPointF(300, 300), true);
    draft = session->textDraft().value();
    draft.style.content = QStringLiteral("Selected");
    session->setTextDraft(draft);
    session->selectLayer(blank);
    QVERIFY(!session->textDraft() && session->activeLayerID() == blank);
    QCOMPARE(session->document().value().layers.back().name, QString("Selected"));
    session->beginText(QPointF(300, 400), true);
    draft = session->textDraft().value();
    draft.style.content = QStringLiteral("Several");
    session->setTextDraft(draft);
    session->selectLayers({blank, layer}, blank);
    QVERIFY(!session->textDraft());
    // Above the active layer, as every new layer goes.
    QCOMPARE(session->document().value().layers[1].name, QString("Several"));
}

void TypeSessionTests::theSwatchColoursOpenTextOnly()
{
    const auto session = makeSession();
    const QUuid id = textLayer(*session, QPointF(10, 10), QStringLiteral("Hello"));
    // Merely selected, the text keeps its colour.
    session->setPaletteColor(PaletteColor{1, 0, 0}, false);
    QCOMPARE(session->foregroundColor(), (PaletteColor{1, 0, 0}));
    QCOMPARE(layerWith(*session, id).liveText().value().style.red, 0.0);
    session->editActiveText();
    session->setPaletteColor(PaletteColor{0.25, 0.5, 1}, false);
    const LayerTextStyle style = session->textDraft().value().style;
    QVERIFY(style.red == 0.25 && style.green == 0.5 && style.blue == 1);
    // The background swatch leaves the open text alone.
    session->setPaletteColor(PaletteColor{1, 1, 0}, true);
    QCOMPARE(session->textDraft().value().style.blue, 1.0);
    // Swapping and resetting move the foreground, and the text.
    session->swapPaletteColors();
    QVERIFY(session->textDraft().value().style.red == 1 && session->textDraft().value().style.blue == 0);
    session->resetPaletteColors();
    QVERIFY(session->textDraft().value().style.red == 0 && session->textDraft().value().style.green == 0);
}

void TypeSessionTests::fillRecoloursLiveText()
{
    const auto session = makeSession();
    const QUuid id = textLayer(*session, QPointF(10, 10), QStringLiteral("Fill"));
    session->setPaletteColor(PaletteColor{1, 0, 0}, false);
    const int steps = session->history.undoCount();
    QVERIFY(landed([&](std::function<void()> done) { session->fillSelection(EditorSession::FillSource::foreground, done); }));
    QCOMPARE(session->history.undoName(), QString("Fill Text"));
    QCOMPARE(layerWith(*session, id).liveText().value().style.red, 1.0);
    QVERIFY(landed([&](std::function<void()> done) { session->fillSelection(EditorSession::FillSource::foreground, done); }));
    QCOMPARE(session->history.undoCount(), steps + 1);
    QVERIFY(!session->recolorText(session->document().value().layers.front().id, PaletteColor{0, 1, 0}));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("text could not be repainted: This is not a valid"));
    QVERIFY(!session->recolorText(id, PaletteColor{2, 0, 0}));
    // A text layer's mask fills as any mask does.
    session->addLayerMask(true);
    QVERIFY(landed([&](std::function<void()> done) { session->fillSelection(EditorSession::FillSource::foreground, done); }));
    QCOMPARE(session->history.undoName(), QString("Fill Mask"));
    QVERIFY(layerWith(*session, id).liveText());
    session->selectLayerTarget(id, false);
    // With a selection the pixels fill: no longer text.
    session->applySelection(rectPath(QRectF(10, 10, 30, 30)), SelectionMode::replace, "Select");
    QVERIFY(landed([&](std::function<void()> done) { session->fillSelection(EditorSession::FillSource::foreground, done); }));
    QCOMPARE(session->history.undoName(), QString("Fill"));
    QVERIFY(!layerWith(*session, id).text);
}

void TypeSessionTests::styleChangesEditTextOrSetDefaults()
{
    const auto session = makeSession();
    session->changeTextStyle([](LayerTextStyle &style) { style.fontSize = 30; });
    QCOMPARE(session->textDefaults().fontSize, 30.0);
    QCOMPARE(session->currentTextStyle().fontSize, 30.0);
    session->changeTextStyle([](LayerTextStyle &style) { style.fontSize = 0; });
    QCOMPARE(session->textDefaults().fontSize, 30.0);
    const QUuid id = textLayer(*session, QPointF(10, 10), QStringLiteral("Styled"));
    QCOMPARE(session->currentTextStyle().content, QString("Styled"));
    // A selected text layer opens for editing and changes.
    session->changeTextStyle([](LayerTextStyle &style) { style.alignment = TextAlignment::center; });
    QCOMPARE(session->textDraft().value().layerID, std::optional(id));
    QCOMPARE(session->currentTextStyle().alignment, TextAlignment::center);
    session->changeTextStyle([](LayerTextStyle &style) { style.tracking = 5000; });
    QCOMPARE(session->textDraft().value().style.tracking, 0.0);
    QCOMPARE(session->textDefaults().content, QString("Styled"));
}

void TypeSessionTests::namesAreFirstWordsOnOneLine()
{
    QCOMPARE(EditorSession::layerName(QStringLiteral("  Hello \n\t World  ")), QString("Hello World"));
    QCOMPARE(EditorSession::layerName(QString()), QString("Text"));
    QCOMPARE(EditorSession::layerName(QStringLiteral(" \n ")), QString("Text"));
    QCOMPARE(EditorSession::layerName(QString(45, QLatin1Char('a'))), QString(40, QLatin1Char('a')));
    // Forty characters as a reader counts them, accents joined.
    const QString accented = QString(39, QLatin1Char('a')) + QStringLiteral("ébb");
    QCOMPARE(EditorSession::layerName(accented), QString(39, QLatin1Char('a')) + QStringLiteral("é"));
}

void TypeSessionTests::aClickFindsTheTextUnderIt()
{
    const auto session = makeSession();
    const QUuid lower = textLayer(*session, QPointF(10, 10), QStringLiteral("Lower"));
    const QUuid upper = textLayer(*session, QPointF(20, 20), QStringLiteral("Upper"));
    session->selectLayer(lower);
    session->beginText(QPointF(40, 40));
    QCOMPARE(session->textDraft().value().layerID, std::optional(upper));
    QCOMPARE(session->activeLayerID(), std::optional(upper));
    QCOMPARE(session->textDraft().value().origin, QPointF(20, 20));
    QCOMPARE(session->textDraft().value().transform, std::optional(layerWith(*session, upper).transform));
    QCOMPARE(session->textDraft().value().style.content, QString("Upper"));
    session->cancelText();
    session->toggleLayerVisibility(upper);
    session->beginText(QPointF(40, 40));
    QCOMPARE(session->textDraft().value().layerID, std::optional(lower));
    session->cancelText();
    session->beginText(QPointF(40, 40), true);
    QVERIFY(!session->textDraft().value().layerID);
    session->cancelText();
    // New text: empty, in the foreground colour, no box.
    session->beginText(QRectF(600, 400, 100, 50));
    TextDraft boxed = session->textDraft().value();
    boxed.style.content = QStringLiteral("Boxed");
    QVERIFY(session->applyText(boxed));
    QVERIFY(session->textDefaults().boxSize);
    session->setPaletteColor(PaletteColor{0.25, 1, 0.5}, false);
    session->selectTool(NavigationTool::move);
    session->beginText(QPointF(700, 300));
    QCOMPARE(session->tool(), NavigationTool::type);
    const TextDraft fresh = session->textDraft().value();
    QVERIFY(!fresh.layerID && fresh.style.content.isEmpty() && !fresh.style.boxSize);
    QVERIFY(fresh.style.red == 0.25 && fresh.style.green == 1 && fresh.style.blue == 0.5);
    QCOMPARE(fresh.origin, QPointF(700, 300));
    session->cancelText();
    session->beginText(QPointF(NAN, 5));
    QVERIFY(!session->textDraft());
    session->beginText(QPointF(5, INFINITY));
    QVERIFY(!session->textDraft());
    // On a mask, new text keeps the defaults' colour.
    session->selectLayer(lower);
    session->addLayerMask(true);
    session->beginText(QPointF(700, 300));
    QCOMPARE(session->textDraft().value().style.green, 0.0);
    QCOMPARE(session->tool(), NavigationTool::type);
}

void TypeSessionTests::applyingKeepsTheCornerAndPinsTheMask()
{
    const auto session = makeSession();
    const QUuid id = textLayer(*session, QPointF(50, 60), QStringLiteral("Hi"));
    const QSize first = layerWith(*session, id).asset.value().size();
    rewrite(*session, [&](ProjectSnapshot &snapshot) { record(snapshot, id).transform.size *= 2; });
    session->addLayerMask(true);
    session->editActiveText();
    TextDraft draft = session->textDraft().value();
    // The draft starts where the layer is, as it is.
    QCOMPARE(draft.origin, QPointF(50, 60));
    QCOMPARE(draft.transform, std::optional(layerWith(*session, id).transform));
    const int focus = session->canvasFocusRequest();
    // Unchanged: nothing recorded, the draft simply closes.
    const int steps = session->history.undoCount();
    QVERIFY(session->applyText(draft));
    QVERIFY(session->history.undoCount() == steps && session->canvasFocusRequest() == focus && !session->textDraft());
    session->editActiveText();
    draft = session->textDraft().value();
    draft.style.content = QStringLiteral("Hi there");
    QVERIFY(session->applyText(draft));
    QCOMPARE(session->history.undoName(), QString("Edit Text"));
    QCOMPARE(session->canvasFocusRequest(), focus + 1);
    const ImageLayer edited = layerWith(*session, id);
    // Point text grows at the layer's scale, corner fixed.
    QCOMPARE(edited.transform.size, QSizeF(edited.asset.value().size()) * 2);
    QCOMPARE(edited.transform.point(QPointF(0, 0)), QPointF(50, 60));
    QVERIFY(edited.asset.value().size().width() > first.width());
    QCOMPARE(edited.mask.value().placement, std::optional(LayerTransform{.origin = {50, 60}, .size = QSizeF(first) * 2}));
    QCOMPARE(session->textDefaults().content, QString("Hi there"));
    // A box keeps the transform it was given.
    session->editActiveText();
    draft = session->textDraft().value();
    draft.style.boxSize = QSizeF(300, 200);
    draft.transform = LayerTransform{.origin = {40, 40}, .size = {600, 400}};
    QVERIFY(session->applyText(draft));
    QCOMPARE(layerWith(*session, id).transform, (LayerTransform{.origin = {40, 40}, .size = {600, 400}}));
    QCOMPARE(layerWith(*session, id).asset.value().size(), QSize(300, 200));
}

void TypeSessionTests::aDraggedBoxIsRoundedAndBounded()
{
    const auto session = makeSession();
    session->beginText(QRectF(10.4, 20.6, 100.4, 8.2));
    QCOMPARE(session->textDraft().value().style.boxSize, std::optional(QSizeF(100, 16)));
    QCOMPARE(session->textDraft().value().origin, QPointF(10.4, 20.6));
    // An open draft keeps its own box.
    session->beginText(QRectF(0, 0, 400, 300));
    QCOMPARE(session->textDraft().value().style.boxSize, std::optional(QSizeF(100, 16)));
    session->cancelText();
    session->beginText(QRectF(0, 0, 30'001, 50));
    QVERIFY(!session->textDraft());
    QCOMPARE(session->brushError().value(), QStringLiteral("That text box exceeds the 30,000-pixel or %1-megapixel limit.").arg(DocumentLimits::maxSurfaceMegapixels()));
    // No number at all is refused without a word.
    session->setBrushError(std::nullopt);
    session->beginText(QRectF(0, 0, INFINITY, 50));
    session->beginText(QRectF(0, 0, 50, NAN));
    session->beginText(QRectF(0, 0, NAN, 50));
    QVERIFY(!session->textDraft() && !session->brushError());
}

void TypeSessionTests::aStyleIsValidWithinSwiftsBounds()
{
    const auto valid = [](const std::function<void(LayerTextStyle &)> &change) {
        LayerTextStyle style;
        change(style);
        return style.isValid();
    };
    QVERIFY(LayerTextStyle().isValid() && LayerTextStyle().boxIsValid());
    // Content counts UTF-16 units, as Swift's `utf16.count`.
    QVERIFY(valid([](LayerTextStyle &style) { style.content = QString(100'000, QLatin1Char('a')); }));
    QVERIFY(!valid([](LayerTextStyle &style) { style.content = QString(100'001, QLatin1Char('a')); }));
    QVERIFY(!valid([](LayerTextStyle &style) { style.content = QStringLiteral("😀").repeated(50'001); }));
    // Every number holds its bounds, and no further.
    const std::vector<std::tuple<double LayerTextStyle::*, double, double>> numbers = {
        {&LayerTextStyle::fontSize, 1, 2000}, {&LayerTextStyle::red, 0, 1}, {&LayerTextStyle::green, 0, 1},
        {&LayerTextStyle::blue, 0, 1}, {&LayerTextStyle::tracking, -100, 1000}, {&LayerTextStyle::leading, 0, 5000}};
    for (const auto &[member, low, high] : numbers) {
        for (const double value : {low, high})
            QVERIFY(valid([&](LayerTextStyle &style) { style.*member = value; }));
        for (const double value : {low - 0.01, high + 0.01, double(NAN), double(INFINITY), -double(INFINITY)})
            QVERIFY(!valid([&](LayerTextStyle &style) { style.*member = value; }));
    }
    // A box: 16 to 30,000 a side, one surface's pixels.
    const auto box = [&](double width, double height) { return valid([&](LayerTextStyle &style) { style.boxSize = QSizeF(width, height); }); };
    QVERIFY(box(16, 16) && box(30'000, 3'000) && box(20'000, 10'000) && box(3'000, 30'000));
    QVERIFY(!box(15.99, 100) && !box(100, 15.99) && !box(30'000.5, 100) && !box(100, 30'000.5));
    QVERIFY(!box(20'000, 10'000.01) && !box(NAN, 100) && !box(100, INFINITY));
}

void TypeSessionTests::everyChangeIsAnnouncedAndRefusalsAreSilent()
{
    const auto session = makeSession();
    QStringList heard;
    connect(session.get(), &EditorSession::changed, session.get(), [&] { heard = described(*session); });
    QSignalSpy spy(session.get(), &EditorSession::changed);
    // A change's last signal sees what the call leaves.
    const auto announced = [&](const std::function<void()> &call) {
        spy.clear();
        call();
        return !spy.isEmpty() && heard == described(*session);
    };
    const auto silent = [&](const std::function<void()> &call) {
        const QStringList before = described(*session);
        spy.clear();
        call();
        return spy.isEmpty() && described(*session) == before;
    };
    const auto restyle = [&](double size) { session->changeTextStyle([size](LayerTextStyle &style) { style.fontSize = size; }); };
    QVERIFY(announced([&] { restyle(40); }));
    QVERIFY(silent([&] { restyle(0); }));
    // Blank new text closes the draft and says so.
    QVERIFY(announced([&] { session->beginText(QPointF(10, 10)); }));
    QVERIFY(announced([&] { session->finishText(); }));
    QVERIFY(announced([&] { session->beginText(QPointF(10, 10)); }));
    QVERIFY(silent([&] { session->beginText(QPointF(20, 20)); }));
    QVERIFY(silent([&] { session->beginText(QRectF(0, 0, 50, 50)); }));
    QVERIFY(announced([&] { session->changeTextStyle([](LayerTextStyle &style) { style.content = QStringLiteral("Heard"); }); }));
    QVERIFY(silent([&] { restyle(3000); }));
    // Busy, the draft stays open and nothing is said.
    session->setIsProjectBusy(true);
    QVERIFY(silent([&] { session->finishText(); }));
    session->setIsProjectBusy(false);
    QVERIFY(announced([&] { session->finishText(); }));
    const QUuid id = session->activeLayerID().value();
    QVERIFY(silent([&] { session->finishText(); }));
    QVERIFY(announced([&] { session->editActiveText(); }));
    QVERIFY(silent([&] { session->editActiveText(); }));
    TextDraft stale = session->textDraft().value();
    stale.documentID = QUuid::createUuid();
    QVERIFY(silent([&] { session->applyText(stale); }));
    // An invalid style is refused before anything is tried.
    TextDraft invalid = session->textDraft().value();
    invalid.style.fontSize = 0;
    QVERIFY(silent([&] { session->applyText(invalid); }));
    QVERIFY(announced([&] { session->finishText(); }));
    QVERIFY(announced([&] { restyle(30); }));
    QVERIFY(announced([&] { session->finishText(); }));
    // An error comes after the draft is back.
    session->editActiveText();
    QVERIFY(announced([&] { session->changeTextStyle([](LayerTextStyle &style) { style.content = QString(80, QLatin1Char('W')); style.fontSize = 2000; }); }));
    QVERIFY(announced([&] { session->finishText(); }));
    QVERIFY(session->textDraft() && session->brushError());
    QVERIFY(announced([&] { session->cancelText(); }));
    QVERIFY(announced([&] { session->recolorText(id, PaletteColor{0, 0, 1}); }));
    QVERIFY(silent([&] { session->recolorText(id, PaletteColor{0, 0, 1}); }));
    QVERIFY(announced([&] { session->beginText(QRectF(0, 0, 40'000, 50)); }));
}

void TypeSessionTests::applyingTakesWhatSwiftTakes()
{
    const auto session = makeSession();
    const QUuid blank = session->activeLayerID().value();
    const size_t count = session->document().value().layers.size();
    // Blank new text goes; emptied old text stays text.
    session->beginText(QPointF(10, 10));
    TextDraft draft = session->textDraft().value();
    draft.style.content = QStringLiteral(" \n\t ");
    const int focus = session->canvasFocusRequest();
    QVERIFY(session->applyText(draft));
    QVERIFY(!session->textDraft() && session->canvasFocusRequest() == focus);
    QCOMPARE(session->document().value().layers.size(), count);
    const QUuid id = textLayer(*session, QPointF(100, 100), QStringLiteral("Kept"));
    session->editActiveText();
    draft = session->textDraft().value();
    draft.style.content = QStringLiteral("  ");
    QVERIFY(session->applyText(draft));
    QCOMPARE(layerWith(*session, id).liveText().value().style.content, QString("  "));
    // A pixel layer never takes a text draft.
    session->editActiveText();
    draft = session->textDraft().value();
    draft.layerID = blank;
    QVERIFY(!session->applyText(draft));
    QCOMPARE(session->textDraft().value().layerID, std::optional(id));
    session->cancelText();
    session->selectLayer(blank);
    session->editActiveText();
    QVERIFY(!session->textDraft());
    // Moved, same words: the corner follows the move.
    session->selectLayer(id);
    session->editActiveText();
    draft = session->textDraft().value();
    const QSizeF size = draft.transform.value().size;
    draft.transform.value().origin = QPointF(300, 200);
    QVERIFY(session->applyText(draft));
    QCOMPARE(layerWith(*session, id).transform, (LayerTransform{.origin = {300, 200}, .size = size}));
    session->editActiveText();
    draft = session->textDraft().value();
    draft.transform.value().origin = QPointF(10, 20);
    draft.style.content = QStringLiteral("Kept and grown");
    QVERIFY(session->applyText(draft));
    const ImageLayer grown = layerWith(*session, id);
    QCOMPARE(grown.transform, (LayerTransform{.origin = {10, 20}, .size = QSizeF(grown.asset.value().size())}));
    // A box without a transform scales as the layer did.
    rewrite(*session, [&](ProjectSnapshot &snapshot) { record(snapshot, id).transform.size *= 2; });
    session->editActiveText();
    draft = session->textDraft().value();
    draft.transform = std::nullopt;
    draft.style.boxSize = QSizeF(200, 100);
    QVERIFY(session->applyText(draft));
    QCOMPARE(layerWith(*session, id).transform, (LayerTransform{.origin = {10, 20}, .size = {400, 200}}));
    // New text leaves the selection where it is.
    session->applySelection(rectPath(QRectF(5, 5, 20, 20)), SelectionMode::replace, "Select");
    textLayer(*session, QPointF(500, 400), QStringLiteral("Selected"));
    QCOMPARE(bounds(*session), QRectF(5, 5, 20, 20));
    // Grown past the layer limits, the edit is refused.
    rewrite(*session, [&](ProjectSnapshot &snapshot) { record(snapshot, id).transform.size = QSizeF(299'000, 100); });
    session->selectLayer(id);
    session->editActiveText();
    draft = session->textDraft().value();
    draft.style.content = QStringLiteral("Kept and grown further");
    draft.style.boxSize = std::nullopt;
    session->setTextDraft(draft);
    QVERIFY(!session->finishText());
    QCOMPARE(session->brushError().value(), QStringLiteral("This project exceeds the supported canvas, layer, file-size, or %1-megapixel document limit.").arg(DocumentLimits::documentBudgetMegapixels()));
    QCOMPARE(session->textDraft().value().style.content, QString("Kept and grown further"));
    QCOMPARE(layerWith(*session, id).liveText().value().style.content, QString("Kept and grown"));
}

void TypeSessionTests::recolouringNeedsANewColour()
{
    const auto session = makeSession();
    const QUuid id = textLayer(*session, QPointF(10, 10), QStringLiteral("Hue"));
    const int steps = session->history.undoCount();
    // Each channel alone is a change.
    for (const PaletteColor &colour : {PaletteColor{1, 0, 0}, PaletteColor{1, 0.5, 0}, PaletteColor{1, 0.5, 0.25}}) {
        QVERIFY(session->recolorText(id, colour));
        const LayerTextStyle style = layerWith(*session, id).liveText().value().style;
        QVERIFY(style.red == colour.red && style.green == colour.green && style.blue == colour.blue);
    }
    QCOMPARE(session->history.undoCount(), steps + 3);
    QVERIFY(session->recolorText(id, PaletteColor{1, 0.5, 0.25}));
    QCOMPARE(session->history.undoCount(), steps + 3);
    // An opacity drag ends before the fill's own step.
    session->beginOpacityEdit();
    session->setLayerOpacity(0.5);
    QVERIFY(session->recolorText(id, PaletteColor{0, 0, 0}));
    QVERIFY(session->canUndo() && session->history.undoName() == QString("Fill Text"));
    QCOMPARE(session->history.undoCount(), steps + 5);
    // An open draft holds the layers.
    session->editActiveText();
    QVERIFY(!session->recolorText(id, PaletteColor{0, 1, 0}));
    session->cancelText();
    EditorSession empty;
    QVERIFY(!empty.recolorText(id, PaletteColor{0, 1, 0}));
}

QTEST_MAIN(TypeSessionTests)
#include "TypeSessionTests.moc"
