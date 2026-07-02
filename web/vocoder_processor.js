/**
 * vocoder_processor.js — AudioWorkletProcessor for the PicoPunk vocoder.
 *
 * Runs in the audio rendering thread. Loads the WASM module, allocates
 * buffers in linear memory, and calls the C vocoder_process_block on
 * each render quantum (128 frames).
 *
 * Communication with the main thread:
 *   IN  (port.onmessage):
 *     - { type: 'init', wasmBinary: ArrayBuffer }
 *     - { type: 'param', name: string, value: number | number[] }
 *     - { type: 'carrier', kind: 'saw'|'square'|'sine', freq: number }
 *     - { type: 'reset' }
 *
 *   OUT (port.postMessage):
 *     - { type: 'ready' }
 *     - { type: 'env', data: Float32Array }  (20 bands, ~60 fps)
 */

const NBANDS = 20;
const RENDER_QUANTUM = 128;
const SAMPLE_RATE = 48000;
const ENV_SEND_INTERVAL = 800; // send envelopes every N frames (~60fps at 48k)

class VocoderProcessor extends AudioWorkletProcessor {
    constructor() {
        super();
        this._ready = false;
        this._wasm = null;
        this._vocoder = 0;   // pointer
        this._voiceBuf = 0;  // pointer to float[128]
        this._carrierBuf = 0;
        this._outBuf = 0;

        // Internal carrier oscillator
        this._carrierKind = 'saw';
        this._carrierFreq = 110;
        this._carrierPhase = 0;
        this._carrierPhase2 = 0; // for richer harmonics
        this._carrierPhase3 = 0;

        this._frameCount = 0;
        this._envData = new Float32Array(NBANDS);

        this.port.onmessage = (e) => this._onMessage(e.data);
    }

    _onMessage(msg) {
        switch (msg.type) {
            case 'init':
                this._initWasm(msg.wasmBinary);
                break;
            case 'param':
                this._setParam(msg.name, msg.value);
                break;
            case 'carrier':
                if (msg.kind) this._carrierKind = msg.kind;
                if (msg.freq !== undefined) this._carrierFreq = msg.freq;
                break;
            case 'reset':
                if (this._ready && this._wasm) {
                    this._wasm._wasm_vocoder_reset(this._vocoder);
                }
                break;
        }
    }

    async _initWasm(wasmBinary) {
        try {
            // AudioWorkletGlobalScope does NOT have fetch(), so Emscripten's
            // default WASM loading (streaming compile via fetch) will fail.
            // We bypass it entirely by providing instantiateWasm, which
            // manually calls WebAssembly.instantiate with the binary that
            // the main thread already fetched and transferred to us.
            const moduleArgs = {
                instantiateWasm: (imports, successCallback) => {
                    WebAssembly.instantiate(wasmBinary, imports)
                        .then(result => {
                            successCallback(result.instance, result.module);
                        })
                        .catch(err => {
                            console.error('[VocoderProcessor] WASM instantiate failed:', err);
                        });
                    return {};
                }
            };

            const mod = await VocoderModule(moduleArgs);
            this._wasm = mod;

            // Create vocoder instance
            const fs = sampleRate || SAMPLE_RATE;
            this._vocoder = mod._wasm_vocoder_create(fs);

            // Allocate float buffers in WASM memory (128 * 4 bytes each)
            const bufSize = RENDER_QUANTUM * 4;
            this._voiceBuf = mod._malloc(bufSize);
            this._carrierBuf = mod._malloc(bufSize);
            this._outBuf = mod._malloc(bufSize);

            this._ready = true;
            this.port.postMessage({ type: 'ready' });
        } catch (err) {
            console.error('[VocoderProcessor] WASM init failed:', err);
        }
    }

    _setParam(name, value) {
        if (!this._ready || !this._wasm) return;
        const w = this._wasm;
        const v = this._vocoder;
        switch (name) {
            case 'wet_dry':
                w._wasm_vocoder_set_wet_dry(v, value[0], value[1]);
                break;
            case 'output_gain':
                w._wasm_vocoder_set_output_gain(v, value);
                break;
            case 'attack_release':
                w._wasm_vocoder_set_attack_release(v, value[0], value[1], value[2]);
                break;
            case 'sibilance':
                w._wasm_vocoder_set_sibilance(v, value);
                break;
            case 'preemphasis':
                w._wasm_vocoder_set_preemphasis(v, value);
                break;
        }
    }

