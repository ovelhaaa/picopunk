#!/usr/bin/env bash
# build_wasm.sh — Compile the PicoPunk vocoder to WebAssembly.
#
# Usage:
#   ./build_wasm.sh          # normal build
#   ./build_wasm.sh clean    # remove generated artefacts
#
# Prerequisites: emcc must be on $PATH (source emsdk_env.sh first,
# or let the GitHub Actions workflow handle it).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$SCRIPT_DIR/src"
OUT_DIR="$SCRIPT_DIR/web"

SOURCES=(
    "$SRC_DIR/dsp/vocoder.c"
    "$SRC_DIR/wasm/vocoder_wasm.c"
)

OUT_JS="$OUT_DIR/vocoder.js"
OUT_WASM="$OUT_DIR/vocoder.wasm"

# ── clean ──────────────────────────────────────────────────────────
if [[ "${1:-}" == "clean" ]]; then
    rm -f "$OUT_JS" "$OUT_WASM"
    echo "Cleaned."
    exit 0
fi

# ── build ──────────────────────────────────────────────────────────
mkdir -p "$OUT_DIR"

EXPORTED_FUNCTIONS="[
  '_malloc','_free',
  '_wasm_vocoder_create',
  '_wasm_vocoder_destroy',
  '_wasm_vocoder_reset',
  '_wasm_vocoder_set_wet_dry',
  '_wasm_vocoder_set_output_gain',
  '_wasm_vocoder_set_attack_release',
  '_wasm_vocoder_set_sibilance',
  '_wasm_vocoder_set_preemphasis',
  '_wasm_vocoder_process',
  '_wasm_vocoder_get_nbands',
  '_wasm_vocoder_get_env',
  '_wasm_vocoder_sizeof'
]"

# Remove whitespace from the exported functions list
EXPORTED_FUNCTIONS="$(echo "$EXPORTED_FUNCTIONS" | tr -d '[:space:]')"

echo "Building WASM..."
emcc \
    -std=c11 \
    -O3 \
    -ffast-math \
    -flto \
    -s MODULARIZE=1 \
    -s EXPORT_NAME="VocoderModule" \
    -s EXPORTED_FUNCTIONS="$EXPORTED_FUNCTIONS" \
    -s EXPORTED_RUNTIME_METHODS="['ccall','cwrap','getValue','setValue']" \
    -s ALLOW_MEMORY_GROWTH=0 \
    -s INITIAL_MEMORY=1048576 \
    -s ENVIRONMENT='web,worker' \
    -s FILESYSTEM=0 \
    -s ASSERTIONS=0 \
    -s MALLOC=emmalloc \
    --no-entry \
    "${SOURCES[@]}" \
    -o "$OUT_JS"

echo "Built: $OUT_JS ($(wc -c < "$OUT_JS") bytes)"
echo "Built: $OUT_WASM ($(wc -c < "$OUT_WASM") bytes)"
echo "Done."
