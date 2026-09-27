#include "AddressSpaceLimit.h"
#include "IO/ImageImporter.h"
#include <QBuffer>
#include <QColorSpace>
#include <QImageWriter>
#include <QTemporaryDir>
#include <QLibraryInfo>
#include <QVersionNumber>
#include <QtTest>

namespace {
const QColor red(255, 0, 0), clear(0, 0, 0, 0), warm(200, 100, 50);

// 64x32: `colour` over the left `columns` and top `rows`.
QImage block(QImage::Format format, const QColor &colour, const QColor &rest, int columns = 32, int rows = 32)
{
    QImage image(64, 32, format);
    image.fill(rest);
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < columns; ++x)
            image.setPixelColor(x, y, colour);
    }
    return image;
}

QByteArray encoded(const QImage &image, const char *format, QImageIOHandler::Transformations turned = QImageIOHandler::TransformationNone)
{
    QByteArray data;
    QBuffer buffer(&data);
    QImageWriter writer(&buffer, format);
    writer.setQuality(100);
    writer.setTransformation(turned);
    if (!writer.write(image))
        throw std::runtime_error("cannot encode the fixture");
    return data;
}

QString written(const QTemporaryDir &folder, const QString &name, const QByteArray &data)
{
    QFile file(folder.filePath(name));
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
        throw std::runtime_error("cannot write the fixture");
    return file.fileName();
}

// An EXIF orientation tag after the JPEG's first marker.
QByteArray withOrientation(QByteArray jpeg, int orientation)
{
    QByteArray exif("\xFF\xE1\x00\x22" "Exif\0\0" "II\x2A\x00\x08\x00\x00\x00" "\x01\x00" "\x12\x01\x03\x00\x01\x00\x00\x00", 28);
    exif += char(orientation);
    exif += QByteArray("\x00\x00\x00" "\x00\x00\x00\x00", 7);
    return jpeg.insert(2, exif);
}

// The HEIC with its nclx box naming other colours.
QByteArray withNclx(QByteArray heic, int primaries, int transfer)
{
    const qsizetype box = heic.indexOf("colrnclx");
    if (box < 0)
        throw std::runtime_error("the fixture holds no nclx box");
    heic[box + 9] = char(primaries);
    heic[box + 11] = char(transfer);
    return heic;
}

std::optional<ImageImportError::Kind> importError(const QString &path, qint64 remainingPixels = 100'000'000)
{
    try {
        ImageImporter::decode(path, remainingPixels);
    } catch (const ImageImportError &error) {
        return error.kind;
    }
    return std::nullopt;
}

// Whether the pixel is nearer the warm block than white.
bool isBlock(const QImage &image, int x, int y)
{
    const QColor pixel = image.pixelColor(x, y);
    return pixel.alpha() == 255 && pixel.red() > 150 && pixel.green() < 140 && pixel.blue() < 100;
}

const QRegularExpression refusal("^cannot import .*");
}

class ImageImportTests : public QObject {
    Q_OBJECT
private slots:
    void init();
    void supportedFormats_data();
    void supportedFormats();
    void everyOrientationTurnsUpright_data();
    void everyOrientationTurnsUpright();
    void orientationAndColorConversion();
    void heicAlphaIsMultipliedOnce_data();
    void heicAlphaIsMultipliedOnce();
    void heicTurnsOnceAndConvertsItsProfile();
    void heicColoursFollowTheNclxBox_data();
    void heicColoursFollowTheNclxBox();
    void pngPreservesTransparency();
    void translucentColoursConvertUnpremultiplied();
    void deepAndGrayImagesBecomeEightBitColour();
    void limitsAndInvalidFiles();
    void theTypeComesFromTheBytes();
    void namesDropOneExtension();
    void thumbnailsRoundOutward_data();
    void thumbnailsRoundOutward();
    void thumbnailsAreSmoothed();
    void errorsCarrySwiftsDescriptions();
    void aDecodeWithoutMemoryIsUnreadable();
};

void ImageImportTests::init()
{
    // A warning no test expects is a failure.
    QTest::failOnWarning(QRegularExpression(".*"));
}

