#include "Document/BrushStroke.h"
#include "IO/CanvasResizer.h"
#include "IO/ImageResizer.h"
#include "SessionFixtures.h"
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

// Swift's GuideTests: guides, the layout grid and snapping.
namespace {
CanvasGuide vertical(double position)
{
    return {QUuid::createUuid(), CanvasGuide::Axis::vertical, position};
}

CanvasGuide horizontal(double position)
{
    return {QUuid::createUuid(), CanvasGuide::Axis::horizontal, position};
}

// A red 100 by 60 layer, centred on the canvas.
std::unique_ptr<EditorSession> paintedSession(int width = 400, int height = 300)
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(width, height);
    QImage image = BrushRaster::context(100, 60, false);
    image.fill(Qt::red);
    session->insert(ImportedImage(image, image, QStringLiteral("Red")));
    return session;
}

QSet<double> set(const std::vector<double> &values)
{
    return QSet<double>(values.begin(), values.end());
}

const std::vector<CanvasGuide> &guides(const EditorSession &session)
{
    return session.document().value().guides;
}
}

class GuideTests : public QObject {
    Q_OBJECT
private slots:
    void newGuidesUndoAndClear();
    void lockPreventsCreatingAndMoving();
    void projectRoundTripAndLegacyRejection();
    void theStoreRefusesWhatSwiftRefuses();
    void canvasAndImageSizeMoveGuides();
    void flipCanvasMirrorsGuides();
    void snapTargetsFollowViewMenu();
    void layoutGridLinesIncludeMajorsAndSubdivisions();
    void aDragCreatesMovesAndDeletesGuides();
    void aDragSnapsAndHitsTheNearestGuide();
};

void GuideTests::newGuidesUndoAndClear()
{
    EditorSession session;
    session.createDocument(200, 100);
    const CanvasGuide first = vertical(40), second = horizontal(25);
    session.addGuide(first);
    session.addGuide(second);
    QCOMPARE(int(guides(session).size()), 2);
    QCOMPARE(session.history.undoName(), QString("New Guide"));
    QVERIFY(session.canClearGuides());
    session.undo();
    QCOMPARE(guides(session), std::vector<CanvasGuide>{first});
    session.clearGuides();
    QVERIFY(guides(session).empty());
    QCOMPARE(session.history.undoName(), QString("Clear Guides"));
    QVERIFY(!session.canClearGuides());
    session.undo();
    QCOMPARE(guides(session), std::vector<CanvasGuide>{first});
    // A hidden guide shows again once one is added.
    session.setShowsGuides(false);
    session.addGuide(horizontal(5));
    QVERIFY(session.showsGuides());
}

void GuideTests::lockPreventsCreatingAndMoving()
{
    EditorSession session;
    session.createDocument(200, 100);
    session.setLocksGuides(true);
    session.addGuide(vertical(10));
    QVERIFY(guides(session).empty());
    session.setLocksGuides(false);
    const CanvasGuide guide = vertical(10);
    session.addGuide(guide);
    session.setLocksGuides(true);
    session.beginGuideMove(guide);
    QVERIFY(!session.guideDrag());
    session.beginGuideCreation(CanvasGuide::Axis::vertical, 50);
    QVERIFY(!session.guideDrag());
    QVERIFY(!session.hitGuide(session.viewport.viewPoint(QPointF(10, 10), QSizeF(200, 100))));
    // Clear Guides still works while locked.
    session.clearGuides();
    QVERIFY(guides(session).empty());
    // Every gate Swift names closes the guides.
    session.setLocksGuides(false);
    QVERIFY(session.canEditGuides());
    for (const auto &[name, close, open] : std::vector<std::tuple<const char *, std::function<void()>, std::function<void()>>>{
             {"busy", [&] { session.setIsProjectBusy(true); }, [&] { session.setIsProjectBusy(false); }},
             {"importing", [&] { session.setIsImporting(true); }, [&] { session.setIsImporting(false); }},
             {"new document", [&] { session.setShowsNewDocument(true); }, [&] { session.setShowsNewDocument(false); }},
             {"renaming", [&] { session.setRenamingLayerID(QUuid::createUuid()); }, [&] { session.setRenamingLayerID(std::nullopt); }}}) {
        close();
        QVERIFY2(!session.canEditGuides(), name);
        open();
        QVERIFY2(session.canEditGuides(), name);
    }
    // The colour edits close the guides too.
    QImage red(10, 10, QImage::Format_RGBA8888_Premultiplied);
    red.fill(Qt::red);
    session.insert(ImportedImage(red, red, QStringLiteral("Red")));
    session.beginLevels();
    QVERIFY(session.levels() && !session.canEditGuides());
    session.cancelLevels();
    session.beginHueSaturation();
    QVERIFY(session.hueSaturation() && !session.canEditGuides());
    session.cancelHueSaturation();
    session.beginFilter(FilterKind::gaussianBlur);
    QVERIFY(session.filterEdit() && !session.canEditGuides());
    session.cancelFilter();
    QVERIFY(session.canEditGuides());
    EditorSession empty;
    QVERIFY(!empty.canEditGuides() && !empty.canClearGuides());
}

