// ============================================================
// jsengine API リファレンス
// ============================================================
//
// このファイルは利用可能な JavaScript API の一覧です。
// 実行用ではなく参照用です。
//

// ************************************************************
// ライフサイクル（グローバル関数として定義すると C++ 側から呼ばれる）
// ************************************************************

function update(dt) {}   // 毎フレーム呼ばれる。dt = 前フレームからの経過ミリ秒
function render() {}     // 毎フレーム（update の後）呼ばれる。描画処理を記述
function done() {}       // アプリ終了時に呼ばれる。リソース解放用

// ************************************************************
// ファイル読み込み
// ************************************************************

loadScript("path/to/file.js");  // JS ファイルを SDL_LoadFile 経由で読み込み実行

// ************************************************************
// タイマー / アニメーションフレーム
// ************************************************************

var id = setTimeout(callback, delayMs);       // 遅延実行
var id = setInterval(callback, intervalMs);    // 定期実行
clearTimeout(id);                              // キャンセル
clearInterval(id);                             // キャンセル
var id = requestAnimationFrame(callback);      // 次フレームで実行（callback(timestamp)）
cancelAnimationFrame(id);
performance.now();                             // SDL_GetTicks() ベースのミリ秒

// ************************************************************
// ImageBitmap（画像読み込み）
// ************************************************************
// SDL3_image 経由で画像を読み込み、RGBA ピクセルデータを持つオブジェクトを返す。
// BMP, PNG, JPG に対応。パスはベースパスからの相対パス。

var img = createImageBitmap("image.png");
img.width;      // 画像の幅（ピクセル）
img.height;     // 画像の高さ（ピクセル）
img.data;       // RGBA ピクセルデータ（ArrayBuffer）

// gl.texImage2D に直接渡せる:
gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, img.width, img.height, 0,
              gl.RGBA, gl.UNSIGNED_BYTE, img);

// ************************************************************
// Web Audio API（miniaudio + SDL3 ベース）
// ************************************************************
// miniaudio エンジンで再生
// 対応フォーマット: WAV, MP3, FLAC 等（miniaudio がサポートする形式）

var audioCtx = new AudioContext();
audioCtx.sampleRate;                   // 読み取り専用（通常 48000）
audioCtx.state;                        // "running" | "suspended" | "closed"
audioCtx.destination;                  // ダミーノード（互換性のため）
audioCtx.masterVolume = 0.8;           // マスターボリューム（0.0 ~ 1.0+）
audioCtx.resume();                     // 再生再開
audioCtx.suspend();                    // 一時停止
audioCtx.close();                      // 全停止

// --- AudioBufferSourceNode ---
// ファイルパスはベースパスからの相対パス
var source = audioCtx.createBufferSource("sound.wav");
source.volume = 1.0;                   // ボリューム（0.0 ~ 1.0+）
source.pitch = 1.0;                    // ピッチ（1.0 = 原速）
source.pan = 0.0;                      // パン（-1.0=左, 0.0=中央, 1.0=右）
source.loop = false;                   // ループ再生
source.ended;                          // 読み取り専用: 再生終了したか
source.start();                        // 再生開始（先頭から）
source.stop();                         // 再生停止

// ************************************************************
// Canvas 2D API（ThorVG ベース）
// ************************************************************
// オフスクリーン 2D 描画キャンバス。描画結果は GL テクスチャとして取得可能。
// ThorVG SwCanvas で描画し、flush() で GL テクスチャにアップロードする。

Canvas2D.loadFont("font.ttf");                          // フォントファイル読み込み（ベースパス相対）

var ctx = new Canvas2D(512, 512);                        // オフスクリーンキャンバス作成
ctx.width;                                               // 読み取り専用
ctx.height;                                              // 読み取り専用
ctx.texture;                                             // WebGLTexture 互換オブジェクト（flush 後に利用可）

// --- スタイル ---
ctx.fillStyle = "#ff0000";                               // "#rrggbb", "#rrggbbaa", "rgba(r,g,b,a)", "rgb(r,g,b)", 色名
ctx.strokeStyle = "blue";
ctx.lineWidth = 2.0;
ctx.globalAlpha = 1.0;                                   // 0.0 ~ 1.0
ctx.lineCap = "butt";                                    // "butt" | "round" | "square"
ctx.lineJoin = "miter";                                  // "miter" | "round" | "bevel"

