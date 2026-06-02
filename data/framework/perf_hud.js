// ============================================================
// PerfHud — FPS / フレーム時間 / draw call の常駐 HUD
// ============================================================
//
// ゴール:
//   - F3 で OFF / Minimal (FPS のみ) / Full (FPS + ms + draw call + 詳細) を循環
//   - WebGL draw call カウント (gl.drawArrays/Elements 系のモンキーパッチ)
//   - 1 秒移動平均の FPS、現フレーム ms、直近 1 秒間の最大 ms
//   - PIXI が利用可能なら PIXI.Text の左上オーバーレイを提供
//   - 任意の demo / フレームワークで使えるよう、表示は attach 側に依存しない
//     (デモが PIXI を使わないなら PerfHud.text() の戻り値を独自描画してもよい)
//
// 提供 (globalThis.PerfHud):
//   PerfHud.init({ instrumentGL, hotkey })   — 初期化 (1 度だけ)
//                                              instrumentGL=false で gl 計測スキップ
//                                              hotkey="F3" 既定 (false で無効化)
//   PerfHud.update(deltaMs)                   — 毎フレーム呼ぶ (FPS 計算 + ms 更新 + draw call ロール)
//   PerfHud.refresh()                         — オーバーレイの可視性/テキストを反映
//   PerfHud.attachPixi(parentContainer)       — PIXI 子要素として HUD を追加 (上層)
//   PerfHud.setDetail(level)                  — 0=off / 1=minimal / 2=full
//   PerfHud.getDetail() / PerfHud.isVisible()
//   PerfHud.toggle()                          — 0→1→2→0 を循環
//   PerfHud.text()                            — 現在の表示文字列 (改行入り)
//   PerfHud.set(label, value)                 — カスタム行を追加 / 更新
//   PerfHud.unset(label)
//   PerfHud.stats()                           — { fps, ms, msMax1s, drawCalls } の生値
//
// 使い方 (PIXI 系):
//   PerfHud.init();                                // 1 回
//   PerfHud.attachPixi(pixiApp.stage);              // 1 回 (sceneRoot より上層へ)
//   demo.update = function(dt) {
//       PerfHud.update(dt);
//       /* 自分の処理 */
//       PerfHud.refresh();
//   };
//
// 使い方 (PIXI 不使用):
//   PerfHud.init();
//   demo.update = function(dt) {
//       PerfHud.update(dt);
//       /* 自分の処理 */
//       var s = PerfHud.text();                     // 文字列を Canvas2D 等で描画
//   };

