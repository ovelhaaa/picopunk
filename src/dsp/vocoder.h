#ifndef PICOPUNK_DSP_VOCODER_H
#define PICOPUNK_DSP_VOCODER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VOCODER_FS          48000
#define VOCODER_BLOCK       64
#define VOCODER_NBANDS      20

typedef struct {
    float b0, b1, b2;
    float a1, a2;
    float z1, z2;
} Biquad;

typedef struct {
    Biquad voice_hpf;
    Biquad out_hpf;

    Biquad ana[VOCODER_NBANDS];
    Biquad car[VOCODER_NBANDS];

    Biquad noise_hpf;
    Biquad noise_bpf;

    float env[VOCODER_NBANDS];
    float env_smooth[VOCODER_NBANDS];
    float makeup[VOCODER_NBANDS];

    float atk_coef[VOCODER_NBANDS];
    float rel_coef[VOCODER_NBANDS];
    float gain_smooth_coef;

    float preemph;
    float preemph_z;

    float sibilance;
    float sibilance_amount;
    float sibilance_coef;

    float output_gain;
    float wet;
    float dry;

    uint32_t rng;
    float fs;
} Vocoder;

void biquad_reset(Biquad *s);
void biquad_set_bandpass(Biquad *s, float fs, float fc, float q);
void biquad_set_highpass(Biquad *s, float fs, float fc, float q);

void vocoder_init(Vocoder *v, float fs);
void vocoder_reset(Vocoder *v);

void vocoder_set_wet_dry(Vocoder *v, float wet, float dry);
void vocoder_set_output_gain(Vocoder *v, float gain);
void vocoder_set_attack_release(Vocoder *v, float attack_ms,
                                float release_low_ms,
                                float release_high_ms);
void vocoder_set_sibilance(Vocoder *v, float amount);
void vocoder_set_preemphasis(Vocoder *v, float amount);

void vocoder_process_block(Vocoder *v,
                           const float *voice_in,
                           const float *carrier_in,
                           float *out,
                           unsigned nframes);

#ifdef __cplusplus
}
#endif

#endif /* PICOPUNK_DSP_VOCODER_H */
