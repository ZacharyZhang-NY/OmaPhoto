#include "Document/LayerMask.h"
#include "ProjectFixtures.h"
#include <QImageWriter>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>

class ProjectTests : public QObject {
    Q_OBJECT
private slots:
    void theCurrentFormatVersionIsOneTheReaderAccepts();
    void projectRoundTripSurvivesPackageMove();
    void masksAndLargeImagesRoundTrip();
    void overwriteReplacesPackageAndDropsRemovedAssets();
    void failedSavePreservesPreviouslySavedPackage();
    void unsupportedCorruptAndUnsafeMetadataAreRejected();
    void missingAndForeignImagesAreRejected();
    void filesMustBePlainFilesInsideTheProject();
    void sizeLimitsHoldOnSaveAndLoad();
    void numbersThatAreNoNumbersAreRefusedOnSave();
    void savesAndLoadsAreLogged();
};

// Saves write current; loads take supported: they must agree.
void ProjectTests::theCurrentFormatVersionIsOneTheReaderAccepts()
{
    QVERIFY(ProjectManifest::supports(ProjectManifest::current));
    QVERIFY((ProjectManifest::supported == std::pair<qint64, qint64>(1, 9)));
    QCOMPARE(ProjectManifest{}.version, ProjectManifest::current);
    QVERIFY(!ProjectManifest::supports(0) && !ProjectManifest::supports(ProjectManifest::current + 1));
    QCOMPARE(QString(ProjectError::unsupportedVersion(10).what()), QString("This project uses format version 10. This app supports versions 1–9."));
}

void ProjectTests::projectRoundTripSurvivesPackageMove()
{
    QTemporaryDir root;
    const ProjectSnapshot before = twoLayers();
    const QString original = root.filePath("Original.comp"), moved = root.filePath("Moved.comp");
    ProjectStore::save(before, original);
    QVERIFY(QDir().rename(original, moved));
    const ProjectSnapshot loaded = ProjectStore::load(moved);
    QCOMPARE(loaded.manifest.encoded(), before.manifest.encoded());
    QCOMPARE(loaded.manifest.documentID, before.manifest.documentID);
    QCOMPARE(loaded.manifest.resolution, std::optional<double>(300));
    QCOMPARE(loaded.manifest.activeLayerID, before.manifest.activeLayerID);
    QCOMPARE(loaded.manifest.layers[0].name, QStringLiteral("Paint & sky \U0001F324"));
    QCOMPARE(loaded.manifest.layers[0].isVisible, false);
    QCOMPARE(loaded.manifest.layers[0].transform, before.manifest.layers[0].transform);
    // The blank layer has no pixels; the image layer does.
    QCOMPARE(int(loaded.images.size()), 1);
    QVERIFY(loaded.masks.empty());
    const ImportedImage &pixels = loaded.images.at(before.manifest.layers[0].id);
    QCOMPARE(pixels.image(), halfRed());
    QCOMPARE(pixels.image().format(), QImage::Format_RGBA8888_Premultiplied);
    QCOMPARE(pixels.thumbnail, halfRed());
    // Up to 96 pixels the thumbnail is the image itself.
    QCOMPARE(pixels.thumbnail.cacheKey(), pixels.image().cacheKey());
    QCOMPARE(pixels.name, QStringLiteral("Paint & sky \U0001F324"));
    // The package holds the manifest and the images, nothing else.
    QCOMPARE(QDir(moved).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden), (QStringList{"images", "manifest.json"}));
    QCOMPARE(QDir(moved + "/images").entryList(QDir::Files), QStringList{uuidString(before.manifest.layers[0].id) + ".png"});
}

