# jsengine

SDL3 + OpenGL ES 3.0 をベースに、duktape JavaScript エンジンと WebGL 2.0 互換バインディングを統合したクロスプラットフォームアプリケーションです。JavaScript から WebGL API を使って描画処理を記述できます。

## 必要環境

- CMake 3.22 以上
- Ninja
- vcpkg（`VCPKG_ROOT` 環境変数を設定）
- C++17 対応コンパイラ（MSVC, GCC, Clang）

## ビルド

```bash
# 構成（初回または CMakeLists.txt 変更後）
make prebuild

# ビルド
make build                       # Release
make build BUILD_TYPE=Debug      # Debug

# 実行（Windows）
make run
```

プリセットは OS に応じて自動選択されます（`x64-windows` / `x64-linux` / `x64-macos`）。

## 依存ライブラリ

| ライブラリ | 管理方法 | 用途 |
|-----------|---------|------|
| SDL3 | FetchContent | ウィンドウ管理、入力、ファイルI/O |
| SDL3_image | FetchContent | 画像読み込み（BMP, JPG, PNG） |
| GLAD | ローカル (glad/) | OpenGL ES 3.0 ローダー |
| duktape | vcpkg | JavaScript エンジン |
| glm | vcpkg | 数学ライブラリ |

## アーキテクチャ

### C++ 側

- `src/main.cpp` - SDL3 コールバックエントリポイント（SDL_AppInit / SDL_AppIterate / SDL_AppEvent / SDL_AppQuit）。入力イベントを App 経由で JsEngine に配信。
- `src/app.hpp / app.cpp` - `App` シングルトン。SDL ウィンドウと GL コンテキストの管理、JsEngine の所有。
- `src/jsengine.hpp / jsengine.cpp` - `JsEngine` クラス。duktape ヒープの管理、JS ファイルの読み込み・実行、イベントディスパッチ。
- `src/dukwebgl.h / dukwebgl.cpp` - WebGL 2.0 互換バインディング（GLES 3.0 ベース）

### JavaScript ライフサイクル

起動時に `main.js` が `SDL_LoadFile` 経由で読み込まれ実行されます。その後、以下のグローバル関数が C++ 側から呼び出されます:

| 関数 | タイミング | 引数 |
|------|-----------|------|
| `update(dt)` | 毎フレーム | 前フレームからの経過ミリ秒 |
| `render()` | 毎フレーム（update の後） | なし |
| `done()` | アプリ終了時 | なし |

### JS から利用可能な API

- **`gl`** - WebGL2RenderingContext 互換オブジェクト（グローバル）
- **`console.log()` / `console.error()`** - SDL ログへの出力
- **`loadScript(path)`** - 追加の JS ファイルを読み込み実行
- **`addEventListener(type, callback)`** - ブラウザ互換イベントリスナー登録
- **`removeEventListener(type, callback)`** - イベントリスナー解除

### 入力イベント

ブラウザと同じ `addEventListener` パターンで入力を受け取れます。SDL3 イベントがブラウザ互換のイベントオブジェクトに変換されます。

| イベント名 | 説明 | 主なプロパティ |
|-----------|------|---------------|
| `keydown` / `keyup` | キーボード | `key`, `code`, `keyCode`, `altKey`, `ctrlKey`, `shiftKey`, `metaKey`, `repeat` |
| `mousedown` / `mouseup` / `mousemove` | マウス | `clientX`, `clientY`, `button`, `buttons`, `movementX`, `movementY`, 修飾キー |
| `wheel` | ホイール | `deltaX`, `deltaY`, `deltaZ`, `deltaMode`, `clientX`, `clientY`, 修飾キー |
| `touchstart` / `touchmove` / `touchend` / `touchcancel` | タッチ | `touches[]`, `changedTouches[]`（各要素: `identifier`, `clientX`, `clientY`, `force`） |

### WebGL バインディング対応範囲

シェーダ/プログラム、バッファ（VBO/UBO）、テクスチャ（2D/3D/CubeMap）、フレームバッファ/レンダーバッファ、VAO、uniform（scalar/vector/matrix）、描画（instanced 含む）、ステート管理、clearBuffer、Transform Feedback、Query、Sampler など WebGL 2.0 の主要 API をカバーしています。

## API リファレンス

`manual.js` に全 API の一覧を JavaScript コード風にまとめています。

## サンプル

`main.js` に入力操作で三角形を動かせるサンプルが含まれています。

- **WASD / 矢印キー** — 三角形を移動
- **マウスドラッグ** — 三角形をドラッグ移動
- **ホイール** — 透明度を変更
- **タッチ** — タッチ位置に移動
- **R キー** — リセット
