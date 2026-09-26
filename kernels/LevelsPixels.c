#include "LevelsPixels.h"
#include <math.h>
void levels_apply(uint8_t *pixels, size_t count, const float *tables) {
    for (size_t i=0; i<count; ++i) {
        uint8_t *p = pixels + i*4;
        float alpha = p[3];
        if (!alpha) continue;
        for (int channel=0; channel<3; ++channel) {
            float x = fminf(255, p[channel]*255.0f/alpha);
            int lo = (int)x, hi = lo < 255 ? lo+1 : 255;
            const float *table = tables + channel*256;
            float result = table[lo] + (table[hi]-table[lo])*(x-lo);
            p[channel] = (uint8_t)fminf(alpha, fmaxf(0, roundf(result*alpha)));
        }
    }
}
void levels_histogram(const uint8_t *pixels, const uint8_t *coverage, size_t count, double *bins) {
    for (size_t i=0; i<count; ++i) {
        const uint8_t *p = pixels+i*4;
        if (!p[3]) continue;
        double weight = p[3]/255.0 * (coverage ? coverage[i]/255.0 : 1);
        for (int channel=0; channel<3; ++channel) {
            int value = (int)fmin(255, round(p[channel]*255.0/p[3]));
            bins[(channel+1)*256+value] += weight;
            bins[value] += weight/3.0;
        }
    }
}
