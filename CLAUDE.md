# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**jsengine** is a C++17 cross-platform application using SDL3 with OpenGL ES 3.0 rendering and an embedded QuickJS-ng JavaScript engine (ES2023). It provides browser-compatible APIs including WebGL 2.0, Canvas 2D (ThorVG), Web Audio (miniaudio), Web Storage, File System Access, and input events. It uses the SDL3 callback-based application model (SDL_AppInit/SDL_AppIterate/SDL_AppEvent/SDL_AppQuit).

## Build System

CMake with presets + Ninja Multi-Config generator. Dependencies are managed via vcpkg (quickjs-ng, miniaudio, libvorbis, libopus, zlib, libpng, freetype, harfbuzz) and FetchContent (SDL3, SDL3_image, ThorVG).

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

### Developer options

- `-D USE_LOCAL_THORVG=ON` — build with `../thorvg/CMakeLists.txt` (via `add_subdirectory`) instead of FetchContent. For iterating on ThorVG patches locally. Default OFF uses FetchContent (`wtnbgo/thorvg` branch `cmake`).

### Direct CMake Usage

```bash
cmake --preset x64-windows
cmake --build build/x64-windows --config Release
```

## Architecture

- `src/main.cpp` — SDL3 callback entry point. Manages the App lifecycle, delta-time calculation, and routes SDL events to App. Supports `-debug` and `-quiet` CLI flags for log level.
- `src/app.hpp` / `src/app.cpp` — `App` singleton class owning the SDL window, OpenGL ES context, and JsEngine. Provides `init()`, `update(delta)`, `render()`, and `handleEvent()` methods.
- `src/jsengine.hpp` / `src/jsengine.cpp` — `JsEngine` class. Manages the QuickJS runtime/context, JS file loading (via SDL_LoadFile), lifecycle calls (update/render/done), and browser-compatible input event dispatch (addEventListener/removeEventListener)。マウスは `mousedown/up/move` と並行して `pointerdown/up/move`、タッチは `touchstart/move/end/cancel` と並行して `pointerdown/move/up/cancel` をネイティブ発火 (pointerId, pointerType "mouse"/"touch", pressure, isPrimary 等)。pointerover/out/leave/enter/upoutside は未発火。
- `src/webgl.h` / `src/webgl.cpp` — WebGL 2.0 compatible bindings mapping to GLES 3.0. Registers `gl` global and `WebGL2RenderingContext`.
- `src/webaudio.h` / `src/webaudio.cpp` — Web Audio API bindings. AudioContext + AudioBufferSourceNode + GainNode (フェード対応: `gain.value`, `setValueAtTime`, `linearRampToValueAtTime`, `exponentialRampToValueAtTime`, `cancelScheduledValues`) + AudioBuffer + `decodeAudioData(arrayBuffer)` を提供。`source.connect(gain).connect(ctx.destination)` のグラフを構築でき、source の実効ボリュームは `source.volume * gain.value`。`createBufferSource(path)` の path 引数はファイル直接ロードする jsengine 拡張で従来コードと互換。`webaudio_update(deltaMs)` を毎フレーム呼び `AudioContext.currentTime` を進めて gain ramp を反映 (旧 `webaudio_gc()` は互換用ラッパ)。
- `src/canvas2d.h` / `src/canvas2d.cpp` — Canvas 2D API bindings using ThorVG SwCanvas. Bitmap-retained mode with deferred rendering: draw ops are batched and rendered to pixel buffer on flush/texture access/getImageData. drawImage uses ThorVG Picture. Dirty rect tracking for partial GL texture upload. テキスト描画は ThorVG の **FreeType + HarfBuzz (FT) ローダー** 経由（`TVG_LOADER_FT=ON`、`TVG_LOADER_TTF=OFF`）で、合字・複雑文字・CJK・多言語フォールバックに対応。`textAlign` (left/center/right/start/end)、`textBaseline` (top/hanging/middle/alphabetic/ideographic/bottom)、`textLocale` (BCP47) をサポート。
- `src/audio/` — AudioEngine (miniaudio singleton with sound groups) and AudioStream (file/memory/stream decoding with SDL3 I/O). Supports WAV, MP3, FLAC, and optionally OGG Vorbis/Opus.
- `glad/` — GLAD loader for OpenGL ES 3.0 (local subdirectory, built as a CMake sub-project).

