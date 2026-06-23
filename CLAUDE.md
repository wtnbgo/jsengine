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
- `src/repl.h` / `src/repl.cpp` — **REPL (Read-Eval-Print Loop)**。 CMake オプション `JSENGINE_USE_REPL` (default ON) でビルド時に有効化。 二系統のチャネル: (a) `-repl` 指定で stdin から行入力 → JS 評価 → stdout に結果 (対話デバッグ用、 簡易メタコマンド `.help/.clear/.quit` 付き、 カッコ釣り合いで multi-line 判定)。 (b) `-replfile <dir>` 指定で `<dir>/cmd` (UTF-8 JS) を監視 → 評価 → `<dir>/resp` に `{ok,result,error}` JSON で返す (外部エージェント / AI 駆動用、 cmd.tmp → cmd / resp.tmp → resp の atomic rename プロトコル)。 ワーカースレッドが提出、 メインスレッドの `App::update` 冒頭で 1 件 drain。 評価は `JsEngine::evalForRepl(source, out)` (グローバルスコープ、 Promise なら一定回数 pending job を回しつつ解決待ち、 結果は JSON.stringify(indent=2) でテキスト化)。 外部 AI が画面を確認 / ファイルを書き出すために `captureScreen(path)` (global) と `fs.writeBinary(path, buf)` を併設。
- `src/video.h` / `src/video.cpp` — **WebM 動画再生** (`wamsoft/movie-player` + libvpx + libvorbis/libopus)。 CMake オプション `JSENGINE_USE_MOVIE_PLAYER` (default ON) で有効化、 FetchContent で movie-player をビルド時取得。 グローバル `MoviePlayer` クラスを公開 (`new MoviePlayer("file.webm", {loop, volume})` / `play/pause/resume/stop/seek` / `width/height/duration/currentTime/paused/ended/loop/volume` / `data` で最新 RGBA フレームを ArrayBuffer 取得)。 内部の `SDLAudioSink` (IAudioSink 実装) が SDL_AudioStream へ PCM を流す (movie-player は内部 audio engine 非搭載なので host が sink を渡す設計)。 `Enqueue` は data を SDL に即コピー + param を consumed queue に積んで返却し、 decoder slot 詰まりを回避 (Android 推奨パターンと同じ)。 video color format は `COLOR_RGBA` で内部 YUV→RGBA 変換 (libyuv) 済み、 `gl.texImage2D(target, level, RGBA, RGBA, UNSIGNED_BYTE, player)` で直接アップロード可。 `HTMLVideoElement` (sysinit.js) はネイティブ MoviePlayer のラッパとして動き、 `video.src = "..."` で内部 player 生成、 `video.play/pause/loop/volume/currentTime/videoWidth/videoHeight/data` 等をブラウザ互換で公開する (`gl.texImage2D(..., video)` も MoviePlayer と同経路でテクスチャ化)。 **getter/setter は `JS_NewCFunction` の標準 4 引数シグネチャ (ctx, this_val, argc, argv) が必須**: 3 引数版を渡すと `val` がガベージになり SEGV (2026-06-23 にこれで踏んだ)。 `data/main.js` の Demo 13 (`\` キー、 Shift+\\ で native ⇄ sim 切替) が利用例。
- `src/app.hpp` / `src/app.cpp` — `App` singleton class owning the SDL window, OpenGL ES context, and JsEngine. Provides `init()`, `update(delta)`, `render()`, and `handleEvent()` methods.
- `src/jsengine.hpp` / `src/jsengine.cpp` — `JsEngine` class. Manages the QuickJS runtime/context, JS file loading (via SDL_LoadFile), lifecycle calls (update/render/done), and browser-compatible input event dispatch (addEventListener/removeEventListener)。マウスは `mousedown/up/move` と並行して `pointerdown/up/move`、タッチは `touchstart/move/end/cancel` と並行して `pointerdown/move/up/cancel` をネイティブ発火 (pointerId, pointerType "mouse"/"touch", pressure, isPrimary 等)。pointerover/out/leave/enter/upoutside は未発火。
- `src/webgl.h` / `src/webgl.cpp` — WebGL 2.0 compatible bindings mapping to GLES 3.0. Registers `gl` global and `WebGL2RenderingContext`.
- `src/webaudio.h` / `src/webaudio.cpp` — Web Audio API bindings。AudioContext + AudioBufferSourceNode + GainNode + AudioBuffer + `decodeAudioData(arrayBuffer)` + **AudioGroup (jsengine 拡張)** を提供。`createBufferSource(path)` の path 引数はファイル直接ロードする jsengine 拡張で従来コードと互換。`webaudio_update(deltaMs)` を毎フレーム呼び `AudioContext.currentTime` を進めて gain ramp と再生終了の延命解除を反映 (旧 `webaudio_gc()` は互換用ラッパ)。
- `src/canvas2d.h` / `src/canvas2d.cpp` — Canvas 2D API bindings using ThorVG SwCanvas. Bitmap-retained mode with deferred rendering: draw ops are batched and rendered to pixel buffer on flush/texture access/getImageData. drawImage uses ThorVG Picture. Dirty rect tracking for partial GL texture upload. テキスト描画は ThorVG の **FreeType + HarfBuzz (FT) ローダー** 経由（`TVG_LOADER_FT=ON`、`TVG_LOADER_TTF=OFF`）で、合字・複雑文字・CJK・多言語フォールバックに対応。`textAlign` (left/center/right/start/end)、`textBaseline` (top/hanging/middle/alphabetic/ideographic/bottom)、`textLocale` (BCP47) をサポート。`ctx.clip()` は ThorVG の `Paint::clip(Shape*)` で実装。現在の path と transform を baked-in にした Shape を `DrawState.clipPath` に ref-counted で保持し、以降の `addPaint()` で各 paint に `Shape::duplicate()` を attach する。save/restore で clip 状態がスタッキングされる。
- `src/webgamepad.h` / `src/webgamepad.cpp` — W3C Gamepad API バインディング (SDL_Gamepad ベース)。`navigator.getGamepads()` で接続中パッドのスナップショット配列を返し、`gamepadconnected` / `gamepaddisconnected` イベントを発火する。`buttons` は W3C 標準 17 ボタン (A/B/X/Y, LB/RB, LT/RT (軸→ボタン), Back/Start, L3/R3, DPad x4, Guide)、`axes` は LX/LY/RX/RY の 4 軸。
- `src/audio/` — AudioEngine (miniaudio singleton with sound groups; `CreateGroupNode/DestroyGroupNode` for dynamic `ma_sound_group` allocation backing AudioGroup) and AudioStream (file/memory/stream decoding with SDL3 I/O; `SetGroupNode(ma_sound_group*)` で動的に出力先 group を切替可能)。Supports WAV, MP3, FLAC, and optionally OGG Vorbis/Opus.
- `glad/` — GLAD loader for OpenGL ES 3.0 (local subdirectory, built as a CMake sub-project).

## Key Technical Details

### Rendering / GL

- Renders via OpenGL ES 3.0 (not desktop GL) — use GLES-compatible API calls.
- GLAD is loaded via `SDL_GL_GetProcAddress`; do not use platform-specific GL loaders.
- SDL3 shared libraries are copied to the build output directory as a post-build step.
- On mobile platforms (iOS/Android), the window is created fullscreen; on desktop, it's resizable.
- デバッグビルドでは `KHR_debug` 拡張が利用可能な場合 `glDebugMessageCallbackKHR` を有効化し、GL エラーを同期的にログ出力する（app.cpp）。Release ビルド（`NDEBUG` 定義時）では `#ifndef NDEBUG` で無効化。

### JS runtime (QuickJS-ng)

- QuickJS-ng is installed via vcpkg (`quickjs-ng`). CMake target: `qjs`. ES2023 対応のため、ES6 ポリフィル (Promise, Map, Set, WeakMap 等) は不要。
- JS files are loaded from the base path (default: `data/`, changeable via `-data <path>` CLI option). All relative paths in `loadScript()` and `fs.*` APIs resolve from this base path.
- **内蔵初期化スクリプト**: `src/sysinit.js` がブラウザ環境シム (window/document/Image/HTMLVideoElement/HTMLAudioElement/XMLHttpRequest/fetch/document.fonts 等) を提供する。 CMake (`cmake/embed_js.cmake`) がビルド時に C++ ソース (`builtin_sysinit.cpp`) に unsigned char 配列として埋め込み、 `JsEngine::loadSysinit()` が `main.js` ロード前に自動評価する。 開発時は `-sysinit <path>` コマンドラインオプションで外部ファイル (例 `src/sysinit.js`) から差し替え可能 (ビルド省略可)。 アプリ側 (data/main.js 等) で `loadScript("lib/browser_shim.js")` を呼ぶ必要はもう無い (廃止済み)。
- **内蔵 RPG Maker MV ブートストラップ**: `src/rpgmv_main.js` は RPG Maker MV プロジェクト用のブート処理 (pixi.js v4 ロード、 rpg_core 系 loadScript、 GameFont 読み込み、 SceneManager 起動、 `localStorage.setPath("jsengine_rpgmv", gameTitle)` でセーブデータ分離) を内蔵する。 CMake (`cmake/embed_js.cmake`) がビルド時に C++ ソース (`builtin_rpgmv_main.cpp`) に埋め込み、 `JsEngine::loadRpgmvMain()` が ES Module として評価する。 `-rpgmv <project-path>` コマンドラインオプション指定時のみ起動され (dataPath も同時に書き換え)、 `main.js` を含まない素の RPG MV プロジェクトフォルダから直接起動可能。 使用例: `jsengine.exe -rpgmv path/to/rpgmv-project`。 ロードするスクリプトは **プロジェクト直下の `index.html` を読んで `<script src=...>` の列挙をそのまま再生する** ので、 `pixi-tilemap.js` / `pixi-picture.js` を含まない構成や、 デプロイ前に minified ファイル名に変わっている構成にも追加対応なしで動く (`main.js` / `iphone-inline-video.*` / `plugins.js` は bootstrap 側で別途扱うのでスキップ)。 `index.html` が無いときは標準構成にフォールバック。
- **本体 (C++) 側でブラウザ互換 API を追加・変更した時は、 `src/sysinit.js` の対応コードも合わせて見直すこと**: 例えば `webaudio.cpp` に AudioContext のメソッドや AudioBufferSourceNode のプロパティを追加したら、 sysinit 側で同名のシムが空オブジェクトを返していたら撤去する。 逆に C++ 側で未実装のブラウザ API を JS で求められたら sysinit にシム (no-op か簡易実装) を追加する。 この同期を怠ると「以前は sysinit のシムで動いていた」「実装したのに sysinit のダミーが優先されて効かない」が起きる。
- JS lifecycle: `data/main.js` は ES Module として読み込まれる（top-level await 対応）。`update(dt)` / `render()` / `done()` は `globalThis` に明示登録が必要（モジュールスコープのため）。
- ESM (ES Modules) 対応: `loadModule(path)` で ESM ファイルを読み込み、export された名前空間オブジェクトを返す。`JS_SetModuleLoaderFunc` によりモジュール間の `import` も動作する。TLA (Top-Level Await) 使用モジュールは未対応（課題）。
- `globalThis.__DEBUG__` フラグ: jsengine.cpp が `NDEBUG` の有無で `true` / `false` を JS に渡す。Demo 9 のレンダ結果ピクセル検証など、本番では出したくないログを `if (globalThis.__DEBUG__) { ... }` でガードする。
- Input events (keyboard, mouse, touch, wheel) are converted from SDL3 to browser-compatible event objects and dispatched via `addEventListener`.
- Comments in the codebase are in Japanese.
- `manual.js` contains the full API reference for the JS environment.

### WebGL bindings (`src/webgl.cpp`)

- `qjs_get_buffer()` は TypedArray の byteOffset / byteLength を正しく処理する。
- `qjs_get_pixels()` は `out_hold` 引数で取得した ArrayBuffer の寿命を呼出し側に渡す。`canvas.data` getter のように新規 ArrayBuffer を毎回作って返すケースで、`JS_FreeValue` を即実行するとそのバッファが解放されて `glTexImage2D` に渡したポインタが dangling になり、GPU が解放済みヒープを読んでテクスチャ端に「起動毎に色が変わる謎の点」を生む。呼出し側は GL 関数を呼び終えてから `JS_FreeValue(ctx, pixelsHold)` する。
- `uniform*v` / `uniformMatrix*fv` / `bufferData` / `bufferSubData` / `clearBuffer*v` は TypedArray/ArrayBuffer に加えて **plain JS Array** も受け付ける (`qjs_array_to_{int,uint,float}_vec` ヘルパで vector に詰め直して GL に渡す)。pixi v4 の MultiTexture sprite shader は `shader.uniforms.uSamplers = [0,1,2,...,N-1]` のように plain Array で sampler 配列をセットしてくるので、 これに対応しないと `uSamplers[]` が default 0 のまま → 全 sampler が TEXTURE0 を sampling → batched sprite の vertex `aTextureId>0` の sprite が真っ黒になる (RPG Maker MV の Title 画面で WindowLayer の FBO 経由描画後、 Castle が消える症状の根本原因だった)。
- `getParameter()` は配列型（VIEWPORT 等）を JS Array で返す。
- `getExtension()` は GLES3 標準拡張に対して機能オブジェクトを返す（VAO 拡張メソッド含む）。
- WebGL2 関数として `texStorage2D(target, levels, internalformat, w, h)` / `texStorage3D` をサポート（three.js r176 のボーンテクスチャ作成に必須）。
- WebGL 固有 enum (`UNPACK_FLIP_Y_WEBGL` 0x9240, `UNPACK_PREMULTIPLY_ALPHA_WEBGL` 0x9241, `UNPACK_COLORSPACE_CONVERSION_WEBGL` 0x9243, `BROWSER_DEFAULT_WEBGL` 0x9244) は `gl` オブジェクトに定数登録済み。 `pixelStorei` で `UNPACK_FLIP_Y_WEBGL` / `UNPACK_COLORSPACE_CONVERSION_WEBGL` は GLES3 にないので no-op。 `UNPACK_PREMULTIPLY_ALPHA_WEBGL` は `g_unpack_premultiply_alpha` に保持し、 `texImage2D` / `texSubImage2D` で source が **straight alpha** (`_ctx2d` プロパティを持たないオブジェクト = Image/ImageBitmap) の場合に CPU 側で premultiply してから `glTexImage2D` に渡す。 canvas (`_ctx2d` 持ち) は `_getRGBA` が既に premultiplied で返すので二重適用を回避する。 これがないと RPG Maker MV の `Bitmap.decode` が初回 `_createBaseTexture(this._image)` で Image を直接テクスチャ化した時、 GPU には straight のまま入って pixi の premultiplied blend (`ONE, ONE_MINUS_SRC_ALPHA`) と不整合になり、 透過 PNG の `(255,255,255,α=0)` 領域が白不透明として描画される (キャラ周囲の白枠)。
- `texImage2D` は WebGL2 の unsized internalformat (`RGBA + FLOAT` 等) を `fixInternalFormat()` で GLES3 の sized format (`RGBA32F` 等) に自動変換する。 同じく `DEPTH_COMPONENT + UNSIGNED_SHORT/UNSIGNED_INT/FLOAT` → `DEPTH_COMPONENT16/24/32F`、 `DEPTH_STENCIL + UNSIGNED_INT_24_8` → `DEPTH24_STENCIL8` 等の型推定翻訳も含む。 `texImage3D` も同様。
- **`renderbufferStorage` / `texStorage2D` / `texStorage3D` / `copyTexImage2D` (type 引数を持たない経路)** は `fixSizedFormatNoType()` で WebGL1 の unsized `DEPTH_STENCIL` / `DEPTH_COMPONENT` を `DEPTH24_STENCIL8` / `DEPTH_COMPONENT16` に翻訳する。 これがないと pixi v2 の `FilterTexture` 構築で renderbuffer が incomplete になり、 FBO 全体が incomplete → filter pass で何も描けず Map が真っ黒になる症状を踏む (pixi v4/三系は最初から sized 形式を渡すので無症状)。 2026-06-23 解決。

### Web Audio (`src/webaudio.cpp`)

- **AudioContext / AudioBufferSourceNode / GainNode / AudioBuffer / AudioGroup** を提供。
- **GainNode**: フェード対応 (`gain.value`, `setValueAtTime`, `linearRampToValueAtTime`, `exponentialRampToValueAtTime`, `cancelScheduledValues`)。`source.connect(gain).connect(ctx.destination)` のグラフに加え、**gain → gain → ... → destination の多段チェーンも対応**。source の実効ボリュームは `source.localVolume × g1.cachedValue × g2.cachedValue × …` で、上流 gain の値変更時は `JsAudioGain::inputGains` を辿って影響範囲のすべての source に再適用する (`apply_volume_to_subtree`)。**注意: GainNode のチェーンは "ソフトウェア音量倍率" であり実際の miniaudio ノードグラフではない**。実 audio 経路は `src.group` で AudioGroup に attach するか、未指定なら master endpoint 直結。
- **AudioGroup (jsengine 拡張、WebAudio に無い)**: `ctx.createGroup(parent?)` で `ma_sound_group` に対応する JS オブジェクトを返す。`source.group = grp` で attach すると、stream→group→(parent→...)→master というネイティブグラフ経路で音が流れる。`group.volume = 0..1` で `ma_sound_group_set_volume` を呼ぶ即時反映 (ramp 非対応)。フレームワーク (`Assets.bgmGroup` / `Assets.seGroup`) は BGM/SE グループ実装にこれを使う。**重要: AudioGroup は GainNode と互換ノードではないので `gain.connect(group)` は無効。グループ attach は必ず `source.group = grp` 経由**。
- **ノード寿命管理 (WebAudio 仕様準拠)**: GainNode と BufferSourceNode は、接続中/再生中の間は JS 参照が切れても GC されない。webaudio.cpp の `JsAudioGain::selfHold` / `JsAudioSource::selfHold` が `JS_DupValue` で自己 JSValue を保持する。`gain.connect()` で hold、`gain.disconnect()` で release。source は `start()` で hold、`webaudio_update` で `IsPlaying() == false` を検知して release。この寿命管理がないと、JS スコープ末尾で GC された source の finalizer が `connectedGain = nullptr` を実行してチェーンを破壊する (旧版で SE 単発再生時に master/グループ音量が効かなくなる原因だった)。
- **source 解放時の gain 連鎖解放**: `js_audio_source_finalizer` で source の `connectedGain` から自分を除外した結果 `connectedSrcs` が空になったら、 その gain の selfHold も `gain_release_keep_alive` で release する。 これは RPG Maker MV の `WebAudio._removeNodes` のように `gain.disconnect()` を呼ばずに JS 参照を null 化するだけのコードに対応するため。 spec 厳密には gain への JS 参照が残っている可能性で release すべきでないが、 selfHold が refcount を支えて GC を阻害し続けるよりは現実的妥協。 別の変数で gain への JS 参照を保持していれば refcount で生き残るので即 finalize はされない。
- `createBufferSource(path)` の path 引数はファイル直接ロードする jsengine 拡張。
- `webaudio_update(deltaMs)` を毎フレーム呼ぶ。AudioContext.currentTime を進める / gain ramp 反映 / 再生終了 source の selfHold 解放を行う。
- **WebAudio spec の表面シム (実音には反映しないが、 spec 期待コードを動かすため)**:
  - `AudioContext.resume() / suspend() / close()` は Promise を返す (`resolved_promise()` ヘルパ)。
  - `AudioContext.createPanner()` は PannerNode 互換オブジェクト (panningModel / setPosition / setOrientation / connect / disconnect 等) を返すが、 中身は全 no-op (実パン無し)。
  - `AudioBufferSourceNode.loopStart` / `loopEnd` 数値フィールド (ループ範囲指定はネイティブ未対応、 全体ループのみ)、 `playbackRate` AudioParam ダミー (value / setValueAtTime / linearRampToValueAtTime / cancelScheduledValues、 全部 no-op で実ピッチ変更無し)。
  - `source.start(when, offset)` は 2 引数で呼んでも問題ないが、 `when` と `offset` は無視される。 これらは RPG Maker MV 等の WebAudio 標準利用コードを「型エラーなしに走らせる」ためのシムで、 機能完全実装ではない。

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
- pixi.js v7.4.3 動作確認済み (UMD 版、 `data/lib/pixi.min.js`)。 ブラウザシム (旧 `data/lib/browser_shim.js` + `polyfill.js`) は jsengine 本体に内蔵 (`src/sysinit.js`) になったので明示 loadScript は不要。 pixi.js v8 は ESM+TLA で読み込み可能だがバッチレンダラーのジオメトリ更新に問題あり (課題)。
- pixi.js v4.5.4（RPG Maker MV）は `test/` で作業中。OES_vertex_array_object 拡張マッピング、CanvasRenderingContext2D シム等を追加済み。
- pixi.ui v1.2.4 は `(PIXI×8, typedSignals, tweedle_js)` を期待する IIFE 形式なので、依存の最小シム `data/lib/pixi-ui-deps-shim.js` を loadScript 順で読み込む。**tweedle は時間ベースの本物最小実装**に置き換え済 (Tween / Group / Easing — Linear/Quadratic/Cubic/Quartic/Sinusoidal/Exponential/Back/Elastic 一通り、`Group.shared` シングルトン、chain / delay / repeat / yoyo / onStart/Update/Complete/Stop 対応)。ホスト側で毎フレーム `tweedle_js.Group.shared.update()` を呼ぶこと (Demo 11/12 がやっている)。これにより FancyButton の `animations.hover/pressed` が補間されるようになり、`framework/scene_manager.js` の `replaceWithFade / pushWithFade` と `framework/ui_effects.js` の flash/ripple/bounce/toast が動く。

### 内蔵 sysinit.js (ブラウザ API シム)

`src/sysinit.js` が jsengine バイナリに埋め込まれ、 `main.js` ロード前に自動評価される (旧 `data/lib/browser_shim.js` + `polyfill.js` をマージしたもの)。

- `HTMLCanvasElement` (`getContext("2d")` で Canvas2D、`"webgl"` / `"webgl2"` でグローバル `gl` を返す)、`Image`、`HTMLVideoElement` / `HTMLAudioElement` / `window.Audio`、`document` (`createElement` で canvas/video/audio/img/汎用、 `document.fonts` の CSS Font Loading API ダミー、 `body`/`head`/`documentElement` の clientWidth/Height)、`window` (innerWidth/outerWidth/scrollX/alert/focus/blur/open 等)、`location` (search/hash/host/origin)、`screen`、`XMLHttpRequest` (`responseType="arraybuffer"` は `fs.readBinary` 使用、 `decodeURI` でパス補正)、`fetch`、`Event` / `CustomEvent` / `URL`、`Object.getOwnPropertyDescriptors` ポリフィル、`webkitAudioContext` エイリアスを提供。
- `Image.prototype.src` setter は内部で同期的に画像をロードしたいが、自身が後段で `globalThis.createImageBitmap` を Promise を返すラッパーに上書きする (Blob / ArrayBuffer 対応のため)。setter は thenable を検出して `awaitPromise` でアンラップしてから `_data` / `width` / `height` を埋める。
- pixi v7 EventSystem 対応:
  1. `pointerdown` / `up` / `move` / `cancel` は C++ 側でネイティブ発火するようになったため shim 側のマッピングは廃止。`pointerover` / `pointerout` / `pointerupoutside` / `pointerleave` / `pointerenter` / `gotpointercapture` / `lostpointercapture` の登録は shim 側で no-op として吸収する。
  2. `MouseEvent[Symbol.hasInstance]` を上書きし「`clientX` / `button` を持つ object」を MouseEvent と判定させて pixi の `normalizeToPointerData` が `pointerId` / `pointerType` 等を補えるようにする (ネイティブ pointer event は既に埋めているが、touch のみ flow に乗る pixi v8 等の互換性のため残置)。
  3. event オブジェクトに `preventDefault` / `stopPropagation` / `stopImmediatePropagation` / `composedPath` を no-op として生やす。
  4. `event.target` に最初に pointer 登録した canvas を埋め、pixi の `onPointerUp` 内 `e !== this.domElement` 判定で pointerup が pointerupoutside にすり替わるのを防ぐ（Button.onPress を発火させるために必須）。

### JS フレームワーク (`data/framework/`)

ゲーム開発の boilerplate を減らすための薄い土台一式。loadScript で読み込まれ globalThis に公開される (ESM ではない)。Demo 11 が最初の利用例。

- `data/framework/scene_manager.js` — Cocos2d Director 風シーン管理。`class Scene` + `SceneManager.push/pop/replace/clear/count/top`。スタックベース、`pauseBelow`/`hideBelow` オプションで下位シーンの更新/描画を止められる。`SceneManager.update(dt)`/`render()`/`handleEvent(e)` を毎フレーム呼ぶ前提。`count()` はスタック深さ (leak 監視で PerfHud に表示等)。`clear()` は Demo 切替時に呼んで全シーンの `exit()` を走らせる (I18n.offChange / removeEventListener 等のリスナー解除を確実にする)。**フェード遷移**: `SceneManager.transitionTarget = sceneRoot` をセットしておくと `replaceWithFade(scene, {duration, args, sceneOpts})` / `pushWithFade(...)` で target の alpha を 1→0→1 補間しつつ replace/push を挟む。Promise を返すので `await` で完了待ち可能、`isTransitioning()` でロック中判定 (ボタン連打防止)。tweedle_js が無ければ即時遷移にフォールバック。
- `data/framework/input_action.js` — Unity InputAction 風入力抽象化。`Input.bind("jump", ["Space", "Gamepad:A"])` → `Input.isPressed("jump")` / `isJustPressed` / `isJustReleased` / `getValue`。キー (`KeyboardEvent.code`)、マウス (`Mouse:Left`)、ゲームパッド (`Gamepad:A`〜`Guide`、`Gamepad:LeftStickUp` 等半軸ボタン化、`Gamepad:LeftStickX` 等軸そのまま) を統合。`Input.update()` を毎フレーム呼んで状態確定。**キーバインド設定 UI 用**に `Input.captureNext({cancelOnEsc, timeoutMs}) → Promise<sourceString|null>` (次の入力 1 回をキャプチャ)、`captureCancel()`、`snapshotBindings()` / `restoreBindings(snap)`、`serialize()` / `deserialize(obj)` (JSON-safe な保存/復元) を提供。キャプチャは「キャプチャ開始時点で既に押されているキー」は一旦離して再プレスしないと捕まらない (rebind 入口の Enter 等が即座に再キャプチャされるのを防ぐため)。
- `data/framework/assets_ext.js` — `Assets.preloadAudio({alias: path})` で MP3/WAV/OGG/FLAC/Opus を fetch+decodeAudioData する自前プリローダ (PIXI v7 の URL リゾルバはブラウザ標準 URL コンストラクタ依存で jsengine の URL シムと非互換のため、音声は PIXI.Assets を経由しない)。フォントは PIXI.Assets の LoadParser として `Canvas2D.loadFont` を呼び family 名を返す。`Assets.audioContext` / `Assets.bgmGroup` / `Assets.seGroup` を公開し、出力グラフは `source → (bgmGroup or seGroup) → ctx master → destination`。master 音量は `Assets.audioContext.masterVolume` で制御 (AudioEngine の master を直接操作)。`Assets.play(alias, opts)` で即時再生ヘルパー (opts.group, opts.volume, opts.loop)、`Assets.getAudio(alias)` で AudioBuffer 取得。`Assets.unloadAudio(alias)` / `unloadAllAudio()` で AudioBuffer キャッシュを破棄 (Demo 切替時のメモリ削減用)、`Assets.audioBufferCount()` / `listAudioAliases()` で leak 監視用カウントを取得可能。
- `data/framework/sound_manager.js` — BGM クロスフェード / ダッキング / SE 単発再生のヘルパー。`SoundManager.playBgm(alias, {fadeIn, volume})` で同 alias は no-op、別 alias ならクロスフェード。`stopBgm(fadeOut)` / `pauseBgm(level, dur)` / `resumeBgm(dur)` でダッキング。BGM は `Assets.bgmGroup` に attach、フェード用の localGain は `currentBgm` で参照保持。`playSe(alias, opts)` は `Assets.seGroup` に attach するだけ (webaudio.cpp の selfHold で再生中は GC されないので JS 側で参照保持不要)。`tick()` は AudioGroup ベース化で不要になったが互換のため no-op stub を残してある。
- `data/framework/save_data.js` — localStorage 上のセーブスロット管理。`SaveData.init({namespace, slots, schemaVersion, migrate})` で初期化、`save(slot, data, {label})` / `load(slot)` / `delete(slot)` / `list()` / `info(slot)` を提供。`latestSlot()` / `loadLatest()` で「Continue」用の最後にセーブされた slot を取得。`quickSave/Load/Info/Delete` で slot とは独立した quick save。`schemaVersion` が変わったら `migrate(data, fromVer, toVer)` コールバックを呼び、null 返却で「読み込めないセーブ」扱い。キー設計は `{namespace}:slot:{n}` / `{namespace}:quick` / `{namespace}:meta` (envelope に version/savedAt/label/data を保持)。
- `data/framework/i18n.js` — 文字列辞書 + ロケール切替。`I18n.init({defaultLocale, fallbackLocale, locales, persistKey, autoRestore})` で初期化、`addLocale(locale, dict)` で後から辞書追加可能。`I18n.t(key, params?)` で文字列取得 (`{name}` プレースホルダを params.name で置換)。`setLocale/getLocale/getAvailable`。`onChange(cb)/offChange(cb)` で locale 変更通知。Demo 11 は `data/i18n/demo11_{en,ja,zh-CN}.json` を `fs.readText + JSON.parse` で同期ロードし `I18n.init()` に渡す。各シーンは `_i18nKey`/`_i18nParams` を PIXI.Text にマークし、`refreshI18nTexts(container)` で一括更新。動的 params (Continue with score 等) は scene の `_i18nListener` 内で params を再計算してから refresh。
- `data/framework/ui_effects.js` — tweedle ベースの汎用 UI 演出ヘルパー。`UIEffects.flash(container, opts)` (色塗りつぶし→フェードアウト)、`ripple(container, x, y, opts)` (Material 風タップ波紋)、`bounce(target, opts)` (scale ばね、`target.scale` を `downScale → upScale` で chain)、`toast(parent, message, opts)` (下からスライドイン + 自動フェードアウト)。完了時に自動 destroy。`tweedle_js.Group.shared.update()` がホスト側で毎フレーム呼ばれることが前提。
- `data/framework/perf_hud.js` — 常駐 HUD で FPS / フレーム時間 / draw call を表示。`PerfHud.init({instrumentGL, hotkey})` で 1 度初期化 (gl.drawArrays/Elements 系をモンキーパッチして draw call カウント、デフォルトホットキーは F3)。F3 で OFF / Minimal (FPS のみ) / Full (FPS + ms + draw + カスタム行) を循環。`PerfHud.update(deltaMs)` を毎フレーム呼んで FPS と ms 平均を計算、`PerfHud.refresh()` でオーバーレイの可視性とテキストを反映。`attachPixi(parent)` で PIXI.Text を追加 (`zIndex=9999` + 毎フレーム `setChildIndex(...)` で最上位を維持)、`PerfHud.set(label, value)` でデモ固有のカスタム行を追加可能。PIXI を使わないデモは `PerfHud.text()` の戻り値を Canvas2D で描画する形でも使える。

### Demos / 検証ツール

- Demo 1 に Canvas2D ベースの HUD オーバーレイ（操作説明・デモ一覧・システム情報）を表示。
- Demo 3: Canvas2D テキスト機能の総合検証サンプル。1280×720 全画面 5 ページ（`[` / `]` で切替）。Page1=font family/size, Page2=textAlign/textBaseline, Page3=measureText 可視化, Page4=多言語+textLocale, Page5=stroke/transform/getImageData。`docs/demo3_text_verification.md` に期待挙動、`docs/demo3_reference.html` にブラウザ参照ページ。
- Demo 9: three-vrm v3 による VRM アバター表示。GLTFLoader.parse でバイナリ VRM パース、MToon シェーダー + SkinnedMesh によるフルカラー描画動作。
- Demo 10: pixi.ui v1.2.4 ウィジェットショーケース（Button / CheckBox / Slider / ProgressBar / ScrollBox）。
- Demo 12 (`=` キー): **派手 UI ショーケース**。pixi.ui FancyButton + 本物 tweedle.js + UIEffects + SceneManager.replaceWithFade を実演。MainScene と SettingsScene を fade トランジションで往復、4 つの FancyButton (hover で 1.05x scale、press で 0.93x scale)、Tap ボタンに ripple + flash、Bounce ボタンに UIEffects.bounce、Show toast でスライドイン Toast、自動アニメする ProgressBar、流れる斜線背景。実体は `data/demos/demo12_flashy_ui.js`。
- Demo 11 (`-` キー): SceneManager / Input / Assets / SoundManager / SaveData / I18n / PerfHud を組み合わせた**フレームワーク事例**。F3 で FPS / ms / draw call の HUD を on/off。Boot (Assets ロード) → Title → Menu → Settings / Game → Pause → SaveLoad / Keybind の遷移サンプル。RPG メニュー型 UI、push/pop/replace の使い分け実演、Settings で Master/BGM/SE 3 スライダー + Language 切替 (en/ja/zh-CN) + Keybindings サブ画面、Game でアクション抽象化 (キーボード/Gamepad)、Pause で `pauseBelow` モーダル + BGM ダッキング、Pause → Save → SaveLoadScene で 3 スロット選択、Menu の Continue/Load で復帰 (`SaveData.loadLatest()`)、Settings → Keybindings で `Input.captureNext()` ベースのリバインド (Enter=REPLACE / Backspace=ADD / Delete=remove last / Reset to Defaults)。Language 切替時は `I18n.onChange` でシーン内全 PIXI.Text を一括翻訳更新。永続化キー: 音量は `localStorage["demo11_volumes"]` (master/bgm/se 各 0..1)、バインドは `localStorage["demo11_keybinds"]` (`Input.serialize()`)、locale は `localStorage["demo11_locale"]`、セーブデータは `localStorage["demo11:slot:N"]` + `demo11:meta`。セーブデータは `{score, playerX, playerY, playTime}` で位置と時間まで復帰可能。BGM (`data/bgm/title.wav`, `data/bgm/game.wav`) と SE (`data/se/select|confirm|cancel|fire|pause.wav`) は `tools/gen_audio_assets.py` で合成生成 (Python 標準ライブラリのみ、wave + math)。i18n 辞書は `data/i18n/demo11_{en,ja,zh-CN}.json`。実体は `data/demos/demo11_scene_showcase.js`。
- ブラウザ参照ページ: Demo 2 / 3 / 4 / 6 / 7 / 10 に `docs/demo{N}_reference.html` + `docs/demo{N}_verification.md` を用意（Demo 3 のみ `_text_verification.md`）。同じ描画コードをブラウザの Canvas 2D API（Demo 10 は同じ pixi.ui lib）で実行した「正解」と jsengine 側を見比べることで実装差分を切り分け可能。`python -m http.server 8000` 等で配信して `http://localhost:8000/docs/demo{N}_reference.html` を開く。
- `docs/demo10_text_compare.html` は pixi v7 `PIXI.TextMetrics` 風のスキャン (`#f00` 背景 + 黒テキスト → 上から非赤の行を探す) をブラウザ native Arial で実行して visible top / bottom を出す測定ツール。jsengine 側の同じ計測 (Canvas2D 経由) と比較してテキスト位置のフォント差を切り分けるのに使う。
