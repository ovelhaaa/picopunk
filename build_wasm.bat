@echo off
REM build_wasm.bat — Compile the PicoPunk vocoder to WebAssembly (Windows).
REM
REM Prerequisites: emsdk must be installed at C:\emsdk (or adjust EMSDK below).

setlocal enabledelayedexpansion

set "EMSDK=C:\emsdk"
set "SCRIPT_DIR=%~dp0"
set "SRC_DIR=%SCRIPT_DIR%src"
set "OUT_DIR=%SCRIPT_DIR%web"

REM ── activate emsdk ──────────────────────────────────────────────
call "%EMSDK%\emsdk_env.bat" >nul 2>&1

REM ── clean ───────────────────────────────────────────────────────
if "%~1"=="clean" (
    del /q "%OUT_DIR%\vocoder.js" 2>nul
    del /q "%OUT_DIR%\vocoder.wasm" 2>nul
    echo Cleaned.
    exit /b 0
)

REM ── build ───────────────────────────────────────────────────────
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

set EXPORTED_FUNCTIONS=['_malloc','_free','_wasm_vocoder_create','_wasm_vocoder_destroy','_wasm_vocoder_reset','_wasm_vocoder_set_wet_dry','_wasm_vocoder_set_output_gain','_wasm_vocoder_set_attack_release','_wasm_vocoder_set_sibilance','_wasm_vocoder_set_preemphasis','_wasm_vocoder_process','_wasm_vocoder_get_nbands','_wasm_vocoder_get_env','_wasm_vocoder_sizeof']

echo Building WASM...
call emcc ^
    -std=c11 ^
    -O3 ^
    -ffast-math ^
    -flto ^
    -s MODULARIZE=1 ^
    -s EXPORT_NAME="VocoderModule" ^
    -s EXPORTED_FUNCTIONS="%EXPORTED_FUNCTIONS%" ^
    -s EXPORTED_RUNTIME_METHODS="['ccall','cwrap','getValue','setValue']" ^
    -s ALLOW_MEMORY_GROWTH=0 ^
    -s INITIAL_MEMORY=1048576 ^
    -s ENVIRONMENT="web,worker" ^
    -s FILESYSTEM=0 ^
    -s ASSERTIONS=0 ^
    -s MALLOC=emmalloc ^
    --no-entry ^
    "%SRC_DIR%\dsp\vocoder.c" ^
    "%SRC_DIR%\wasm\vocoder_wasm.c" ^
    -o "%OUT_DIR%\vocoder.js"

if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)

echo Done.
endlocal
