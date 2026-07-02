#include "../dsp/vocoder.h"

#include <emscripten/emscripten.h>
#include <stdlib.h>
#include <string.h>

/*
 * WASM bridge for the PicoPunk vocoder DSP core.
 * All exported functions are prefixed with wasm_vocoder_ and use
 * EMSCRIPTEN_KEEPALIVE to survive dead-code elimination.
 *
 * Memory layout: the caller (JS AudioWorkletProcessor) allocates
 * float buffers in WASM linear memory via _malloc / _free and passes
 * the raw byte offsets to the process function.
 */

/* ── lifecycle ─────────────────────────────────────────────────────── */

EMSCRIPTEN_KEEPALIVE
Vocoder *wasm_vocoder_create(float fs)
{
    Vocoder *v = (Vocoder *)malloc(sizeof(Vocoder));
    if (v) vocoder_init(v, fs);
    return v;
}

EMSCRIPTEN_KEEPALIVE
void wasm_vocoder_destroy(Vocoder *v)
{
    free(v);
}

EMSCRIPTEN_KEEPALIVE
void wasm_vocoder_reset(Vocoder *v)
{
    vocoder_reset(v);
}

/* ── parameter setters ─────────────────────────────────────────────── */

EMSCRIPTEN_KEEPALIVE
void wasm_vocoder_set_wet_dry(Vocoder *v, float wet, float dry)
{
    vocoder_set_wet_dry(v, wet, dry);
}

EMSCRIPTEN_KEEPALIVE
void wasm_vocoder_set_output_gain(Vocoder *v, float gain)
{
    vocoder_set_output_gain(v, gain);
}

EMSCRIPTEN_KEEPALIVE
void wasm_vocoder_set_attack_release(Vocoder *v, float atk_ms,
                                     float rel_low_ms, float rel_high_ms)
{
    vocoder_set_attack_release(v, atk_ms, rel_low_ms, rel_high_ms);
}

EMSCRIPTEN_KEEPALIVE
void wasm_vocoder_set_sibilance(Vocoder *v, float amount)
{
    vocoder_set_sibilance(v, amount);
}

EMSCRIPTEN_KEEPALIVE
void wasm_vocoder_set_preemphasis(Vocoder *v, float amount)
{
    vocoder_set_preemphasis(v, amount);
}

/* ── audio processing ──────────────────────────────────────────────── */

EMSCRIPTEN_KEEPALIVE
void wasm_vocoder_process(Vocoder *v,
                          const float *voice_in,
                          const float *carrier_in,
                          float *out,
                          unsigned nframes)
{
    vocoder_process_block(v, voice_in, carrier_in, out, nframes);
}

/* ── envelope readback (for UI visualisation) ──────────────────────── */

EMSCRIPTEN_KEEPALIVE
int wasm_vocoder_get_nbands(void)
{
    return VOCODER_NBANDS;
}

EMSCRIPTEN_KEEPALIVE
float wasm_vocoder_get_env(const Vocoder *v, unsigned k)
{
    if (!v || k >= VOCODER_NBANDS) return 0.0f;
    return v->env_smooth[k];
}

/* ── helper: struct size (for debugging) ───────────────────────────── */

EMSCRIPTEN_KEEPALIVE
unsigned wasm_vocoder_sizeof(void)
{
    return (unsigned)sizeof(Vocoder);
}