void ImageImportTests::supportedFormats_data()
{
    QTest::addColumn<QString>("name");
    QTest::newRow("png") << "fixture.png";
    QTest::newRow("jpeg") << "fixture.jpeg";
    QTest::newRow("tiff") << "fixture.tiff";
    QTest::newRow("heic") << "red.heic";
}

void ImageImportTests::supportedFormats()
{
    QFETCH(QString, name);
    QTemporaryDir folder;
    const QByteArray format = QFileInfo(name).suffix().toLatin1();
    const QString path = format == "heic" ? QFINDTESTDATA("fixtures/red.heic")
                                          : written(folder, name, encoded(block(QImage::Format_RGBA8888, red, clear), format.constData()));
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression("^imported .* 64 x 32$"));
    const ImportedImage result = ImageImporter::decode(path);
    QCOMPARE(result.size(), QSize(64, 32));
    QCOMPARE(result.image().format(), QImage::Format_RGBA8888_Premultiplied);
    QCOMPARE(result.image().colorSpace(), QColorSpace(QColorSpace::SRgb));
    QCOMPARE(result.thumbnail.size(), QSize(64, 32));
    QCOMPARE(result.name, QFileInfo(name).completeBaseName());
    // Red left; JPEG has no alpha: black right.
    const QColor left = result.image().pixelColor(8, 16), right = result.image().pixelColor(56, 8);
    QVERIFY2(left.red() >= 250 && left.green() <= 5 && left.blue() <= 5 && left.alpha() == 255, qPrintable(left.name(QColor::HexArgb)));
    QCOMPARE(right.alpha(), format == "jpeg" ? 255 : 0);
    QVERIFY2(right.red() <= 5, qPrintable(right.name(QColor::HexArgb)));
}

void ImageImportTests::heicAlphaIsMultipliedOnce_data()
{
    QTest::addColumn<QString>("fixture");
    QTest::newRow("straight alpha in the file") << "fixtures/red.heic";
    QTest::newRow("multiplied alpha in the file") << "fixtures/premultiplied.heic";
}

void ImageImportTests::heicAlphaIsMultipliedOnce()
{
    QFETCH(QString, fixture);
    const QImage image = ImageImporter::decode(QFINDTESTDATA(fixture)).image();
    // Blue (0, 0, 255) under alpha 128, bottom right.
    const uchar *bytes = image.constScanLine(24) + 56 * 4;
    QVERIFY2(bytes[0] <= 3 && bytes[1] <= 3 && qAbs(bytes[2] - 128) <= 3 && qAbs(bytes[3] - 128) <= 3,
             qPrintable(QStringLiteral("%1 %2 %3 %4").arg(bytes[0]).arg(bytes[1]).arg(bytes[2]).arg(bytes[3])));
    QCOMPARE(image.pixelColor(56, 8).alpha(), 0);
    QVERIFY(image.pixelColor(8, 16).red() >= 250 && image.pixelColor(8, 16).alpha() == 255);
}

void ImageImportTests::everyOrientationTurnsUpright_data()
{
    QTest::addColumn<int>("orientation");
    QTest::addColumn<QSize>("size");
    // The quarter of the result that holds the block.
    QTest::addColumn<QPoint>("quarter");
    QTest::newRow("1 upright") << 1 << QSize(64, 32) << QPoint(0, 0);
    QTest::newRow("2 mirrored") << 2 << QSize(64, 32) << QPoint(1, 0);
    QTest::newRow("3 half turn") << 3 << QSize(64, 32) << QPoint(1, 1);
    QTest::newRow("4 flipped") << 4 << QSize(64, 32) << QPoint(0, 1);
    QTest::newRow("5 transposed") << 5 << QSize(32, 64) << QPoint(0, 0);
    QTest::newRow("6 quarter turn") << 6 << QSize(32, 64) << QPoint(1, 0);
    QTest::newRow("7 transversed") << 7 << QSize(32, 64) << QPoint(1, 1);
    QTest::newRow("8 three quarters") << 8 << QSize(32, 64) << QPoint(0, 1);
}

