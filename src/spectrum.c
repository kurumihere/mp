#include "spectrum.h"

#include <math.h>
#include <string.h>

#define SPECTRUM_PI 3.14159265358979323846f

static float clamp_float(float value, float minimum, float maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;

    return value;
}

static float smooth_level(float current, float target, float *velocity,
                          float delta_time)
{
    const float smooth_time = 0.08f;
    float frequency = 2.0f / smooth_time;
    float step = frequency * delta_time;
    float decay = 1.0f / (1.0f + step + 0.48f * step * step +
                          0.235f * step * step * step);
    float difference = current - target;
    float movement = (*velocity + frequency * difference) * delta_time;

    *velocity = (*velocity - frequency * movement) * decay;

    float result = target + (difference + movement) * decay;

    if (result < 0.0f) {
        result = 0.0f;
        if (*velocity < 0.0f) *velocity = 0.0f;
    } else if (result > 1.0f) {
        result = 1.0f;
        if (*velocity > 0.0f) *velocity = 0.0f;
    }

    return result;
}

static void fft(float real[SPECTRUM_SAMPLE_COUNT],
                float imaginary[SPECTRUM_SAMPLE_COUNT],
                const Spectrum *spectrum)
{
    size_t swapped = 0;

    for (size_t i = 1; i < SPECTRUM_SAMPLE_COUNT; ++i) {
        size_t bit = SPECTRUM_SAMPLE_COUNT / 2;

        while ((swapped & bit) != 0) {
            swapped ^= bit;
            bit /= 2;
        }

        swapped ^= bit;

        if (i < swapped) {
            float temporary = real[i];
            real[i] = real[swapped];
            real[swapped] = temporary;
            temporary = imaginary[i];
            imaginary[i] = imaginary[swapped];
            imaginary[swapped] = temporary;
        }
    }

    for (size_t length = 2; length <= SPECTRUM_SAMPLE_COUNT; length *= 2) {
        size_t half = length / 2;
        size_t table_step = SPECTRUM_SAMPLE_COUNT / length;

        for (size_t offset = 0; offset < SPECTRUM_SAMPLE_COUNT;
             offset += length) {
            for (size_t i = 0; i < half; ++i) {
                size_t table_index = i * table_step;
                float twiddle_real = spectrum->cos_table[table_index];
                float twiddle_imaginary = -spectrum->sin_table[table_index];
                size_t upper = offset + i;
                size_t lower = upper + half;
                float product_real = real[lower] * twiddle_real -
                                     imaginary[lower] * twiddle_imaginary;
                float product_imaginary = real[lower] * twiddle_imaginary +
                                          imaginary[lower] * twiddle_real;

                real[lower] = real[upper] - product_real;
                imaginary[lower] = imaginary[upper] - product_imaginary;
                real[upper] += product_real;
                imaginary[upper] += product_imaginary;
            }
        }
    }
}

void spectrum_init(Spectrum *spectrum)
{
    *spectrum = (Spectrum){0};

    for (size_t i = 0; i < SPECTRUM_SAMPLE_COUNT; ++i) {
        float phase =
            2.0f * SPECTRUM_PI * (float)i / (float)(SPECTRUM_SAMPLE_COUNT - 1);
        spectrum->window[i] = 0.5f - 0.5f * cosf(phase);
    }

    for (size_t i = 0; i < SPECTRUM_SAMPLE_COUNT / 2; ++i) {
        float phase = 2.0f * SPECTRUM_PI * (float)i / SPECTRUM_SAMPLE_COUNT;
        spectrum->cos_table[i] = cosf(phase);
        spectrum->sin_table[i] = sinf(phase);
    }
}

void spectrum_decay(Spectrum *spectrum, float delta_time)
{
    float frame_time = clamp_float(delta_time, 0.0f, 0.05f);

    for (size_t bar = 0; bar < spectrum->bar_count; ++bar) {
        spectrum->levels[bar] =
            smooth_level(spectrum->levels[bar], 0.0f,
                         &spectrum->velocities[bar], frame_time);

        if (spectrum->levels[bar] < 0.001f &&
            fabsf(spectrum->velocities[bar]) < 0.01f) {
            spectrum->levels[bar] = 0.0f;
            spectrum->velocities[bar] = 0.0f;
        }
    }
}