// --- 矩形 ---
ctx.fillRect(x, y, w, h);
ctx.strokeRect(x, y, w, h);
ctx.clearRect(x, y, w, h);

// --- パス ---
ctx.beginPath();
ctx.moveTo(x, y);
ctx.lineTo(x, y);
ctx.bezierCurveTo(cp1x, cp1y, cp2x, cp2y, x, y);
ctx.rect(x, y, w, h);
ctx.arc(cx, cy, r, startAngle, endAngle, counterclockwise);  // 角度はラジアン
ctx.closePath();
ctx.fill();
ctx.stroke();

// --- テキスト ---
ctx.font = "24px FontName";                              // "サイズpx フォント名"
ctx.textAlign = "left";                                  // "left" | "center" | "right"（予約）
ctx.fillText("Hello", x, y);                             // y はベースライン位置
ctx.strokeText("Hello", x, y);
var m = ctx.measureText("Hello");                        // => { width }

// --- 変換 ---
ctx.save();
ctx.translate(x, y);
ctx.rotate(angle);                                       // ラジアン
ctx.scale(sx, sy);
ctx.restore();

// --- 描画確定 ---
ctx.flush();                                             // ThorVG レンダリング → GL テクスチャ更新
// flush 後に ctx.texture を gl.bindTexture で使用可能

// ************************************************************
// コンソール
// ************************************************************

console.log("message", arg1, arg2);   // SDL_Log へ出力
console.error("message", arg1, arg2); // SDL_LogError へ出力

// ************************************************************
// Web Storage API（ブラウザ互換）
// ************************************************************
// データは SDL_GetPrefPath("jsengine","jsengine") 配下の
// localStorage.json に JSON 形式で永続化される。

localStorage.setItem("key", "value");           // 値を保存（自動永続化）
localStorage.getItem("key");                    // => "value" | null
localStorage.removeItem("key");                 // キーを削除（自動永続化）
localStorage.clear();                           // 全データ削除（自動永続化）
localStorage.key(0);                            // => インデックス指定でキー名取得 | null
localStorage.length;                            // => 保存されているキーの数

// ************************************************************
// File System Access API（同期版・ブラウザ互換構造）
// ************************************************************
// SDL3 のファイル関数を使用。パスは実行ディレクトリからの相対/絶対パス。

// --- ショートカット関数 ---
fs.readText("path/to/file.txt");                        // => string（ファイル全体を読み込み）
fs.writeText("path/to/file.txt", "content");            // テキストを書き込み
fs.exists("path/to/file.txt");                          // => boolean
fs.stat("path/to/file.txt");                            // => { type, size, createTime, modifyTime, accessTime } | null
                                                        //    type: "file" | "directory" | "other"
fs.mkdir("path/to/dir");                                // ディレクトリ作成（親も含む）
fs.remove("path/to/file.txt");                          // ファイルまたは空ディレクトリを削除
fs.rename("old/path", "new/path");                      // 名前変更 / 移動

// --- FileSystemFileHandle ---
var fileHandle = fs.getFileHandle("path/to/file.txt");               // 既存ファイルを取得
var fileHandle = fs.getFileHandle("path/to/file.txt", {create:true});// なければ作成

fileHandle.kind;      // "file"
fileHandle.name;      // ファイル名

var file = fileHandle.getFile();    // File オブジェクトを取得
file.name;                          // ファイル名
file.size;                          // バイトサイズ
file.text();                        // => string（全テキスト読み込み）
file.arrayBuffer();                 // => ArrayBuffer（全バイナリ読み込み）

var writable = fileHandle.createWritable();   // WritableStream を取得
writable.write("text data");                  // 文字列を追加
writable.write(arrayBuffer);                  // バイナリを追加
writable.close();                             // ファイルに書き出し

// --- FileSystemDirectoryHandle ---
var dirHandle = fs.getDirectoryHandle("path/to/dir");               // 既存ディレクトリ
var dirHandle = fs.getDirectoryHandle("path/to/dir", {create:true});// なければ作成

dirHandle.kind;       // "directory"
dirHandle.name;       // ディレクトリ名

var entries = dirHandle.entries();                      // => [[name, handle], ...]
                                                        //    handle は FileHandle or DirectoryHandle
var sub = dirHandle.getFileHandle("file.txt");           // 子ファイルを取得
var sub = dirHandle.getFileHandle("file.txt", {create:true});
var sub = dirHandle.getDirectoryHandle("subdir");        // 子ディレクトリを取得
var sub = dirHandle.getDirectoryHandle("subdir", {create:true});
dirHandle.removeEntry("file.txt");                       // 子エントリを削除