void ImageImportTests::everyOrientationTurnsUpright()
{
    QFETCH(int, orientation);
    QFETCH(QSize, size);
    QFETCH(QPoint, quarter);
    QTemporaryDir folder;
    // The block fills the top left quarter as stored.
    const QImage stored = block(QImage::Format_RGB888, warm, Qt::white, 32, 16);
    // EXIF 1 to 8 as Qt's mirror, flip, turn bits.
    const int flags[] = {0, 1, 3, 2, 6, 4, 5, 7};
    const QStringList paths{written(folder, "turned.jpg", withOrientation(encoded(stored, "jpeg"), orientation)),
                            written(folder, "turned.tiff", encoded(stored, "tiff", QImageIOHandler::Transformations(flags[orientation - 1])))};
    for (const QString &path : paths) {
        const QImage image = ImageImporter::decode(path).image();
        QCOMPARE(image.size(), size);
        for (int row = 0; row < 2; ++row) {
            for (int column = 0; column < 2; ++column) {
                const QPoint centre(size.width() / 4 + column * size.width() / 2, size.height() / 4 + row * size.height() / 2);
                QVERIFY2(isBlock(image, centre.x(), centre.y()) == (QPoint(column, row) == quarter),
                         qPrintable(QStringLiteral("%1 at quarter %2, %3").arg(path).arg(column).arg(row)));
            }
        }
    }
}

void ImageImportTests::orientationAndColorConversion()
{
    QTemporaryDir folder;
    QImage wide = block(QImage::Format_RGBA8888, warm, clear);
    wide.setColorSpace(QColorSpace::DisplayP3);
    const ImportedImage result = ImageImporter::decode(written(folder, "p3.tiff", encoded(wide, "tiff", QImageIOHandler::TransformationRotate90)));
    QCOMPARE(result.size(), QSize(32, 64));
    QCOMPARE(result.image().colorSpace(), QColorSpace(QColorSpace::SRgb));
    // Display P3 (200, 100, 50) is sRGB (215, 93, 31).
    QCOMPARE(result.image().pixelColor(16, 8), QColor(215, 93, 31));
    QCOMPARE(result.image().pixelColor(16, 56), clear);
    // Untagged pixels are sRGB already.
    const ImportedImage plain = ImageImporter::decode(written(folder, "plain.tiff", encoded(block(QImage::Format_RGBA8888, warm, clear), "tiff")));
    QCOMPARE(plain.image().pixelColor(8, 16), warm);
}

void ImageImportTests::heicTurnsOnceAndConvertsItsProfile()
{
    // The file holds a turn box and EXIF orientation 6.
    const ImportedImage result = ImageImporter::decode(QFINDTESTDATA("fixtures/turned-p3.heic"));
    QCOMPARE(result.size(), QSize(32, 64));
    QCOMPARE(result.name, QString("turned-p3"));
    // Stored left turns to the top; white and gray follow.
    const QColor top = result.image().pixelColor(16, 8), white = result.image().pixelColor(24, 56), dark = result.image().pixelColor(8, 56);
    QVERIFY2(qAbs(top.red() - 215) <= 4 && qAbs(top.green() - 93) <= 4 && qAbs(top.blue() - 31) <= 4, qPrintable(top.name()));
    QVERIFY2(white.red() >= 250 && white.green() >= 250 && white.blue() >= 250, qPrintable(white.name()));
    QVERIFY2(qAbs(dark.red() - 16) <= 4 && qAbs(dark.green() - 16) <= 4 && qAbs(dark.blue() - 16) <= 4, qPrintable(dark.name()));
    // Its budget and its damage are judged like any other's.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("^cannot import .*turned-p3.heic: 32 x 64 pixels$"));
    QCOMPARE(importError(QFINDTESTDATA("fixtures/turned-p3.heic"), 2047), std::optional(ImageImportError::Kind::tooLarge));
    QCOMPARE(importError(QFINDTESTDATA("fixtures/turned-p3.heic"), 2048), std::nullopt);
    QTemporaryDir folder;
    QFile whole(QFINDTESTDATA("fixtures/red.heic"));
    QVERIFY(whole.open(QIODevice::ReadOnly));
    QByteArray bytes = whole.readAll();
    // Cut short, the file cannot be read; spoiled, not decoded.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("^cannot import .*cut.heic: Invalid input: No 'meta' box(: .*)?$"));
    QCOMPARE(importError(written(folder, "cut.heic", bytes.left(700))), std::optional(ImageImportError::Kind::unreadable));
    for (qsizetype index = bytes.size() - 300; index < bytes.size(); ++index)
        bytes[index] = char(index * 37);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("^cannot import .*spoiled.heic: Decoder plugin generated an error: .*"));
    QCOMPARE(importError(written(folder, "spoiled.heic", bytes)), std::optional(ImageImportError::Kind::unreadable));
}

