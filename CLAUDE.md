# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**jsengine** is a C++17 cross-platform application using SDL3 with OpenGL ES 3.0 rendering and an embedded QuickJS-ng JavaScript engine (ES2023). It provides browser-compatible APIs including WebGL 2.0, Canvas 2D (ThorVG), Web Audio (miniaudio), Web Storage, File System Access, and input events. It uses the SDL3 callback-based application model (SDL_AppInit/SDL_AppIterate/SDL_AppEvent/SDL_AppQuit).

## Build System

CMake with presets + Ninja Multi-Config generator. Dependencies are managed via vcpkg (glm, quickjs-ng, freetype, miniaudio, libvorbis, libopus) and FetchContent (SDL3, SDL3_image, ThorVG, HarfBuzz).

**Prerequisites:** vcpkg installed with `VCPKG_ROOT` environment variable set.

### Common Commands

```bash
# Configure (first time or after CMakeLists.txt changes)
make prebuild                    # auto-detects OS preset

# Build
make build                       # default: Release
make build BUILD_TYPE=Debug

# Run (Windows only via Makefile)
make run

# Clean
make clean
```

The Makefile auto-selects the preset based on OS: `x64-windows`, `x64-linux`, or `x64-macos`. Override with `PRESET=<name>`.

### Available CMake Presets

- `x64-windows` — Windows desktop (MSVC, static vcpkg linkage)
- `x64-linux` — Linux desktop
- `arm64-android` / `x64-android` — Android builds (shared SDL3)

### Direct CMake Usage

```bash
cmake --preset x64-windows
cmake --build build/x64-windows --config Release
```

## Architecture

- `src/main.cpp` — SDL3 callback entry point. Manages the App lifecycle, delta-time calculation, and routes SDL events to App. Supports `-debug` and `-quiet` CLI flags for log level.
- `src/app.hpp` / `src/app.cpp` — `App` singleton class owning the SDL window, OpenGL ES context, and JsEngine. Provides `init()`, `update(delta)`, `render()`, and `handleEvent()` methods.
- `src/jsengine.hpp` / `src/jsengine.cpp` — `JsEngine` class. Manages the QuickJS runtime/context, JS file loading (via SDL_LoadFile), lifecycle calls (update/render/done), and browser-compatible input event dispatch (addEventListener/removeEventListener).
- `src/dukwebgl.h` / `src/dukwebgl.cpp` — WebGL 2.0 compatible bindings mapping to GLES 3.0. Registers `gl` global and `WebGL2RenderingContext`.
- `src/webaudio.h` / `src/webaudio.cpp` — Web Audio API bindings. Uses AudioEngine/AudioStream for playback.
- `src/canvas2d.h` / `src/canvas2d.cpp` — Canvas 2D API bindings using ThorVG SwCanvas. Bitmap-retained mode with deferred rendering: draw ops are batched and rendered to pixel buffer on flush/texture access/getImageData. drawImage uses ThorVG Picture. Dirty rect tracking for partial GL texture upload.
- `src/audio/` — AudioEngine (miniaudio singleton with sound groups) and AudioStream (file/memory/stream decoding with SDL3 I/O). Supports WAV, MP3, FLAC, and optionally OGG Vorbis/Opus.
- `glad/` — GLAD loader for OpenGL ES 3.0 (local subdirectory, built as a CMake sub-project).

## Key Technical Details

- Renders via OpenGL ES 3.0 (not desktop GL) — use GLES-compatible API calls.
- GLAD is loaded via `SDL_GL_GetProcAddress`; do not use platform-specific GL loaders.
- SDL3 shared libraries are copied to the build output directory as a post-build step.
- On mobile platforms (iOS/Android), the window is created fullscreen; on desktop, it's resizable.
- JS files are loaded from the base path (default: `data/`, changeable via `-data <path>` CLI option). All relative paths in `loadScript()` and `fs.*` APIs resolve from this base path.
- JS lifecycle: `data/main.js` は ES Module として読み込まれる（top-level await 対応）。`update(dt)` / `render()` / `done()` は `globalThis` に明示登録が必要（モジュールスコープのため）。
- Input events (keyboard, mouse, touch, wheel) are converted from SDL3 to browser-compatible event objects and dispatched via `addEventListener`.
- QuickJS-ng is installed via vcpkg (`quickjs-ng`). CMake target: `qjs`.
- Comments in the codebase are in Japanese.
- `manual.js` contains the full API reference for the JS environment.
- QuickJS-ng は ES2023 対応。duktape 時代に必要だった ES6 ポリフィル（Promise, Map, Set, WeakMap 等）は不要。polyfill.js は最小限のみ。
- ESM (ES Modules) 対応: `loadModule(path)` で ESM ファイルを読み込み、export された名前空間オブジェクトを返す。`JS_SetModuleLoaderFunc` によりモジュール間の `import` も動作する。TLA (Top-Level Await) 使用モジュールは未対応（課題）。
- three.js r176 は ESM 版（three.module.min.js + three.core.min.js）を `loadModule()` で読み込み。Babel トランスパイル不要。
- pixi.js v7.4.3 動作確認済み（UMD 版、data/lib/ に polyfill.js, browser_shim.js, pixi.min.js）。pixi.js v8 は ESM+TLA で読み込み可能だがバッチレンダラーのジオメトリ更新に問題あり（課題）。
- pixi.js v4.5.4（RPG Maker MV）は test/ で作業中。OES_vertex_array_object 拡張マッピング、CanvasRenderingContext2D シム等を追加済み。
- `qjs_get_buffer()` は TypedArray の byteOffset を正しく処理する。
- `getParameter()` は配列型（VIEWPORT 等）を JS Array で返す。
- `getExtension()` は GLES3 標準拡張に対して機能オブジェクトを返す（VAO 拡張メソッド含む）。
- Demo 1 に Canvas2D ベースの HUD オーバーレイ（操作説明・デモ一覧・システム情報）を表示。
