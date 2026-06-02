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

loadScript("path/to/file.js");  // JS ファイルを読み込み実行（グローバルスコープ）

// ************************************************************
// ES Module ロード
// ************************************************************

// main.js は ES Module として実行される（top-level await 対応）
// ライフサイクル関数は globalThis に明示登録が必要:
//   globalThis.update = update;
//   globalThis.render = render;
//   globalThis.done = done;

var mod = loadModule("path/to/module.mjs");  // ESM を読み込み、名前空間オブジェクトを返す
// mod.exportedFunction(), mod.exportedValue 等でアクセス

var result = awaitPromise(somePromise);      // Promise を同期的に解決して結果を返す
// 同期 API (例: Image の src setter) から browser_shim.js でラップされた
// createImageBitmap など Promise を返す関数を呼ぶ際にも使う

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

// 注: browser_shim.js を読み込むと globalThis.createImageBitmap が
// Blob / ArrayBuffer 対応の Promise を返すラッパーに置き換わる。
// 同期的に使う場合は awaitPromise(createImageBitmap(...)) でアンラップする。

// ************************************************************
// Web Audio API（miniaudio + SDL3 ベース）
// ************************************************************
// miniaudio エンジンで再生
// 対応フォーマット: WAV, MP3, FLAC, OGG Vorbis/Opus

var audioCtx = new AudioContext();
audioCtx.sampleRate;                   // 読み取り専用（通常 48000）
audioCtx.currentTime;                  // 読み取り専用: アプリ起動からの経過秒数（setValueAtTime 等の基準）
audioCtx.state;                        // "running" | "suspended" | "closed"
audioCtx.destination;                  // ダミーノード（互換性のため）
audioCtx.masterVolume = 0.8;           // マスターボリューム（0.0 ~ 1.0+）
audioCtx.resume();                     // 再生再開
audioCtx.suspend();                    // 一時停止
audioCtx.close();                      // 全停止

// --- AudioBufferSourceNode (拡張: ファイル直接ロード) ---
// jsengine 独自の簡略 API: createBufferSource(path) でファイルを直接読み込み即再生可能
var beep = audioCtx.createBufferSource("beep.wav");
beep.volume = 1.0;                     // ボリューム（0.0 ~ 1.0+）。後述の GainNode が無い場合のローカル値
beep.pan = 0.0;                        // パン（-1.0=左, 0.0=中央, 1.0=右）
beep.loop = false;                     // ループ再生
beep.ended;                            // 読み取り専用: 再生終了したか
beep.start();                          // 再生開始（先頭から）
beep.stop();                           // 再生停止

// --- AudioBuffer + decodeAudioData (標準) ---
// fetch でファイルを ArrayBuffer として読み込み、decodeAudioData でデコードして再利用可能な AudioBuffer に
var ab = await fetch("bgm.mp3").then(function(r) { return r.arrayBuffer(); });
var audioBuf = await audioCtx.decodeAudioData(ab);
audioBuf.sampleRate;                   // サンプルレート (Hz)
audioBuf.length;                       // チャンネルあたりのサンプル数
audioBuf.duration;                     // 秒
audioBuf.numberOfChannels;             // チャンネル数

// --- AudioBufferSourceNode (標準: 引数なし + .buffer 代入) ---
var bgm = audioCtx.createBufferSource();
bgm.buffer = audioBuf;
bgm.loop = true;
bgm.start();

// --- GainNode + AudioParam (フェードイン/アウト) ---
var gain = audioCtx.createGain();
gain.gain.value = 1.0;                                          // 即時設定
gain.gain.setValueAtTime(1.0, audioCtx.currentTime);            // 時刻指定で値を設定
gain.gain.linearRampToValueAtTime(0.0, audioCtx.currentTime + 2.0); // 2秒かけて 0.0 へ線形フェード
gain.gain.exponentialRampToValueAtTime(0.5, audioCtx.currentTime + 1.0); // 指数フェード
gain.gain.cancelScheduledValues(audioCtx.currentTime);          // 予約された自動化を取り消し