## Key Technical Details

### Rendering / GL

- Renders via OpenGL ES 3.0 (not desktop GL) — use GLES-compatible API calls.
- GLAD is loaded via `SDL_GL_GetProcAddress`; do not use platform-specific GL loaders.
- SDL3 shared libraries are copied to the build output directory as a post-build step.
- On mobile platforms (iOS/Android), the window is created fullscreen; on desktop, it's resizable.
- デバッグビルドでは `KHR_debug` 拡張が利用可能な場合 `glDebugMessageCallbackKHR` を有効化し、GL エラーを同期的にログ出力する（app.cpp）。Release ビルド（`NDEBUG` 定義時）では `#ifndef NDEBUG` で無効化。

### JS runtime (QuickJS-ng)

- QuickJS-ng is installed via vcpkg (`quickjs-ng`). CMake target: `qjs`. ES2023 対応のため、ES6 ポリフィル（Promise, Map, Set, WeakMap 等）は不要。`polyfill.js` は最小限のみ。
- JS files are loaded from the base path (default: `data/`, changeable via `-data <path>` CLI option). All relative paths in `loadScript()` and `fs.*` APIs resolve from this base path.
- JS lifecycle: `data/main.js` は ES Module として読み込まれる（top-level await 対応）。`update(dt)` / `render()` / `done()` は `globalThis` に明示登録が必要（モジュールスコープのため）。
- ESM (ES Modules) 対応: `loadModule(path)` で ESM ファイルを読み込み、export された名前空間オブジェクトを返す。`JS_SetModuleLoaderFunc` によりモジュール間の `import` も動作する。TLA (Top-Level Await) 使用モジュールは未対応（課題）。
- `globalThis.__DEBUG__` フラグ: jsengine.cpp が `NDEBUG` の有無で `true` / `false` を JS に渡す。Demo 9 のレンダ結果ピクセル検証など、本番では出したくないログを `if (globalThis.__DEBUG__) { ... }` でガードする。
- Input events (keyboard, mouse, touch, wheel) are converted from SDL3 to browser-compatible event objects and dispatched via `addEventListener`.
- Comments in the codebase are in Japanese.
- `manual.js` contains the full API reference for the JS environment.

### WebGL bindings (`src/webgl.cpp`)

- `qjs_get_buffer()` は TypedArray の byteOffset / byteLength を正しく処理する。
- `qjs_get_pixels()` は `out_hold` 引数で取得した ArrayBuffer の寿命を呼出し側に渡す。`canvas.data` getter のように新規 ArrayBuffer を毎回作って返すケースで、`JS_FreeValue` を即実行するとそのバッファが解放されて `glTexImage2D` に渡したポインタが dangling になり、GPU が解放済みヒープを読んでテクスチャ端に「起動毎に色が変わる謎の点」を生む。呼出し側は GL 関数を呼び終えてから `JS_FreeValue(ctx, pixelsHold)` する。
- `getParameter()` は配列型（VIEWPORT 等）を JS Array で返す。
- `getExtension()` は GLES3 標準拡張に対して機能オブジェクトを返す（VAO 拡張メソッド含む）。
- WebGL2 関数として `texStorage2D(target, levels, internalformat, w, h)` / `texStorage3D` をサポート（three.js r176 のボーンテクスチャ作成に必須）。
- WebGL 固有 enum（`UNPACK_FLIP_Y_WEBGL` 0x9240, `UNPACK_PREMULTIPLY_ALPHA_WEBGL` 0x9241, `UNPACK_COLORSPACE_CONVERSION_WEBGL` 0x9243, `BROWSER_DEFAULT_WEBGL` 0x9244）は `gl` オブジェクトに定数登録済み。`pixelStorei` ではこれらをスキップ。
- `texImage2D` は WebGL2 の unsized internalformat (`RGBA + FLOAT` 等) を `fixInternalFormat()` で GLES3 の sized format (`RGBA32F` 等) に自動変換する。

