#pragma once
#include "Document/Filters.h"
#include <QImage>
#include <optional>
#include <stdexcept>
#include <vector>

// Swift's SubjectRemoval.Failure; the model's own failure besides.
class SubjectRemovalError : public std::runtime_error {
public:
    enum class Kind { noSubject, model };
    explicit SubjectRemovalError(Kind kind, const QString &detail = QString());
    const Kind kind;
};

// Swift's SubjectRemoval: U²-Net's subject mask, refined as asked.
namespace SubjectRemoval {
// U²-Net reads a square this size, as rembg does.
inline constexpr int side = 320;
// Premultiplied, square, by the brightest byte, then ImageNet's spread.
std::vector<float> modelInput(const QImage &image);
// The answer stretched to bytes at `size`; none past half.
QImage modelMask(const float *prediction, QSize size);
// White over the subject in the layer's grid, times `existing`.
QImage subjectMask(const QImage &image, const std::optional<QImage> &existing, const FilterSettings &settings);
// The model's mask at the image's size, cached by image.
QImage foreground(const QImage &image);
// The preview: the layer with its background cleared.
QImage run(const QImage &image, const FilterSettings &settings);
}