void ImageImportTests::heicColoursFollowTheNclxBox_data()
{
    QTest::addColumn<int>("primaries");
    QTest::addColumn<int>("transfer");
    // What (200, 100, 50) and gray 16 become.
    QTest::addColumn<QColor>("expected");
    QTest::addColumn<int>("gray");
    QTest::addColumn<QString>("reason");
    QTest::newRow("Display P3, sRGB curve") << 12 << 13 << QColor(215, 93, 31) << 16 << "";
    QTest::newRow("Display P3, no curve named") << 12 << 2 << QColor(215, 93, 31) << 16 << "";
    QTest::newRow("sRGB named") << 1 << 13 << QColor(200, 100, 50) << 16 << "";
    QTest::newRow("nothing named") << 2 << 2 << QColor(200, 100, 50) << 16 << "";
    // The video curve's straight toe lifts gray 16 to 31.
    QTest::newRow("BT.709 video") << 1 << 1 << QColor(206, 114, 66) << 31 << "";
    QTest::newRow("video curve, no primaries named") << 2 << 6 << QColor(206, 114, 66) << 31 << "";
    QTest::newRow("BT.2020, 10 bits") << 9 << 14 << QColor(246, 94, 50) << 31 << "";
    QTest::newRow("BT.2020, 12 bits") << 9 << 15 << QColor(246, 94, 50) << 31 << "";
    QTest::newRow("PQ") << 9 << 16 << QColor() << 0 << "transfer characteristics 16 have no conversion to sRGB here";
    QTest::newRow("HLG") << 9 << 18 << QColor() << 0 << "transfer characteristics 18 have no conversion to sRGB here";
    QTest::newRow("xvYCC") << 1 << 11 << QColor(206, 114, 66) << 31 << "";
    QTest::newRow("BT.1361") << 1 << 12 << QColor(206, 114, 66) << 31 << "";
    QTest::newRow("linear light") << 1 << 8 << QColor(229, 168, 122) << 71 << "";
    QTest::newRow("gamma 2.2") << 1 << 4 << QColor(201, 100, 46) << 7 << "";
    QTest::newRow("gamma 2.8") << 1 << 5 << QColor(189, 76, 26) << 1 << "";
    QTest::newRow("SMPTE 240M") << 1 << 7 << QColor() << 0 << "transfer characteristics 7 have no conversion to sRGB here";
    QTest::newRow("a logarithmic curve") << 1 << 9 << QColor() << 0 << "transfer characteristics 9 have no conversion to sRGB here";
    QTest::newRow("XYZ primaries, which Qt cannot hold") << 10 << 13 << QColor() << 0 << "colour primaries 10 have no conversion to sRGB here";
    QTest::newRow("primaries libheif refuses") << 3 << 13 << QColor() << 0 << "its nclx colour box: ";
    QTest::newRow("a curve libheif refuses") << 1 << 3 << QColor() << 0 << "its nclx colour box: ";
}

void ImageImportTests::heicColoursFollowTheNclxBox()
{
    QFETCH(int, primaries);
    QFETCH(int, transfer);
    QFETCH(QColor, expected);
    QFETCH(int, gray);
    QFETCH(QString, reason);
    QFile fixture(QFINDTESTDATA("fixtures/p3-nclx.heic"));
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    QTemporaryDir folder;
    const QString path = written(folder, "named.heic", withNclx(fixture.readAll(), primaries, transfer));
    if (!expected.isValid()) {
        // Qt says so itself before the importer refuses.
        if (primaries == 10)
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression("^QColorSpace attempted constructed from invalid primaries: .*"));
        // libheif words its own refusals; ours are whole.
        const QString tail = reason.endsWith(": ") ? ".+$" : "$";
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("^cannot import .*named.heic: " + QRegularExpression::escape(reason) + tail));
        QCOMPARE(importError(path), std::optional(ImageImportError::Kind::unreadable));
        return;
    }
    const QImage image = ImageImporter::decode(path).image();
    QCOMPARE(image.colorSpace(), QColorSpace(QColorSpace::SRgb));
    // The fixture is lossless: one level is Qt's rounding.
    const QColor left = image.pixelColor(8, 16), dark = image.pixelColor(56, 24);
    QVERIFY2(qAbs(left.red() - expected.red()) <= 1 && qAbs(left.green() - expected.green()) <= 1 && qAbs(left.blue() - expected.blue()) <= 1,
             qPrintable(left.name()));
    QVERIFY2(qAbs(dark.red() - gray) <= 1 && qAbs(dark.green() - gray) <= 1 && qAbs(dark.blue() - gray) <= 1, qPrintable(dark.name()));
    QCOMPARE(image.pixelColor(56, 8), QColor(255, 255, 255));
}