(function() {

if (typeof globalThis.PerfHud !== "undefined") return;

// --- 内部状態 ---
var initialized = false;
var detail = 0;                  // 0=off / 1=minimal / 2=full
var hotkey = "F3";

// FPS / 時間
var frameAccumMs = 0;
var frameAccumCount = 0;
var msCurrent = 0;
var msMax1s = 0;
var fpsCurrent = 0;
var samplePeriodMs = 500;        // 0.5 秒平均

// Draw calls
var glInstrumented = false;
var drawCallsCurrent = 0;        // 現フレーム蓄積中
var drawCallsLastFrame = 0;      // 直前フレームの確定値

// カスタム行 (label -> string)
var customRows = {};

// PIXI オーバーレイ
var pixiOverlay = null;
var pixiContainer = null;

// --- 内部ヘルパー ---
function instrumentGL() {
    if (glInstrumented) return;
    if (typeof gl === "undefined" || !gl) return;
    var names = ["drawArrays", "drawElements", "drawArraysInstanced", "drawElementsInstanced"];
    for (var i = 0; i < names.length; i++) {
        (function(n) {
            var orig = gl[n];
            if (typeof orig !== "function") return;
            gl[n] = function() {
                drawCallsCurrent++;
                return orig.apply(gl, arguments);
            };
        })(names[i]);
    }
    glInstrumented = true;
}

function setupHotkey() {
    if (!hotkey) return;
    addEventListener("keydown", function(e) {
        if (e.code === hotkey) {
            detail = (detail + 1) % 3;
            if (pixiOverlay) pixiOverlay.visible = (detail > 0);
        }
    });
}

function formatNumber(n, digits) {
    if (typeof n !== "number" || isNaN(n)) return "—";
    return n.toFixed(digits);
}

// --- API ---
globalThis.PerfHud = {
    init: function(opts) {
        if (initialized) return;
        opts = opts || {};
        if (typeof opts.hotkey === "string") hotkey = opts.hotkey;
        else if (opts.hotkey === false) hotkey = null;
        setupHotkey();
        if (opts.instrumentGL !== false) instrumentGL();
        initialized = true;
    },

    update: function(deltaMs) {
        var dt = (typeof deltaMs === "number" && isFinite(deltaMs)) ? deltaMs : 0;
        msCurrent = dt;
        if (dt > msMax1s) msMax1s = dt;
        frameAccumMs += dt;
        frameAccumCount++;
        if (frameAccumMs >= samplePeriodMs && frameAccumCount > 0) {
            // 平均 FPS = (フレーム数 * 1000) / 蓄積 ms
            fpsCurrent = (frameAccumCount * 1000.0) / frameAccumMs;
            frameAccumMs = 0;
            frameAccumCount = 0;
            // ms の最大値もウィンドウ更新時にリセット
            msMax1s = msCurrent;
        }
        // draw call をフレーム境界で確定して 0 に戻す
        drawCallsLastFrame = drawCallsCurrent;
        drawCallsCurrent = 0;
    },

    refresh: function() {
        if (!pixiOverlay) return;
        if (detail === 0) {
            pixiOverlay.visible = false;
            return;
        }
        pixiOverlay.visible = true;
        pixiOverlay.text = this.text();
        // 親 container が他のシーン子要素を後から addChild した場合に
        // 上層を維持するため、毎回最後に持ち上げる
        if (pixiContainer && pixiOverlay.parent === pixiContainer) {
            try { pixiContainer.setChildIndex(pixiOverlay, pixiContainer.children.length - 1); }
            catch (_) {}
        }
    },

    text: function() {
        if (detail === 0) return "";
        if (detail === 1) {
            return Math.round(fpsCurrent) + " fps";
        }
        // detail === 2: フル
        var lines = [
            "FPS:  " + Math.round(fpsCurrent),
            "ms:   " + formatNumber(msCurrent, 1) + "  max1s " + formatNumber(msMax1s, 1),
            "Draw: " + drawCallsLastFrame,
        ];
        for (var k in customRows) {
            if (customRows.hasOwnProperty(k)) lines.push(k + ": " + customRows[k]);
        }
        return lines.join("\n");
    },

    stats: function() {
        return {
            fps:        fpsCurrent,
            ms:         msCurrent,
            msMax1s:    msMax1s,
            drawCalls:  drawCallsLastFrame,
        };
    },

    set: function(label, value) { customRows[label] = String(value); },
    unset: function(label)       { delete customRows[label]; },

    setDetail: function(d) {
        detail = ((d | 0) % 3 + 3) % 3;
        if (pixiOverlay) pixiOverlay.visible = (detail > 0);
    },
    getDetail: function() { return detail; },
    isVisible: function() { return detail > 0; },
    toggle:    function() { this.setDetail(detail + 1); },

    attachPixi: function(parent, opts) {
        if (typeof PIXI === "undefined") {
            console.warn("PerfHud.attachPixi: PIXI が無い");
            return null;
        }
        if (pixiOverlay && pixiOverlay.parent) {
            pixiOverlay.parent.removeChild(pixiOverlay);
        }
        opts = opts || {};
        if (!pixiOverlay) {
            pixiOverlay = new PIXI.Text("", {
                fontFamily: opts.fontFamily || "monospace",
                fontSize:   opts.fontSize   || 14,
                fill:       (typeof opts.fill === "number") ? opts.fill : 0x00ff66,
                stroke:     0x000000,
                strokeThickness: 3,
                lineJoin:   "round",
            });
            pixiOverlay.x = opts.x || 8;
            pixiOverlay.y = opts.y || 8;
            pixiOverlay.zIndex = 9999;
        }
        pixiOverlay.visible = (detail > 0);
        pixiContainer = parent;
        parent.addChild(pixiOverlay);
        return pixiOverlay;
    },

    detach: function() {
        if (pixiOverlay && pixiOverlay.parent) pixiOverlay.parent.removeChild(pixiOverlay);
        pixiContainer = null;
    },
};

console.log("framework/perf_hud.js loaded");

})();
