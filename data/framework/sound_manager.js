// ============================================================
// SoundManager — BGM クロスフェード / SE 単発再生のヘルパー
// ============================================================
//
// Assets.preloadAudio() でロード済みの AudioBuffer を前提とした
// 薄いラッパー。すべての出力は Assets.masterGain → destination に流れる
// ので、マスター音量制御は Settings 側で Assets.masterGain.gain.value を
// 弄れば一括で効く。
//
// 提供 API (globalThis.SoundManager):
//   playBgm(alias, opts)
//     - opts.fadeIn   : ms (default 500)  立ち上がりの linear ramp
//     - opts.volume   : 0..1 (default 1)  目標ボリューム
//     - 既に同じ alias の BGM が鳴っていれば何もしない
//     - 別 BGM が鳴っていればクロスフェード (前の BGM は fadeOut で消す)
//   stopBgm(fadeOut)
//     - 現在の BGM を fadeOut ms で消す (default 500)
//   pauseBgm(level, dur)
//     - 一時的にダッキング (Pause メニュー等)。level=0..1, dur=ms (default 200)
//   resumeBgm(dur)
//     - pauseBgm を解除して元のボリュームに戻す
//   playSe(alias, opts)
//     - opts.volume : 0..1 (default 1)
//
// 使用例:
//   SoundManager.playBgm("bgm_title", { fadeIn: 800, volume: 0.6 });
//   SoundManager.playSe("se_confirm");
//   SoundManager.pauseBgm(0.3, 150);   // ダッキング
//   SoundManager.resumeBgm(150);

(function() {

if (typeof Assets === "undefined" || !Assets.audioContext) {
    console.error("framework/sound_manager.js: Assets / audioContext が無い (assets_ext.js を先に loadScript してください)");
    return;
}

var ctx = Assets.audioContext;

// 現在の BGM
//   { alias, source, gain, targetVolume }
var currentBgm = null;
var isDucked = false;
var savedVolumeBeforeDuck = 1.0;

// 再生中の SE / ワンショット source のリスト。
// 注: ここで参照を保持しないと QuickJS GC が JS の AudioBufferSourceNode / GainNode を
//     回収して finalizer が走り、s.connectedGain が null 化されてチェーンが切れる
//     (master/SE 音量が SE に効かなくなる)。再生終了 (source.ended==true) を見て掃除する。
var activeSounds = [];

function now() { return ctx.currentTime; }

function fadeGain(gainNode, from, to, durMs) {
    var t0 = now();
    var t1 = t0 + Math.max(0.001, durMs / 1000);
    try {
        gainNode.gain.cancelScheduledValues(t0);
        gainNode.gain.setValueAtTime(from, t0);
        gainNode.gain.linearRampToValueAtTime(to, t1);
    } catch (e) {
        // フォールバック: 即時セット
        gainNode.gain.value = to;
    }
}

function stopBgmImmediate(bgm, fadeOutMs) {
    if (!bgm || !bgm.source) return;
    var cur = bgm.gain.gain.value;
    fadeGain(bgm.gain, cur, 0.0, fadeOutMs);
    // 自然消滅させる: フェード完了後に stop() を呼ぶ
    var src = bgm.source;
    setTimeout(function() {
        try { src.stop(); } catch (_) {}
    }, fadeOutMs + 50);
}

globalThis.SoundManager = {
    playBgm: function(alias, opts) {
        opts = opts || {};
        var fadeIn = (typeof opts.fadeIn === "number") ? opts.fadeIn : 500;
        var targetVol = (typeof opts.volume === "number") ? opts.volume : 1.0;

        // 同じ BGM がもう鳴ってる場合は何もしない
        if (currentBgm && currentBgm.alias === alias) {
            return currentBgm;
        }
        // 別 BGM が鳴ってればフェードアウト
        if (currentBgm) {
            stopBgmImmediate(currentBgm, fadeIn);
        }

        var buf = Assets.getAudio(alias);
        if (!buf) { console.error("SoundManager.playBgm: not loaded:", alias); return null; }

        var src = ctx.createBufferSource();
        src.buffer = buf;
        src.loop   = true;
        var g = ctx.createGain();
        g.gain.value = 0.0;
        src.connect(g).connect(Assets.bgmGain || Assets.masterGain);
        src.start();
        // フェードイン
        fadeGain(g, 0.0, targetVol, fadeIn);

        currentBgm = { alias: alias, source: src, gain: g, targetVolume: targetVol };
        isDucked = false;
        return currentBgm;
    },

    stopBgm: function(fadeOut) {
        if (!currentBgm) return;
        stopBgmImmediate(currentBgm, (typeof fadeOut === "number") ? fadeOut : 500);
        currentBgm = null;
        isDucked = false;
    },

    pauseBgm: function(level, dur) {
        if (!currentBgm) return;
        var d = (typeof dur === "number") ? dur : 200;
        var lv = (typeof level === "number") ? level : 0.3;
        if (!isDucked) {
            savedVolumeBeforeDuck = currentBgm.targetVolume;
            isDucked = true;
        }
        var from = currentBgm.gain.gain.value;
        fadeGain(currentBgm.gain, from, lv, d);
    },

    resumeBgm: function(dur) {
        if (!currentBgm) return;
        var d = (typeof dur === "number") ? dur : 200;
        var from = currentBgm.gain.gain.value;
        var to = isDucked ? savedVolumeBeforeDuck : currentBgm.targetVolume;
        fadeGain(currentBgm.gain, from, to, d);
        isDucked = false;
    },

    playSe: function(alias, opts) {
        opts = opts || {};
        var vol = (typeof opts.volume === "number") ? opts.volume : 1.0;
        var buf = Assets.getAudio(alias);
        if (!buf) { console.error("SoundManager.playSe: not loaded:", alias); return null; }
        var src = ctx.createBufferSource();
        src.buffer = buf;
        var g = ctx.createGain();
        g.gain.value = vol;
        src.connect(g).connect(Assets.seGain || Assets.masterGain);
        src.start();
        var entry = { source: src, gain: g };
        activeSounds.push(entry);  // GC されないよう参照保持。tick() で掃除
        return entry;
    },

    // 毎フレーム呼ぶ。再生終了した SE エントリを掃除する。
    tick: function() {
        for (var i = activeSounds.length - 1; i >= 0; i--) {
            if (activeSounds[i].source.ended) activeSounds.splice(i, 1);
        }
    },

    // 内部用 (デバッグ向け)
    _current: function() { return currentBgm; },
    _activeCount: function() { return activeSounds.length; },
};

console.log("framework/sound_manager.js loaded");

})();