void ProjectTests::masksAndLargeImagesRoundTrip()
{
    QTemporaryDir root;
    ProjectSnapshot masked = twoLayers();
    const QUuid id = masked.manifest.layers[0].id;
    QImage soft = gray(40, 20, 0);
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 40; ++x)
            soft.scanLine(y)[x] = uchar(x * 6 + y);
    }
    masked.manifest.layers[0].maskFile = uuidString(id) + ".mask.png";
    masked.manifest.layers[0].maskEnabled = false;
    masked.manifest.layers[0].maskLinked = false;
    masked.manifest.layers[0].maskPlacement = LayerTransform{.origin = {3, 4}, .size = {50, 60}, .rotation = 10};
    masked.masks.insert({id, asset(soft, "mask")});
    // A long, thin image: its thumbnail keeps one pixel across.
    const QUuid wideID = QUuid::createUuid();
    masked.manifest.layers.push_back({.id = wideID, .name = "Wide", .isVisible = true, .transform = {.origin = {0, 0}, .size = {300, 2}},
                                      .imageFile = uuidString(wideID) + ".png"});
    masked.images.insert({wideID, asset(noise(300, 2, 5), "wide")});
    // At 96 pixels the thumbnail is still the image.
    const QUuid edgeID = QUuid::createUuid();
    masked.manifest.layers.push_back({.id = edgeID, .name = "Edge", .isVisible = true, .transform = {.origin = {0, 0}, .size = {96, 10}},
                                      .imageFile = uuidString(edgeID) + ".png"});
    masked.images.insert({edgeID, asset(noise(96, 10, 6), "edge")});
    ProjectStore::save(masked, root.filePath("Masked.comp"));
    const ProjectSnapshot loaded = ProjectStore::load(root.filePath("Masked.comp"));
    QCOMPARE(loaded.manifest.encoded(), masked.manifest.encoded());
    QCOMPARE(loaded.masks.at(id).image(), soft);
    QCOMPARE(loaded.masks.at(id).image().format(), QImage::Format_Grayscale8);
    const LayerMask mask = loaded.mask(loaded.manifest.layers[0]).value();
    QCOMPARE(mask.isEnabled, false);
    QCOMPARE(mask.isLinked, false);
    QCOMPARE(mask.placement.value(), (LayerTransform{.origin = {3, 4}, .size = {50, 60}, .rotation = 10}));
    QCOMPARE(loaded.images.at(wideID).image(), noise(300, 2, 5));
    QCOMPARE(loaded.images.at(wideID).thumbnail.size(), QSize(96, 1));
    QCOMPARE(loaded.images.at(wideID).thumbnail.format(), QImage::Format_RGBA8888_Premultiplied);
    QCOMPARE(loaded.images.at(edgeID).thumbnail.cacheKey(), loaded.images.at(edgeID).image().cacheKey());
    QVERIFY(loaded.images.at(wideID).thumbnail.cacheKey() != loaded.images.at(wideID).image().cacheKey());
}

void ProjectTests::overwriteReplacesPackageAndDropsRemovedAssets()
{
    QTemporaryDir root;
    const QString destination = root.filePath("Overwrite.comp");
    ProjectSnapshot snapshot = twoLayers();
    ProjectStore::save(snapshot, destination);
    QCOMPARE(QDir(destination + "/images").entryList(QDir::Files).size(), 1);
    snapshot.manifest.layers.erase(snapshot.manifest.layers.begin());
    snapshot.images.clear();
    ProjectStore::save(snapshot, destination);
    const ProjectSnapshot loaded = ProjectStore::load(destination);
    QVERIFY(loaded.images.empty());
    QCOMPARE(int(loaded.manifest.layers.size()), 1);
    QVERIFY(QDir(destination + "/images").entryList(QDir::Files).isEmpty());
    // Nothing is left beside the project: no staging, no copy.
    QCOMPARE(QDir(root.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden), QStringList{"Overwrite.comp"});
    // A plain file there is replaced too; trailing slashes pass.
    const QString file = root.filePath("WasAFile.comp");
    overwrite(file, "not a project");
    ProjectStore::save(snapshot, file + "/");
    QCOMPARE(int(ProjectStore::load(file).manifest.layers.size()), 1);
    QCOMPARE(QDir(root.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden), (QStringList{"Overwrite.comp", "WasAFile.comp"}));
}