void spectrum_reset(Spectrum *spectrum)
{
    memset(spectrum->levels, 0, sizeof(spectrum->levels));
    memset(spectrum->velocities, 0, sizeof(spectrum->velocities));
    spectrum->bar_count = 0;
}

void spectrum_update(Spectrum *spectrum,
                     const float samples[SPECTRUM_SAMPLE_COUNT],
                     unsigned int sample_rate, size_t bar_count,
                     float delta_time)
{
    if (bar_count > SPECTRUM_MAX_BARS) bar_count = SPECTRUM_MAX_BARS;

    if (bar_count != spectrum->bar_count) {
        memset(spectrum->levels, 0, sizeof(spectrum->levels));
        memset(spectrum->velocities, 0, sizeof(spectrum->velocities));
        spectrum->bar_count = bar_count;
    }

    if (bar_count == 0 || samples == NULL || sample_rate == 0) return;

    float real[SPECTRUM_SAMPLE_COUNT];
    float imaginary[SPECTRUM_SAMPLE_COUNT] = {0};

    for (size_t i = 0; i < SPECTRUM_SAMPLE_COUNT; ++i) {
        real[i] = samples[i] * spectrum->window[i];
    }

    fft(real, imaginary, spectrum);

    float nyquist = (float)sample_rate / 2.0f;
    float minimum_frequency = 45.0f;
    float maximum_frequency = nyquist * 0.95f;

    if (maximum_frequency > 16000.0f) maximum_frequency = 16000.0f;
    if (maximum_frequency <= minimum_frequency) return;

    float frequency_range = maximum_frequency / minimum_frequency;
    float frame_time = clamp_float(delta_time, 0.0f, 0.05f);
    float targets[SPECTRUM_MAX_BARS] = {0};

    for (size_t bar = 0; bar < bar_count; ++bar) {
        float lower_fraction = (float)bar / (float)bar_count;
        float upper_fraction = (float)(bar + 1) / (float)bar_count;
        float lower_frequency =
            minimum_frequency * powf(frequency_range, lower_fraction);
        float upper_frequency =
            minimum_frequency * powf(frequency_range, upper_fraction);
        size_t first_bin = (size_t)ceilf(lower_frequency *
                                         SPECTRUM_SAMPLE_COUNT / sample_rate);
        size_t last_bin = (size_t)floorf(upper_frequency *
                                         SPECTRUM_SAMPLE_COUNT / sample_rate);

        if (first_bin < 1) first_bin = 1;
        if (last_bin < first_bin) last_bin = first_bin;
        if (last_bin >= SPECTRUM_SAMPLE_COUNT / 2) {
            last_bin = SPECTRUM_SAMPLE_COUNT / 2 - 1;
        }

        float magnitude = 0.0f;

        for (size_t bin = first_bin; bin <= last_bin; ++bin) {
            float bin_magnitude =
                sqrtf(real[bin] * real[bin] + imaginary[bin] * imaginary[bin]) *
                (4.0f / SPECTRUM_SAMPLE_COUNT);

            if (bin_magnitude > magnitude) magnitude = bin_magnitude;
        }

        float decibels = 20.0f * log10f(magnitude + 0.0000001f);
        float high_frequency_boost = lower_fraction * 12.0f;
        targets[bar] = clamp_float(
            (decibels + high_frequency_boost + 65.0f) / 60.0f, 0.0f, 1.0f);
    }

    for (size_t bar = 0; bar < bar_count; ++bar) {
        float target = targets[bar] * 4.0f;
        float weight = 4.0f;

        if (bar > 0) {
            target += targets[bar - 1];
            weight += 1.0f;
        }

        if (bar + 1 < bar_count) {
            target += targets[bar + 1];
            weight += 1.0f;
        }

        target /= weight;

        spectrum->levels[bar] =
            smooth_level(spectrum->levels[bar], target,
                         &spectrum->velocities[bar], frame_time);

        if (target == 0.0f && spectrum->levels[bar] < 0.001f &&
            fabsf(spectrum->velocities[bar]) < 0.01f) {
            spectrum->levels[bar] = 0.0f;
            spectrum->velocities[bar] = 0.0f;
        }
    }

    for (size_t bar = bar_count; bar < SPECTRUM_MAX_BARS; ++bar) {
        spectrum->levels[bar] = 0.0f;
        spectrum->velocities[bar] = 0.0f;
    }
}