void GuideTests::projectRoundTripAndLegacyRejection()
{
    QTemporaryDir root;
    EditorSession session;
    session.createDocument(80, 40);
    QVERIFY(!session.projectSnapshot().value().manifest.guides);
    const CanvasGuide first = vertical(16), second = horizontal(12);
    session.addGuide(first);
    session.addGuide(second);
    ProjectSnapshot snapshot = session.projectSnapshot().value();
    QCOMPARE(snapshot.manifest.version, qint64(8));
    QCOMPARE(snapshot.manifest.guides.value(), (std::vector<CanvasGuide>{first, second}));
    // Swift's keys: an array of id, axis and position.
    const QByteArray json = snapshot.manifest.encoded();
    QVERIFY(json.contains("\"axis\": \"vertical\"") && json.contains("\"position\": 16") && json.contains(uuidString(first.id).toUtf8()));
    const QString path = root.filePath("Guides.comp");
    ProjectStore::save(snapshot, path);
    EditorSession reopened;
    reopened.installProject(ProjectStore::load(path), path);
    QCOMPARE(guides(reopened), (std::vector<CanvasGuide>{first, second}));
    // Version 7 carries no guides.
    snapshot.manifest.version = 7;
    QVERIFY_THROWS_EXCEPTION(ProjectError, ProjectStore::save(snapshot, root.filePath("Legacy.comp")));
    try {
        ProjectStore::save(snapshot, root.filePath("Legacy.comp"));
    } catch (const ProjectError &error) {
        QCOMPARE(error.kind, ProjectError::Kind::invalid);
    }
}