void ProjectTests::failedSavePreservesPreviouslySavedPackage()
{
    QTemporaryDir root;
    const ProjectSnapshot snapshot = twoLayers();
    const QString path = root.filePath("Safe.comp");
    ProjectStore::save(snapshot, path);
    const QByteArray original = contents(path + "/manifest.json");
    const QStringList beside{"Safe.comp"};
    ProjectSnapshot future = snapshot;
    future.manifest.version = 99;
    try {
        ProjectStore::save(future, path);
        QFAIL("an unsupported version was saved");
    } catch (const ProjectError &error) {
        QCOMPARE(error.kind, ProjectError::Kind::version);
        QCOMPARE(error.version, std::optional(qint64(99)));
    }
    // Pixels named but missing; a mask that is no mask.
    ProjectSnapshot hollow = snapshot;
    hollow.images.clear();
    QCOMPARE(projectError([&] { ProjectStore::save(hollow, path); }), std::optional(ProjectError::Kind::missingImage));
    ProjectSnapshot coloured = snapshot;
    coloured.manifest.layers[1].maskFile = uuidString(coloured.manifest.layers[1].id) + ".mask.png";
    QCOMPARE(projectError([&] { ProjectStore::save(coloured, path); }), std::optional(ProjectError::Kind::missingImage));
    coloured.masks.insert({coloured.manifest.layers[1].id, asset(halfRed(), "not gray")});
    QCOMPARE(projectError([&] { ProjectStore::save(coloured, path); }), std::optional(ProjectError::Kind::invalid));
    QCOMPARE(contents(path + "/manifest.json"), original);
    QCOMPARE(QDir(root.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden), beside);
    // No folder to write in: a file, a missing one.
    overwrite(root.filePath("not-a-directory"), "x");
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("could not create .*"));
    QVERIFY_EXCEPTION_THROWN(ProjectStore::save(snapshot, root.filePath("not-a-directory/CannotSave.comp")), std::runtime_error);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("could not create .*"));
    QVERIFY_EXCEPTION_THROWN(ProjectStore::save(snapshot, root.filePath("missing/CannotSave.comp")), std::runtime_error);
    QVERIFY(!QFileInfo::exists(root.filePath("missing")));
    QCOMPARE(contents(path + "/manifest.json"), original);
    QCOMPARE(int(ProjectStore::load(path).manifest.layers.size()), 2);
}

