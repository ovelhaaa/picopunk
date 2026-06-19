#include "vocoder.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int finite_buffer(const float *x, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) {
        if (!isfinite(x[i])) return 0;
        if (fabsf(x[i]) > 1.25f) return 0;
    }
    return 1;
}

static float sine(float phase)
{
    return sinf(2.0f * (float)M_PI * phase);
}

static float saw(float phase)
{
    phase -= floorf(phase);
    return 2.0f * phase - 1.0f;
}

static int run_signal_test(unsigned block)
{
    Vocoder v;
    float voice[137];
    float carrier[137];
    float out[137];
    const unsigned frames = (unsigned)(VOCODER_FS * 3u);
    unsigned done = 0;

    if (block > 137u) return 1;
    vocoder_init(&v, (float)VOCODER_FS);

    while (done < frames) {
        const unsigned nframes = ((frames - done) < block) ? (frames - done) : block;
        for (unsigned i = 0; i < nframes; ++i) {
            const float t = (float)(done + i) / (float)VOCODER_FS;
            const float am = 0.55f + 0.45f * sine(3.0f * t);
            voice[i] = am * (0.45f * sine(180.0f * t) + 0.25f * sine(720.0f * t) +
                             0.18f * sine(2400.0f * t));
            carrier[i] = 0.20f * saw(110.0f * t) + 0.16f * saw(220.0f * t) +
                         0.12f * saw(330.0f * t);
        }
        vocoder_process_block(&v, voice, carrier, out, nframes);
        if (!finite_buffer(out, nframes)) return 1;
        done += nframes;
    }

    for (unsigned k = 0; k < VOCODER_NBANDS; ++k) {
        if (!isfinite(v.env[k]) || v.env[k] > 4.0f) return 1;
    }
    return 0;
}

static float rms_after_processing(Vocoder *v, float voice_amp, float carrier_amp)
{
    float voice[VOCODER_BLOCK];
    float carrier[VOCODER_BLOCK];
    float out[VOCODER_BLOCK];
    double sum = 0.0;
    const unsigned blocks = 160u;

    for (unsigned b = 0; b < blocks; ++b) {
        for (unsigned i = 0; i < VOCODER_BLOCK; ++i) {
            const float t = (float)(b * VOCODER_BLOCK + i) / (float)VOCODER_FS;
            voice[i] = voice_amp * (0.6f * sine(220.0f * t) + 0.25f * sine(1800.0f * t));
            carrier[i] = carrier_amp * (0.5f * saw(130.0f * t) + 0.3f * saw(260.0f * t));
        }
        vocoder_process_block(v, voice, carrier, out, VOCODER_BLOCK);
        if (!finite_buffer(out, VOCODER_BLOCK)) return 1000.0f;
        for (unsigned i = 0; i < VOCODER_BLOCK; ++i) sum += (double)out[i] * (double)out[i];
    }
    return sqrtf((float)(sum / (double)(blocks * VOCODER_BLOCK)));
}


static int reset_is_deterministic(void)
{
    Vocoder v;
    float voice[VOCODER_BLOCK];
    float carrier[VOCODER_BLOCK];
    float out_a[VOCODER_BLOCK];
    float out_b[VOCODER_BLOCK];

    vocoder_init(&v, (float)VOCODER_FS);
    for (unsigned i = 0; i < VOCODER_BLOCK; ++i) {
        const float t = (float)i / (float)VOCODER_FS;
        voice[i] = 0.7f * sine(6500.0f * t) + 0.2f * sine(220.0f * t);
        carrier[i] = 0.7f * saw(110.0f * t) + 0.3f * saw(220.0f * t);
    }

    vocoder_process_block(&v, voice, carrier, out_a, VOCODER_BLOCK);
    vocoder_reset(&v);
    vocoder_process_block(&v, voice, carrier, out_b, VOCODER_BLOCK);

    for (unsigned i = 0; i < VOCODER_BLOCK; ++i) {
        if (fabsf(out_a[i] - out_b[i]) > 1.0e-7f) return 1;
    }
    return 0;
}


