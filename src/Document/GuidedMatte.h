#pragma once
#include <QImage>
#include <vector>

// Swift's GuidedMatte: a mask pulled onto its image's own edges.
namespace GuidedMatte {
// Mean over a (2r+1)² square, two running-sum passes.
std::vector<float> box(const std::vector<float> &source, int width, int height, int radius);
// `mask` refined by `guide`: both 0 to 1, one size.
std::vector<float> filter(const std::vector<float> &mask, const std::vector<float> &guide, int width, int height, int radius, float epsilon);
// `image`'s gray levels at this size, 0 to 1.
std::vector<float> levels(const QImage &image, int width, int height);
// Levels back to a gray image.
QImage image(const std::vector<float> &levels, int width, int height);
// Refined on a copy `limit` long at most, drawn back.
QImage refine(const QImage &mask, const QImage &guide, double radius, double limit);
}