void ProjectTests::unsupportedCorruptAndUnsafeMetadataAreRejected()
{
    QTemporaryDir root;
    const ProjectSnapshot snapshot = twoLayers();
    const QString path = root.filePath("Invalid.comp"), metadata = path + "/manifest.json";
    ProjectStore::save(snapshot, path);
    const QByteArray good = contents(metadata);
    // Each edit below replaces this text; it must be there.
    QVERIFY(good.contains("\"version\": 9"));
    const auto loading = [&](const QByteArray &manifest) {
        overwrite(metadata, manifest);
        return projectError([&] { ProjectStore::load(path); });
    };
    // A version out of range is named whatever else fails.
    const QByteArray future = R"({"format": "com.compositor.project", "version": 42, "layers": "from the future"})";
    for (const qint64 strange : {qint64(42), qint64(10), qint64(0)}) {
        try {
            overwrite(metadata, QByteArray(future).replace("42", QByteArray::number(strange)));
            ProjectStore::load(path);
            QFAIL("a strange version opened");
        } catch (const ProjectError &error) {
            QCOMPARE(error.kind, ProjectError::Kind::version);
            QCOMPARE(error.version, std::optional(strange));
        }
    }
    // Versions in range go on to decode the rest.
    QCOMPARE(loading(QByteArray(future).replace("42", "7")), std::optional(ProjectError::Kind::invalid));
    QCOMPARE(loading(QByteArray(future).replace("42", "1")), std::optional(ProjectError::Kind::invalid));
    // Another format is invalid whatever version it claims.
    QCOMPARE(loading(QByteArray(future).replace("compositor", "example")), std::optional(ProjectError::Kind::invalid));
    QCOMPARE(loading(QByteArray(good).replace("\"version\": 9", "\"version\": 0")), std::optional(ProjectError::Kind::version));
    // Swift's Int holds 64 bits, each one: no double between.
    const qint64 most = std::numeric_limits<qint64>::max(), least = std::numeric_limits<qint64>::min();
    for (const qint64 wild : {qint64(2147483648), qint64(-2147483648), qint64(-1), qint64(9007199254740993), most, least}) {
        try {
            overwrite(metadata, QByteArray(good).replace("\"version\": 9", "\"version\": " + QByteArray::number(wild)));
            ProjectStore::load(path);
            QFAIL("a wild version opened");
        } catch (const ProjectError &error) {
            QCOMPARE(error.kind, ProjectError::Kind::version);
            QCOMPARE(error.version, std::optional(wild));
            QVERIFY(QString(error.what()).contains(QString::number(wild)));
        }
    }
    QCOMPARE(loading(QByteArray(good).replace("\"width\": 100", "\"width\": 3000000000")), std::optional(ProjectError::Kind::tooLarge));
    QCOMPARE(loading(QByteArray(good).replace("\"version\": 9", "\"version\": 9223372036854775808")), std::optional(ProjectError::Kind::invalid));
    // Declared: Qt rounds a token just past Int64's minimum.
    try {
        overwrite(metadata, QByteArray(good).replace("\"version\": 9", "\"version\": -9223372036854775809"));
        ProjectStore::load(path);
        QFAIL("a version below every integer opened");
    } catch (const ProjectError &error) {
        QCOMPARE(error.version, std::optional(least));
    }
    QCOMPARE(loading(QByteArray(good).replace("\"version\": 9", "\"version\": 1e19")), std::optional(ProjectError::Kind::invalid));
    QCOMPARE(loading(QByteArray(good).replace("\"version\": 9", "\"version\": 7.5")), std::optional(ProjectError::Kind::invalid));
    QCOMPARE(loading(QByteArray(good).replace("\"version\": 9", "\"version\": 1e300")), std::optional(ProjectError::Kind::invalid));
    QCOMPARE(loading(QByteArray(good).replace("\"version\": 9", "\"version\": \"7\"")), std::optional(ProjectError::Kind::invalid));
    QCOMPARE(loading(QByteArray(good).replace("com.compositor.project", "com.example.project")), std::optional(ProjectError::Kind::invalid));
    QCOMPARE(loading("not json"), std::optional(ProjectError::Kind::invalid));
    QCOMPARE(loading("[1, 2]"), std::optional(ProjectError::Kind::invalid));
    // A file name that climbs out of the project.
    const QByteArray name = (uuidString(snapshot.manifest.layers[0].id) + ".png").toUtf8();
    QCOMPARE(loading(QByteArray(good).replace(name, "../../outside.png")), std::optional(ProjectError::Kind::invalid));
    QCOMPARE(loading(good), std::nullopt);
    // Not a folder, no manifest inside, no such place.
    QCOMPARE(projectError([&] { ProjectStore::load(metadata); }), std::optional(ProjectError::Kind::invalid));
    QVERIFY(QFile::remove(metadata));
    QCOMPARE(projectError([&] { ProjectStore::load(path); }), std::optional(ProjectError::Kind::invalid));
    QCOMPARE(projectError([&] { ProjectStore::load(root.filePath("Nowhere.comp")); }), std::optional(ProjectError::Kind::invalid));
}

