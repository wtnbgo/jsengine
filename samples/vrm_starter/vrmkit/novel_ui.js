// ============================================================
// vrmkit/novel_ui.js — ノベルゲーム UI (メッセージウィンドウ / 選択肢)
// ============================================================
//
// Canvas2D オーバーレイでノベルゲームの会話 UI を描画する。
//   - メッセージウィンドウ + 名前プレート + タイプライタ表示
//   - ページ送りインジケータ (▼)
//   - 選択肢 (マウスホバー / クリック、 ↑↓ + Enter)
//
// 使い方 (通常は ScriptRunner 経由で操作される):
//   const ui = new NovelUI();
//   ui.say("アオイ", "こんにちは!\nよく来たね。");
//   毎フレーム: ui.update(dtSec); ui.draw();
//   入力: const act = ui.handleEvent(e);
//     act = null | { type:"advance" } | { type:"choice", index }

import { CanvasOverlay } from "./overlay.js";

const DEFAULTS = {
    width: 1280, height: 720,
    font: "26px NotoSansJP-Regular",
    nameFont: "22px NotoSansJP-Regular",
    choiceFont: "24px NotoSansJP-Regular",
    charsPerSec: 32,
    lineHeight: 38,
    // メッセージウィンドウ
    winX: 70, winY: 498, winW: 1140, winH: 192, winR: 14,
    textPadX: 34, textPadY: 26,
    // 選択肢
    choiceW: 600, choiceH: 58, choiceGap: 16, choiceTop: 170,
};

export class NovelUI {
    constructor(opts) {
        this.cfg = Object.assign({}, DEFAULTS, opts || {});
        this.overlay = new CanvasOverlay(this.cfg.width, this.cfg.height, this.cfg.width, this.cfg.height);
        this.visible = false;       // メッセージウィンドウ表示中か
        this.name = "";
        this._lines = [];           // wrap 済み行
        this._totalChars = 0;
        this._visibleChars = 0;     // タイプライタ進行 (float)
        this._typing = false;
        this._choices = null;       // [{ label }] 表示中の選択肢
        this._choiceIndex = 0;
        this._time = 0;
        this._dirty = true;
        this._lastIndicatorOn = false;
    }

    // ---- 表示制御 ----

    // セリフを表示 (タイプライタ開始)。 name は "" で名前プレート非表示
    say(name, text) {
        this.name = name || "";
        this._lines = this._wrapText(text || "");
        this._totalChars = 0;
        for (let i = 0; i < this._lines.length; i++) this._totalChars += this._lines[i].length;
        this._visibleChars = 0;
        this._typing = this._totalChars > 0;
        this.visible = true;
        this._dirty = true;
    }

    // タイプライタを最後まで進める
    completeText() {
        if (this._typing) {
            this._visibleChars = this._totalChars;
            this._typing = false;
            this._dirty = true;
        }
    }

    get isTyping() { return this._typing; }
    get isChoosing() { return !!this._choices; }

    // 選択肢を表示。 選択結果は handleEvent の戻り値で受け取る
    showChoices(items) {
        this._choices = items.map(function(it) {
            return (typeof it === "string") ? { label: it } : it;
        });
        this._choiceIndex = 0;
        this._dirty = true;
    }

    hideChoices() {
        this._choices = null;
        this._dirty = true;
    }

    // メッセージウィンドウだけ隠す (次の say で再表示される)
    hideWindow() {
        if (this.visible) {
            this.visible = false;
            this._dirty = true;
        }
    }

    // 全部消す
    hide() {
        this.visible = false;
        this._choices = null;
        this._typing = false;
        this._dirty = true;
    }

