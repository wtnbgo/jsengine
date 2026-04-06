# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**jsengine** is a C++17 cross-platform application using SDL3 with OpenGL ES 3.0 rendering and an embedded duktape JavaScript engine with WebGL 2.0 compatible bindings. It uses the SDL3 callback-based application model (SDL_AppInit/SDL_AppIterate/SDL_AppEvent/SDL_AppQuit). JavaScript code in `main.js` drives the rendering and input handling via browser-compatible APIs.

## Build System

CMake with presets + Ninja Multi-Config generator. Dependencies are managed via vcpkg (glm, duktape) and FetchContent (SDL3, SDL3_image).

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
- `src/jsengine.hpp` / `src/jsengine.cpp` — `JsEngine` class. Manages the duktape heap, JS file loading (via SDL_LoadFile), lifecycle calls (update/render/done), and browser-compatible input event dispatch (addEventListener/removeEventListener).
- `src/dukwebgl.h` / `src/dukwebgl.cpp` — WebGL 2.0 compatible bindings mapping to GLES 3.0. Registers `gl` global and `WebGL2RenderingContext`.
- `glad/` — GLAD loader for OpenGL ES 3.0 (local subdirectory, built as a CMake sub-project).

## Key Technical Details

- Renders via OpenGL ES 3.0 (not desktop GL) — use GLES-compatible API calls.
- GLAD is loaded via `SDL_GL_GetProcAddress`; do not use platform-specific GL loaders.
- SDL3 shared libraries are copied to the build output directory as a post-build step.
- On mobile platforms (iOS/Android), the window is created fullscreen; on desktop, it's resizable.
- JS files are loaded from the base path (default: `data/`, changeable via `-data <path>` CLI option). All relative paths in `loadScript()` and `fs.*` APIs resolve from this base path.
- JS lifecycle: `data/main.js` loaded at init → `update(dt)` and `render()` called each frame → `done()` at quit.
- Input events (keyboard, mouse, touch, wheel) are converted from SDL3 to browser-compatible event objects and dispatched via `addEventListener`.
- The vcpkg duktape package name is `unofficial-duktape` (target: `unofficial::duktape::duktape`).
- Comments in the codebase are in Japanese.
- `manual.js` contains the full API reference for the JS environment.