void ProjectTests::missingAndForeignImagesAreRejected()
{
    QTemporaryDir root;
    ProjectSnapshot snapshot = twoLayers();
    const QUuid id = snapshot.manifest.layers[0].id;
    snapshot.manifest.layers[0].maskFile = uuidString(id) + ".mask.png";
    snapshot.masks.insert({id, asset(gray(8, 8, 200), "mask")});
    const QString path = root.filePath("Images.comp");
    ProjectStore::save(snapshot, path);
    const QString image = path + "/images/" + uuidString(id) + ".png", mask = path + "/images/" + uuidString(id) + ".mask.png";
    const QByteArray goodImage = contents(image), goodMask = contents(mask);
    const auto written = [](const QImage &pixels, const char *format) {
        QByteArray data;
        QBuffer buffer(&data);
        QImageWriter writer(&buffer, format);
        writer.write(pixels);
        return data;
    };
    const auto loading = [&] { return projectError([&] { ProjectStore::load(path); }); };
    QVERIFY(QFile::remove(image));
    QCOMPARE(loading(), std::optional(ProjectError::Kind::missingImage));
    // A JPEG named PNG; sixteen-bit channels; bytes of no image.
    overwrite(image, written(halfRed().convertToFormat(QImage::Format_RGB888), "jpeg"));
    QCOMPARE(loading(), std::optional(ProjectError::Kind::missingImage));
    overwrite(image, written(halfRed().convertToFormat(QImage::Format_RGBA64), "png"));
    QCOMPARE(loading(), std::optional(ProjectError::Kind::missingImage));
    overwrite(image, "\x89PNG broken");
    QCOMPARE(loading(), std::optional(ProjectError::Kind::missingImage));
    // A PNG cut short: the header reads, the pixels fail.
    overwrite(image, goodImage.left(goodImage.size() * 6 / 10));
    QCOMPARE(loading(), std::optional(ProjectError::Kind::missingImage));
    // Any eight-bit PNG serves as pixels; masks must be gray.
    overwrite(image, written(halfRed().convertToFormat(QImage::Format_RGB888), "png"));
    QCOMPARE(loading(), std::nullopt);
    QCOMPARE(ProjectStore::load(path).images.at(id).image().format(), QImage::Format_RGBA8888_Premultiplied);
    overwrite(image, goodImage);
    overwrite(mask, written(halfRed(), "png"));
    QCOMPARE(loading(), std::optional(ProjectError::Kind::invalid));
    QVERIFY(QFile::remove(mask));
    QCOMPARE(loading(), std::optional(ProjectError::Kind::missingImage));
    overwrite(mask, goodMask);
    QCOMPARE(loading(), std::nullopt);
}

