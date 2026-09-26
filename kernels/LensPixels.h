#ifndef LensPixels_h
#define LensPixels_h
#include <stdint.h>
#include <stddef.h>
// Radial lens distortion over premultiplied RGBA (4 bytes per pixel, `stride` bytes per row, same
// layout for source and destination). Each destination pixel samples the source bilinearly at
// its offset from the image center scaled by (1 - k * r²), where r is that offset relative to the
// half-diagonal: k > 0 pulls samples inward (straightens barrel distortion, corners crop), k < 0
// pushes them outward (straightens pincushion distortion, corners turn transparent). Pixels
// outside the source are transparent. k = 0 copies the source exactly.
void lens_distort(const uint8_t *source, uint8_t *destination, size_t width, size_t height, size_t stride, double k);
#endif