void GuideTests::theStoreRefusesWhatSwiftRefuses()
{
    EditorSession session;
    session.createDocument(80, 40);
    const ProjectSnapshot plain = session.projectSnapshot().value();
    const auto refusal = [&](const std::vector<CanvasGuide> &lines) -> std::optional<ProjectError::Kind> {
        ProjectSnapshot snapshot = plain;
        snapshot.manifest.guides = lines;
        QTemporaryDir root;
        try {
            ProjectStore::save(snapshot, root.filePath("Rules.comp"));
        } catch (const ProjectError &error) {
            return error.kind;
        }
        return std::nullopt;
    };
    QCOMPARE(refusal({vertical(1'000'000), horizontal(-1'000'000)}), std::nullopt);
    QCOMPARE(refusal({vertical(1'000'000.5)}), std::optional(ProjectError::Kind::invalid));
    QCOMPARE(refusal({vertical(std::nan(""))}), std::optional(ProjectError::Kind::invalid));
    const CanvasGuide twice = vertical(3);
    QCOMPARE(refusal({twice, CanvasGuide{twice.id, CanvasGuide::Axis::horizontal, 4}}), std::optional(ProjectError::Kind::invalid));
    std::vector<CanvasGuide> many(1'000);
    for (CanvasGuide &guide : many)
        guide = vertical(1);
    QCOMPARE(refusal(many), std::nullopt);
    many.push_back(vertical(2));
    QCOMPARE(refusal(many), std::optional(ProjectError::Kind::tooLarge));
    // The decoder refuses strange axes, missing keys, non-arrays.
    const QByteArray base = plain.manifest.encoded();
    const auto decodes = [&](const QByteArray &guidesJson) {
        QByteArray json = base;
        json.insert(json.lastIndexOf('}'), ",\n  \"guides\": " + guidesJson + "\n");
        try {
            return ProjectManifest::decoded(json).guides.has_value();
        } catch (const ProjectError &) {
            return false;
        }
    };
    const QByteArray id = uuidString(QUuid::createUuid()).toUtf8();
    QVERIFY(decodes("[{\"id\": \"" + id + "\", \"axis\": \"vertical\", \"position\": 5}]"));
    QVERIFY(decodes("null") == false);
    QVERIFY(!decodes("[{\"id\": \"" + id + "\", \"axis\": \"diagonal\", \"position\": 5}]"));
    QVERIFY(!decodes("[{\"id\": \"" + id + "\", \"axis\": \"vertical\"}]"));
    QVERIFY(!decodes("[{\"id\": \"" + id + "\", \"axis\": \"vertical\", \"position\": \"5\"}]"));
    QVERIFY(!decodes("[5]"));
    QVERIFY(!decodes("{}"));
}

void GuideTests::canvasAndImageSizeMoveGuides()
{
    EditorSession session;
    session.createDocument(100, 50);
    session.addGuide(vertical(20));
    session.addGuide(horizontal(10));
    const ProjectSnapshot source = session.projectSnapshot().value();
    // Anchor 8 is bottom right: room goes left and above.
    const ProjectSnapshot expanded = CanvasResizer::resize(source, CanvasSizeOptions{.width = 140, .height = 80, .anchor = 8});
    QCOMPARE(expanded.manifest.guides.value()[0].position, 60.0);
    QCOMPARE(expanded.manifest.guides.value()[1].position, 40.0);
    const ProjectSnapshot scaled = ImageResizer::resize(source, ImageSizeOptions{.width = 200, .height = 100, .resolution = 72});
    QCOMPARE(scaled.manifest.guides.value()[0].position, 40.0);
    QCOMPARE(scaled.manifest.guides.value()[1].position, 20.0);
    // Each axis scales by its own side.
    const ProjectSnapshot wide = ImageResizer::resize(source, ImageSizeOptions{.width = 200, .height = 50, .resolution = 72});
    QCOMPARE(wide.manifest.guides.value()[0].position, 40.0);
    QCOMPARE(wide.manifest.guides.value()[1].position, 10.0);
    const ProjectSnapshot resolved = ImageResizer::resize(source, ImageSizeOptions{.width = 100, .height = 50, .resolution = 144});
    QCOMPARE(resolved.manifest.guides, source.manifest.guides);
    // None stay none; the session's rebuild keeps them.
    EditorSession bare;
    bare.createDocument(10, 10);
    QVERIFY(!CanvasResizer::resize(bare.projectSnapshot().value(), CanvasSizeOptions{.width = 20, .height = 20}).manifest.guides);
    QVERIFY(!ImageResizer::resize(bare.projectSnapshot().value(), ImageSizeOptions{.width = 20, .height = 20, .resolution = 72}).manifest.guides);
    session.applyDocumentSize(expanded, QStringLiteral("Canvas Size"));
    QCOMPARE(guides(session), expanded.manifest.guides.value());
}

void GuideTests::flipCanvasMirrorsGuides()
{
    EditorSession session;
    session.createDocument(100, 40);
    session.addGuide(vertical(20));
    session.addGuide(horizontal(10));
    session.flipCanvas(true);
    QCOMPARE(guides(session)[0].position, 80.0);
    QCOMPARE(guides(session)[1].position, 10.0);
    session.flipCanvas(false);
    QCOMPARE(guides(session)[0].position, 80.0);
    QCOMPARE(guides(session)[1].position, 30.0);
    session.undo();
    QCOMPARE(guides(session)[1].position, 10.0);
}

void GuideTests::snapTargetsFollowViewMenu()
{
    const auto session = paintedSession();
    // The canvas and the centred layer: 150–250, 120–180.
    const SnapGuides crop = session->cropSnapTargets();
    QCOMPARE(set(crop.xs), (QSet<double>{0, 400, 150, 250}));
    QCOMPARE(set(crop.ys), (QSet<double>{0, 300, 120, 180}));
    QCOMPARE(session->transformSnapTargets({}).xs, (std::vector<double>{0, 400, 200, 150, 200, 250}));
    session->setSnapEnabled(false);
    QVERIFY(session->cropSnapTargets().xs.empty() && session->cropSnapTargets().ys.empty());
    session->setSnapEnabled(true);
    session->setSnapToLayers(false);
    QCOMPARE(set(session->cropSnapTargets().xs), (QSet<double>{0, 400}));
    session->setSnapToDocumentBounds(false);
    QVERIFY(session->cropSnapTargets().xs.empty());
    session->setSnapToGuides(true);
    session->setShowsGuides(true);
    session->addGuide(vertical(33));
    QCOMPARE(session->cropSnapTargets().xs, std::vector<double>{33});
    // Hidden extras do not snap.
    session->setShowsGuides(false);
    QVERIFY(session->cropSnapTargets().xs.empty());
    session->setShowsGuides(true);
    session->setSnapToGuides(false);
    QVERIFY(session->cropSnapTargets().xs.empty());
    session->setShowsGrid(true);
    QVERIFY(session->cropSnapTargets().xs.empty());
    session->setSnapToGrid(true);
    QVERIFY(set(session->cropSnapTargets().xs).contains(64) && set(session->cropSnapTargets().xs).contains(8));
    // Each axis takes the lines along its own edge.
    QVERIFY(set(session->cropSnapTargets().xs).contains(392) && !set(session->cropSnapTargets().ys).contains(304));
    session->setShowsGrid(false);
    QVERIFY(session->cropSnapTargets().xs.empty());
    // A move snaps to a guide through the same targets.
    session->setSnapToGuides(true);
    const QUuid red = session->activeLayerID().value();
    QCOMPARE(session->snappedMove(LayerTransform{.origin = {35, 0}, .size = {100, 60}}, {red}, 5).origin.x(), 33.0);
}

void GuideTests::layoutGridLinesIncludeMajorsAndSubdivisions()
{
    const std::vector<double> lines = LayoutGrid::lines(64);
    QCOMPARE(int(lines.size()), 9);
    QVERIFY(lines.front() == 0 && lines.back() == 64 && lines[1] == 8);
    QVERIFY(LayoutGrid::isMajor(0) && LayoutGrid::isMajor(64) && LayoutGrid::isMajor(128) && !LayoutGrid::isMajor(8));
    QVERIFY(LayoutGrid::isMajor(63.6) && !LayoutGrid::isMajor(63.4));
    QCOMPARE(LayoutGrid::lines(-1), std::vector<double>{0});
    QCOMPARE(LayoutGrid::lines(7.9), std::vector<double>{0});
    QCOMPARE(int(LayoutGrid::lines(20).size()), 3);
}

void GuideTests::aDragCreatesMovesAndDeletesGuides()
{
    const auto session = paintedSession();
    session->setShowsGuides(false);
    QSignalSpy changes(session.get(), &EditorSession::changed);
    session->beginGuideCreation(CanvasGuide::Axis::vertical, 40.3);
    QVERIFY(session->showsGuides() && changes.count() == 1);
    QCOMPARE(session->displayedGuides().size(), size_t(1));
    QVERIFY(guides(*session).empty());
    session->moveGuideDrag(70);
    QCOMPARE(session->displayedGuides()[0].position, 70.0);
    session->finishGuideDrag(false);
    QCOMPARE(guides(*session)[0].position, 70.0);
    QCOMPARE(session->history.undoName(), QString("New Guide"));
    QVERIFY(!session->guideDrag());
    // A new guide dropped on a ruler is never made.
    const int steps = session->history.undoCount();
    session->beginGuideCreation(CanvasGuide::Axis::horizontal, 30);
    session->finishGuideDrag(true);
    QCOMPARE(guides(*session).size(), size_t(1));
    QCOMPARE(session->history.undoCount(), steps);
    // An old one moves as one step, or none unmoved.
    const CanvasGuide guide = guides(*session)[0];
    session->beginGuideMove(guide);
    QCOMPARE(session->guideDrag().value(), (GuideDrag{guide.id, guide.axis, 70, false, 70.0}));
    session->finishGuideDrag(false);
    QCOMPARE(session->history.undoCount(), steps);
    session->beginGuideMove(guide);
    session->moveGuideDrag(90);
    QCOMPARE(session->displayedGuides(), (std::vector<CanvasGuide>{{guide.id, guide.axis, 90}}));
    session->finishGuideDrag(false);
    QCOMPARE(session->history.undoName(), QString("Move Guide"));
    QCOMPARE(guides(*session)[0].position, 90.0);
    // A guide cleared mid-drag no longer shows.
    session->beginGuideMove(guides(*session)[0]);
    session->clearGuides();
    QVERIFY(session->displayedGuides().empty());
    session->cancelGuideDrag();
    session->undo();
    QCOMPARE(guides(*session)[0].position, 90.0);
    // Cancelled, nothing changes; dropped on a ruler, it goes.
    session->beginGuideMove(guides(*session)[0]);
    session->moveGuideDrag(20);
    session->cancelGuideDrag();
    QVERIFY(!session->guideDrag());
    QCOMPARE(guides(*session)[0].position, 90.0);
    session->beginGuideMove(guides(*session)[0]);
    session->finishGuideDrag(true);
    QVERIFY(guides(*session).empty());
    QCOMPARE(session->history.undoName(), QString("Delete Guide"));
    session->finishGuideDrag(false);
    session->moveGuideDrag(5);
    QVERIFY(guides(*session).empty() && !session->guideDrag());
    // A project installed or cleared drops the drag.
    session->beginGuideCreation(CanvasGuide::Axis::vertical, 10);
    session->installProject(session->projectSnapshot().value(), QString());
    QVERIFY(!session->guideDrag());
    session->beginGuideCreation(CanvasGuide::Axis::vertical, 10);
    session->clearProject();
    QVERIFY(!session->guideDrag());
}

void GuideTests::aDragSnapsAndHitsTheNearestGuide()
{
    const auto session = paintedSession();
    session->viewport.resize(QSizeF(400, 300), 1, QSizeF(400, 300));
    session->zoom(1);
    // Within ten view points: the layer's left edge at 150.
    session->beginGuideCreation(CanvasGuide::Axis::vertical, 144);
    QCOMPARE(session->guideDrag().value().position, 150.0);
    session->moveGuideDrag(139);
    QCOMPARE(session->guideDrag().value().position, 139.0);
    // The nearest wins: the middle 200 over 150 at 180.
    session->moveGuideDrag(193);
    QCOMPARE(session->guideDrag().value().position, 200.0);
    session->finishGuideDrag(false);
    // Another guide snaps, but not the dragged one itself.
    const CanvasGuide placed = guides(*session)[0];
    session->beginGuideCreation(CanvasGuide::Axis::vertical, 196);
    QCOMPARE(session->guideDrag().value().position, 200.0);
    session->cancelGuideDrag();
    session->setSnapToLayers(false);
    // The canvas's middle snaps too, from its bounds alone.
    session->beginGuideCreation(CanvasGuide::Axis::horizontal, 146);
    QCOMPARE(session->guideDrag().value().position, 150.0);
    session->cancelGuideDrag();
    session->setSnapToDocumentBounds(false);
    session->beginGuideMove(placed);
    session->moveGuideDrag(203);
    QCOMPARE(session->guideDrag().value().position, 203.0);
    session->cancelGuideDrag();
    // The grid's lines snap only while shown and asked.
    session->setSnapToGrid(true);
    session->beginGuideCreation(CanvasGuide::Axis::horizontal, 61);
    QCOMPARE(session->guideDrag().value().position, 61.0);
    session->cancelGuideDrag();
    session->setShowsGrid(true);
    session->beginGuideCreation(CanvasGuide::Axis::horizontal, 61);
    QCOMPARE(session->guideDrag().value().position, 64.0);
    session->cancelGuideDrag();
    // Ten view points reach 2.5 pixels at 400%.
    session->zoom(4);
    session->beginGuideCreation(CanvasGuide::Axis::horizontal, 60);
    QCOMPARE(session->guideDrag().value().position, 60.0);
    session->moveGuideDrag(62);
    QCOMPARE(session->guideDrag().value().position, 64.0);
    session->cancelGuideDrag();
    session->zoom(1);
    session->setSnapEnabled(false);
    session->beginGuideCreation(CanvasGuide::Axis::horizontal, 61);
    QCOMPARE(session->guideDrag().value().position, 61.0);
    session->cancelGuideDrag();
    // A press finds the nearest guide within five points.
    session->addGuide(horizontal(100));
    session->addGuide(horizontal(104));
    const QPointF near = session->viewport.viewPoint(QPointF(0, 103), QSizeF(400, 300));
    QCOMPARE(session->hitGuide(near).value().position, 104.0);
    QVERIFY(!session->hitGuide(near + QPointF(0, 7)));
    QCOMPARE(session->hitGuide(near + QPointF(0, 7), 7).value().position, 104.0);
    QCOMPARE(session->hitGuide(session->viewport.viewPoint(QPointF(201, 0), QSizeF(400, 300))).value().id, placed.id);
    session->setShowsGuides(false);
    QVERIFY(!session->hitGuide(near));
}

QTEST_GUILESS_MAIN(GuideTests)
#include "GuideTests.moc"