// --- 接続 (グラフ構築) ---
bgm.connect(gain).connect(audioCtx.destination);
// gain にぶら下げた source の実効ボリュームは source.volume × gain.value × (上流 gain の値…)
// gain.disconnect() / source.disconnect() で接続解除（解除後は source.volume のみが反映）
//
// 注: GainNode は jsengine では「ソフトウェア音量倍率」として実装されており、実際の
//     ma_node グラフ上のノードではない。BGM/SE のグルーピング用途には AudioGroup を使う。
// 寿命管理: WebAudio 仕様準拠で、接続中の GainNode と再生中の BufferSourceNode は
//          JS 参照が切れても GC されない (内部で selfHold が JSValue を保持)。

// --- AudioGroup (jsengine 拡張: ma_sound_group のラッパ) ---
// WebAudio に無いが、master/BGM/SE のような階層的グループ音量を JS GC と無関係に
// 安定動作させたい場合に使う。source.group = grp で attach すると、
// ネイティブグラフ上で stream → group → master と流れる。
var bgmGroup = audioCtx.createGroup();                 // (parent 省略 = master 直下)
var seGroup  = audioCtx.createGroup();
bgmGroup.volume = 0.8;                                 // 0..1、即時反映 (ramp 非対応)
seGroup.volume  = 1.0;

var se = audioCtx.createBufferSource();
se.buffer = audioBuf;
se.group  = seGroup;                                   // SE グループに attach
se.start();
// se の JS 参照を捨てても、再生中はネイティブグラフ上でグループ経由で鳴り続ける。
// seGroup.volume の変更は se を含む group 配下すべての source に即時反映される。
//
// 重要: gain.connect(group) は無効 (GainNode と AudioGroup は別系統)。group attach は
//       必ず source.group = grp 経由で行う。

// ************************************************************
// Canvas 2D API（ThorVG ベース・ビットマップ保持型）
// ************************************************************
// オフスクリーン 2D 描画キャンバス。各描画操作は即座にピクセルバッファに反映される。
// clearRect で明示的にクリアされるまでバッファ内容を保持する。
// flush() で GL テクスチャにアップロードし、WebGL から利用可能。
// browser_shim.js の getContext("2d") が返す 2D コンテキストとしても使用される。

Canvas2D.loadFont("font.ttf");                          // フォントファイル読み込み（ファイル名 / family / "family Style" で参照可能）
Canvas2D.loadFont("font.ttf", "MyFont");                // alias 名で登録（任意のフォント名で使用可能）

// ロード済みフォントの内部 family / style 名を取得 (見つからなければ null)
var info = Canvas2D.fontInfo("NotoSansJP-Regular");      // => { family: "Noto Sans JP", style: "Regular" }
// ctx.font は "サイズpx ファイル名" / "サイズpx 'family 名'" / 'サイズpx "family Style"' のいずれでも指定可

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
ctx.globalCompositeOperation = "source-over";            // ダミー（現在固定）
ctx.imageSmoothingEnabled = true;                        // ダミー（現在固定）

// --- 矩形（即座にバッファに描画） ---
ctx.fillRect(x, y, w, h);
ctx.strokeRect(x, y, w, h);
ctx.clearRect(x, y, w, h);                              // ピクセルバッファを直接クリア

// --- パス ---
ctx.beginPath();
ctx.moveTo(x, y);
ctx.lineTo(x, y);
ctx.bezierCurveTo(cp1x, cp1y, cp2x, cp2y, x, y);
ctx.rect(x, y, w, h);
ctx.arc(cx, cy, r, startAngle, endAngle, counterclockwise);  // 角度はラジアン
ctx.closePath();
ctx.fill();                                              // ThorVG Shape で即座に描画
ctx.stroke();
ctx.clip();                                              // 現在のパスを以降の描画のクリップマスクに設定
                                                         // (現在の transform が baked-in、save/restore でスタッキング)
                                                         // fill-rule 引数 / Path2D 引数は未対応

// --- テキスト ---
ctx.font = "24px FontName";                              // "サイズpx フォント名"
ctx.textAlign = "left";                                  // "left" | "center" | "right" | "start" | "end"
ctx.textBaseline = "alphabetic";                         // "top" | "hanging" | "middle" | "alphabetic" | "ideographic" | "bottom"
ctx.textLocale = "ja-JP";                                // BCP47 (ThorVG FT loader の HarfBuzz 言語ヒント)。"zh-CN"/"zh-TW" 等
ctx.fillText("Hello", x, y);                             // y は textBaseline で指定した位置に対応
ctx.strokeText("Hello", x, y);
var m = ctx.measureText("Hello");
// m: { width, actualBoundingBoxAscent, actualBoundingBoxDescent,
//      fontBoundingBoxAscent, fontBoundingBoxDescent } (CSS px)

