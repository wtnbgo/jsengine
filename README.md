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

### Developer Options

```bash
# Build using a local ../thorvg checkout instead of the FetchContent fork
cmake --preset x64-windows -DUSE_LOCAL_THORVG=ON
```

`USE_LOCAL_THORVG=ON` uses `../thorvg/CMakeLists.txt` via `add_subdirectory`, intended for iterating on ThorVG patches without a push/tag-bump round-trip. Default OFF retains the FetchContent fetch from `wtnbgo/thorvg` (branch `cmake`).

### Packaging / Release (Windows)

```bash
make package                     # build (if needed) + produce dist/jsengine-<version>-win64.zip
make package VERSION=1.0.0        # pin the version string explicitly
```

`make package` runs `tools/package_win.ps1`, which assembles a self-contained release ZIP whose root holds `jsengine.exe`, the bundled `SDL3.dll` / `SDL3_image.dll`, `README.md`, `manual.js`, and the `data/` folder (the large `data/title.webm` demo video is excluded). The script is the single source of truth for the layout so local and CI packaging stay identical.

CI: pushing a `v*` tag (e.g. `v0.1.0`) triggers `.github/workflows/release-win.yml`, which builds on `windows-latest`, runs the same packaging script, uploads the ZIP as a build artifact, and publishes it to **GitHub Releases**. Regular pushes do nothing; the workflow can also be run manually from the Actions tab. Only the Windows pipeline exists for now — other platforms will be added once validated on their own hosts.

#### Versioning

The version's single source of truth is the root **`VERSION`** file (semver, e.g. `0.1.0`). `CMakeLists.txt` reads it at configure time (no hardcoded version), and the packaging script defaults to it. Bump everything in one step:

```bash
make bump VERSION=0.2.0     # rewrites VERSION + vcpkg.json (CMake reads VERSION automatically)
git add VERSION vcpkg.json && git commit -m "Bump version to 0.2.0"
git tag v0.2.0 && git push origin main --tags     # → CI builds and publishes the Release
```

The release workflow refuses to run if the pushed `v*` tag doesn't match the `VERSION` file, so a forgotten bump fails fast instead of shipping a mislabeled build. CMake also warns at configure time if `vcpkg.json`'s version drifts from `VERSION`.

### Launch Options

```bash
jsengine                       # Load main.js from data/ folder
jsengine -data path/to/dir     # Load main.js from specified folder
jsengine -sysinit path/to/file # Override built-in sysinit.js with an external file (dev convenience)
jsengine -rpgmv path/to/proj   # Boot a RPG Maker MV project directly using the built-in rpgmv_main.js
jsengine -repl                 # Enable interactive REPL on stdin (type JS, get results)
jsengine -replfile <dir>       # Enable file-channel REPL (write JS to <dir>/cmd, read JSON from <dir>/resp) — for AI / agent control
jsengine -debug                # Enable debug logging
jsengine -quiet                # Warnings and errors only
```

`-repl` / `-replfile` need the CMake option `JSENGINE_USE_REPL=ON` (default). Set OFF for embedded builds without stdin or to shrink the binary. The file-channel protocol (`<dir>/cmd` written via `cmd.tmp` → rename, response in `<dir>/resp` as `{"ok":bool,"result":"...","error":"..."}`) is designed so an external AI agent can drive jsengine end-to-end: evaluate JS, take screenshots via `captureScreen("foo.png")` (global), and write arbitrary bytes via `fs.writeBinary(path, buf)`.

`-rpgmv` boots the built-in RPG Maker MV bootstrap (`src/rpgmv_main.js`) instead of `main.js`, with the supplied folder used as the data path. Use it to run any standard MV project (containing `data/System.json`, `js/`, `audio/`, `img/`, etc.) without copying a custom `main.js` into the project folder. Per-game save data is isolated under `%APPDATA%/jsengine_rpgmv/<gameTitle>/` (via `localStorage.setPath`).

## Dependencies

| Library | Source | Purpose |
|---------|--------|---------|
| SDL3 | FetchContent | Window management, input, file I/O |
| SDL3_image | FetchContent | Image loading (BMP, JPG, PNG) |
| GLAD | Local (glad/) | OpenGL ES 3.0 loader |
| QuickJS-ng | vcpkg | JavaScript engine (ES2023) |
| miniaudio 0.11.25 | Local (src/audio/) | Audio engine (WAV, MP3, FLAC, OGG) |
| ThorVG | FetchContent | 2D vector graphics (Canvas 2D API) |
| FreeType / HarfBuzz | vcpkg | Multilingual text shaping (ThorVG FT loader) |
| zlib / libpng | vcpkg | Shared between SDL3_image and FreeType to avoid vendored conflicts |
| libvorbis / libopus | vcpkg | OGG Vorbis / Opus audio decoding (also used by movie-player) |
| libvpx | vcpkg | VP8 / VP9 video decoding (movie-player, optional via `JSENGINE_USE_MOVIE_PLAYER`) |
| movie-player | FetchContent | WebM playback (wamsoft/movie-player) — VP8/VP9 + Vorbis/Opus via nestegg demux |