void ProjectTests::filesMustBePlainFilesInsideTheProject()
{
    QTemporaryDir root;
    const ProjectSnapshot snapshot = twoLayers();
    const QString path = root.filePath("Links.comp");
    ProjectStore::save(snapshot, path);
    const QString image = path + "/images/" + uuidString(snapshot.manifest.layers[0].id) + ".png", metadata = path + "/manifest.json";
    const auto loading = [&] { return projectError([&] { ProjectStore::load(path); }); };
    // Links leading out, within, and nowhere; then a folder.
    QVERIFY(QFile::rename(image, root.filePath("outside.png")));
    QVERIFY(QFile::link(root.filePath("outside.png"), image));
    QCOMPARE(loading(), std::optional(ProjectError::Kind::invalid));
    QVERIFY(QFile::remove(image));
    QVERIFY(QFile::copy(root.filePath("outside.png"), path + "/images/real.png"));
    QVERIFY(QFile::link(path + "/images/real.png", image));
    QCOMPARE(loading(), std::optional(ProjectError::Kind::tooLarge));
    QVERIFY(QFile::remove(image));
    QVERIFY(QFile::link(root.filePath("nothing.png"), image));
    QCOMPARE(loading(), std::optional(ProjectError::Kind::invalid));
    QVERIFY(QFile::remove(image));
    QVERIFY(QDir().mkdir(image));
    QCOMPARE(loading(), std::optional(ProjectError::Kind::tooLarge));
    QVERIFY(QDir().rmdir(image));
    QVERIFY(QFile::copy(root.filePath("outside.png"), image));
    QCOMPARE(loading(), std::nullopt);
    // The manifest is held to the same rules.
    QVERIFY(QFile::rename(metadata, root.filePath("manifest.json")));
    QVERIFY(QFile::link(root.filePath("manifest.json"), metadata));
    QCOMPARE(loading(), std::optional(ProjectError::Kind::invalid));
    // A manifest that may not be read.
    QVERIFY(QFile::remove(metadata));
    QVERIFY(QFile::rename(root.filePath("manifest.json"), metadata));
    QVERIFY(QFile::setPermissions(metadata, QFileDevice::Permissions()));
    QCOMPARE(loading(), std::optional(ProjectError::Kind::invalid));
    QVERIFY(QFile::setPermissions(metadata, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    QVERIFY(QFile::rename(metadata, root.filePath("manifest.json")));
    QVERIFY(QFile::link(root.filePath("manifest.json"), metadata));
    // A project reached through a link is still itself.
    QVERIFY(QFile::remove(metadata));
    QVERIFY(QFile::rename(root.filePath("manifest.json"), metadata));
    QVERIFY(QFile::link(path, root.filePath("Shortcut.comp")));
    QCOMPARE(projectError([&] { ProjectStore::load(root.filePath("Shortcut.comp")); }), std::nullopt);
}

void ProjectTests::sizeLimitsHoldOnSaveAndLoad()
{
    QTemporaryDir root;
    const ProjectSnapshot snapshot = twoLayers();
    const QString path = root.filePath("Limits.comp");
    ProjectStore::save(snapshot, path);
    const QUuid id = snapshot.manifest.layers[0].id;
    const QString image = path + "/images/" + uuidString(id) + ".png", metadata = path + "/manifest.json";
    const QByteArray goodImage = contents(image), goodManifest = contents(metadata);
    const auto loading = [&] { return projectError([&] { ProjectStore::load(path); }); };
    // A manifest over 4 MiB, on disk and in memory.
    overwrite(metadata, goodManifest + QByteArray(4 * 1024 * 1024, ' '));
    QCOMPARE(loading(), std::optional(ProjectError::Kind::tooLarge));
    overwrite(metadata, goodManifest + QByteArray(4 * 1024 * 1024 - goodManifest.size(), ' '));
    QCOMPARE(loading(), std::nullopt);
    overwrite(metadata, goodManifest);
    ProjectSnapshot wordy = snapshot;
    for (int index = 0; index < 270; ++index)
        wordy.manifest.layers.push_back({.id = QUuid::createUuid(), .name = QString(16'000, 'n'), .isVisible = true,
                                         .transform = {.origin = {0, 0}, .size = {1, 1}}, .imageFile = std::nullopt});
    QVERIFY(wordy.manifest.encoded().size() > 4 * 1024 * 1024);
    QCOMPARE(projectError([&] { ProjectStore::save(wordy, root.filePath("Wordy.comp")); }), std::optional(ProjectError::Kind::tooLarge));
    // An encoded image over 512 MiB, as a sparse file.
    {
        QFile sparse(image);
        QVERIFY(sparse.open(QIODevice::ReadWrite));
        QVERIFY(sparse.resize(512ll * 1024 * 1024 + 1));
    }
    QCOMPARE(loading(), std::optional(ProjectError::Kind::tooLarge));
    // At 512 MiB the padded PNG still opens.
    {
        QFile sparse(image);
        QVERIFY(sparse.open(QIODevice::ReadWrite));
        QVERIFY(sparse.resize(512ll * 1024 * 1024));
    }
    QCOMPARE(loading(), std::nullopt);
    overwrite(image, goodImage);
    // A side over 30,000 pixels, read from the header alone.
    const auto png = [](const QImage &pixels) {
        QByteArray data;
        QBuffer buffer(&data);
        QImageWriter(&buffer, "png").write(pixels);
        return data;
    };
    overwrite(image, png(gray(30'001, 1, 9)));
    QCOMPARE(loading(), std::optional(ProjectError::Kind::tooLarge));
    overwrite(image, png(gray(1, 30'001, 9)));
    QCOMPARE(loading(), std::optional(ProjectError::Kind::tooLarge));
    overwrite(image, png(gray(30'000, 1, 9)));
    QCOMPARE(loading(), std::nullopt);
    overwrite(image, png(gray(1, 30'000, 9)));
    QCOMPARE(loading(), std::nullopt);
    overwrite(image, goodImage);
    ProjectSnapshot long1 = snapshot;
    long1.images.insert_or_assign(id, asset(gray(30'001, 1, 9).convertToFormat(QImage::Format_RGBA8888_Premultiplied), "long"));
    QCOMPARE(projectError([&] { ProjectStore::save(long1, root.filePath("Long.comp")); }), std::optional(ProjectError::Kind::tooLarge));
    // Masks share 100 million pixels, apart from the images'.
    ProjectSnapshot heavy = snapshot;
    const QUuid second = heavy.manifest.layers[1].id;
    heavy.manifest.layers[0].maskFile = uuidString(id) + ".mask.png";
    heavy.manifest.layers[1].maskFile = uuidString(second) + ".mask.png";
    heavy.masks.insert({id, asset(gray(10'000, 10'000, 255), "whole budget")});
    heavy.masks.insert({second, asset(gray(1, 1, 255), "one too many")});
    QCOMPARE(projectError([&] { ProjectStore::save(heavy, root.filePath("Heavy.comp")); }), std::optional(ProjectError::Kind::tooLarge));
    // 99,990,000 and 10,000 pixels fill the budget exactly.
    heavy.masks.insert_or_assign(id, asset(gray(10'000, 9'999, 255), "nearly all"));
    heavy.masks.insert_or_assign(second, asset(gray(100, 100, 255), "the rest"));
    QCOMPARE(projectError([&] { ProjectStore::save(heavy, root.filePath("Heavy.comp")); }), std::nullopt);
    QCOMPARE(projectError([&] { ProjectStore::load(root.filePath("Heavy.comp")); }), std::nullopt);
    // On load the same sum comes from the files' headers.
    const QString firstMask = root.filePath("Heavy.comp/images/") + uuidString(id) + ".mask.png";
    overwrite(firstMask, png(gray(10'000, 10'000, 255)));
    QCOMPARE(projectError([&] { ProjectStore::load(root.filePath("Heavy.comp")); }), std::optional(ProjectError::Kind::tooLarge));
}

void ProjectTests::numbersThatAreNoNumbersAreRefusedOnSave()
{
    QTemporaryDir root;
    const auto saving = [&](const ProjectSnapshot &snapshot) { return projectError([&] { ProjectStore::save(snapshot, root.filePath("Numbers.comp")); }); };
    for (const double value : {std::nan(""), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()}) {
        ProjectSnapshot resolution = twoLayers(), opacity = twoLayers(), origin = twoLayers();
        resolution.manifest.resolution = value;
        opacity.manifest.layers[1].opacity = value;
        origin.manifest.layers[1].transform.origin.setX(value);
        QCOMPARE(saving(resolution), std::optional(ProjectError::Kind::invalid));
        QCOMPARE(saving(opacity), std::optional(ProjectError::Kind::invalid));
        QCOMPARE(saving(origin), std::optional(ProjectError::Kind::invalid));
    }
    QVERIFY(!QFileInfo::exists(root.filePath("Numbers.comp")));
}

void ProjectTests::savesAndLoadsAreLogged()
{
    QTemporaryDir root;
    const QString path = root.filePath("Logged.comp");
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression("saved \".*Logged.comp\" with 2 layers and 1 images"));
    ProjectStore::save(twoLayers(), path);
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression("loaded \".*Logged.comp\" with 2 layers and 1 images"));
    ProjectStore::load(path);
}

QTEST_MAIN(ProjectTests)
#include "ProjectTests.moc"
