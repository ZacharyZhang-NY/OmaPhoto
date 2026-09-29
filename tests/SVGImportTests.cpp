#include "BudgetFixtures.h"
#include "Document/EditorSession.h"
#include "IO/ImageImporter.h"
#include "IO/PSD/PSDReader.h"
#include <QColorSpace>
#include <QTemporaryDir>
#include <QtTest>
#include <zlib.h>

// Swift 1.2.10 (0534e00): SVG drawn once into an image layer.
namespace {
// A red rectangle filling a document of the given size.
QByteArray svg(const QByteArray &width, const QByteArray &height)
{
    return "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + width + "\" height=\"" + height + "\"><rect width=\"100%\" height=\"100%\" fill=\"red\"/></svg>";
}

QString written(const QTemporaryDir &folder, const QString &name, const QByteArray &bytes)
{
    QFile file(folder.filePath(name));
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        throw std::runtime_error("cannot write a fixture");
    return file.fileName();
}

// The same bytes in gzip, as `.svgz` holds them.
QByteArray gzipped(const QByteArray &bytes)
{
    z_stream stream{};
    if (deflateInit2(&stream, 9, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        throw std::runtime_error("no zlib");
    QByteArray out(int(deflateBound(&stream, uLong(bytes.size()))), '\0');
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(bytes.constData()));
    stream.avail_in = uInt(bytes.size());
    stream.next_out = reinterpret_cast<Bytef *>(out.data());
    stream.avail_out = uInt(out.size());
    deflate(&stream, Z_FINISH);
    out.resize(qsizetype(stream.total_out));
    deflateEnd(&stream);
    return out;
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

bool imported(EditorSession &session, const QList<QUrl> &urls)
{
    bool done = false;
    session.importImages(urls, std::nullopt, [&] { done = true; });
    return QTest::qWaitFor([&] { return done && !session.isImporting(); }, 10'000);
}
}

class SVGImportTests : public QObject {
    Q_OBJECT
private slots:
    void anSVGFitsTheCanvasOrKeepsItsSize();
    void brokenOrOversizedSVGsRefuse();
    void theSessionImportsSVGBySuffix();
};

void SVGImportTests::anSVGFitsTheCanvasOrKeepsItsSize()
{
    const QTemporaryDir folder;
    const QString path = written(folder, QStringLiteral("icon.svg"), svg("40", "20"));
    const ImportedImage alone = ImageImporter::decodeSVG(path, std::nullopt);
    QCOMPARE(alone.size(), QSize(40, 20));
    QCOMPARE(alone.name, QString("icon"));
    QCOMPARE(alone.image().pixelColor(20, 10), QColor(255, 0, 0));
    QCOMPARE(alone.image().colorSpace(), QColorSpace(QColorSpace::SRgb));
    QVERIFY(!alone.thumbnail.isNull());
    // Fitted, the smaller side's ratio rules and sides round.
    QCOMPARE(ImageImporter::decodeSVG(path, QSizeF(400, 300)).size(), QSize(400, 200));
    QCOMPARE(ImageImporter::decodeSVG(path, QSizeF(10, 10)).size(), QSize(10, 5));
    const QString thin = written(folder, QStringLiteral("thin.svg"), svg("3", "1"));
    QCOMPARE(ImageImporter::decodeSVG(thin, QSizeF(10, 10)).size(), QSize(10, 3));
    QCOMPARE(ImageImporter::decodeSVG(thin, QSizeF(1, 1)).size(), QSize(1, 1));
    QCOMPARE(ImageImporter::decodeSVG(thin, QSizeF(5, 5)).size(), QSize(5, 2));
    QCOMPARE(ImageImporter::decodeSVG(path, QSizeF(400, 100)).size(), QSize(200, 100));
    const QString narrow = written(folder, QStringLiteral("narrow.svg"), svg("1", "3"));
    QCOMPARE(ImageImporter::decodeSVG(narrow, QSizeF(1, 1)).size(), QSize(1, 1));
    QCOMPARE(ImageImporter::decodeSVG(narrow, QSizeF(5, 5)).size(), QSize(2, 5));
    // Red top left, blue bottom right, clear between: upright.
    const QString corners = written(folder, QStringLiteral("corners.svg"),
                                    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"20\"><rect width=\"5\" height=\"10\" fill=\"red\"/>"
                                    "<rect x=\"5\" y=\"10\" width=\"5\" height=\"10\" fill=\"blue\"/></svg>");
    const QImage fitted = ImageImporter::decodeSVG(corners, QSizeF(100, 100)).image();
    QCOMPARE(fitted.size(), QSize(50, 100));
    QCOMPARE(fitted.pixelColor(12, 25), QColor(255, 0, 0));
    QCOMPARE(fitted.pixelColor(37, 75), QColor(0, 0, 255));
    QCOMPARE(fitted.pixelColor(37, 25).alpha(), 0);
    QCOMPARE(fitted.pixelColor(12, 75).alpha(), 0);
    // A large drawing's thumbnail is 96 on its long side.
    const QString large = written(folder, QStringLiteral("large.svg"), svg("200", "400"));
    QCOMPARE(ImageImporter::decodeSVG(large, std::nullopt).thumbnail.size(), QSize(48, 96));
    // Compressed and upper-case names read as SVG.
    const QString compressed = written(folder, QStringLiteral("icon.SVGZ"), gzipped(svg("40", "20")));
    QVERIFY(ImageImporter::isSVG(compressed));
    QCOMPARE(ImageImporter::decodeSVG(compressed, std::nullopt).size(), QSize(40, 20));
    QVERIFY(ImageImporter::isSVG(QStringLiteral("/a/b.Svg")));
    QVERIFY(!ImageImporter::isSVG(QStringLiteral("/a/b.png")));
    QVERIFY(!ImageImporter::isSVG(QStringLiteral("/a/svg")));
}

void SVGImportTests::brokenOrOversizedSVGsRefuse()
{
    const QTemporaryDir folder;
    using Kind = ImageImportError::Kind;
    const QString broken = written(folder, QStringLiteral("broken.svg"), "<svg");
    QCOMPARE(refusal([&] { ImageImporter::decodeSVG(broken, std::nullopt); }), std::optional(Kind::unreadable));
    // An empty drawing with a zero side has no size.
    const QString empty = written(folder, QStringLiteral("empty.svg"), "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"0\" height=\"20\"/>");
    QCOMPARE(refusal([&] { ImageImporter::decodeSVG(empty, std::nullopt); }), std::optional(Kind::unreadable));
    for (const char *box : {"0 0 0 10", "0 0 10 0"}) {
        const QString flat = written(folder, QStringLiteral("flat.svg"), QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"") + box + "\"/>");
        QCOMPARE(refusal([&] { ImageImporter::decodeSVG(flat, std::nullopt); }), std::optional(Kind::unreadable));
    }
    const QString wide = written(folder, QStringLiteral("wide.svg"), svg("30001", "1"));
    QCOMPARE(refusal([&] { ImageImporter::decodeSVG(wide, std::nullopt); }), std::optional(Kind::tooLarge));
    QCOMPARE(ImageImporter::decodeSVG(wide, QSizeF(30'000, 30'000)).size(), QSize(30'000, 1));
    const QString tall = written(folder, QStringLiteral("tall.svg"), svg("1", "30001"));
    QCOMPARE(refusal([&] { ImageImporter::decodeSVG(tall, std::nullopt); }), std::optional(Kind::tooLarge));
    QCOMPARE(ImageImporter::decodeSVG(tall, QSizeF(30'000, 30'000)).size(), QSize(1, 30'000));
    // The budget counts the pixels drawn.
    const QString icon = written(folder, QStringLiteral("icon.svg"), svg("40", "20"));
    QCOMPARE(ImageImporter::decodeSVG(icon, std::nullopt, 800).size(), QSize(40, 20));
    QCOMPARE(refusal([&] { ImageImporter::decodeSVG(icon, std::nullopt, 799); }), std::optional(Kind::tooLarge));
    // Fitted, the budget counts the drawn size, not the declared.
    QCOMPARE(ImageImporter::decodeSVG(icon, QSizeF(400, 300), 80'000).size(), QSize(400, 200));
    QCOMPARE(refusal([&] { ImageImporter::decodeSVG(icon, QSizeF(400, 300), 79'999); }), std::optional(Kind::tooLarge));
    QCOMPARE(ImageImporter::decodeSVG(icon, QSizeF(10, 10), 50).size(), QSize(10, 5));
}

void SVGImportTests::theSessionImportsSVGBySuffix()
{
    const QTemporaryDir folder;
    const QString icon = written(folder, QStringLiteral("icon.svg"), svg("40", "20"));
    // Without a document, its declared size makes the canvas.
    EditorSession session;
    QVERIFY(imported(session, {QUrl::fromLocalFile(icon)}));
    QCOMPARE(session.document().value().size(), QSizeF(40, 20));
    QCOMPARE(session.activeLayer().value().name, QString("icon"));
    // Over a canvas, it is drawn to fit it.
    EditorSession canvas;
    canvas.createDocument(400, 300, true);
    QVERIFY(imported(canvas, {QUrl::fromLocalFile(icon)}));
    QCOMPARE(canvas.activeLayer().value().asset.value().size(), QSize(400, 200));
    QCOMPARE(canvas.activeLayer().value().transform.origin, QPointF(0, 50));
    // The suffix rules before Photoshop's bytes.
    const QString disguised = written(folder, QStringLiteral("disguised.svg"), QByteArray("8BPS\0\1", 6) + QByteArray(40, '\0'));
    QVERIFY(PSDReader::matches(disguised));
    QVERIFY(imported(canvas, {QUrl::fromLocalFile(disguised)}));
    QCOMPARE(canvas.importError().value(), QString("disguised.svg: The image could not be read. It may be damaged or unavailable."));
    // The drawing counts against what the canvas already holds.
    EditorSession full;
    full.createDocument(40, 20, true);
    for (const ImportedImage &layer : claiming(DocumentLimits::documentPixelBudget() - 800))
        full.insert(layer);
    QVERIFY(imported(full, {QUrl::fromLocalFile(icon)}));
    QCOMPARE(full.importError(), std::nullopt);
    QCOMPARE(full.activeLayer().value().asset.value().size(), QSize(40, 20));
    QVERIFY(imported(full, {QUrl::fromLocalFile(icon)}));
    QVERIFY(full.importError().value().startsWith("icon.svg: This import exceeds"));
}

QTEST_MAIN(SVGImportTests)
#include "SVGImportTests.moc"
