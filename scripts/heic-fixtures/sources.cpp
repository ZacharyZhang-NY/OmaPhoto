#include <QBuffer>
#include <QColorSpace>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QImageWriter>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    // Red on the left; right: clear over half-clear blue.
    QImage red(64, 32, QImage::Format_RGBA8888);
    red.fill(Qt::transparent);
    // (200, 100, 50) left; right: white over dark gray.
    QImage warm(64, 32, QImage::Format_RGB888);
    warm.fill(Qt::white);
    for (int y = 16; y < 32; ++y) {
        for (int x = 32; x < 64; ++x)
            warm.setPixel(x, y, qRgb(16, 16, 16));
    }
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            red.setPixel(x, y, qRgba(255, 0, 0, 255));
            warm.setPixel(x, y, qRgb(200, 100, 50));
            if (y >= 16)
                red.setPixel(x + 32, y, qRgba(0, 0, 255, 128));
        }
    }
    // The same picture with its colours already multiplied.
    const QImage multiplied = red.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    const QImage stored(multiplied.constBits(), 64, 32, multiplied.bytesPerLine(), QImage::Format_RGBA8888);
    bool written = red.save("red.png") && stored.save("premultiplied.png") && warm.save("warm.png");
    // Display P3 by ICC profile, turned by EXIF orientation 6.
    warm.setColorSpace(QColorSpace::DisplayP3);
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    QImageWriter writer(&buffer, "jpeg");
    writer.setQuality(100);
    written = written && writer.write(warm);
    QByteArray exif("\xFF\xE1\x00\x22" "Exif\0\0" "II\x2A\x00\x08\x00\x00\x00" "\x01\x00" "\x12\x01\x03\x00\x01\x00\x00\x00", 28);
    exif += QByteArray("\x06\x00\x00\x00" "\x00\x00\x00\x00", 8);
    jpeg.insert(2, exif);
    QFile file("turned-p3.jpg");
    written = written && file.open(QIODevice::WriteOnly) && file.write(jpeg) == jpeg.size();
    return written ? 0 : 1;
}