    /**
     * Generate one block of carrier signal into the given Float32Array.
     */
    _generateCarrier(buffer, nframes) {
        const fs = sampleRate || SAMPLE_RATE;
        const f1 = this._carrierFreq;
        const f2 = f1 * 2;
        const f3 = f1 * 3;
        const kind = this._carrierKind;
        const twoPi = 2 * Math.PI;

        for (let i = 0; i < nframes; i++) {
            let s = 0;
            if (kind === 'saw') {
                // Naive saw with 3 harmonics for richness
                const p1 = this._carrierPhase;
                const p2 = this._carrierPhase2;
                const p3 = this._carrierPhase3;
                s = 0.4 * (2 * p1 - 1) + 0.3 * (2 * p2 - 1) + 0.2 * (2 * p3 - 1);
            } else if (kind === 'square') {
                s = (this._carrierPhase < 0.5 ? 0.55 : -0.55) +
                    (this._carrierPhase3 < 0.5 ? 0.18 : -0.18);
            } else { // sine
                s = 0.6 * Math.sin(twoPi * this._carrierPhase) +
                    0.25 * Math.sin(twoPi * this._carrierPhase2);
            }
            buffer[i] = s;

            this._carrierPhase += f1 / fs;
            this._carrierPhase2 += f2 / fs;
            this._carrierPhase3 += f3 / fs;
            if (this._carrierPhase >= 1) this._carrierPhase -= 1;
            if (this._carrierPhase2 >= 1) this._carrierPhase2 -= 1;
            if (this._carrierPhase3 >= 1) this._carrierPhase3 -= 1;
        }
    }

    process(inputs, outputs, _parameters) {
        if (!this._ready || !this._wasm) {
            return true; // keep alive while loading
        }

        const mod = this._wasm;
        const nframes = RENDER_QUANTUM;

        // ── Voice input (input 0, channel 0) ────────────────────────
        const voiceInput = inputs[0];
        const voiceData = (voiceInput && voiceInput[0]) ? voiceInput[0] : null;

        const voiceHeap = new Float32Array(
            mod.HEAPF32.buffer, this._voiceBuf, nframes
        );
        if (voiceData && voiceData.length >= nframes) {
            voiceHeap.set(voiceData);
        } else {
            voiceHeap.fill(0);
        }

        // ── Carrier (generated internally) ──────────────────────────
        const carrierHeap = new Float32Array(
            mod.HEAPF32.buffer, this._carrierBuf, nframes
        );
        const tempCarrier = new Float32Array(nframes);
        this._generateCarrier(tempCarrier, nframes);
        carrierHeap.set(tempCarrier);

        // ── Process ─────────────────────────────────────────────────
        mod._wasm_vocoder_process(
            this._vocoder,
            this._voiceBuf,
            this._carrierBuf,
            this._outBuf,
            nframes
        );

        // ── Copy output ─────────────────────────────────────────────
        const outHeap = new Float32Array(
            mod.HEAPF32.buffer, this._outBuf, nframes
        );
        const output = outputs[0];
        if (output && output[0]) {
            output[0].set(outHeap);
            // Copy to second channel if stereo
            if (output[1]) output[1].set(outHeap);
        }

        // ── Send envelopes to UI thread periodically ────────────────
        this._frameCount += nframes;
        if (this._frameCount >= ENV_SEND_INTERVAL) {
            this._frameCount = 0;
            for (let k = 0; k < NBANDS; k++) {
                this._envData[k] = mod._wasm_vocoder_get_env(this._vocoder, k);
            }
            this.port.postMessage(
                { type: 'env', data: this._envData },
                [/* no transfer — small buffer */]
            );
        }

        return true;
    }
}

registerProcessor('vocoder-processor', VocoderProcessor);
