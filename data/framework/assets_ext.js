// ============================================================
// Assets — 音声プリロード + AudioGroup 出力経路 + PIXI フォントローダー
// ============================================================
//
// 提供する globalThis.Assets:
//   audioContext            — Web Audio の AudioContext
//   bgmGroup                — BGM 用 AudioGroup (jsengine 拡張、ma_sound_group ラッパ)
//   seGroup                 — SE 用 AudioGroup (同上)
//   preloadAudio(map)       — { alias: "path/to.wav" } の map を受けて全部 decode、Promise を返す
//   getAudio(alias)         — ロード済 AudioBuffer 取得
//   play(alias, opts)       — 即時再生 (loop / volume / group 指定可)
//
// 出力グラフ:
//   source ──┬─ bgmGroup ─┐
//            └─ seGroup ──┴── ctx (master volume) ── destination
//
// 旧 API との違い:
//   - 廃止: Assets.masterGain / bgmGain / seGain (GainNode 多段チェーン)
//   - 追加: Assets.bgmGroup / Assets.seGroup (AudioGroup、ma_sound_group ベース)
//   - master 音量は ctx.masterVolume を直接代入する (AudioEngine の master)
//   - source.group = group で attach。GainNode 経由のグルーピングは不要
//
// 注: 音声は PIXI.Assets の LoadParser ではなく自前 preloadAudio で扱う。PIXI v7 のリゾルバが
//     ブラウザ標準の URL コンストラクタに依存しており jsengine の URL シム (createObjectURL のみ)
//     と非互換で TypeError になるため。フォントは PIXI.Assets でも普通に通る。
//
// 使い方:
//   await Assets.preloadAudio({ bgm_title: "bgm/title.wav", se_ok: "se/ok.wav" });
//   var bgm = Assets.play("bgm_title", { loop: true, volume: 0.5, group: Assets.bgmGroup });
//   Assets.bgmGroup.volume = 0.7;     // BGM だけ音量変更
//   Assets.audioContext.masterVolume = 0.5;  // master 全体
//
//   // フォントは PIXI 経由:
//   PIXI.Assets.add({ alias: "ui_font", src: "fonts/Roboto-Regular.ttf" });
//   await PIXI.Assets.load("ui_font");
//   var info = PIXI.Assets.get("ui_font");  // { url, family, style }
//   ctx.font = "24px " + info.family;

(function() {

if (typeof PIXI === "undefined" || !PIXI.Assets || !PIXI.extensions) {
    console.error("framework/assets_ext.js: PIXI が未ロードか、Assets/extensions が利用不可");
    return;
}

// ロードした AudioBuffer はこの AudioContext に紐づく。
// 出力グラフ:
//   source ──┬─ bgmGroup ─┐
//            └─ seGroup ──┴── ctx master (= AudioEngine master) ── destination
// master 音量は audioCtx.masterVolume を代入。bgm/se は AudioGroup.volume を代入。
var audioCtx = new AudioContext();
var bgmGroup = audioCtx.createGroup();
var seGroup  = audioCtx.createGroup();
bgmGroup.volume = 1.0;
seGroup.volume  = 1.0;

// 拡張子で判定するヘルパー
function hasExt(url, exts) {
    var clean = String(url).split("?")[0].split("#")[0].toLowerCase();
    for (var i = 0; i < exts.length; i++) {
        if (clean.endsWith(exts[i])) return true;
    }
    return false;
}

// 音声バッファのキャッシュ (alias → AudioBuffer)。
// 注: PIXI.Assets の LoadParser でも実装可能だが、PIXI v7 のリゾルバが
//     URL コンストラクタ (ブラウザ標準) に依存していて jsengine の URL シム
//     (createObjectURL のみ) と互換性が無く "TypeError: not a function" で
//     失敗する。代わりに自前 preload で fetch + decodeAudioData する。
var audioBuffers = {};

function preloadAudio(map) {
    // map: { alias: "path/to.wav", ... }
    var aliases = [];
    var promises = [];
    for (var alias in map) {
        if (!map.hasOwnProperty(alias)) continue;
        if (audioBuffers[alias]) continue;  // 既にロード済
        (function(a, p) {
            aliases.push(a);
            promises.push(
                fetch(p)
                    .then(function(r) { return r.arrayBuffer(); })
                    .then(function(ab) { return audioCtx.decodeAudioData(ab); })
                    .then(function(buf) { audioBuffers[a] = buf; })
            );
        })(alias, map[alias]);
    }
    return Promise.all(promises).then(function() { return aliases; });
}

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

PIXI.extensions.add(fontLoader);

// グローバルから AudioContext / グループにアクセスできるようにする
globalThis.Assets = {
    audioContext: audioCtx,
    bgmGroup:     bgmGroup,    // BGM グループ (SoundManager.playBgm 経由)
    seGroup:      seGroup,     // SE グループ (SoundManager.playSe 経由)
    // 音声プリロード (PIXI.Assets を経由しない自前ルート)。
    //   Assets.preloadAudio({ bgm_title: "bgm/title.wav", ... })
    //   .then(function(aliases) { ... });
    preloadAudio: preloadAudio,
    // 音声バッファ取得 (alias → AudioBuffer)
    getAudio: function(alias) { return audioBuffers[alias]; },
    // 便利ヘルパー: ロード済 AudioBuffer を即再生
    //   opts: { loop, volume, group } — group 省略時は master 直結 (= bgm/se どちらでもない)
    //   戻り値: source — JS 側で参照を捨てても再生中は GC されない (selfHold)
    play: function(alias, opts) {
        var buf = audioBuffers[alias];
        if (!buf) { console.error("Assets.play: not loaded:", alias); return null; }
        var src = audioCtx.createBufferSource();
        src.buffer = buf;
        src.loop   = !!(opts && opts.loop);
        if (opts && opts.group) src.group = opts.group;
        if (opts && typeof opts.volume === "number") src.volume = opts.volume;
        src.start();
        return src;
    },
};

console.log("framework/assets_ext.js loaded (font parser registered, audio via preloadAudio, bgm/se AudioGroups ready)");

})();