void ImageImportTests::pngPreservesTransparency()
{
    QTemporaryDir folder;
    const QImage image = ImageImporter::decode(written(folder, "clear.png", encoded(block(QImage::Format_RGBA8888, red, clear), "png"))).image();
    const uchar *bytes = image.constBits();
    QCOMPARE(bytes[3], uchar(255));
    QVERIFY(bytes[0] >= 250);
    QCOMPARE(bytes[63 * 4 + 3], uchar(0));
}

void ImageImportTests::translucentColoursConvertUnpremultiplied()
{
    QTemporaryDir folder;
    QImage wide = block(QImage::Format_RGBA8888, QColor(200, 100, 50, 128), clear);
    wide.setColorSpace(QColorSpace::DisplayP3);
    const QImage image = ImageImporter::decode(written(folder, "veil.png", encoded(wide, "png"))).image();
    // sRGB (215, 93, 31) under alpha 128, premultiplied.
    const uchar *bytes = image.constBits();
    QVERIFY2(qAbs(bytes[0] - 108) <= 1 && qAbs(bytes[1] - 47) <= 1 && qAbs(bytes[2] - 16) <= 1, qPrintable(QString::number(bytes[0])));
    QCOMPARE(bytes[3], uchar(128));
}

void ImageImportTests::deepAndGrayImagesBecomeEightBitColour()
{
    QTemporaryDir folder;
    const QImage deep = ImageImporter::decode(written(folder, "deep.png", encoded(block(QImage::Format_RGBA64, QColor(200, 100, 50, 128), clear), "png"))).image();
    QCOMPARE(deep.format(), QImage::Format_RGBA8888_Premultiplied);
    QCOMPARE(QColor::fromRgba(qRgba(deep.constBits()[0], deep.constBits()[1], deep.constBits()[2], deep.constBits()[3])), QColor(100, 50, 25, 128));
    const QImage gray = ImageImporter::decode(written(folder, "gray.jpg", encoded(block(QImage::Format_Grayscale8, QColor(90, 90, 90), Qt::white), "jpeg"))).image();
    QCOMPARE(gray.format(), QImage::Format_RGBA8888_Premultiplied);
    QCOMPARE(gray.pixelColor(8, 16), QColor(90, 90, 90));
}