    // ---- 入力 ----
    // 戻り値: null | { type:"advance" } | { type:"choice", index }
    handleEvent(e) {
        if (this._choices) {
            if (e.type === "mousemove") {
                const hit = this._choiceHit(e.clientX, e.clientY);
                if (hit >= 0 && hit !== this._choiceIndex) {
                    this._choiceIndex = hit;
                    this._dirty = true;
                }
            } else if (e.type === "mousedown" && e.button === 0) {
                const hit = this._choiceHit(e.clientX, e.clientY);
                if (hit >= 0) return { type: "choice", index: hit };
            } else if (e.type === "keydown") {
                if (e.code === "ArrowUp") {
                    this._choiceIndex = (this._choiceIndex + this._choices.length - 1) % this._choices.length;
                    this._dirty = true;
                } else if (e.code === "ArrowDown") {
                    this._choiceIndex = (this._choiceIndex + 1) % this._choices.length;
                    this._dirty = true;
                } else if (e.code === "Enter" || e.code === "Space" || e.code === "KeyZ") {
                    return { type: "choice", index: this._choiceIndex };
                }
            }
            return null;
        }
        if (!this.visible) return null;
        if (e.type === "mousedown" && e.button === 0) return { type: "advance" };
        if (e.type === "keydown" && (e.code === "Enter" || e.code === "Space" || e.code === "KeyZ")) {
            return { type: "advance" };
        }
        return null;
    }

    // ---- 毎フレーム ----

    update(dtSec) {
        this._time += dtSec;
        if (this._typing) {
            this._visibleChars += dtSec * this.cfg.charsPerSec;
            if (this._visibleChars >= this._totalChars) {
                this._visibleChars = this._totalChars;
                this._typing = false;
            }
            this._dirty = true;
        }
        // ページ送りインジケータの点滅 (状態が変わった時だけ再描画)
        if (this.visible && !this._typing && !this._choices) {
            const on = (this._time % 1.0) < 0.55;
            if (on !== this._lastIndicatorOn) {
                this._lastIndicatorOn = on;
                this._dirty = true;
            }
        }
    }

    draw() {
        if (this._dirty) {
            this._redraw();
            this._dirty = false;
        }
        if (this.visible || this._choices) {
            this.overlay.draw(0, 0);
        }
    }

    // ---- 内部 ----

    _wrapText(text) {
        const c = this.overlay.canvas;
        c.font = this.cfg.font;
        const maxW = this.cfg.winW - this.cfg.textPadX * 2;
        const lines = [];
        const paras = String(text).split("\n");
        for (let p = 0; p < paras.length; p++) {
            let line = "";
            let lineW = 0;
            for (const ch of paras[p]) {   // サロゲートペア対応で for-of
                const w = c.measureText(ch).width;
                if (lineW + w > maxW && line.length > 0) {
                    lines.push(line);
                    line = ch;
                    lineW = w;
                } else {
                    line += ch;
                    lineW += w;
                }
            }
            lines.push(line);
        }
        return lines;
    }

    _roundRectPath(c, x, y, w, h, r) {
        c.beginPath();
        c.moveTo(x + r, y);
        c.lineTo(x + w - r, y);
        c.arc(x + w - r, y + r, r, -Math.PI / 2, 0, false);
        c.lineTo(x + w, y + h - r);
        c.arc(x + w - r, y + h - r, r, 0, Math.PI / 2, false);
        c.lineTo(x + r, y + h);
        c.arc(x + r, y + h - r, r, Math.PI / 2, Math.PI, false);
        c.lineTo(x, y + r);
        c.arc(x + r, y + r, r, Math.PI, Math.PI * 1.5, false);
        c.closePath();
    }

    _choiceRect(i) {
        const cfg = this.cfg;
        const x = (cfg.width - cfg.choiceW) / 2;
        const y = cfg.choiceTop + i * (cfg.choiceH + cfg.choiceGap);
        return { x: x, y: y, w: cfg.choiceW, h: cfg.choiceH };
    }

    _choiceHit(mx, my) {
        if (!this._choices) return -1;
        for (let i = 0; i < this._choices.length; i++) {
            const r = this._choiceRect(i);
            if (mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h) return i;
        }
        return -1;
    }

