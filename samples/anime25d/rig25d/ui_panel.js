// ============================================================
// rig25d/ui_panel.js — テスト操作 UI 部 (Canvas2D パネル)
// ============================================================
//
// index.html の右パネル (スライダー / トグル / 表情プリセット /
// レイヤー順) を Canvas2D オーバーレイ + マウス操作で再現する。
// マイク口パク / カメラ追従は機能カット。
//
// 使い方:
//   const panel = new UIPanel25D(avatar, { screenW, screenH, onRebuild });
//   イベント: consumed = panel.handleEvent(e);  (true なら UI が消費)
//   毎フレーム: panel.draw();                    (GL 描画の最後に呼ぶ)
"use strict";

(function () {

    var PANEL_W = 300;
    var ROW_H = 24, SEC_H = 30, CHIP_H = 24;
    var TRACK_X = 100, TRACK_W = 130;

    var COLORS = {
        bg: "rgba(18,18,22,0.92)", line: "#3a3a44",
        txt: "#ececee", sub: "#9a9aa8",
        acc: "#d43b55", accDim: "rgba(160,16,48,0.35)",
        track: "#33333c", chipOn: "rgba(160,16,48,0.55)", chipOff: "#26262e"
    };

    // スライダー定義 (index.html の range と同一の min/max)
    var SECTIONS = [
        { label: "自動", toggles: [
            ["idle", "アイドル"], ["blink", "まばたき"], ["rand", "ランダム動作"],
            ["talk", "口パク"], ["mouse", "マウス追従"], ["phys", "髪の物理"]
        ] },
        { label: "表情プリセット", presets: [
            ["neutral", "通常"], ["smile", "笑顔"], ["usume", "薄目"], ["surprise", "驚き"],
            ["jito", "ジト目"], ["winkL", "ウインク左"], ["winkR", "ウインク右"]
        ] },
        { label: "顔の向き", sliders: [
            ["angleX", "角度 X", -1, 1], ["angleY", "角度 Y", -1, 1], ["angleZ", "角度 Z", -1, 1]
        ] },
        { label: "目", sliders: [
            ["eyeOpenL", "左目の開き", 0, 1], ["eyeOpenR", "右目の開き", 0, 1],
            ["eyeX", "視線 X", -1, 1], ["eyeY", "視線 Y", -1, 1],
            ["irisScale", "瞳スケール", 0.5, 1.3],
            ["eyeScaleL", "閉じ目スケール左", 0.5, 1.5], ["eyeScaleR", "閉じ目スケール右", 0.5, 1.5],
            ["eyeEase", "閉じやすさ", 0, 1],
            ["eyeCY", "差分 上下", -1, 1], ["eyeCAng", "差分 角度", -1, 1]
        ] },
        { label: "眉", sliders: [
            ["brow", "眉（上下）", -1, 1], ["browAngSym", "眉角度 対称", -1, 1],
            ["browAngL", "眉角度 左", -1, 1], ["browAngR", "眉角度 右", -1, 1]
        ] },
        { label: "口", sliders: [
            ["mouthOpen", "開き", 0, 1], ["mouthForm", "形（笑い）", -1, 1],
            ["mouthCY", "閉じ口 上下", -1, 1], ["mouthEase", "閉じやすさ", 0, 1],
            ["mouthCAng", "閉じ口 角度", -1, 1], ["mouthScale", "口スケール", 0.5, 1.5]
        ] },
        { label: "前髪", sliders: [
            ["fhAmp", "揺れ", 0, 3], ["fhSoft", "柔らかさ", 0, 2],
            ["bangL", "ブロック左", -1, 1], ["bangC", "ブロック中央", -1, 1], ["bangR", "ブロック右", -1, 1]
        ] },
        { label: "体・物理", sliders: [
            ["body", "体の傾き", -1, 1], ["armY", "腕の高さ", -1, 1], ["armPos", "腕位置補正", -1, 1],
            ["bust", "胸の揺れ", 0, 4], ["bustY", "胸位置 上下", -3, 3],
            ["physAmp", "後髪の揺れ", 0, 3], ["soft", "後髪柔らかさ", 0, 3]
        ] },
        { label: "ユーティリティ", buttons: [
            ["reset", "数値リセット"], ["rebuild", "リグ再生成"], ["bg", "背景切替"]
        ] },
        { label: "レイヤー（上=奥 / ◀▶で順変更）", layerList: true }
    ];

    function UIPanel25D(avatar, opts) {
        opts = opts || {};
        this.avatar = avatar;
        this.screenW = opts.screenW || 1280;
        this.screenH = opts.screenH || 720;
        this.x = this.screenW - PANEL_W;
        this.onRebuild = opts.onRebuild || null;
        this.onBg = opts.onBg || null;
        this.font = opts.font || "12px NotoSansJP-Regular";
        this.canvas = new Canvas2D(PANEL_W, this.screenH);
        this.scrollY = 0;
        this.contentH = 0;
        this.items = [];        // {type, y, h, ...} パネルローカル座標 (スクロール前)
        this.dirty = true;
        this.dragItem = null;
        this.activePreset = null;
        this.hoverItem = null;
        this._layout();
    }

    // ---------- レイアウト ----------
    UIPanel25D.prototype._layout = function () {
        var items = [], y = 10;
        for (var si = 0; si < SECTIONS.length; si++) {
            var sec = SECTIONS[si];
            items.push({ type: "section", label: sec.label, y: y, h: SEC_H });
            y += SEC_H;
            var x, i;
            if (sec.toggles) {
                x = 12;
                for (i = 0; i < sec.toggles.length; i++) {
                    var tw = sec.toggles[i][1].length * 13 + 22;
                    if (x + tw > PANEL_W - 10) { x = 12; y += CHIP_H + 4; }
                    items.push({ type: "toggle", key: sec.toggles[i][0], label: sec.toggles[i][1],
                                 x: x, y: y, w: tw, h: CHIP_H });
                    x += tw + 6;
                }
                y += CHIP_H + 10;
            }
            if (sec.presets) {
                x = 12;
                for (i = 0; i < sec.presets.length; i++) {
                    var pw = sec.presets[i][1].length * 13 + 22;
                    if (x + pw > PANEL_W - 10) { x = 12; y += CHIP_H + 4; }
                    items.push({ type: "preset", key: sec.presets[i][0], label: sec.presets[i][1],
                                 x: x, y: y, w: pw, h: CHIP_H });
                    x += pw + 6;
                }
                y += CHIP_H + 10;
            }
            if (sec.sliders) {
                for (i = 0; i < sec.sliders.length; i++) {
                    var s = sec.sliders[i];
                    items.push({ type: "slider", key: s[0], label: s[1], min: s[2], max: s[3],
                                 y: y, h: ROW_H });
                    y += ROW_H;
                }
                y += 6;
            }
            if (sec.buttons) {
                x = 12;
                for (i = 0; i < sec.buttons.length; i++) {
                    var bw = sec.buttons[i][1].length * 13 + 26;
                    if (x + bw > PANEL_W - 10) { x = 12; y += CHIP_H + 4; }
                    items.push({ type: "button", key: sec.buttons[i][0], label: sec.buttons[i][1],
                                 x: x, y: y, w: bw, h: CHIP_H });
                    x += bw + 6;
                }
                y += CHIP_H + 10;
            }
            if (sec.layerList) {
                items.push({ type: "layers", y: y, h: 0 });   // h は draw 時に決まる
                y += this.avatar.layers.length * 18 + 8;
            }
        }
        this.items = items;
        this.contentH = y + 20;
    };

    // レイヤー数が変わったとき (リグ再ロード) に呼ぶ
    UIPanel25D.prototype.refresh = function () {
        this._layout();
        this.dirty = true;
    };

    // ---------- 描画 ----------
    UIPanel25D.prototype._drawPanel = function () {
        var c = this.canvas;   // Canvas2D インスタンス自体が 2d コンテキスト
        var av = this.avatar;
        c.clearRect(0, 0, PANEL_W, this.screenH);
        c.fillStyle = COLORS.bg;
        c.fillRect(0, 0, PANEL_W, this.screenH);
        c.strokeStyle = COLORS.line;
        c.beginPath(); c.moveTo(0.5, 0); c.lineTo(0.5, this.screenH); c.stroke();

        c.save();
        c.translate(0, -this.scrollY);
        c.font = this.font;
        for (var ii = 0; ii < this.items.length; ii++) {
            var it = this.items[ii];
            var top = it.y - this.scrollY;
            if (top > this.screenH || top + (it.h || 200) + 400 < 0) continue;
            if (it.type === "section") {
                c.fillStyle = COLORS.acc;
                c.fillRect(10, it.y + 8, 3, 14);
                c.fillStyle = COLORS.sub;
                c.textAlign = "left"; c.textBaseline = "middle";
                c.fillText(it.label, 19, it.y + SEC_H / 2 + 2);
                c.strokeStyle = COLORS.line;
                c.beginPath(); c.moveTo(10, it.y + SEC_H - 3.5); c.lineTo(PANEL_W - 10, it.y + SEC_H - 3.5); c.stroke();
            } else if (it.type === "toggle" || it.type === "preset" || it.type === "button") {
                var on = it.type === "toggle" ? av.auto[it.key]
                       : it.type === "preset" ? this.activePreset === it.key : false;
                var hov = this.hoverItem === it;
                c.fillStyle = on ? COLORS.chipOn : (hov ? "#31313c" : COLORS.chipOff);
                c.fillRect(it.x, it.y, it.w, it.h);
                c.strokeStyle = on || hov ? COLORS.acc : COLORS.line;
                c.strokeRect(it.x + 0.5, it.y + 0.5, it.w - 1, it.h - 1);
                c.fillStyle = on ? "#ffd7de" : COLORS.txt;
                c.textAlign = "center"; c.textBaseline = "middle";
                c.fillText(it.label, it.x + it.w / 2, it.y + it.h / 2 + 1);
            } else if (it.type === "slider") {
                var v = av.T[it.key];
                var r = (v - it.min) / (it.max - it.min);
                c.fillStyle = COLORS.txt;
                c.textAlign = "left"; c.textBaseline = "middle";
                c.fillText(it.label, 12, it.y + ROW_H / 2 + 1);
                // track
                var ty = it.y + ROW_H / 2;
                c.fillStyle = COLORS.track;
                c.fillRect(TRACK_X, ty - 3, TRACK_W, 6);
                c.fillStyle = this.dragItem === it ? COLORS.acc : COLORS.accDim;
                c.fillRect(TRACK_X, ty - 3, TRACK_W * Math.max(0, Math.min(1, r)), 6);
                // knob
                c.fillStyle = COLORS.acc;
                c.fillRect(TRACK_X + TRACK_W * Math.max(0, Math.min(1, r)) - 2, ty - 7, 4, 14);
                c.fillStyle = COLORS.sub;
                c.textAlign = "right";
                c.fillText(v.toFixed(2), PANEL_W - 12, it.y + ROW_H / 2 + 1);
            } else if (it.type === "layers") {
                c.textBaseline = "middle";
                for (var li = 0; li < av.layers.length; li++) {
                    var L = av.layers[li];
                    var ly = it.y + li * 18 + 9;
                    c.textAlign = "left";
                    c.fillStyle = COLORS.sub;
                    c.fillText("◀", 12, ly);
                    c.fillText("▶", 30, ly);
                    c.fillStyle = COLORS.txt;
                    var tag = (L.synthetic ? " [自動生成]" : "") +
                              (L.strands ? " [房×" + L.strands.length + "]" : "") +
                              (L.side ? " [" + L.side + "]" : "") +
                              (L.fade ? " [差分]" : "");
                    c.fillText(L.name + tag, 48, ly);
                }
            }
        }
        c.restore();

        // スクロールバー
        if (this.contentH > this.screenH) {
            var sbH = Math.max(30, this.screenH * this.screenH / this.contentH);
            var sbY = (this.screenH - sbH) * this.scrollY / (this.contentH - this.screenH);
            c.fillStyle = COLORS.accDim;
            c.fillRect(PANEL_W - 4, sbY, 3, sbH);
        }
        this.canvas.flush();
    };

    // ---------- GL 合成 ----------
    var _prog = null, _vbo = null, _ibo = null, _locPos, _locUV;
    function ensureBlit() {
        if (_prog) return true;
        function sh(type, src) {
            var s = gl.createShader(type);
            gl.shaderSource(s, src); gl.compileShader(s);
            if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) {
                console.error("ui blit shader error: " + gl.getShaderInfoLog(s));
                return null;
            }
            return s;
        }
        var vs = sh(gl.VERTEX_SHADER,
            "attribute vec2 aPos; attribute vec2 aUV; varying vec2 vUV;" +
            "void main(){ vUV=aUV; gl_Position=vec4(aPos,0.0,1.0); }");
        var fs = sh(gl.FRAGMENT_SHADER,
            "precision mediump float; varying vec2 vUV; uniform sampler2D uTex;" +
            "void main(){ gl_FragColor=texture2D(uTex,vUV); }");
        if (!vs || !fs) return false;
        _prog = gl.createProgram();
        gl.attachShader(_prog, vs); gl.attachShader(_prog, fs);
        gl.linkProgram(_prog);
        if (!gl.getProgramParameter(_prog, gl.LINK_STATUS)) { _prog = null; return false; }
        _locPos = gl.getAttribLocation(_prog, "aPos");
        _locUV = gl.getAttribLocation(_prog, "aUV");
        _vbo = gl.createBuffer();
        _ibo = gl.createBuffer();
        gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, _ibo);
        gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint16Array([0, 1, 2, 0, 2, 3]), gl.STATIC_DRAW);
        return true;
    }

    UIPanel25D.prototype.draw = function () {
        if (this.dirty) { this._drawPanel(); this.dirty = false; }
        if (!ensureBlit()) return;
        var x0 = this.x / this.screenW * 2 - 1, x1 = 1;
        var verts = new Float32Array([
            x0, 1, 0, 0,   1, 1, 1, 0,   1, -1, 1, 1,   x0, -1, 0, 1
        ]);
        gl.useProgram(_prog);
        gl.disable(gl.DEPTH_TEST);
        gl.disable(gl.CULL_FACE);
        gl.disable(gl.SCISSOR_TEST);
        gl.disable(gl.STENCIL_TEST);
        gl.enable(gl.BLEND);
        gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA);   // Canvas2D は premultiplied
        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, this.canvas.texture);
        gl.bindBuffer(gl.ARRAY_BUFFER, _vbo);
        gl.bufferData(gl.ARRAY_BUFFER, verts, gl.DYNAMIC_DRAW);
        gl.enableVertexAttribArray(_locPos);
        gl.vertexAttribPointer(_locPos, 2, gl.FLOAT, false, 16, 0);
        gl.enableVertexAttribArray(_locUV);
        gl.vertexAttribPointer(_locUV, 2, gl.FLOAT, false, 16, 8);
        gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, _ibo);
        gl.drawElements(gl.TRIANGLES, 6, gl.UNSIGNED_SHORT, 0);
        gl.disableVertexAttribArray(_locPos);
        gl.disableVertexAttribArray(_locUV);
    };

    // ---------- 入力 ----------
    UIPanel25D.prototype._hitTest = function (px, py) {
        // px/py = パネルローカル (スクロール補正後)
        for (var ii = 0; ii < this.items.length; ii++) {
            var it = this.items[ii];
            if (it.type === "slider") {
                if (py >= it.y && py < it.y + it.h && px >= TRACK_X - 6 && px <= TRACK_X + TRACK_W + 6)
                    return it;
            } else if (it.type === "toggle" || it.type === "preset" || it.type === "button") {
                if (px >= it.x && px < it.x + it.w && py >= it.y && py < it.y + it.h)
                    return it;
            } else if (it.type === "layers") {
                var n = this.avatar.layers.length;
                if (py >= it.y && py < it.y + n * 18) {
                    var li = Math.floor((py - it.y) / 18);
                    if (px >= 8 && px < 26) return { type: "layerOrd", index: li, dir: -1 };
                    if (px >= 26 && px < 44) return { type: "layerOrd", index: li, dir: 1 };
                }
            }
        }
        return null;
    };

    UIPanel25D.prototype._setSliderFromX = function (it, px) {
        var r = Math.max(0, Math.min(1, (px - TRACK_X) / TRACK_W));
        this.avatar.T[it.key] = it.min + (it.max - it.min) * r;
        this.dirty = true;
    };

    // true を返したらイベント消費 (アバター側に渡さない)
    UIPanel25D.prototype.handleEvent = function (e) {
        var inPanel = e.clientX >= this.x;
        var px = e.clientX - this.x, py = e.clientY + this.scrollY;

        if (e.type === "mousemove") {
            if (this.dragItem) {
                this._setSliderFromX(this.dragItem, px);
                return true;
            }
            var hov = inPanel ? this._hitTest(px, py) : null;
            if (hov && hov.type === "layerOrd") hov = null;
            if (hov !== this.hoverItem) { this.hoverItem = hov; this.dirty = true; }
            return inPanel;
        }
        if (e.type === "mousedown" && inPanel) {
            var it = this._hitTest(px, py);
            if (!it) return true;
            if (it.type === "slider") {
                this.dragItem = it;
                this._setSliderFromX(it, px);
            } else if (it.type === "toggle") {
                this.avatar.auto[it.key] = !this.avatar.auto[it.key];
                this.dirty = true;
            } else if (it.type === "preset") {
                if (this.activePreset === it.key) {
                    this.activePreset = null;
                    this.avatar.applyPreset("neutral");
                } else {
                    this.activePreset = it.key;
                    this.avatar.applyPreset(it.key);
                }
                this.dirty = true;
            } else if (it.type === "button") {
                if (it.key === "reset") {
                    this.avatar.resetParams();
                    this.activePreset = null;
                } else if (it.key === "rebuild" && this.onRebuild) {
                    this.onRebuild();
                } else if (it.key === "bg" && this.onBg) {
                    this.onBg();
                }
                this.dirty = true;
            } else if (it.type === "layerOrd") {
                if (this.avatar.moveLayer(it.index, it.dir)) this.dirty = true;
            }
            return true;
        }
        if (e.type === "mouseup") {
            if (this.dragItem) { this.dragItem = null; this.dirty = true; return true; }
            return inPanel;
        }
        if (e.type === "wheel" && inPanel) {
            var max = Math.max(0, this.contentH - this.screenH);
            this.scrollY = Math.max(0, Math.min(max, this.scrollY + (e.deltaY > 0 ? 40 : -40)));
            this.dirty = true;
            return true;
        }
        return false;
    };

    UIPanel25D.PANEL_W = PANEL_W;
    globalThis.UIPanel25D = UIPanel25D;
})();