// --- 画像描画（ThorVG Picture ベース） ---
ctx.drawImage(image, dx, dy);                            // 3引数: 原寸描画
ctx.drawImage(image, dx, dy, dw, dh);                    // 5引数: リサイズ描画
ctx.drawImage(image, sx, sy, sw, sh, dx, dy, dw, dh);   // 9引数: 切り出し+リサイズ
// image: createImageBitmap() の戻り値、または data プロパティを持つオブジェクト

// --- ピクセル操作（C++ 側で直接バッファアクセス） ---
var imgData = ctx.getImageData(x, y, w, h);              // => { width, height, data: Uint8ClampedArray(RGBA) }
ctx.putImageData(imgData, dx, dy);                       // RGBA バッファを直接書き込み
// imgData.data は CSS Canvas 仕様の Uint8ClampedArray なので imgData.data[i]
// で各バイトに直接アクセス可能（PIXI.TextMetrics.measureFont 等の R チャネル
// スキャンに必須）。putImageData は Uint8ClampedArray / ArrayBuffer 両対応。

// --- 変換 ---
ctx.save();
ctx.translate(x, y);
ctx.rotate(angle);                                       // ラジアン
ctx.scale(sx, sy);
ctx.setTransform(a, b, c, d, e, f);                     // 変換行列を直接設定
ctx.restore();

// --- 描画確定 ---
ctx.flush();                                             // 蓄積描画をバッファに反映 + dirty 領域を GL テクスチャにアップロード
// flush 後に ctx.texture を gl.bindTexture で使用可能
// ctx.texture の getter でも自動的に flush される
// getImageData も自動的に蓄積描画を反映してからバッファを読み出す
// 注: 描画操作（fillRect, fill, stroke, fillText, drawImage 等）は蓄積され、
//     flush / texture取得 / getImageData 時にまとめて ThorVG で描画される（遅延描画）
//     clearRect / putImageData / blitPixels は先に蓄積分を反映してから実行される

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

// --- PointerEvent ("pointerdown", "pointermove", "pointerup", "pointercancel") ---
// マウス/タッチ両方を統一的に扱える Pointer Events 仕様。C++ 側で mouse*/touch* と
// 並行してネイティブ発火する (browser_shim.js の pointer→mouse マッピングは廃止済み)。
// pointerover / pointerout / pointerupoutside / pointerleave / pointerenter /
// gotpointercapture / lostpointercapture は未発火 (shim 側で登録を no-op で吸収)。
addEventListener("pointerdown", function(e) {
    // MouseEvent のプロパティ (clientX/Y, button, buttons, movement*, modifier keys) を全て継承
    e.pointerId;          // マウス=1, タッチ=fingerID+2 (衝突回避)
    e.pointerType;        // "mouse" | "touch"
    e.isPrimary;          // 主要ポインタか (常に true)
    e.pressure;           // 0.0 ~ 1.0 (mouse=0.5 押下中/0.0 通常, touch=実際の force)
    e.tangentialPressure; // 0.0 (ペン専用、未対応)
    e.width;              // 接触面サイズ (常に 1.0)
    e.height;             // 同上
    e.tiltX; e.tiltY;     // スタイラスティルト (常に 0)
    e.twist;              // スタイラスツイスト (常に 0)
});

addEventListener("pointermove", function(e) { /* pointerdown と同じ */ });
addEventListener("pointerup",   function(e) { /* pointerdown と同じ。pressure=0 */ });
addEventListener("pointercancel", function(e) { /* タッチキャンセル時のみ発火 */ });

// --- GamepadEvent ("gamepadconnected", "gamepaddisconnected") ---
// SDL3 の SDL_Gamepad 経由でコントローラ接続/切断を検出。e.gamepad に Gamepad オブジェクト。
addEventListener("gamepadconnected", function(e) {
    console.log("Gamepad connected:", e.gamepad.id, "index=" + e.gamepad.index);
});

