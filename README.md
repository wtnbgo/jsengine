# jsengine

A cross-platform application built on SDL3 + OpenGL ES 3.0, integrating the QuickJS-ng JavaScript engine (ES2023) with WebGL 2.0 compatible bindings. Write rendering logic in JavaScript using browser-compatible WebGL APIs.

## Prerequisites

- CMake 3.22+
- Ninja
- vcpkg (with `VCPKG_ROOT` environment variable set)
- C++17 compiler (MSVC, GCC, or Clang)

## Build

```bash
# Configure (first time or after CMakeLists.txt changes)
make prebuild

# Build
make build                       # Release
make build BUILD_TYPE=Debug      # Debug

# Run (Windows)
make run
```

The preset is auto-selected based on the OS (`x64-windows` / `x64-linux` / `x64-macos`).

### Launch Options

```bash
jsengine                    # Load main.js from data/ folder
jsengine -data path/to/dir  # Load main.js from specified folder
jsengine -debug             # Enable debug logging
jsengine -quiet             # Warnings and errors only
```

## Dependencies

| Library | Source | Purpose |
|---------|--------|---------|
| SDL3 | FetchContent | Window management, input, file I/O |
| SDL3_image | FetchContent | Image loading (BMP, JPG, PNG) |
| GLAD | Local (glad/) | OpenGL ES 3.0 loader |
| QuickJS-ng | vcpkg | JavaScript engine (ES2023) |
| glm | vcpkg | Math library |
| miniaudio 0.11.25 | Local (src/audio/) | Audio engine (WAV, MP3, FLAC, OGG) |
| ThorVG | FetchContent | 2D vector graphics (Canvas 2D API) |
| FreeType | vcpkg | Font rasterization |
| HarfBuzz | FetchContent | Text shaping |
| libvorbis / libopus | vcpkg (optional) | OGG Vorbis / Opus audio decoding |

## Architecture

### C++ Side

- `src/main.cpp` — SDL3 callback entry point (SDL_AppInit / SDL_AppIterate / SDL_AppEvent / SDL_AppQuit). Routes input events to JsEngine via App.
- `src/app.hpp / app.cpp` — `App` singleton. Manages the SDL window, GL context, and owns JsEngine.
- `src/jsengine.hpp / jsengine.cpp` — `JsEngine` class. Manages the duktape heap, JS file loading/execution, and event dispatch.
- `src/dukwebgl.h / dukwebgl.cpp` — WebGL 2.0 compatible bindings (based on GLES 3.0).
- `src/webaudio.h / webaudio.cpp` — Web Audio API bindings.
- `src/canvas2d.h / canvas2d.cpp` — Canvas 2D API bindings (ThorVG-based).
- `src/audio/` — AudioEngine / AudioStream (miniaudio + SDL3 audio).

### JavaScript Lifecycle

At startup, `main.js` is loaded and executed from the base path (default: `data/`). The base path can be changed with the `-data` option. All relative paths in `loadScript()` and `fs.*` APIs are resolved from this base path.

Define the following global functions to receive callbacks from the C++ side:

| Function | Timing | Arguments |
|----------|--------|-----------|
| `update(dt)` | Every frame | Elapsed milliseconds since last frame |
| `render()` | Every frame (after update) | None |
| `done()` | On app quit | None |

### Available JS APIs

- **`gl`** — WebGL2RenderingContext compatible object (global)
- **`console.log()` / `console.error()`** — Output to SDL log
- **`loadScript(path)`** — Load and execute additional JS files
- **`addEventListener(type, callback)`** — Register browser-compatible event listeners
- **`removeEventListener(type, callback)`** — Remove event listeners
- **`fs`** — File System Access API (`readText`, `writeText`, `getFileHandle`, `getDirectoryHandle`, `exists`, `stat`, `mkdir`, `remove`, `rename`)
- **`new AudioContext()`** — Web Audio API (`createBufferSource`, master volume)
- **`new Canvas2D(w, h)`** — Canvas 2D API (bitmap-retained; rectangles, paths, text, drawImage, getImageData/putImageData, transforms, GL texture output)
- **`Canvas2D.loadFont(path)`** — Load font file for ThorVG
- **`createImageBitmap(path)`** — Load image as RGBA pixel data