### Canvas 2D (`src/canvas2d.cpp`, ThorVG)

- ThorVG SwCanvas でビットマップ保持型に描画。draw ops は蓄積され `flush` / texture 取得 / `getImageData` 時に ThorVG でまとめて描画される。`clearRect` / `putImageData` 等は先に蓄積分を反映してから実行。dirty rect 追跡で GL テクスチャへの部分アップロードに対応。
- テキスト描画は ThorVG の **FreeType + HarfBuzz (FT) ローダー**経由（`TVG_LOADER_FT=ON`、`TVG_LOADER_TTF=OFF`）で、合字・複雑文字・CJK・多言語フォールバックに対応。`textAlign` (left / center / right / start / end)、`textBaseline` (top / hanging / middle / alphabetic / ideographic / bottom)、`textLocale` (BCP47) をサポート。
- テキストの座標系: ThorVG `Text` の anchor は ascender top（≒ `TextMetrics.ascent` 分だけベースラインより上）。CSS Canvas 仕様の `textBaseline` は em-square 基準なので、`ascent + descent`（hhea 由来で em より大きい）を em の比率で按分してから anchor を計算する。CSS px → ThorVG size は `* 72/96`、`TextMetrics` / `GlyphMetrics` の戻り値は CSS px と同じスケールで扱う。
- フォント解決: ThorVG 側 (FT loader) を拡張し `Text::load()` 時に FT_Face から family / style 名を取り込む。`LoaderMgr::font()` の name マッチングが「ロード時 name / family / "family Style"」のいずれにも対応するので、`ctx.font = "24px Open Sans"` のような CSS 風指定がそのまま動く。`ctx.font` パーサは引用符なしの family 名でも空白を含む文字列をカンマまで取り込む。`Canvas2D.fontInfo(name)` で `{family, style}` を取得可能。
- `ctx.getImageData()` の戻り値 `data` は CSS Canvas 仕様通り `Uint8ClampedArray` を返す。ArrayBuffer のままだと `data[i]` でのインデックスアクセスが `undefined` になり、PIXI v7 `TextMetrics.measureFont()` のような R チャネルスキャン (`if (data[i] !== 255)`) が破綻して `ascent = baseline` (本来の 1.5倍) を返し、テキスト描画位置が大きく下にずれる。
- `ctx.putImageData()` / `ctx.drawImage()` の source の `data` プロパティは TypedArray (Uint8ClampedArray) と ArrayBuffer の両方を受け付ける。`JS_GetTypedArrayBuffer` で offset / length 込みで取り出す。
- `canvas.width` / `canvas.height` の代入は CSS Canvas 仕様通り、サイズが同じでもピクセルバッファをクリアし context state (fillStyle / font / transform 等) を初期化する。PIXI.Text の updateText() は毎回 width / height を代入してから描画するので、これが守られないと前回描画の残骸が残る。

### 3rd-party JS libraries

- three.js r176 は ESM 版（`three.module.min.js` + `three.core.min.js`）を `loadModule()` で読み込み。Babel トランスパイル不要。
- pixi.js v7.4.3 動作確認済み（UMD 版、`data/lib/` に `polyfill.js`, `browser_shim.js`, `pixi.min.js`）。pixi.js v8 は ESM+TLA で読み込み可能だがバッチレンダラーのジオメトリ更新に問題あり（課題）。
- pixi.js v4.5.4（RPG Maker MV）は `test/` で作業中。OES_vertex_array_object 拡張マッピング、CanvasRenderingContext2D シム等を追加済み。
- pixi.ui v1.2.4 は `(PIXI×8, typedSignals, tweedle_js)` を期待する IIFE 形式なので、依存の最小シム `data/lib/pixi-ui-deps-shim.js` を loadScript 順で読み込む。tweedle は即時遷移実装（アニメーションなし）でも素の Button / Slider / ProgressBar / ScrollBox は問題なく動作する。