## Architecture

### C++ Side

- `src/main.cpp` — SDL3 callback entry point (SDL_AppInit / SDL_AppIterate / SDL_AppEvent / SDL_AppQuit). Routes input events to JsEngine via App.
- `src/app.hpp / app.cpp` — `App` singleton. Manages the SDL window, GL context, and owns JsEngine.
- `src/jsengine.hpp / jsengine.cpp` — `JsEngine` class. Manages the QuickJS runtime/context, JS file loading/execution, and event dispatch.
- `src/webgl.h / webgl.cpp` — WebGL 2.0 compatible bindings (based on GLES 3.0).
- `src/webaudio.h / webaudio.cpp` — Web Audio API bindings.
- `src/canvas2d.h / canvas2d.cpp` — Canvas 2D API bindings (ThorVG-based).
- `src/audio/` — AudioEngine / AudioStream (miniaudio + SDL3 audio).
- `src/sysinit.js` — Built-in browser shim (window / document / HTMLCanvasElement / HTMLVideoElement / HTMLAudioElement / Image / XMLHttpRequest / fetch / document.fonts etc.). Embedded into the binary via CMake (`cmake/embed_js.cmake`) and auto-evaluated before `main.js`. Override at runtime with `-sysinit <path>` (skip rebuild while iterating on the shim).
- `src/rpgmv_main.js` — Built-in RPG Maker MV bootstrap (pixi.js v4 loader, `rpg_core/managers/objects/scenes/sprites/windows/system` loader, `Canvas2D.loadFont` for GameFont, `localStorage.setPath` for per-game save isolation, `SceneManager` startup). Embedded via the same `cmake/embed_js.cmake` and evaluated when `-rpgmv <project>` is given.

### JavaScript Lifecycle

At startup, `main.js` is loaded as an **ES Module** from the base path (default: `data/`). The base path can be changed with the `-data` option. All relative paths in `loadScript()` and `fs.*` APIs are resolved from this base path.

Since `main.js` runs as a module, **top-level `await` is supported**. You can use `await import(...)` or `await` any Promise directly at the top level.

**Important:** ES Modules have their own scope — functions and variables declared at the top level are NOT automatically visible to the C++ engine. Lifecycle functions must be explicitly registered on `globalThis`:

```js
function update(dt) { /* ... */ }
function render() { /* ... */ }
function done() { /* ... */ }

// Register lifecycle functions for C++ callbacks
globalThis.update = update;
globalThis.render = render;
globalThis.done = done;
```

| Function | Timing | Arguments |
|----------|--------|-----------|
| `globalThis.update(dt)` | Every frame | Elapsed milliseconds since last frame |
| `globalThis.render()` | Every frame (after update) | None |
| `globalThis.done()` | On app quit | None |

### Available JS APIs

- **`gl`** — WebGL2RenderingContext compatible object (global)
- **`console.log()` / `console.error()`** — Output to SDL log
- **`loadScript(path)`** — Load and execute additional JS files (global scope)
- **`loadModule(path)`** — Load an ES Module and return its namespace (exports)
- **`awaitPromise(promise)`** — Synchronously resolve a Promise (for use in non-module context)
- **`addEventListener(type, callback)`** — Register browser-compatible event listeners
- **`removeEventListener(type, callback)`** — Remove event listeners
- **`fs`** — File System Access API (`readText`, `writeText`, `getFileHandle`, `getDirectoryHandle`, `exists`, `stat`, `mkdir`, `remove`, `rename`)
- **`new AudioContext()`** — Web Audio API: `createBufferSource(path)` (file shortcut) / `createBufferSource()` + `.buffer`, `createGain()` + `gain.gain.linearRampToValueAtTime` for fade in/out, `decodeAudioData(arrayBuffer)`, `currentTime`, master volume. **AudioGroup extension** (`createGroup()` + `source.group = grp`) wraps `ma_sound_group` for stable BGM/SE volume control independent of JS GC.
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
| `pointerdown` / `pointermove` / `pointerup` / `pointercancel` | Pointer (mouse/touch unified) | MouseEvent props + `pointerId`, `pointerType`, `isPrimary`, `pressure`, `width`, `height`, `tiltX`, `tiltY`, `twist` |
| `gamepadconnected` / `gamepaddisconnected` | Gamepad | `gamepad` (Gamepad object: `id`, `index`, `connected`, `timestamp`, `mapping`, `axes[]`, `buttons[]`) |

Use `navigator.getGamepads()` for polling the current gamepad state every frame.

### WebGL Binding Coverage

