#include "Document/Dither.h"
#include "Document/BrushStroke.h"
#include "Document/PixelAdjust.h"
#include "IO/ImageExporter.h"
#include "Rendering/PoolMap.h"
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QTextBoundaryFinder>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>
extern "C" {
#include "DitherPixels.h"
}

// The kernel's bands, where Swift's kernel calls dispatch_apply.
extern "C" void dither_bands(size_t bands, void (*band)(void *context, size_t index), void *context)
{
    std::vector<size_t> indices(bands);
    std::iota(indices.begin(), indices.end(), size_t(0));
    PoolMap::blocking(indices, [band, context](size_t index) { band(context, index); });
}

// Swift's Dither.swift: the settings, the chunky pixels, the glyphs.
namespace {
// Swift's Characters: the text's graphemes.
QStringList graphemes(const QString &text)
{
    QStringList found;
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
    qsizetype start = 0;
    while (finder.toNextBoundary() >= 0) {
        found << text.mid(start, finder.position() - start);
        start = finder.position();
    }
    return found;
}

// Swift's Character.isNewline.
bool isNewline(const QString &grapheme)
{
    static const QString breaks = QStringLiteral("\n\r\u000B\u000C\u0085  ");
    return grapheme == QStringLiteral("\r\n") || (grapheme.size() == 1 && breaks.contains(grapheme.front()));
}

std::array<uint8_t, 3> bytes(const AdjustmentColor &color)
{
    return {uint8_t(std::lround(color.red * 255)), uint8_t(std::lround(color.green * 255)), uint8_t(std::lround(color.blue * 255))};
}
}

QString rawValue(DitherStyle style)
{
    switch (style) {
    case DitherStyle::atkinson: return QStringLiteral("Atkinson (Classic Mac)");
    case DitherStyle::floydSteinberg: return QStringLiteral("Floyd–Steinberg");
    case DitherStyle::bayer2: return QStringLiteral("Bayer 2 × 2");
    case DitherStyle::bayer4: return QStringLiteral("Bayer 4 × 4");
    case DitherStyle::bayer8: return QStringLiteral("Bayer 8 × 8");
    case DitherStyle::dots: return QStringLiteral("Halftone Dots");
    case DitherStyle::lines: return QStringLiteral("Halftone Lines");
    case DitherStyle::diamonds: return QStringLiteral("Halftone Diamonds");
    case DitherStyle::patterns: return QStringLiteral("Mac Patterns");
    case DitherStyle::ascii: return QStringLiteral("ASCII");
    case DitherStyle::scanlines: return QStringLiteral("Scanlines (CRT)");
    }
    throw std::logic_error("unknown dither style");
}

int ditherGroup(DitherStyle style)
{
    const int index = int(style);
    return index < 2 ? 0 : index < 5 ? 1 : index < 8 ? 2 : 3;
}

bool diffuses(DitherStyle style)
{
    return ditherGroup(style) == 0;
}

bool hasTones(DitherStyle style)
{
    return ditherGroup(style) < 2;
}

bool isHalftone(DitherStyle style)
{
    return ditherGroup(style) == 2;
}

bool drawsMarks(DitherStyle style)
{
    return !hasTones(style) && style != DitherStyle::scanlines;
}

bool usesPixelSize(DitherStyle style)
{
    return style != DitherStyle::ascii && style != DitherStyle::scanlines;
}

QString rawValue(DitherPixelShape shape)
{
    return shape == DitherPixelShape::square ? QStringLiteral("Square") : QStringLiteral("Dot");
}

QString rawValue(DitherColors colors)
{
    switch (colors) {
    case DitherColors::blackWhite: return QStringLiteral("Black & White");
    case DitherColors::twoColors: return QStringLiteral("Two Colors");
    case DitherColors::original: return QStringLiteral("Original");
    }
    throw std::logic_error("unknown dither colours");
}

DitherSettings DitherSettings::normalized() const
{
    using ImageAdjustmentPixels::clamp;
    DitherSettings result = *this;
    result.pixelSize = std::round(clamp(pixelSize, pixelSizeLow, pixelSizeHigh, 2));
    result.cellSize = std::round(clamp(cellSize, cellSizeLow, cellSizeHigh, 8));
    result.textSize = std::round(clamp(textSize, textSizeLow, textSizeHigh, 14));
    result.lineSpacing = std::round(clamp(lineSpacing, lineSpacingLow, lineSpacingHigh, 4));
    result.glow = clamp(glow, 0, 100, 35);
    result.dots = clamp(dots, 0, 100, 0);
    result.wobble = clamp(wobble, wobbleLow, wobbleHigh, 0);
    result.angle = clamp(angle, -90, 90, 45);
    result.levels = std::round(clamp(levels, levelsLow, levelsHigh, 2));
    result.diffusion = clamp(diffusion, 0, 100, 100);
    result.density = clamp(density, -100, 100, 0);
    result.contrast = clamp(contrast, -100, 100, 0);
    result.dark = dark.clamped();
    result.light = light.clamped();
    // Line breaks go; 64 characters at most.
    QStringList kept;
    for (const QString &grapheme : graphemes(characters)) {
        if (!isNewline(grapheme) && kept.size() < 64)
            kept << grapheme;
    }
    result.characters = kept.join(QString());
    return result;
}

