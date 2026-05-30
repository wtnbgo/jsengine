// ============================================================
// PIXI.Assets 拡張 — 音声 / フォントローダーを追加
// ============================================================
//
// PIXI v7 標準の Assets は image / JSON / spritesheet / bitmap font / video は
// 扱えるが、Web Audio の AudioBuffer とゲーム用 TTF/OTF はカバーされない。
// extensions.add で LoadParser を 2 つ追加してそれを埋める。
//
// 使い方:
//   PIXI.Assets.add({ alias: "bgm_title", src: "bgm/title.mp3" });
//   PIXI.Assets.add({ alias: "ui_font",   src: "fonts/Roboto-Regular.ttf" });
//   await PIXI.Assets.load(["bgm_title", "ui_font"]);
//   var buf = PIXI.Assets.get("bgm_title");   // AudioBuffer
//   // フォント側はファミリー名で Canvas2D.font に使う:
//   var info = PIXI.Assets.get("ui_font");    // { url, family, style }
//   ctx.font = "24px " + info.family;
//
// グローバル:
//   Assets.audioContext  — ローダー内部で使う AudioContext (再生時にも使える)

(function() {

if (typeof PIXI === "undefined" || !PIXI.Assets || !PIXI.extensions) {
    console.error("framework/assets_ext.js: PIXI が未ロードか、Assets/extensions が利用不可");
    return;
}

// ロードした AudioBuffer はこの AudioContext に紐づく。
// アプリ側で再生する AudioBufferSourceNode もこの context から作る:
//   var src = Assets.audioContext.createBufferSource();
//   src.buffer = PIXI.Assets.get("bgm");
//   src.connect(Assets.audioContext.destination);
//   src.start();
var audioCtx = new AudioContext();

// 拡張子で判定するヘルパー
function hasExt(url, exts) {
    var clean = String(url).split("?")[0].split("#")[0].toLowerCase();
    for (var i = 0; i < exts.length; i++) {
        if (clean.endsWith(exts[i])) return true;
    }
    return false;
}

// ----- 音声ローダー -----
var audioLoader = {
    extension: {
        type: PIXI.ExtensionType.LoadParser,
        priority: PIXI.LoaderParserPriority ? PIXI.LoaderParserPriority.High : 2,
        name: "audioBuffer",
    },
    name: "audioBuffer",
    test: function(url) {
        return hasExt(url, [".mp3", ".wav", ".ogg", ".flac", ".opus", ".m4a"]);
    },
    load: function(url) {
        // fetch → arrayBuffer → decodeAudioData
        return fetch(url)
            .then(function(r) { return r.arrayBuffer(); })
            .then(function(ab) { return audioCtx.decodeAudioData(ab); });
    },
    unload: function(_buffer) {
        // AudioBuffer に明示的な destroy は無いので GC 任せ
    },
};

// ----- フォントローダー -----
// 注: PIXI v7 にも WebFont ローダーがあるが、jsengine では ThorVG (Canvas2D.loadFont)
//     に直接渡したいので priority: High で上書きする
var fontLoader = {
    extension: {
        type: PIXI.ExtensionType.LoadParser,
        priority: PIXI.LoaderParserPriority ? PIXI.LoaderParserPriority.High : 2,
        name: "jsengineFont",
    },
    name: "jsengineFont",
    test: function(url) {
        return hasExt(url, [".ttf", ".otf"]);
    },
    load: function(url) {
        // Canvas2D.loadFont は jsengine の base path 解決を通すので、
        // PIXI が組み立てた絶対 URL ではなく相対パスをそのまま渡す
        if (typeof Canvas2D === "undefined" || !Canvas2D.loadFont) {
            return Promise.reject(new Error("Canvas2D.loadFont not available"));
        }
        try {
            Canvas2D.loadFont(url);
        } catch (e) {
            return Promise.reject(e);
        }
        // フォント名を取り出す
        var info = (Canvas2D.fontInfo && Canvas2D.fontInfo(url)) || {};
        return Promise.resolve({
            url: url,
            family: info.family || url,
            style:  info.style  || "Regular",
        });
    },
    unload: function(_info) {
        // ThorVG にフォント unload API は無いので保持しっぱなし
    },
};

PIXI.extensions.add(audioLoader);
PIXI.extensions.add(fontLoader);

// グローバルから AudioContext にアクセスできるようにする
globalThis.Assets = {
    audioContext: audioCtx,
    // 便利ヘルパー: ロード済 AudioBuffer を即再生
    play: function(alias, opts) {
        var buf = PIXI.Assets.get(alias);
        if (!buf) { console.error("Assets.play: not loaded:", alias); return null; }
        var src = audioCtx.createBufferSource();
        src.buffer = buf;
        src.loop   = !!(opts && opts.loop);
        var gain = audioCtx.createGain();
        gain.gain.value = (opts && typeof opts.volume === "number") ? opts.volume : 1.0;
        src.connect(gain).connect(audioCtx.destination);
        src.start();
        return { source: src, gain: gain };
    },
};

console.log("framework/assets_ext.js loaded (audio + font parsers registered)");

})();