Covers the major WebGL 2.0 APIs including: shaders/programs, buffers (VBO/UBO), textures (2D/3D/CubeMap), framebuffers/renderbuffers, VAO, uniforms (scalar/vector/matrix), drawing (including instanced), state management, clearBuffer, Transform Feedback, Query, and Sampler. Array-style entry points (`uniform*v`, `uniformMatrix*fv`, `bufferData`, `bufferSubData`, `clearBuffer*v`) accept either TypedArray/ArrayBuffer or a plain JavaScript Array (the latter is needed by pixi v4's `uSamplers = [0,1,2,...]` sampler array setup).

## API Reference

See `manual.js` for a complete API listing in JavaScript-style documentation.

## Demos

`data/main.js` includes 12 demos switchable by key press.

Demo 1 displays a HUD overlay showing controls, demo list, and system information.

| Key | Demo |
|-----|------|
| **1** | Vertex-colored triangle (WASD movement, wheel alpha, HUD overlay) |
| **2** | Canvas2D shapes (rectangles, circles, Bezier curves, transparency) |
| **3** | Canvas2D text verification (5 pages via `[`/`]`: family/size, textAlign/textBaseline, measureText, multilingual + textLocale, stroke/transform/getImageData) |
| **4** | Canvas2D animation (rotating shapes, orbital circles) |
| **5** | pixi.js v7.4.3 test (Graphics drawing, animation) |
| **6** | Canvas2D drawImage / getImageData / putImageData test |
| **7** | Canvas2D dirty rect partial update test |
| **8** | three.js r176 test (cube, sphere, floor plane, ESM) |
| **9** | three-vrm v3 VRM avatar display (MToon shader, SkinnedMesh) |
| **0** | pixi.ui v1.2.4 widget showcase (Button / CheckBox / Slider / ProgressBar / ScrollBox) |
| **-** | Scene Showcase: SceneManager + Input + Assets + SoundManager + SaveData + I18n + PerfHud framework demo (Boot → Title / Menu / Settings / Game / Pause / SaveLoad / Keybind) with BGM, SE, a 3-slider volume settings page, 3-slot save with continue/load, a rebindable input UI (Input.captureNext), live en/ja/zh-CN locale switching (all persisted in localStorage), and an F3-toggled FPS / ms / draw-call overlay |
| **=** | Fancy UI Showcase: pixi.ui FancyButton (hover/press scale anim via real tweedle.js), UIEffects (flash / ripple / bounce / toast), SceneManager.replaceWithFade scene transition, animated progress bar, flowing background lines |
| **Space** | Play beep sound |
| **R** | Reset |

Use `-demo N` to select the initial demo mode at launch.

Place font files in `data/fonts/` (e.g., OpenSans, Roboto).

### Browser reference pages

`docs/demo{N}_reference.html` (for Demo 2, 3, 4, 6, 7, 10) execute the same rendering code in a browser as a ground-truth reference (Canvas 2D for 2/3/4/6/7, same pixi.ui lib for 10). Use them side-by-side with jsengine to isolate rendering differences. Each demo also has a `docs/demo{N}_verification.md` (or `demo3_text_verification.md`) describing expected appearance and common failure modes. Serve the project root via HTTP to view them:

```bash
cd D:/test/jsengine
python -m http.server 8000
# then open http://localhost:8000/docs/demoN_reference.html
```

## Standalone samples

Self-contained samples launched with `-data samples/<name>` (independent from the default `data/`):

- **`samples/vrm_starter/`** — VRM base system (**vrmkit** reusable ESM library) plus two template
  modes: a 3D walk-around mode (WASD movement, third-person camera, VRMA emotes, NPC dialogue)
  and a novel-game style mode (telephoto 2D-like framing, message window, typewriter text,
  choices, expression / VRMA / camera scripting). Switch modes with Tab. VRMA playback is powered
  by the bundled `@pixiv/three-vrm-animation` v3.5.1. See `samples/vrm_starter/README.md` for the
  API reference and asset setup (VRM models and the VRMA motion pack are **not** in the repo for
  license reasons — download links are in that README).
- **`samples/anime25d/`** — port of [Anime2.5DRig](https://github.com/852wa/Anime2.5DRig) (MIT):
  drop-in auto-rigged 2.5D avatars from layered PSD files (mesh-warp rendering, stencil-clipped
  irises, dual-spring hair physics, idle / blink / lip-sync animation). Split into an embeddable
  library (`rig25d/`: PSD→rig data builder with binary caching, display runtime, test UI panel).
  Model PSDs are **not** in the repo — see `samples/anime25d/README.md`.

```bash
jsengine.exe -data samples/vrm_starter
# or: make run ARGS="-data samples/vrm_starter"
```

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
| [libvorbis](https://xiph.org/vorbis/) | BSD License | Vorbis audio decoding (optional) |
| [libogg](https://xiph.org/ogg/) | BSD License | Ogg container format (optional) |
| [opusfile](https://opus-codec.org/) | BSD License | Opus audio decoding (optional) |
| [pixi.js](https://pixijs.com/) v7.4.3 | MIT License | 2D renderer (bundled in data/lib/) |
| [three.js](https://threejs.org/) r176 | MIT License | 3D graphics (ESM, bundled in data/lib/) |

EGL/KHR headers are provided by The Khronos Group under the Apache-2.0 License.
