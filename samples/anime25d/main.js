// ============================================================
// anime25d — Anime2.5DRig 移植サンプル
// ============================================================
//
// パーツ分け PSD を自動リギングして動かす 2.5D アバター
// (https://github.com/852wa/Anime2.5DRig の jsengine 移植)。
//
// 構成 (組み込みライブラリとして分割):
//   lib/     : ag-psd / rigger / genericparts (上流のまま、無改変)
//   rig25d/  : loader.js  — データ作製部 (PSD → rig + キャッシュ)
//              avatar.js  — 表示機能部 (Avatar25D)
//              ui_panel.js — テスト操作 UI 部 (Canvas2D パネル)
//
// 起動: jsengine.exe -data samples/anime25d
// 起動後、左上の「画像を読み込む」からパーツ分け PSD を選択する。
// 初回はリグ生成に 1 分前後かかる (以後は <psd>.rig キャッシュで即表示)。
"use strict";

loadScript("lib/ag-psd.min.js");
loadScript("lib/rigger.js");
loadScript("lib/genericparts.js");
loadScript("rig25d/loader.js");
loadScript("rig25d/avatar.js");
loadScript("rig25d/ui_panel.js");

Canvas2D.loadFont("fonts/NotoSansJP-Regular.otf");

var SCREEN_W = (typeof window !== "undefined" && window.innerWidth) || 1280;
var SCREEN_H = (typeof window !== "undefined" && window.innerHeight) || 720;

// idle (未ロード) → build-pending (1 フレーム描画) → building (同期ビルド) → ready
var state = "idle";
var buildingMsg = "";
var currentPsdPath = null;   // ファイルダイアログで選ばれた絶対パス
var dialogOpen = false;
var avatar = null;
var panel = null;
var rigInfo = "";

// 背景 (クリアカラー) 切替
var BGS = [
    { name: "dark", c: [0.078, 0.082, 0.11, 1] },
    { name: "green", c: [0.0, 0.694, 0.251, 1] },
    { name: "gray", c: [0.35, 0.35, 0.38, 1] }
];
var bgIndex = 0;

// アバター表示レイアウト (パネルを除いた左側にフィット)
var view = { x: 0, y: 0, scale: 1 };
function layoutView() {
    if (!avatar) return;
    var availW = SCREEN_W - UIPanel25D.PANEL_W, availH = SCREEN_H;
    view.scale = Math.min(availW / avatar.CW, availH / avatar.CH);
    view.x = (availW - avatar.CW * view.scale) / 2;
    view.y = (availH - avatar.CH * view.scale) / 2;
}

// ---------- 全画面テクスチャブリット (共通最小実装) ----------
var _bprog = null, _bvbo = null, _bibo = null, _bpos, _buv;
// (x,y,w,h) スクリーン px 位置にテクスチャを合成。省略時は全画面。
function blitTex(tex, x, y, w, h) {
    if (!_bprog) {
        function sh(t, s) {
            var o = gl.createShader(t);
            gl.shaderSource(o, s); gl.compileShader(o);
            return o;
        }
        var vs = sh(gl.VERTEX_SHADER,
            "attribute vec2 aPos; attribute vec2 aUV; varying vec2 vUV;" +
            "void main(){ vUV=aUV; gl_Position=vec4(aPos,0.0,1.0); }");
        var fs = sh(gl.FRAGMENT_SHADER,
            "precision mediump float; varying vec2 vUV; uniform sampler2D uTex;" +
            "void main(){ gl_FragColor=texture2D(uTex,vUV); }");
        _bprog = gl.createProgram();
        gl.attachShader(_bprog, vs); gl.attachShader(_bprog, fs);
        gl.linkProgram(_bprog);
        _bpos = gl.getAttribLocation(_bprog, "aPos");
        _buv = gl.getAttribLocation(_bprog, "aUV");
        _bvbo = gl.createBuffer();
        _bibo = gl.createBuffer();
        gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, _bibo);
        gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint16Array([0, 1, 2, 0, 2, 3]), gl.STATIC_DRAW);
    }
    var x0 = -1, y0 = 1, x1 = 1, y1 = -1;
    if (w !== undefined) {
        x0 = x / SCREEN_W * 2 - 1;
        y0 = 1 - y / SCREEN_H * 2;
        x1 = (x + w) / SCREEN_W * 2 - 1;
        y1 = 1 - (y + h) / SCREEN_H * 2;
    }
    gl.useProgram(_bprog);
    gl.disable(gl.DEPTH_TEST); gl.disable(gl.CULL_FACE);
    gl.disable(gl.SCISSOR_TEST); gl.disable(gl.STENCIL_TEST);
    gl.enable(gl.BLEND);
    gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.bindBuffer(gl.ARRAY_BUFFER, _bvbo);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
        x0, y0, 0, 0,  x1, y0, 1, 0,  x1, y1, 1, 1,  x0, y1, 0, 1
    ]), gl.DYNAMIC_DRAW);
    gl.enableVertexAttribArray(_bpos);
    gl.vertexAttribPointer(_bpos, 2, gl.FLOAT, false, 16, 0);
    gl.enableVertexAttribArray(_buv);
    gl.vertexAttribPointer(_buv, 2, gl.FLOAT, false, 16, 8);
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, _bibo);
    gl.drawElements(gl.TRIANGLES, 6, gl.UNSIGNED_SHORT, 0);
    gl.disableVertexAttribArray(_bpos);
    gl.disableVertexAttribArray(_buv);
}

