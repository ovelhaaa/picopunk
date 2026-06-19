#include "vocoder.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#if VOCODER_NBANDS != 20
#error "VOCODER_NBANDS must be exactly 20 to match the static k_band_edges array."
#endif

static const float k_band_edges[VOCODER_NBANDS + 1] = {
    90.0f, 113.0f, 143.0f, 180.0f, 226.0f, 285.0f, 358.0f,
    451.0f, 568.0f, 715.0f, 900.0f, 1133.0f, 1426.0f, 1796.0f,
    2261.0f, 2846.0f, 3583.0f, 4511.0f, 5680.0f, 7150.0f, 9000.0f
};

static float clampf_default(float x, float lo, float hi, float def)
{
    if (!isfinite(x)) x = def;
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static inline float time_to_coef(float time_s, float fs)
{
    time_s = clampf_default(time_s, 0.0001f, 10.0f, 0.001f);
    fs = clampf_default(fs, 1000.0f, 384000.0f, (float)VOCODER_FS);
    return expf(-1.0f / (time_s * fs));
}

static inline float env_follow(float env, float x_abs, float atk_coef, float rel_coef)
{
    const float c = (x_abs > env) ? atk_coef : rel_coef;
    return c * env + (1.0f - c) * x_abs;
}

static inline float xorshift_noise(uint32_t *state)
{
    uint32_t x = *state;
    if (x == 0u) x = 0x12345678u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return ((float)x * (1.0f / 2147483648.0f)) - 1.0f;
}

static inline float softclip(float x)
{
    if (x >  2.0f) x =  2.0f;
    if (x < -2.0f) x = -2.0f;
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

static inline float biquad_process(Biquad *s, float x)
{
    float y = s->b0 * x + s->z1;
    s->z1 = s->b1 * x - s->a1 * y + s->z2;
    s->z2 = s->b2 * x - s->a2 * y;
    return y;
}

void biquad_reset(Biquad *s)
{
    if (s != NULL) {
        s->z1 = 0.0f;
        s->z2 = 0.0f;
    }
}

static void biquad_normalize(Biquad *s, float b0, float b1, float b2,
                             float a0, float a1, float a2)
{
    const float inv_a0 = 1.0f / a0;
    s->b0 = b0 * inv_a0;
    s->b1 = b1 * inv_a0;
    s->b2 = b2 * inv_a0;
    s->a1 = a1 * inv_a0;
    s->a2 = a2 * inv_a0;
}

void biquad_set_bandpass(Biquad *s, float fs, float fc, float q)
{
    if (s == NULL) return;
    fs = clampf_default(fs, 1000.0f, 384000.0f, (float)VOCODER_FS);
    fc = clampf_default(fc, 1.0f, 0.45f * fs, 1000.0f);
    q = clampf_default(q, 0.1f, 20.0f, 0.707f);

    const float w0 = 2.0f * (float)M_PI * fc / fs;
    const float cw = cosf(w0);
    const float sw = sinf(w0);
    const float alpha = sw / (2.0f * q);

    biquad_normalize(s, alpha, 0.0f, -alpha,
                     1.0f + alpha, -2.0f * cw, 1.0f - alpha);
}

void biquad_set_highpass(Biquad *s, float fs, float fc, float q)
{
    if (s == NULL) return;
    fs = clampf_default(fs, 1000.0f, 384000.0f, (float)VOCODER_FS);
    fc = clampf_default(fc, 1.0f, 0.45f * fs, 1000.0f);
    q = clampf_default(q, 0.1f, 20.0f, 0.707f);

    const float w0 = 2.0f * (float)M_PI * fc / fs;
    const float cw = cosf(w0);
    const float sw = sinf(w0);
    const float alpha = sw / (2.0f * q);
    const float b0 = (1.0f + cw) * 0.5f;
    const float b1 = -(1.0f + cw);
    const float b2 = (1.0f + cw) * 0.5f;

    biquad_normalize(s, b0, b1, b2,
                     1.0f + alpha, -2.0f * cw, 1.0f - alpha);
}

static float band_q(unsigned k)
{
    if (k < 4u) return 3.0f + 0.5f * ((float)k / 3.0f);
    if (k < 16u) return 3.5f + 1.0f * ((float)(k - 4u) / 11.0f);
    return 4.0f - 1.0f * ((float)(k - 16u) / 3.0f);
}

void biquad_set_bandpass_reset(Biquad *s, float fs, float fc, float q)
{
    biquad_set_bandpass(s, fs, fc, q);
    biquad_reset(s);
}

void biquad_set_highpass_reset(Biquad *s, float fs, float fc, float q)
{
    biquad_set_highpass(s, fs, fc, q);
    biquad_reset(s);
}

void vocoder_set_attack_release(Vocoder *v, float attack_ms,
                                float release_low_ms,
                                float release_high_ms)
{
    if (v == NULL) return;
    attack_ms = clampf_default(attack_ms, 0.5f, 50.0f, 3.0f);
    release_low_ms = clampf_default(release_low_ms, 10.0f, 500.0f, 120.0f);
    release_high_ms = clampf_default(release_high_ms, 10.0f, 500.0f, 50.0f);
    for (unsigned k = 0; k < VOCODER_NBANDS; ++k) {
        const float t = (float)k / (float)(VOCODER_NBANDS - 1u);
        const float rel_ms = release_low_ms + (release_high_ms - release_low_ms) * t;
        v->atk_coef[k] = time_to_coef(attack_ms * 0.001f, v->fs);
        v->rel_coef[k] = time_to_coef(rel_ms * 0.001f, v->fs);
    }
}

void vocoder_init(Vocoder *v, float fs)
{
    if (v == NULL) return;
    memset(v, 0, sizeof(*v));
    v->fs = clampf_default(fs, 1000.0f, 384000.0f, (float)VOCODER_FS);
    v->preemph = 0.70f;
    v->output_gain = 0.8f;
    v->wet = 1.0f;
    v->dry = 0.0f;
    v->sibilance_amount = 1.0f;
    v->sibilance_coef = time_to_coef(0.010f, v->fs);
    v->gain_smooth_coef = time_to_coef(0.003f, v->fs);
    v->rng = 0x12345678u;

    biquad_set_highpass(&v->voice_hpf, v->fs, 70.0f, 0.707f);
    biquad_set_highpass(&v->out_hpf, v->fs, 30.0f, 0.707f);
    biquad_set_highpass(&v->noise_hpf, v->fs, 4000.0f, 0.707f);
    biquad_set_bandpass(&v->noise_bpf, v->fs, 7000.0f, 1.2f);

    for (unsigned k = 0; k < VOCODER_NBANDS; ++k) {
        const float fc = sqrtf(k_band_edges[k] * k_band_edges[k + 1u]);
        const float q = band_q(k);
        const float t = (float)k / (float)(VOCODER_NBANDS - 1u);
        float presence = 1.0f;
        float low_cut;
        biquad_set_bandpass(&v->ana[k], v->fs, fc, q);
        biquad_set_bandpass(&v->car[k], v->fs, fc, q);
        if (k >= 10u && k <= 16u) presence = 1.25f;
        low_cut = 0.65f + 0.35f * t;
        v->makeup[k] = presence * low_cut * (1.0f / (float)VOCODER_NBANDS);
    }
    vocoder_set_attack_release(v, 3.0f, 120.0f, 50.0f);
}

void vocoder_reset(Vocoder *v)
{
    if (v == NULL) return;
    biquad_reset(&v->voice_hpf);
    biquad_reset(&v->out_hpf);
    biquad_reset(&v->noise_hpf);
    biquad_reset(&v->noise_bpf);
    for (unsigned k = 0; k < VOCODER_NBANDS; ++k) {
        biquad_reset(&v->ana[k]);
        biquad_reset(&v->car[k]);
        v->env[k] = 0.0f;
        v->env_smooth[k] = 0.0f;
    }
    v->preemph_z = 0.0f;
    v->sibilance = 0.0f;
    v->rng = 0x12345678u;
}

void vocoder_set_wet_dry(Vocoder *v, float wet, float dry)
{
    if (v == NULL) return;
    v->wet = clampf_default(wet, 0.0f, 1.0f, 1.0f);
    v->dry = clampf_default(dry, 0.0f, 1.0f, 0.0f);
}

void vocoder_set_output_gain(Vocoder *v, float gain)
{
    if (v != NULL) v->output_gain = clampf_default(gain, 0.0f, 4.0f, 0.8f);
}

void vocoder_set_sibilance(Vocoder *v, float amount)
{
    if (v != NULL) v->sibilance_amount = clampf_default(amount, 0.0f, 1.0f, 1.0f);
}

void vocoder_set_preemphasis(Vocoder *v, float amount)
{
    if (v != NULL) v->preemph = clampf_default(amount, 0.0f, 0.95f, 0.70f);
}

void vocoder_process_block(Vocoder *v, const float *voice_in,
                           const float *carrier_in, float *out,
                           unsigned nframes)
{
    if (v == NULL || voice_in == NULL || carrier_in == NULL || out == NULL) return;

    for (unsigned n = 0; n < nframes; ++n) {
        const float voice_raw = voice_in[n];
        const float carrier = carrier_in[n];
        float voice = biquad_process(&v->voice_hpf, voice_raw);
        float env_sum = 0.0f;
        float env_hi = 0.0f;
        float y = 0.0f;

        const float pre = voice - v->preemph * v->preemph_z;
        v->preemph_z = voice;
        voice = pre;

        for (unsigned k = 0; k < VOCODER_NBANDS; ++k) {
            const float vk = biquad_process(&v->ana[k], voice);
            const float e = env_follow(v->env[k], fabsf(vk), v->atk_coef[k], v->rel_coef[k]);
            v->env[k] = e;
            env_sum += e;
            if (k >= (VOCODER_NBANDS >= 5u ? VOCODER_NBANDS - 5u : 0u)) env_hi += e;
        }

        float sib_target = ((env_hi / (env_sum + 1.0e-6f)) - 0.25f) * 3.0f;
        sib_target = clampf_default(sib_target, 0.0f, 1.0f, 0.0f) * v->sibilance_amount;
        v->sibilance = v->sibilance_coef * v->sibilance +
                       (1.0f - v->sibilance_coef) * sib_target;

        float noise = xorshift_noise(&v->rng);
        noise = biquad_process(&v->noise_hpf, noise);
        noise = biquad_process(&v->noise_bpf, noise);

        for (unsigned k = 0; k < VOCODER_NBANDS; ++k) {
            float ck = biquad_process(&v->car[k], carrier);
            if (k >= (VOCODER_NBANDS >= 5u ? VOCODER_NBANDS - 5u : 0u)) {
                const float mix = v->sibilance * 0.6f;
                ck = ck * (1.0f - mix) + noise * mix;
            }
            const float target_gain = v->env[k] * v->makeup[k];
            v->env_smooth[k] = v->gain_smooth_coef * v->env_smooth[k] +
                               (1.0f - v->gain_smooth_coef) * target_gain;
            y += ck * v->env_smooth[k];
        }

        y *= v->output_gain;
        y = biquad_process(&v->out_hpf, y);
        y = v->wet * y + v->dry * voice_raw;
        out[n] = softclip(y);
    }
}
