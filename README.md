# PicoPunk Vocoder

PicoPunk is a low-cost, high-performance 20-band digital vocoder originally targeted for the RP2350 microcontroller. It features a custom C-based DSP core designed for efficiency and zero-latency processing.

This repository also includes a **WebAssembly (WASM)** port of the DSP core, allowing the vocoder to run entirely in the browser using the Web Audio API's `AudioWorklet` for real-time, low-latency audio processing.

## 🌟 Live Demo

The Web UI is automatically built and deployed via GitHub Pages:
**[Play with the PicoPunk Web Vocoder here!](https://ovelhaaa.github.io/picopunk/)**

*(Note: Requires microphone access. Works best on desktop browsers like Chrome or Edge).*

---

## 🛠 Features

- **20-Band Analog-Modeled Filter Bank**: Utilizes Transposed Direct Form II biquads for precise bandpass and highpass filtering.
- **Envelope Followers**: Independent attack/release envelope tracking per band.
- **Sibilance Detection**: Analyzes high-frequency energy to inject noise bursts for clear consonants ("S", "T", "Ch" sounds).
- **Built-in Carrier Oscillator**: Generates saw, square, and sine waves for testing without an external synthesizer.
- **High-Performance**: Written in pure C11, compiled with `-O3 -ffast-math` for both embedded targets and WebAssembly.
- **Real-Time Browser Processing**: Runs in an isolated audio thread using `AudioWorklet`, preventing UI-induced audio stuttering.

---

## 📂 Project Structure

- `src/dsp/`: The core C DSP library (`vocoder.c`, `vocoder.h`) — completely platform agnostic.
- `src/wasm/`: The WebAssembly bridge mapping the C functions for JavaScript consumption.
- `web/`: The frontend Vanilla HTML, CSS, and JS files. Features a responsive neon-dark UI.
- `build_wasm.sh` / `build_wasm.bat`: Scripts to compile the DSP core into WebAssembly using Emscripten.

---

## ⚙️ Building the WebAssembly Module

To build the WASM binary locally, you need the [Emscripten SDK (emsdk)](https://emscripten.org/docs/getting_started/downloads.html) installed.

### Windows
```cmd
# Make sure Emscripten is activated
# C:\emsdk\emsdk_env.bat

build_wasm.bat
```

### Linux / macOS
```bash
# Make sure Emscripten is activated
# source /path/to/emsdk/emsdk_env.sh

./build_wasm.sh
```

This will output `vocoder.wasm` and `vocoder.js` directly into the `web/` directory.

---

## 🚀 Running Locally

Because the Web Audio API and WASM fetching require a secure context, you cannot just open `index.html` from the file system. You need a local web server.

```bash
# Using Node.js
npx serve web

# Or using Python
cd web && python -m http.server 8000
```
Then navigate to `http://localhost:8000`.

---

## 📄 License
MIT License.