// ---------- 左上「画像を読み込む」ボタン ----------
var BTN = { x: 12, y: 12, w: 168, h: 40 };
var btnCanvas = new Canvas2D(BTN.w, BTN.h);
var btnDirty = true;
var btnHover = false;
function drawButton() {
    if (btnDirty) {
        var c = btnCanvas;
        c.clearRect(0, 0, BTN.w, BTN.h);
        var disabled = (state === "building" || state === "build-pending" || dialogOpen);
        c.fillStyle = disabled ? "rgba(40,40,46,0.9)" : (btnHover ? "rgba(190,30,64,0.95)" : "rgba(160,16,48,0.9)");
        c.fillRect(0, 0, BTN.w, BTN.h);
        c.strokeStyle = disabled ? "#55555f" : "#d43b55";
        c.strokeRect(1, 1, BTN.w - 2, BTN.h - 2);
        c.fillStyle = disabled ? "#88888f" : "#ffffff";
        c.font = "16px NotoSansJP-Regular";
        c.textAlign = "center"; c.textBaseline = "middle";
        c.fillText("画像を読み込む", BTN.w / 2, BTN.h / 2 + 1);
        c.flush();
        btnDirty = false;
    }
    blitTex(btnCanvas.texture, BTN.x, BTN.y, BTN.w, BTN.h);
}
function inButton(x, y) {
    return x >= BTN.x && x < BTN.x + BTN.w && y >= BTN.y && y < BTN.y + BTN.h;
}

// ---------- ステータス画面 (idle / building / error) ----------
var loadCanvas = new Canvas2D(SCREEN_W, SCREEN_H);
function drawStatus(msg, sub) {
    if (msg !== drawStatus._last || sub !== drawStatus._lastSub) {
        drawStatus._last = msg; drawStatus._lastSub = sub;
        var c = loadCanvas;
        c.clearRect(0, 0, SCREEN_W, SCREEN_H);
        c.fillStyle = "#d43b55";
        c.fillRect(SCREEN_W / 2 - 70, SCREEN_H / 2 + 34, 140, 2);
        c.fillStyle = "#ececee";
        c.font = "24px NotoSansJP-Regular";
        c.textAlign = "center"; c.textBaseline = "middle";
        c.fillText("Anime2.5DRig on jsengine", SCREEN_W / 2, SCREEN_H / 2 - 30);
        c.font = "15px NotoSansJP-Regular";
        c.fillStyle = "#9a9aa8";
        if (msg) c.fillText(msg, SCREEN_W / 2, SCREEN_H / 2 + 6);
        if (sub) {
            c.font = "12px NotoSansJP-Regular";
            c.fillStyle = "#6a6a75";
            c.fillText(sub, SCREEN_W / 2, SCREEN_H / 2 + 62);
        }
        c.flush();
    }
    blitTex(loadCanvas.texture);
}

// ---------- ファイル選択 ----------
function openPsdDialog() {
    if (dialogOpen || state === "building" || state === "build-pending") return;
    dialogOpen = true;
    btnDirty = true;
    fs.showOpenFileDialog([
        { name: "PSD ファイル", pattern: "psd" },
        { name: "すべてのファイル", pattern: "*" }
    ]).then(function (path) {
        dialogOpen = false;
        btnDirty = true;
        if (!path) return;   // キャンセル
        currentPsdPath = path;
        buildingMsg = fs.exists(Rig25D.cachePathOf(path))
            ? "リグキャッシュ読込中…"
            : "リグ生成中… 初回は 1 分ほどかかります (処理中はウィンドウが応答なしになりますが異常ではありません)";
        state = "build-pending";
        console.log("[anime25d] selected: " + path);
    }).catch(function (e) {
        dialogOpen = false;
        btnDirty = true;
        console.log("[anime25d] dialog error: " + e.message);
    });
}