### `browser_shim.js` (ブラウザ API シム)

- `HTMLCanvasElement` (`getContext("2d")` で Canvas2D、`"webgl"` / `"webgl2"` でグローバル `gl` を返す)、`Image`、`document`、`window` 等を提供。
- `Image.prototype.src` setter は内部で同期的に画像をロードしたいが、自身が後段で `globalThis.createImageBitmap` を Promise を返すラッパーに上書きする (Blob / ArrayBuffer 対応のため)。setter は thenable を検出して `awaitPromise` でアンラップしてから `_data` / `width` / `height` を埋める。
- pixi v7 EventSystem 対応:
  1. `pointerdown` / `up` / `move` / `cancel` は C++ 側でネイティブ発火するようになったため shim 側のマッピングは廃止。`pointerover` / `pointerout` / `pointerupoutside` / `pointerleave` / `pointerenter` / `gotpointercapture` / `lostpointercapture` の登録は shim 側で no-op として吸収する。
  2. `MouseEvent[Symbol.hasInstance]` を上書きし「`clientX` / `button` を持つ object」を MouseEvent と判定させて pixi の `normalizeToPointerData` が `pointerId` / `pointerType` 等を補えるようにする (ネイティブ pointer event は既に埋めているが、touch のみ flow に乗る pixi v8 等の互換性のため残置)。
  3. event オブジェクトに `preventDefault` / `stopPropagation` / `stopImmediatePropagation` / `composedPath` を no-op として生やす。
  4. `event.target` に最初に pointer 登録した canvas を埋め、pixi の `onPointerUp` 内 `e !== this.domElement` 判定で pointerup が pointerupoutside にすり替わるのを防ぐ（Button.onPress を発火させるために必須）。

### Demos / 検証ツール

- Demo 1 に Canvas2D ベースの HUD オーバーレイ（操作説明・デモ一覧・システム情報）を表示。
- Demo 3: Canvas2D テキスト機能の総合検証サンプル。1280×720 全画面 5 ページ（`[` / `]` で切替）。Page1=font family/size, Page2=textAlign/textBaseline, Page3=measureText 可視化, Page4=多言語+textLocale, Page5=stroke/transform/getImageData。`docs/demo3_text_verification.md` に期待挙動、`docs/demo3_reference.html` にブラウザ参照ページ。
- Demo 9: three-vrm v3 による VRM アバター表示。GLTFLoader.parse でバイナリ VRM パース、MToon シェーダー + SkinnedMesh によるフルカラー描画動作。
- Demo 10: pixi.ui v1.2.4 ウィジェットショーケース（Button / CheckBox / Slider / ProgressBar / ScrollBox）。
- ブラウザ参照ページ: Demo 2 / 3 / 4 / 6 / 7 / 10 に `docs/demo{N}_reference.html` + `docs/demo{N}_verification.md` を用意（Demo 3 のみ `_text_verification.md`）。同じ描画コードをブラウザの Canvas 2D API（Demo 10 は同じ pixi.ui lib）で実行した「正解」と jsengine 側を見比べることで実装差分を切り分け可能。`python -m http.server 8000` 等で配信して `http://localhost:8000/docs/demo{N}_reference.html` を開く。
- `docs/demo10_text_compare.html` は pixi v7 `PIXI.TextMetrics` 風のスキャン (`#f00` 背景 + 黒テキスト → 上から非赤の行を探す) をブラウザ native Arial で実行して visible top / bottom を出す測定ツール。jsengine 側の同じ計測 (Canvas2D 経由) と比較してテキスト位置のフォント差を切り分けるのに使う。