// ************************************************************
// イベントシステム（ブラウザ互換）
// ************************************************************

addEventListener("eventType", callback);    // イベントリスナー登録
removeEventListener("eventType", callback); // イベントリスナー解除

// --- KeyboardEvent ("keydown", "keyup") ---
addEventListener("keydown", function(e) {
    e.type;      // "keydown"
    e.key;       // "a", "Enter", "ArrowLeft", "Shift", "Control", "Alt", "Meta", ...
    e.code;      // "KeyA", "Enter", "ArrowLeft", "ShiftLeft", "ControlRight", ...
    e.keyCode;   // SDL_Keycode 値（レガシー互換）
    e.altKey;    // boolean
    e.ctrlKey;   // boolean
    e.shiftKey;  // boolean
    e.metaKey;   // boolean (Windows/Command キー)
    e.repeat;    // boolean (キーリピート)
});

addEventListener("keyup", function(e) {
    // keydown と同じプロパティ（repeat は常に false）
});

// --- MouseEvent ("mousedown", "mouseup", "mousemove") ---
addEventListener("mousedown", function(e) {
    e.type;       // "mousedown"
    e.clientX;    // マウス X 座標（ウィンドウ内ピクセル）
    e.clientY;    // マウス Y 座標（ウィンドウ内ピクセル）
    e.button;     // 0=左, 1=中, 2=右, 3=X1, 4=X2
    e.buttons;    // 押下中ボタンのビットマスク
    e.movementX;  // X 移動量
    e.movementY;  // Y 移動量
    e.altKey;     // boolean
    e.ctrlKey;    // boolean
    e.shiftKey;   // boolean
    e.metaKey;    // boolean
});

addEventListener("mouseup", function(e) {
    // mousedown と同じプロパティ
});

addEventListener("mousemove", function(e) {
    // mousedown と同じプロパティ（movementX/Y に前フレームからの差分）
});

// --- WheelEvent ("wheel") ---
addEventListener("wheel", function(e) {
    e.type;       // "wheel"
    e.deltaX;     // 横スクロール量（ピクセル近似）
    e.deltaY;     // 縦スクロール量（下方向が正、ピクセル近似）
    e.deltaZ;     // 常に 0
    e.deltaMode;  // 常に 0 (DOM_DELTA_PIXEL)
    e.clientX;    // マウス X 座標
    e.clientY;    // マウス Y 座標
    e.altKey;     // boolean
    e.ctrlKey;    // boolean
    e.shiftKey;   // boolean
    e.metaKey;    // boolean
});

// --- TouchEvent ("touchstart", "touchmove", "touchend", "touchcancel") ---
addEventListener("touchstart", function(e) {
    e.type;                        // "touchstart"
    e.touches;                     // Touch オブジェクトの配列
    e.changedTouches;              // 変更のあった Touch の配列
    e.touches[0].identifier;       // タッチ識別子
    e.touches[0].clientX;          // X 座標（ウィンドウ内ピクセル）
    e.touches[0].clientY;          // Y 座標（ウィンドウ内ピクセル）
    e.touches[0].pageX;            // = clientX
    e.touches[0].pageY;            // = clientY
    e.touches[0].force;            // 圧力（0.0 ~ 1.0）
});

addEventListener("touchmove", function(e) {
    // touchstart と同じプロパティ
});

addEventListener("touchend", function(e) {
    // touches は空配列、changedTouches に離れた指の情報
});

addEventListener("touchcancel", function(e) {
    // touchend と同じ構造
});