// --- navigator.getGamepads() ---
// 接続中のゲームパッドを poll する。スロット順 (index) 配列。未接続位置は null。
var pads = navigator.getGamepads();   // (Gamepad | null)[]
if (pads[0]) {
    var p = pads[0];
    p.id;            // "Xbox Series Controller" 等
    p.index;         // 0..n-1
    p.connected;     // true
    p.timestamp;     // 接続時刻 (ms, performance.now ベース)
    p.mapping;       // "standard" 固定
    p.axes;          // 数値配列 [-1.0..1.0]: [LX, LY, RX, RY]
    p.buttons;       // ボタン配列 (W3C 標準 17 個):
                     //   0=A 1=B 2=X 3=Y 4=LB 5=RB 6=LT 7=RT
                     //   8=Back 9=Start 10=L3 11=R3
                     //   12=Up 13=Down 14=Left 15=Right 16=Guide
    p.buttons[0].pressed;   // boolean
    p.buttons[0].value;     // 0.0 ~ 1.0 (LT/RT は連続値、他は 0 / 1)
    p.buttons[0].touched;   // = pressed
}

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
//     texStorage2D, texStorage3D,
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
// オプショナル: JS フレームワーク（data/framework/）
// ************************************************************
// ゲーム制作の boilerplate を減らす薄い土台。loadScript で読み込むと
// globalThis に Scene / SceneManager / Input / Assets / SoundManager が登録される。
// Demo 11 が利用例。
//
// loadScript("framework/scene_manager.js");
// loadScript("framework/input_action.js");
// loadScript("framework/assets_ext.js");   // PIXI が先にロード済みであること
// loadScript("framework/sound_manager.js"); // assets_ext.js の後
//
// --- SceneManager (Cocos2d Director 風) ---
// class MyScene extends Scene {
//     enter(args) {}   // push / replace で 1 回
//     exit() {}        // pop / replace で 1 回
//     pause() {}       // 上に別シーンが push された時
//     resume() {}      // 上のシーンが pop された時
//     update(dt) {}    // 毎フレーム (上位の pauseBelow で止まる)
//     render() {}      // 毎フレーム (上位の hideBelow で隠れる)
//     handleEvent(e) {} // 最上位シーンのみ
// }
// SceneManager.push(scene, args, opts);    // opts: { pauseBelow, hideBelow }
// SceneManager.pop();
// SceneManager.replace(scene, args, opts);
// SceneManager.clear();
// SceneManager.top();
// SceneManager.update(dt);                 // 毎フレーム呼ぶ
// SceneManager.render();
// SceneManager.handleEvent(e);
//
// --- Input (Unity InputAction 風) ---
// Input.bind("jump", ["Space", "Gamepad:A"]);
// Input.bind("moveX", ["ArrowLeft|ArrowRight", "Gamepad:LeftStickX"]);
// Input.update();                          // 毎フレーム呼ぶ
// Input.isPressed("jump");
// Input.isJustPressed("jump");
// Input.isJustReleased("jump");
// Input.getValue("moveX");                  // 0..1 / -1..1
// source 文字列: KeyboardEvent.code, "Mouse:Left|Middle|Right",
//   "Gamepad:A|B|X|Y|LB|RB|LT|RT|Back|Start|L3|R3|DpadUp|DpadDown|DpadLeft|DpadRight|Guide",
//   "Gamepad:LeftStickX|Y|RightStickX|Y" (軸そのまま -1..1),
//   "Gamepad:LeftStickUp|Down|Left|Right|RightStickUp|..." (半軸ボタン 0..1)
//
// キーバインド設定 UI (リバインドフロー) 用:
//   var src = await Input.captureNext();                     // 次の 1 入力を待つ
//   var src = await Input.captureNext({ timeoutMs: 5000 });  // timeout 付き
//   var src = await Input.captureNext({ cancelOnEsc: false }); // Esc も普通の入力として扱う
//   Input.captureCancel();                                   // 進行中の capture を null で解決
//   // src 例: "KeyZ" / "Mouse:Left" / "Gamepad:A" / "Gamepad:LeftStickRight" / null (キャンセル/timeout)
//   // 注: 開始時点で押されているキー/ボタンは「離して再プレス」しないと拾われない
//
//   var snap = Input.snapshotBindings();        // 現在のバインドの deep copy
//   Input.restoreBindings(snap);                // snapshot から一括復元
//   var json = Input.serialize();               // localStorage 保存用
//   Input.deserialize(json);                    // 起動時に復元
//
// --- Assets (PIXI.Assets 拡張 + 音声プリローダ + AudioGroup) ---
// フォント (.ttf .otf) は PIXI.Assets の LoadParser として登録され、
// Canvas2D.loadFont を呼んで family/style を返す。
//   PIXI.Assets.add({ alias: "ui_font", src: "fonts/Roboto.ttf" });
//   await PIXI.Assets.load("ui_font");
//   var info = PIXI.Assets.get("ui_font");   // { url, family, style }
//   ctx.font = "24px " + info.family;
//
// 音声 (.mp3 .wav .ogg .flac .opus .m4a) は PIXI.Assets を経由せず
// 自前 fetch + decodeAudioData でプリロードする (PIXI v7 のリゾルバが
// ブラウザ標準 URL コンストラクタに依存しており jsengine のシムと非互換のため):
//   await Assets.preloadAudio({ bgm: "bgm.mp3", se_ok: "se/ok.wav" });
//   var buf = Assets.getAudio("bgm");       // AudioBuffer
//   Assets.play("bgm", { loop: true, volume: 0.5, group: Assets.bgmGroup });
//
// 出力経路:
//   source ──┬─ bgmGroup ─┐
//            └─ seGroup ──┴── ctx master (= AudioEngine master) ── destination
//   - master 音量: Assets.audioContext.masterVolume = 0..1
//   - BGM 音量:    Assets.bgmGroup.volume          = 0..1
//   - SE 音量:     Assets.seGroup.volume           = 0..1
//
// --- SoundManager (BGM クロスフェード / SE) ---
// SoundManager.playBgm(alias, { fadeIn, volume });
//   - 同 alias を再生中なら no-op、別 alias ならクロスフェード (BGM 用 localGain 経由)
//   - 実 audio 経路は Assets.bgmGroup に attach
// SoundManager.stopBgm(fadeOut);
// SoundManager.pauseBgm(level, dur);   // ダッキング (Pause メニュー等)
// SoundManager.resumeBgm(dur);
// SoundManager.playSe(alias, { volume });   // Assets.seGroup に attach するだけ
// SoundManager.tick();                  // AudioGroup ベース化で no-op に。互換のため残置
//
// --- SaveData (localStorage 上のセーブスロット管理) ---
// SaveData.init({
//     namespace: "mygame",
//     slots: 3,
//     schemaVersion: 1,
//     migrate: function(data, fromVer, toVer) {     // 任意
//         // 旧スキーマから新スキーマへ変換 (null 返却で「読めない」扱い)
//         return data;
//     },
// });
//
// SaveData.save(slot, data, { label: "..." });      // スロットに保存
// var d  = SaveData.load(slot);                     // スロットから読込 (無ければ null)
// SaveData.delete(slot);                            // 削除
// SaveData.exists(slot);                            // 真偽
// SaveData.info(slot);                              // { exists, savedAt, label, schemaVersion }
// SaveData.list();                                  // info の配列 (各エントリに slot 番号も含む)
// SaveData.latestSlot();                            // 直近に save した slot (delete で追随、無ければ -1)
// SaveData.loadLatest();                            // { slot, data } or null (Continue 用)
//
// SaveData.quickSave(data, opts);                   // slot とは独立した 1 枠
// SaveData.quickLoad();
// SaveData.quickInfo();
// SaveData.quickDelete();
//
// SaveData.wipeAll();                               // 全消去 (デバッグ用)
//
// 内部キー: {namespace}:slot:{n} / {namespace}:quick / {namespace}:meta
// envelope: { version, savedAt, label, data }
// schemaVersion 不一致時に migrate が無ければ load は null を返す。

// ************************************************************
// ポリフィル / ブラウザシム（pixi.js 等のライブラリ動作用）
// ************************************************************
//
// lib/polyfill.js:
//   ブラウザ互換ポリフィル（QuickJS は ES2023 対応のため最小限）。
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
//   loadScript("lib/pixi.min.js");  // pixi.js v7.4.3 動作確認済み
//   var THREE = loadModule("lib/three.module.min.js");  // three.js r176 (ESM)