    _redraw() {
        const c = this.overlay.canvas;
        const cfg = this.cfg;

        // 使用領域だけクリア (全面 clear は GL アップロードが重くなる)
        c.clearRect(cfg.winX - 12, cfg.winY - 64, cfg.winW + 24, cfg.winH + 80);
        c.clearRect((cfg.width - cfg.choiceW) / 2 - 12, cfg.choiceTop - 12,
                    cfg.choiceW + 24, 4 * (cfg.choiceH + cfg.choiceGap) + 24);

        if (this.visible) {
            // メッセージウィンドウ
            c.fillStyle = "rgba(16,20,44,0.82)";
            this._roundRectPath(c, cfg.winX, cfg.winY, cfg.winW, cfg.winH, cfg.winR);
            c.fill();
            c.strokeStyle = "rgba(140,170,255,0.85)";
            c.lineWidth = 2;
            this._roundRectPath(c, cfg.winX, cfg.winY, cfg.winW, cfg.winH, cfg.winR);
            c.stroke();

            // 名前プレート
            if (this.name) {
                c.font = cfg.nameFont;
                const nw = c.measureText(this.name).width;
                const px = cfg.winX + 22, py = cfg.winY - 42, ph = 42;
                c.fillStyle = "rgba(52,64,140,0.92)";
                this._roundRectPath(c, px, py, nw + 40, ph, 10);
                c.fill();
                c.strokeStyle = "rgba(140,170,255,0.85)";
                c.lineWidth = 2;
                this._roundRectPath(c, px, py, nw + 40, ph, 10);
                c.stroke();
                c.fillStyle = "#ffe9a8";
                c.textBaseline = "middle";
                c.textAlign = "left";
                c.fillText(this.name, px + 20, py + ph / 2 + 1);
            }

            // 本文 (タイプライタ)
            c.font = cfg.font;
            c.fillStyle = "#ffffff";
            c.textBaseline = "top";
            c.textAlign = "left";
            let remain = Math.floor(this._visibleChars);
            let ty = cfg.winY + cfg.textPadY;
            for (let i = 0; i < this._lines.length && remain > 0; i++) {
                const line = this._lines[i];
                const n = Math.min(line.length, remain);
                c.fillText(line.substr(0, n), cfg.winX + cfg.textPadX, ty);
                remain -= n;
                ty += cfg.lineHeight;
            }

            // ページ送りインジケータ (▼ を path で描く)
            if (!this._typing && !this._choices && this._lastIndicatorOn) {
                const ix = cfg.winX + cfg.winW - 42;
                const iy = cfg.winY + cfg.winH - 30;
                c.fillStyle = "#a8c4ff";
                c.beginPath();
                c.moveTo(ix, iy);
                c.lineTo(ix + 18, iy);
                c.lineTo(ix + 9, iy + 12);
                c.closePath();
                c.fill();
            }
        }

        // 選択肢
        if (this._choices) {
            c.font = cfg.choiceFont;
            c.textBaseline = "middle";
            c.textAlign = "center";
            for (let i = 0; i < this._choices.length; i++) {
                const r = this._choiceRect(i);
                const sel = (i === this._choiceIndex);
                c.fillStyle = sel ? "rgba(78,100,200,0.95)" : "rgba(24,28,56,0.88)";
                this._roundRectPath(c, r.x, r.y, r.w, r.h, 12);
                c.fill();
                c.strokeStyle = sel ? "rgba(210,225,255,1.0)" : "rgba(140,170,255,0.7)";
                c.lineWidth = sel ? 3 : 2;
                this._roundRectPath(c, r.x, r.y, r.w, r.h, 12);
                c.stroke();
                c.fillStyle = sel ? "#ffffff" : "#c8d4f8";
                c.fillText(this._choices[i].label, r.x + r.w / 2, r.y + r.h / 2 + 1);
            }
        }

        c.flush();
    }
}