void ImageImportTests::limitsAndInvalidFiles()
{
    QTemporaryDir folder;
    const QString path = written(folder, "fixture.png", encoded(block(QImage::Format_RGBA8888, red, clear), "png"));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("^cannot import .*fixture.png: 64 x 32 pixels$"));
    QCOMPARE(importError(path, 10), std::optional(ImageImportError::Kind::tooLarge));
    // The budget holds 64 x 32 exactly.
    QTest::ignoreMessage(QtWarningMsg, refusal);
    QCOMPARE(importError(path, 2047), std::optional(ImageImportError::Kind::tooLarge));
    QCOMPARE(importError(path, 2048), std::nullopt);
    // 30,000 a side, either side.
    for (const QSize &size : {QSize(30'001, 1), QSize(1, 30'001)}) {
        QTest::ignoreMessage(QtWarningMsg, refusal);
        QCOMPARE(importError(written(folder, "long.png", encoded(QImage(size, QImage::Format_Grayscale8), "png"))),
                 std::optional(ImageImportError::Kind::tooLarge));
    }
    QCOMPARE(ImageImporter::decode(written(folder, "edge.png", encoded(QImage(30'000, 1, QImage::Format_Grayscale8), "png"))).size(), QSize(30'000, 1));
    QCOMPARE(ImageImporter::decode(written(folder, "edge.png", encoded(QImage(1, 30'000, QImage::Format_Grayscale8), "png"))).size(), QSize(1, 30'000));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("^cannot import .*text.png: no image format is recognised$"));
    QCOMPARE(importError(written(folder, "text.png", "not an image")), std::optional(ImageImportError::Kind::unreadable));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("^cannot import .*missing.png: No such file or directory$"));
    QCOMPARE(importError(folder.filePath("missing.png")), std::optional(ImageImportError::Kind::unreadable));
    // Half a TIFF lost its directory; half a PNG, pixels.
    const QImage big = block(QImage::Format_RGBA8888, red, clear).scaled(640, 320);
    // From Qt 6.9 the TIFF plugin logs libtiff errors critically.
    if (QLibraryInfo::version() >= QVersionNumber(6, 9)) {
        QTest::ignoreMessage(QtCriticalMsg, QRegularExpression("^\"Can not read TIFF directory count\"$"));
        QTest::ignoreMessage(QtCriticalMsg, QRegularExpression("^\"Failed to read directory at offset \\d+\"$"));
    }
    const QString cutTiff = written(folder, "cut.tiff", encoded(big, "tiff").left(400'000));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("^cannot import .*cut.tiff: it states no size$"));
    QCOMPARE(importError(cutTiff), std::optional(ImageImportError::Kind::unreadable));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("^cannot import .*cut.png: Unable to read image data$"));
    const QByteArray png = encoded(big, "png");
    QCOMPARE(importError(written(folder, "cut.png", png.left(png.size() / 2))), std::optional(ImageImportError::Kind::unreadable));
    // Formats Qt reads and the app does not take.
    const QByteArray gif = QByteArray::fromHex("47494638396101000100800000000000ffffff21f90401000000002c00000000010001000002024401003b");
    const QByteArray avif = QByteArray::fromHex("0000001c667479706176696600000000617669666d6966316d696166");
    const QList<std::pair<QString, QByteArray>> unwanted{{"tiny.gif", gif}, {"tiny.avif", avif},
                                                         {"fixture.bmp", encoded(block(QImage::Format_RGB888, red, Qt::white), "bmp")},
                                                         {"fixture.webp", encoded(block(QImage::Format_RGB888, red, Qt::white), "webp")}};
    for (const auto &[name, data] : unwanted) {
        QTest::ignoreMessage(QtWarningMsg, refusal);
        QVERIFY2(importError(written(folder, name, data)) == std::optional(ImageImportError::Kind::unsupported), qPrintable(name));
    }
}

void ImageImportTests::theTypeComesFromTheBytes()
{
    QTemporaryDir folder;
    const QByteArray png = encoded(block(QImage::Format_RGBA8888, red, clear), "png");
    QCOMPARE(ImageImporter::decode(written(folder, "really-png.jpg", png)).image().pixelColor(56, 16), clear);
    QCOMPARE(ImageImporter::decode(written(folder, "no-suffix", png)).size(), QSize(64, 32));
    QFile heic(QFINDTESTDATA("fixtures/red.heic"));
    QVERIFY(heic.open(QIODevice::ReadOnly));
    QCOMPARE(ImageImporter::decode(written(folder, "really-heic.png", heic.readAll())).image().pixelColor(56, 8), clear);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("^cannot import .*really-gif.png: gif$"));
    const QByteArray gif = QByteArray::fromHex("47494638396101000100800000000000ffffff21f90401000000002c00000000010001000002024401003b");
    QCOMPARE(importError(written(folder, "really-gif.png", gif)), std::optional(ImageImportError::Kind::unsupported));
}

void ImageImportTests::namesDropOneExtension()
{
    QTemporaryDir folder;
    const QByteArray png = encoded(block(QImage::Format_RGBA8888, red, clear), "png");
    QCOMPARE(ImageImporter::decode(written(folder, "holiday.final.png", png)).name, QString("holiday.final"));
    QCOMPARE(ImageImporter::decode(written(folder, "Paint & sky.png", png)).name, QString("Paint & sky"));
    QCOMPARE(ImageImporter::decode(written(folder, "bare", png)).name, QString("bare"));
    // A dot file has no extension to drop.
    QCOMPARE(ImageImporter::decode(written(folder, ".png", png)).name, QString(".png"));
}

void ImageImportTests::thumbnailsRoundOutward_data()
{
    QTest::addColumn<QSize>("size");
    QTest::addColumn<QSize>("thumbnail");
    QTest::newRow("small images keep their size") << QSize(96, 40) << QSize(96, 40);
    QTest::newRow("97 wide") << QSize(97, 40) << QSize(96, 40);
    QTest::newRow("wide") << QSize(200, 100) << QSize(96, 48);
    QTest::newRow("28.8 rounds up") << QSize(100, 30) << QSize(96, 29);
    QTest::newRow("tall") << QSize(30, 100) << QSize(29, 96);
    QTest::newRow("a strip stays one pixel high") << QSize(2400, 1) << QSize(96, 1);
}

void ImageImportTests::thumbnailsRoundOutward()
{
    QFETCH(QSize, size);
    QFETCH(QSize, thumbnail);
    QTemporaryDir folder;
    QImage image(size, QImage::Format_RGB888);
    image.fill(warm);
    const ImportedImage result = ImageImporter::decode(written(folder, "sized.png", encoded(image, "png")));
    QCOMPARE(result.thumbnail.size(), thumbnail);
    QCOMPARE(result.thumbnail.pixelColor(thumbnail.width() - 1, thumbnail.height() - 1), warm);
    QCOMPARE(result.thumbnail.colorSpace(), QColorSpace(QColorSpace::SRgb));
}

void ImageImportTests::thumbnailsAreSmoothed()
{
    QTemporaryDir folder;
    // Columns of black and white: nearest picks one, smooth blends.
    QImage stripes(192, 192, QImage::Format_RGB888);
    for (int y = 0; y < 192; ++y) {
        for (int x = 0; x < 192; ++x)
            stripes.setPixel(x, y, x % 2 == 0 ? qRgb(0, 0, 0) : qRgb(255, 255, 255));
    }
    const QImage thumbnail = ImageImporter::decode(written(folder, "stripes.png", encoded(stripes, "png"))).thumbnail;
    QCOMPARE(thumbnail.size(), QSize(96, 96));
    QVERIFY2(qAbs(thumbnail.pixelColor(48, 48).red() - 128) <= 2, qPrintable(thumbnail.pixelColor(48, 48).name()));
}

void ImageImportTests::errorsCarrySwiftsDescriptions()
{
    QCOMPARE(QString(ImageImportError(ImageImportError::Kind::unreadable).what()), QString("The image could not be read. It may be damaged or unavailable."));
    QCOMPARE(QString(ImageImportError(ImageImportError::Kind::unsupported).what()), QString("Choose a JPEG, PNG, HEIC, TIFF, or Photoshop (PSD) file."));
    QCOMPARE(QString(ImageImportError(ImageImportError::Kind::tooLarge).what()),
             QString("This import exceeds the current 100-megapixel document budget or 30,000-pixel side limit."));
    QCOMPARE(ImageImportError(ImageImportError::Kind::tooLarge).kind, ImageImportError::Kind::tooLarge);
}

void ImageImportTests::aDecodeWithoutMemoryIsUnreadable()
{
    QTemporaryDir folder;
    // 9000 x 9000 needs 324 MB; the file is small.
    QImage large(9000, 9000, QImage::Format_Grayscale8);
    large.fill(90);
    const QString path = written(folder, "large.png", encoded(large, "png"));
    large = QImage();
    QTest::ignoreMessage(QtWarningMsg, "QImage: out of memory, returning null image");
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("^cannot import .*large.png: out of memory$"));
    std::optional<ImageImportError::Kind> kind;
    {
        const AddressSpaceLimit limit(200ll * 1024 * 1024);
        kind = importError(path);
    }
    QCOMPARE(kind, std::optional(ImageImportError::Kind::unreadable));
    QCOMPARE(ImageImporter::decode(path).size(), QSize(9000, 9000));
}

QTEST_MAIN(ImageImportTests)
#include "ImageImportTests.moc"