### Input Events

Input is received via the browser-standard `addEventListener` pattern. SDL3 events are converted to browser-compatible event objects.

| Event | Description | Key Properties |
|-------|-------------|----------------|
| `keydown` / `keyup` | Keyboard | `key`, `code`, `keyCode`, `altKey`, `ctrlKey`, `shiftKey`, `metaKey`, `repeat` |
| `mousedown` / `mouseup` / `mousemove` | Mouse | `clientX`, `clientY`, `button`, `buttons`, `movementX`, `movementY`, modifier keys |
| `wheel` | Mouse wheel | `deltaX`, `deltaY`, `deltaZ`, `deltaMode`, `clientX`, `clientY`, modifier keys |
| `touchstart` / `touchmove` / `touchend` / `touchcancel` | Touch | `touches[]`, `changedTouches[]` (each: `identifier`, `clientX`, `clientY`, `force`) |

### WebGL Binding Coverage

Covers the major WebGL 2.0 APIs including: shaders/programs, buffers (VBO/UBO), textures (2D/3D/CubeMap), framebuffers/renderbuffers, VAO, uniforms (scalar/vector/matrix), drawing (including instanced), state management, clearBuffer, Transform Feedback, Query, and Sampler.

## API Reference

See `manual.js` for a complete API listing in JavaScript-style documentation.

## Demos

`data/main.js` includes 8 demos switchable by key press.

Demo 1 displays a HUD overlay showing controls, demo list, and system information.

| Key | Demo |
|-----|------|
| **1** | Vertex-colored triangle (WASD movement, wheel alpha, HUD overlay) |
| **2** | Canvas2D shapes (rectangles, circles, Bezier curves, transparency) |
| **3** | Canvas2D text (multiple fonts, sizes, colors) |
| **4** | Canvas2D animation (rotating shapes, orbital circles) |
| **5** | pixi.js v5 test (Graphics drawing, animation) |
| **6** | Canvas2D drawImage / getImageData / putImageData test |
| **7** | Canvas2D dirty rect partial update test |
| **8** | three.js r128 test (cube, sphere, floor plane) |
| **Space** | Play beep sound |
| **R** | Reset |

Use `-demo N` to select the initial demo mode at launch.

Place font files in `data/fonts/` (e.g., OpenSans, Roboto).

## License

This project is licensed under the [MIT License](LICENSE).

### Third-Party Licenses

| Library | License | Notes |
|---------|---------|-------|
| [SDL3](https://github.com/libsdl-org/SDL) | zlib License | Window management, input, file I/O |
| [SDL3_image](https://github.com/libsdl-org/SDL_image) | zlib License | Image loading |
| [QuickJS-ng](https://github.com/quickjs-ng/quickjs) | MIT License | JavaScript engine (ES2023, via vcpkg) |
| [GLAD](https://github.com/Dav1dde/glad) | MIT License / Public Domain | OpenGL ES 3.0 loader (bundled in glad/) |
| [miniaudio](https://miniaud.io/) | MIT-0 / Public Domain | Audio engine (bundled in src/audio/) |
| [ThorVG](https://www.thorvg.org/) | MIT License | 2D vector graphics |
| [glm](https://github.com/g-truc/glm) | MIT License | Math library |
| [FreeType](https://freetype.org/) | FreeType License (BSD-style) | Font rasterization |
| [HarfBuzz](https://harfbuzz.github.io/) | MIT License | Text shaping |
| [libvorbis](https://xiph.org/vorbis/) | BSD License | Vorbis audio decoding (optional) |
| [libogg](https://xiph.org/ogg/) | BSD License | Ogg container format (optional) |
| [opusfile](https://opus-codec.org/) | BSD License | Opus audio decoding (optional) |
| [pixi.js](https://pixijs.com/) v5.3.12 | MIT License | 2D renderer (bundled in data/lib/) |
| [three.js](https://threejs.org/) r128 | MIT License | 3D graphics (bundled in data/lib/) |

EGL/KHR headers are provided by The Khronos Group under the Apache-2.0 License.