static int coeffs_are_finite(const Vocoder *v)
{
    if (!isfinite(v->wet) || v->wet < 0.0f || v->wet > 1.0f) return 0;
    if (!isfinite(v->dry) || v->dry < 0.0f || v->dry > 1.0f) return 0;
    if (!isfinite(v->output_gain) || v->output_gain < 0.0f || v->output_gain > 4.0f) return 0;
    if (!isfinite(v->sibilance_amount) || v->sibilance_amount < 0.0f ||
        v->sibilance_amount > 1.0f) return 0;
    if (!isfinite(v->preemph) || v->preemph < 0.0f || v->preemph > 0.95f) return 0;
    if (!isfinite(v->fs) || v->fs < 1000.0f || v->fs > 384000.0f) return 0;
    for (unsigned k = 0; k < VOCODER_NBANDS; ++k) {
        if (!isfinite(v->atk_coef[k]) || !isfinite(v->rel_coef[k])) return 0;
        if (v->atk_coef[k] < 0.0f || v->atk_coef[k] >= 1.0f) return 0;
        if (v->rel_coef[k] < 0.0f || v->rel_coef[k] >= 1.0f) return 0;
    }
    return 1;
}

static int invalid_parameter_test(void)
{
    Vocoder v;
    float voice[VOCODER_BLOCK];
    float carrier[VOCODER_BLOCK];
    float out[VOCODER_BLOCK];

    vocoder_init(&v, NAN);
    if (!coeffs_are_finite(&v) || v.fs != (float)VOCODER_FS) return 1;

    vocoder_set_wet_dry(&v, NAN, INFINITY);
    vocoder_set_output_gain(&v, NAN);
    vocoder_set_attack_release(&v, NAN, INFINITY, -INFINITY);
    vocoder_set_sibilance(&v, NAN);
    vocoder_set_preemphasis(&v, INFINITY);
    if (!coeffs_are_finite(&v)) return 1;

    for (unsigned b = 0; b < 8u; ++b) {
        for (unsigned i = 0; i < VOCODER_BLOCK; ++i) {
            const float t = (float)(b * VOCODER_BLOCK + i) / (float)VOCODER_FS;
            voice[i] = 0.6f * sine(180.0f * t) + 0.2f * sine(2200.0f * t);
            carrier[i] = 0.5f * saw(110.0f * t) + 0.25f * saw(330.0f * t);
        }
        vocoder_process_block(&v, voice, carrier, out, VOCODER_BLOCK);
        if (!finite_buffer(out, VOCODER_BLOCK)) return 1;
    }

    return 0;
}

int main(void)
{
    Vocoder v;

    if (run_signal_test(VOCODER_BLOCK) != 0) return 1;
    if (run_signal_test(37u) != 0) return 1;
    if (run_signal_test(100u) != 0) return 1;
    if (reset_is_deterministic() != 0) return 1;
    if (invalid_parameter_test() != 0) return 1;

    vocoder_init(&v, (float)VOCODER_FS);
    {
        const float rms = rms_after_processing(&v, 0.8f, 0.8f);
        if (rms < 0.001f || rms > 0.8f) return 1;
    }

    vocoder_init(&v, (float)VOCODER_FS);
    if (rms_after_processing(&v, 0.0f, 0.8f) > 0.015f) return 1;

    vocoder_init(&v, (float)VOCODER_FS);
    if (rms_after_processing(&v, 0.8f, 0.0f) > 0.010f) return 1;

    vocoder_init(&v, (float)VOCODER_FS);
    vocoder_set_wet_dry(&v, 2.0f, -1.0f);
    vocoder_set_output_gain(&v, 10.0f);
    vocoder_set_attack_release(&v, 0.01f, 1.0f, 999.0f);
    vocoder_set_sibilance(&v, 2.0f);
    vocoder_set_preemphasis(&v, 2.0f);
    if (v.wet != 1.0f || v.dry != 0.0f || v.output_gain != 4.0f ||
        v.sibilance_amount != 1.0f || v.preemph != 0.95f) {
        return 1;
    }

    return 0;
}
