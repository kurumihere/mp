#ifndef MP_SPECTRUM_H
#define MP_SPECTRUM_H

#include <stddef.h>

#define SPECTRUM_SAMPLE_COUNT 1024
#define SPECTRUM_MAX_BARS 64

typedef struct {
    float levels[SPECTRUM_MAX_BARS];
    float window[SPECTRUM_SAMPLE_COUNT];
    float cos_table[SPECTRUM_SAMPLE_COUNT / 2];
    float sin_table[SPECTRUM_SAMPLE_COUNT / 2];
    size_t bar_count;
} Spectrum;

void spectrum_init(Spectrum *spectrum);
void spectrum_reset(Spectrum *spectrum);
void spectrum_update(Spectrum *spectrum,
                     const float samples[SPECTRUM_SAMPLE_COUNT],
                     unsigned int sample_rate, size_t bar_count,
                     float delta_time);

#endif