// ************************************************************
// WebGL 2.0 互換 API（グローバル gl オブジェクト）
// ************************************************************
//
// WebGL2RenderingContext 互換。GLES 3.0 にマッピングされている。
// シェーダは "#version 300 es" を使用する。
//
// 対応機能カテゴリ:
//
//   コンテキスト情報
//     getContextAttributes, isContextLost, getSupportedExtensions,
//     getExtension, getParameter, getError
//
//   シェーダ / プログラム
//     createShader, deleteShader, shaderSource, compileShader,
//     getShaderParameter, getShaderInfoLog, getShaderSource, isShader,
//     createProgram, deleteProgram, attachShader, detachShader,
//     linkProgram, useProgram, validateProgram, isProgram,
//     getProgramParameter, getProgramInfoLog,
//     bindAttribLocation, getAttribLocation, getUniformLocation,
//     getActiveAttrib, getActiveUniform
//
//   バッファ (VBO/IBO/UBO)
//     createBuffer, deleteBuffer, isBuffer, bindBuffer,
//     bufferData, bufferSubData
//
//   テクスチャ (2D/3D/CubeMap/2DArray)
//     createTexture, deleteTexture, isTexture, bindTexture,
//     activeTexture, texParameteri, texParameterf,
//     texImage2D, texSubImage2D, texImage3D,
//     copyTexImage2D, copyTexSubImage2D,
//     generateMipmap, pixelStorei, readPixels
//
//   フレームバッファ / レンダーバッファ
//     createFramebuffer, deleteFramebuffer, isFramebuffer, bindFramebuffer,
//     framebufferTexture2D, framebufferRenderbuffer, checkFramebufferStatus,
//     createRenderbuffer, deleteRenderbuffer, isRenderbuffer, bindRenderbuffer,
//     renderbufferStorage, drawBuffers, blitFramebuffer
//
//   VAO (Vertex Array Object)
//     createVertexArray, deleteVertexArray, isVertexArray, bindVertexArray
//
//   頂点属性
//     enableVertexAttribArray, disableVertexAttribArray,
//     vertexAttribPointer, vertexAttribIPointer, vertexAttribDivisor,
//     vertexAttrib1f, vertexAttrib2f, vertexAttrib3f, vertexAttrib4f
//
//   Uniform
//     uniform[1234][fi](location, ...)        — スカラー
//     uniform[1234][fiu]v(location, array)     — ベクトル/配列 (TypedArray)
//     uniformMatrix[234]fv(location, transpose, array)
//     uniformMatrix[2x3|2x4|3x2|3x4|4x2|4x3]fv(location, transpose, array)
//
//   描画
//     drawArrays, drawElements,
//     drawArraysInstanced, drawElementsInstanced
//
//   ステート管理
//     enable, disable, isEnabled, viewport, scissor
//
//   クリア
//     clearColor, clearDepth, clearStencil, clear,
//     clearBufferfv, clearBufferiv, clearBufferuiv, clearBufferfi
//
//   カラー / 深度 / ステンシル
//     colorMask, depthMask, depthFunc, depthRange,
//     blendFunc, blendFuncSeparate, blendEquation, blendEquationSeparate, blendColor,
//     stencilFunc, stencilFuncSeparate, stencilMask, stencilMaskSeparate,
//     stencilOp, stencilOpSeparate
//
//   ラスタライザ
//     cullFace, frontFace, lineWidth, polygonOffset, sampleCoverage
//
//   Uniform Block
//     getUniformBlockIndex, uniformBlockBinding,
//     bindBufferBase, bindBufferRange
//
//   Transform Feedback
//     createTransformFeedback, deleteTransformFeedback, bindTransformFeedback,
//     beginTransformFeedback, endTransformFeedback, transformFeedbackVaryings
//
//   Query
//     createQuery, deleteQuery, beginQuery, endQuery
//
//   Sampler
//     createSampler, deleteSampler, bindSampler,
//     samplerParameteri, samplerParameterf
//
//   その他
//     hint, flush, finish
//
// 定数は WebGL 2.0 仕様に準拠（gl.TRIANGLES, gl.TEXTURE_2D, gl.FLOAT 等）。
// 詳細は MDN WebGL2RenderingContext リファレンスを参照。

// ************************************************************
// ポリフィル / ブラウザシム（pixi.js 等のライブラリ動作用）
// ************************************************************
//
// lib/polyfill.js:
//   ES6 ポリフィル。duktape に不足している機能を補う。
//   Promise, Map, Set, WeakMap, WeakSet, Array.from,
//   Array.prototype.find/findIndex/fill
//
// lib/browser_shim.js:
//   ブラウザ API シム。以下を提供:
//   window, document, navigator, location,
//   HTMLCanvasElement (getContext で gl を返す),
//   Image (createImageBitmap ベース),
//   WebGLRenderingContext / WebGL2RenderingContext,
//   XMLHttpRequest, fetch (fs.readText ベース),
//   Event, CustomEvent, URL
//
// 使用方法:
//   loadScript("lib/polyfill.js");
//   loadScript("lib/browser_shim.js");
//   loadScript("lib/pixi.min.js");  // pixi.js v5.3.12 動作確認済み