// ---------- リグ生成 (同期・重い) ----------
function buildNow() {
    var t0 = Date.now();
    var hadCache = fs.exists(Rig25D.cachePathOf(currentPsdPath));
    var rig = Rig25D.loadRig(currentPsdPath, {
        onStage: function (st) { console.log("[anime25d] stage: " + st); }
    });
    if (!avatar) avatar = new Avatar25D();
    avatar.setRig(rig);
    avatar.resetParams();
    layoutView();
    if (!panel) {
        panel = new UIPanel25D(avatar, {
            screenW: SCREEN_W, screenH: SCREEN_H,
            onRebuild: function () {
                if (!currentPsdPath) return;
                Rig25D.clearCache(currentPsdPath);
                buildingMsg = "リグ再生成中… 1 分ほどかかります (処理中はウィンドウが応答なしになりますが異常ではありません)";
                state = "build-pending";
            },
            onBg: function () { bgIndex = (bgIndex + 1) % BGS.length; }
        });
    } else {
        panel.activePreset = null;
        panel.refresh();
    }
    var nStr = 0;
    for (var i = 0; i < avatar.layers.length; i++)
        nStr += avatar.layers[i].strands ? avatar.layers[i].strands.length : 0;
    rigInfo = avatar.layers.length + "パーツ / 髪" + nStr + "房 (" +
              (hadCache ? "キャッシュ" : "生成 " + Math.round((Date.now() - t0) / 1000) + "s") + ")";
    console.log("[anime25d] ready: " + rigInfo);
    if (avatar.warnings.length) avatar.warnings.forEach(function (w) { console.log("[anime25d] warn: " + w); });
    state = "ready";
    btnDirty = true;
}

// ---------- ライフサイクル ----------
function update(dt) {
    if (state === "build-pending") {
        // ステータス画面を 1 フレーム描画してから同期ビルドに入る
        state = "building-draw";
        return;
    }
    if (state === "building-draw") {
        state = "building";
        return;
    }
    if (state === "building") {
        try {
            buildNow();
        } catch (e) {
            buildingMsg = "エラー: " + e.message;
            console.log("[anime25d] build failed: " + e.message + "\n" + e.stack);
            state = "error";
            btnDirty = true;
        }
        return;
    }
    if (state === "ready" && avatar) {
        avatar.update(dt);
    }
}

function render() {
    var bg = BGS[bgIndex].c;
    gl.viewport(0, 0, SCREEN_W, SCREEN_H);
    gl.clearColor(bg[0], bg[1], bg[2], bg[3]);
    gl.clearStencil(0);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT | gl.STENCIL_BUFFER_BIT);

    if (state === "idle") {
        drawStatus("左上の「画像を読み込む」からパーツ分け PSD を選択してください",
                   "レイヤー命名規約: face / eyewhite / irides / mouth_open / front hair など (README 参照)");
        drawButton();
        return;
    }
    if (state === "building" || state === "building-draw" || state === "build-pending") {
        drawStatus(buildingMsg, currentPsdPath || "");
        return;
    }
    if (state === "error") {
        drawStatus(buildingMsg, "別のファイルを選ぶ場合は左上のボタンからやり直してください");
        drawButton();
        return;
    }
    // ready
    avatar.render(view.x, view.y, view.scale, SCREEN_W, SCREEN_H);
    panel.draw();
    drawButton();
}

function done() {
    if (avatar) avatar.dispose();
}

globalThis.update = update;
globalThis.render = render;
globalThis.done = done;

// ---------- 入力 ----------
["mousedown", "mouseup", "mousemove", "wheel"].forEach(function (type) {
    addEventListener(type, function (e) {
        // 「画像を読み込む」ボタン (常時有効)
        if (type === "mousemove") {
            var h = inButton(e.clientX, e.clientY);
            if (h !== btnHover) { btnHover = h; btnDirty = true; }
        }
        if (type === "mousedown" && inButton(e.clientX, e.clientY)) {
            openPsdDialog();
            return;
        }
        if (state !== "ready") return;
        if (panel.handleEvent(e)) {
            if (type === "mousemove") avatar.mouse.in = false;
            return;
        }
        if (type === "mousemove") {
            // アバター表示域基準の -1..1 (マウス追従用)
            var w = avatar.CW * view.scale, hh = avatar.CH * view.scale;
            avatar.mouse.x = ((e.clientX - view.x) / w) * 2 - 1;
            avatar.mouse.y = ((e.clientY - view.y) / hh) * 2 - 1;
            avatar.mouse.in = e.clientX >= view.x && e.clientX < view.x + w &&
                              e.clientY >= view.y && e.clientY < view.y + hh;
        }
    });
});

addEventListener("keydown", function (e) {
    if (e.code === "KeyO") openPsdDialog();
    if (state !== "ready") return;
    if (e.code === "KeyB") bgIndex = (bgIndex + 1) % BGS.length;
    if (e.code === "KeyR") { avatar.resetParams(); panel.activePreset = null; panel.dirty = true; }
});

// REPL / 外部エージェント用フック (-replfile デバッグ)
globalThis.__anime25d = {
    isReady: function () { return state === "ready"; },
    state: function () { return state; },
    avatar: function () { return avatar; },
    panel: function () { return panel; },
    rigInfo: function () { return rigInfo; },
    // ダイアログを介さず直接ロードする (自動検証用)
    loadPsd: function (path) {
        currentPsdPath = path;
        buildingMsg = "リグ生成中…";
        state = "build-pending";
    }
};

console.log("--- anime25d (Anime2.5DRig port) ---");
console.log("左上ボタン or O: PSD を開く, B: 背景切替, R: 数値リセット");
