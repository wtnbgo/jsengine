# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**jsengine** is a C++17 cross-platform application using SDL3 with OpenGL ES 3.0 rendering. It uses the SDL3 callback-based application model (SDL_AppInit/SDL_AppIterate/SDL_AppEvent/SDL_AppQuit).

## Build System

CMake with presets + Ninja Multi-Config generator. Dependencies are managed via vcpkg (glm) and FetchContent (SDL3, SDL3_image).

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

- `src/main.cpp` — SDL3 callback entry point. Manages the App lifecycle and delta-time calculation. Supports `-debug` and `-quiet` CLI flags for log level.
- `src/app.hpp` / `src/app.cpp` — `App` singleton class owning the SDL window and OpenGL ES context. Provides `init()`, `update(delta)`, and `draw()` methods.
- `glad/` — GLAD loader for OpenGL ES 2.0/3.0 (local subdirectory, built as a CMake sub-project).

## Key Technical Details

- Renders via OpenGL ES 3.0 (not desktop GL) — use GLES-compatible API calls.
- GLAD is loaded via `SDL_GL_GetProcAddress`; do not use platform-specific GL loaders.
- SDL3 shared libraries are copied to the build output directory as a post-build step.
- On mobile platforms (iOS/Android), the window is created fullscreen; on desktop, it's resizable.
- Comments in the codebase are in Japanese.