QImage DitherSettings::apply(const QImage &image) const
{
    const DitherSettings settings = normalized();
    // ASCII and Scanlines draw full size: chunks would blur them.
    const int block = usesPixelSize(settings.style) ? int(settings.pixelSize) : 1;
    const QImage source = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    if (source.isNull())
        throw ExportError(ExportError::Kind::render);
    const int width = source.width(), height = source.height();
    QImage working = source;
    if (block > 1) {
        // Chunky pixels: each block's area averaged, then dithered small.
        working = BrushRaster::context((width + block - 1) / block, (height + block - 1) / block, false);
        const int area = block * block;
        for (int y = 0; y < working.height(); ++y) {
            uchar *out = working.scanLine(y);
            for (int x = 0; x < working.width(); ++x) {
                std::array<int, 4> sum{};
                for (int sy = y * block; sy < std::min(height, (y + 1) * block); ++sy) {
                    const uchar *row = source.constScanLine(sy);
                    for (int sx = x * block; sx < std::min(width, (x + 1) * block); ++sx) {
                        for (int channel = 0; channel < 4; ++channel)
                            sum[size_t(channel)] += row[sx * 4 + channel];
                    }
                }
                for (int channel = 0; channel < 4; ++channel)
                    out[x * 4 + channel] = uchar((sum[size_t(channel)] + area / 2) / area);
            }
        }
    }
    const QImage dithered = settings.dither(working);
    if (settings.style == DitherStyle::scanlines && settings.glow > 0)
        return settings.glowing(dithered);
    if (block <= 1)
        return dithered;
    // Blown back up without smoothing.
    QImage full = BrushRaster::context(width, height, false);
    for (int y = 0; y < height; ++y) {
        const uchar *row = dithered.constScanLine(y / block);
        uchar *out = full.scanLine(y);
        for (int x = 0; x < width; ++x)
            std::copy_n(row + (x / block) * 4, 4, out + x * 4);
    }
    if (settings.pixelShape == DitherPixelShape::dot) {
        // The gaps are the dark colour: black, or picked.
        const std::array<uint8_t, 3> gap = settings.colors == DitherColors::twoColors ? bytes(settings.dark) : std::array<uint8_t, 3>{0, 0, 0};
        dither_dots(full.bits(), size_t(width), size_t(height), size_t(full.bytesPerLine()), block, gap.data());
    }
    return full;
}

// The lines' light blurred wide and added back: phosphor bloom.
QImage DitherSettings::glowing(const QImage &image) const
{
    // Blurred small, scaled back up: same light, less work.
    const double sigma = lineSpacing * 3 + 3, shrink = std::max(1.0, std::floor(sigma / 4));
    const int width = image.width(), height = image.height();
    // Swift clamps first: edges repeat outward before shrinking.
    const int step = int(shrink), pad = int(std::ceil(sigma * 3 / shrink)) * step;
    // Far sides fill whole steps: both scales are exact.
    const auto whole = [step](int side) { return (side + step - 1) / step * step; };
    QImage padded = BrushRaster::context(whole(width + 2 * pad), whole(height + 2 * pad), false);
    for (int y = 0; y < padded.height(); ++y) {
        const uchar *source = image.constScanLine(std::clamp(y - pad, 0, height - 1));
        uchar *out = padded.scanLine(y);
        for (int x = 0; x < padded.width(); ++x)
            std::copy_n(source + std::clamp(x - pad, 0, width - 1) * 4, 4, out + x * 4);
    }
    const int smallWidth = padded.width() / step, smallHeight = padded.height() / step;
    const QImage small = padded.scaled(smallWidth, smallHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (small.isNull())
        throw ExportError(ExportError::Kind::render);
    const QImage blurred = PixelAdjust::gaussianBlur(small, sigma / shrink, true)
                               .scaled(smallWidth * step, smallHeight * step, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                               .convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    QImage result = image.copy();
    if (blurred.isNull() || result.isNull())
        throw ExportError(ExportError::Kind::render);
    // The scaled light's own rows, cropped to the image.
    QImage bloom = BrushRaster::context(width, height, false);
    for (int y = 0; y < height; ++y)
        std::copy_n(blurred.constScanLine(y + pad) + pad * 4, width * 4, bloom.scanLine(y));
    dither_glow(result.bits(), bloom.constBits(), size_t(width), size_t(height), size_t(result.bytesPerLine()), float(glow / 100 * 2.5));
    return result;
}

QImage DitherSettings::dither(const QImage &image) const
{
    const int cell = int(style == DitherStyle::scanlines ? lineSpacing : cellSize);
    const Glyphs glyphs = style == DitherStyle::ascii ? DitherSettings::glyphs(characters.isEmpty() ? defaultCharacters() : characters, int(textSize)) : Glyphs();
    const bool two = colors == DitherColors::twoColors;
    const std::array<uint8_t, 3> darkColor = two ? bytes(dark) : std::array<uint8_t, 3>{0, 0, 0};
    const std::array<uint8_t, 3> lightColor = two ? bytes(light) : std::array<uint8_t, 3>{255, 255, 255};
    bool failed = false;
    const QImage result = ImageAdjustmentPixels::run(image, [&](uchar *pixels, int width, int height, qsizetype stride) {
        DitherParams params{.style = int(style), .levels = int(levels), .diffusion = float(diffusion / 100), .density = float(density / 100),
                            .contrast = float(contrast / 100), .cell = cell, .angle = float(angle * M_PI / 180), .lightOnDark = lightOnDark ? 1 : 0,
                            .originalColors = colors == DitherColors::original ? 1 : 0, .dark = {darkColor[0], darkColor[1], darkColor[2]},
                            .light = {lightColor[0], lightColor[1], lightColor[2]}, .glyphWidth = glyphs.width, .glyphHeight = glyphs.height, .glyphs = glyphs.maps.data(),
                            .glyphCoverage = glyphs.coverage.data(), .glyphCount = int(glyphs.coverage.size()), .dots = float(dots / 100),
                            .wobble = float(wobble)};
        failed = dither_apply(pixels, size_t(width), size_t(height), size_t(stride), &params) == 0;
    });
    if (failed)
        throw ExportError(ExportError::Kind::render);
    return result;
}

DitherSettings::Glyphs DitherSettings::glyphs(const QString &characters, int lineHeight)
{
    // Swift's bold system monospace, at a whole pixel size.
    QFont font(QStringLiteral("monospace"));
    font.setWeight(QFont::Bold);
    font.setPixelSize(std::max(1, int(std::lround(lineHeight / 1.2))));
    const QFontMetricsF metrics(font);
    const int height = lineHeight, width = std::max(1, int(std::lround(metrics.horizontalAdvance(QStringLiteral("M")))));
    // One baseline, mid-line, as a terminal sets it.
    const double baseline = height - std::round((height - (metrics.ascent() + metrics.descent())) / 2 + metrics.descent());
    std::vector<std::pair<std::vector<uint8_t>, float>> drawn;
    // Swift's Set<Character>: canonically equivalent graphemes are one.
    std::set<QString> seen;
    for (const QString &character : graphemes(characters)) {
        if (!seen.insert(character.normalized(QString::NormalizationForm_C)).second)
            continue;
        QImage map(width, height, QImage::Format_Grayscale8);
        if (map.isNull())
            throw ExportError(ExportError::Kind::render);
        map.fill(0);
        QPainterPath path;
        path.addText(0, 0, font, character);
        // Overlapping outlines stay inked, as text rendering fills them.
        path.setFillRule(Qt::WindingFill);
        {
            // Centred on its advance, on the shared baseline.
            QPainter painter(&map);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.translate(std::round((width - metrics.horizontalAdvance(character)) / 2), baseline);
            painter.fillPath(path, Qt::white);
        }
        std::vector<uint8_t> bytes(size_t(width * height));
        long sum = 0;
        for (int y = 0; y < height; ++y) {
            std::copy_n(map.constScanLine(y), width, bytes.begin() + y * width);
            for (int x = 0; x < width; ++x)
                sum += map.constScanLine(y)[x];
        }
        drawn.emplace_back(std::move(bytes), float(sum) / float(255 * width * height));
    }
    std::stable_sort(drawn.begin(), drawn.end(), [](const auto &a, const auto &b) { return a.second < b.second; });
    Glyphs result{.maps = {}, .coverage = {}, .width = width, .height = height};
    for (const auto &[map, coverage] : drawn) {
        result.maps.insert(result.maps.end(), map.begin(), map.end());
        result.coverage.push_back(coverage);
    }
    return result;
}
