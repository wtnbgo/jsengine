// ============================================================
// Demo 11: シーン管理ショーケース (RPG メニュー型)
// ============================================================
//
// SceneManager + Input + PIXI.Assets + SoundManager を組み合わせた
// フレームワーク事例。
// 遷移パターン:
//   Boot (Assets ロード) → Title → (replace) → Menu → (push)    → Settings → (pop)  → Menu
//                                                   → (replace) → Game     → (push) → Pause
//                                                                                   → (pop)  → Game
//                                                                                   → (clear → push Title)
//   セーブデータは localStorage の "demo11_save" キーに JSON で保存。
//   マスター音量は localStorage の "demo11_volume" に 0..1 で保存 (Settings 変更時)。
//
// アセット:
//   bgm/title.wav  bgm/game.wav        — ループ再生 (SoundManager.playBgm)
//   se/select.wav  se/confirm.wav      — メニュー UI 用 SE
//   se/cancel.wav  se/fire.wav         — キャンセル / GameScene fire
//   se/pause.wav                       — Pause を被せた時

(function() {

var APP_W = 1280, APP_H = 720;

// ---------- 共有 pixi Application ----------
var pixiApp = null;
var sceneRoot = null;  // 全シーンの Container がぶら下がる親

// Scene.pause(topOpts) のヘルパー: 上に乗ったシーンが hideBelow=true なら
// 自分の container を非表示にする (resume() で再表示)
function applyPause(container, topOpts) {
    if (container && topOpts && topOpts.hideBelow) container.visible = false;
}
function applyResume(container) {
    if (container) container.visible = true;
}

function ensurePixi() {
    if (pixiApp) return pixiApp;
    if (typeof PIXI === "undefined") {
        console.error("Demo 11: PIXI が未ロード");
        return null;
    }
    pixiApp = new PIXI.Application({
        width: APP_W, height: APP_H,
        backgroundColor: 0x101820,
        antialias: false,
        autoStart: false,
        sharedTicker: false,
    });
    pixiApp.stage.interactive = true;
    pixiApp.stage.hitArea = pixiApp.screen;
    sceneRoot = new PIXI.Container();
    pixiApp.stage.addChild(sceneRoot);
    return pixiApp;
}

// ---------- Input バインド初期化 ----------
function setupInput() {
    Input.bind("confirm", ["Space", "Enter", "NumpadEnter", "Gamepad:A"]);
    Input.bind("cancel",  ["Escape", "Backspace", "Gamepad:B"]);
    Input.bind("menu",    ["Escape", "Gamepad:Start"]);
    Input.bind("up",      ["ArrowUp", "KeyW", "Gamepad:DpadUp", "Gamepad:LeftStickUp"]);
    Input.bind("down",    ["ArrowDown", "KeyS", "Gamepad:DpadDown", "Gamepad:LeftStickDown"]);
    Input.bind("left",    ["ArrowLeft", "KeyA", "Gamepad:DpadLeft", "Gamepad:LeftStickLeft"]);
    Input.bind("right",   ["ArrowRight", "KeyD", "Gamepad:DpadRight", "Gamepad:LeftStickRight"]);
    Input.bind("fire",    ["KeyX", "Gamepad:X"]);
}

// ---------- セーブデータ ----------
var SAVE_KEY = "demo11_save";
function loadSave() {
    try {
        var raw = localStorage.getItem(SAVE_KEY);
        if (!raw) return null;
        return JSON.parse(raw);
    } catch (_) { return null; }
}
function writeSave(data) {
    try { localStorage.setItem(SAVE_KEY, JSON.stringify(data)); } catch (_) {}
}
function clearSave() { try { localStorage.removeItem(SAVE_KEY); } catch (_) {} }

// ---------- 簡易 Button (pixi.ui 非依存) ----------
class SimpleButton extends PIXI.Container {
    constructor(label, opts) {
        super();
        opts = opts || {};
        this.w = opts.width  || 320;
        this.h = opts.height || 56;
        this.bgColor      = (typeof opts.bgColor       === "number") ? opts.bgColor       : 0x1f3148;
        this.hoverColor   = (typeof opts.hoverColor    === "number") ? opts.hoverColor    : 0x2d4d72;
        this.disabledColor= (typeof opts.disabledColor === "number") ? opts.disabledColor : 0x202020;
        this.textColor    = (typeof opts.textColor     === "number") ? opts.textColor     : 0xffffff;
        this.disabled = !!opts.disabled;

        this.bg = new PIXI.Graphics();
        this.addChild(this.bg);

        this.label = new PIXI.Text(label, {
            fontFamily: "Arial",
            fontSize: opts.fontSize || 22,
            fill: this.textColor,
            fontWeight: opts.fontWeight || "normal",
        });
        this.label.anchor.set(0.5, 0.5);
        this.label.x = this.w / 2;
        this.label.y = this.h / 2;
        this.addChild(this.label);

        this._hover = false;
        this._focused = false;
        this.interactive = true;
        this.cursor = "pointer";
        var self = this;
        this.on("pointerover", function() { self._hover = true; self._redraw(); });
        this.on("pointerout",  function() { self._hover = false; self._redraw(); });
        this.on("pointertap", function() {
            if (!self.disabled && self.onClick) self.onClick();
        });

        this._redraw();
    }
    setFocused(b) {
        this._focused = !!b;
        this._redraw();
    }
    setDisabled(b) {
        this.disabled = !!b;
        this.cursor = b ? "default" : "pointer";
        this._redraw();
    }
    setText(s) {
        this.label.text = s;
    }
    _redraw() {
        var c = this.disabled ? this.disabledColor
              : (this._focused || this._hover) ? this.hoverColor
              : this.bgColor;
        var b = this.bg;
        b.clear();
        b.beginFill(c, 1);
        b.lineStyle(this._focused ? 3 : 1, this._focused ? 0xffcc66 : 0x000000, 1);
        b.drawRoundedRect(0, 0, this.w, this.h, 8);
        b.endFill();
    }
}

// ============================================================
// TitleScene
// ============================================================
class TitleScene extends Scene {
    enter() {
        this.container = new PIXI.Container();
        sceneRoot.addChild(this.container);

        var title = new PIXI.Text("Demo 11: Scene Showcase", {
            fontFamily: "Arial", fontSize: 48, fill: 0xffffff, fontWeight: "bold",
        });
        title.anchor.set(0.5, 0.5);
        title.x = APP_W / 2; title.y = APP_H * 0.35;
        this.container.addChild(title);

        var sub = new PIXI.Text("SceneManager + Input + Assets + SoundManager サンプル", {
            fontFamily: "Arial", fontSize: 20, fill: 0xa0b0c0,
        });
        sub.anchor.set(0.5, 0.5);
        sub.x = APP_W / 2; sub.y = APP_H * 0.35 + 50;
        this.container.addChild(sub);

        this.prompt = new PIXI.Text("Press SPACE / ENTER / Gamepad A to start", {
            fontFamily: "Arial", fontSize: 22, fill: 0xffcc66,
        });
        this.prompt.anchor.set(0.5, 0.5);
        this.prompt.x = APP_W / 2; this.prompt.y = APP_H * 0.65;
        this.container.addChild(this.prompt);

        this.t = 0;

        // タイトル BGM (PauseScene 経由で来た場合はもう鳴ってるので playBgm が no-op)
        SoundManager.playBgm("bgm_title", { fadeIn: 800, volume: 0.5 });
    }
    exit() {
        sceneRoot.removeChild(this.container);
        this.container.destroy({ children: true });
        this.container = null;
    }
    pause(topOpts) { applyPause(this.container, topOpts); }
    resume() { applyResume(this.container); }
    update(dt) {
        this.t += dt;
        // プロンプトの脈動
        var a = 0.5 + 0.5 * Math.sin(this.t * 0.005);
        this.prompt.alpha = 0.6 + 0.4 * a;

        if (Input.isJustPressed("confirm")) {
            SoundManager.playSe("se_confirm");
            SceneManager.replace(new MenuScene());
        }
    }
}

// ============================================================
// MenuScene
// ============================================================
class MenuScene extends Scene {
    enter() {
        this.container = new PIXI.Container();
        sceneRoot.addChild(this.container);

        var hdr = new PIXI.Text("MENU", {
            fontFamily: "Arial", fontSize: 36, fill: 0xffffff, fontWeight: "bold",
        });
        hdr.x = 80; hdr.y = 60;
        this.container.addChild(hdr);

        var save = loadSave();

        this.items = [
            { label: "New Game",          action: this._onNewGame.bind(this) },
            { label: "Continue" + (save ? "  (score: " + save.score + ")" : ""),
              action: this._onContinue.bind(this), disabled: !save },
            { label: "Settings",          action: this._onSettings.bind(this) },
            { label: "Back to Demo Menu", action: this._onBack.bind(this) },
        ];
        this.buttons = [];
        for (var i = 0; i < this.items.length; i++) {
            var b = new SimpleButton(this.items[i].label, {
                width: 360, height: 56,
                disabled: !!this.items[i].disabled,
            });
            b.x = 80; b.y = 140 + i * 72;
            b.onClick = this.items[i].action;
            this.container.addChild(b);
            this.buttons.push(b);
        }
        this.focusIndex = 0;
        this._refocus();

        this.helpText = new PIXI.Text(
            "↑↓ / W S / DPad / Stick — confirm / cancel — mouse click も可",
            { fontFamily: "Arial", fontSize: 14, fill: 0x80909a });
        this.helpText.x = 80; this.helpText.y = APP_H - 40;
        this.container.addChild(this.helpText);
    }
    exit() {
        sceneRoot.removeChild(this.container);
        this.container.destroy({ children: true });
        this.container = null;
    }
    pause(topOpts) { applyPause(this.container, topOpts); }
    resume() {
        applyResume(this.container);
        // Settings から戻った時にセーブの有無で Continue を再評価
        var save = loadSave();
        var btn = this.buttons[1];
        if (save) {
            btn.setText("Continue  (score: " + save.score + ")");
            btn.setDisabled(false);
        } else {
            btn.setText("Continue");
            btn.setDisabled(true);
        }
    }
    update(_dt) {
        // 上下移動 (justPressed 連打)
        if (Input.isJustPressed("up"))   this._moveFocus(-1);
        if (Input.isJustPressed("down")) this._moveFocus(+1);
        if (Input.isJustPressed("confirm")) {
            var it = this.items[this.focusIndex];
            if (it && !it.disabled && it.action) {
                SoundManager.playSe("se_confirm");
                it.action();
            }
        }
        if (Input.isJustPressed("cancel")) {
            // Cancel で「Demo メニューに戻る」項目にフォーカスして決定するショートカット
            SoundManager.playSe("se_cancel");
        }
    }
    _moveFocus(d) {
        var n = this.items.length;
        for (var k = 0; k < n; k++) {
            this.focusIndex = (this.focusIndex + d + n) % n;
            if (!this.items[this.focusIndex].disabled) break;
        }
        this._refocus();
        SoundManager.playSe("se_select", { volume: 0.7 });
    }
    _refocus() {
        for (var i = 0; i < this.buttons.length; i++) {
            this.buttons[i].setFocused(i === this.focusIndex);
        }
    }
    _onNewGame()  { clearSave(); SceneManager.replace(new GameScene({ score: 0 })); }
    _onContinue() { var s = loadSave(); if (s) SceneManager.replace(new GameScene({ score: s.score })); }
    // Settings は Menu を完全に覆うフルスクリーン (透過モーダルではない) なので
    // hideBelow=true で Menu の描画を止め、pauseBelow=true で update も止める
    _onSettings() { SceneManager.push(new SettingsScene(), null, { hideBelow: true, pauseBelow: true }); }
    _onBack()     {
        // Demo Menu (= Demo 1) に戻す。main.js のグローバルを直接いじる
        SoundManager.stopBgm(300);
        SceneManager.clear();
        if (typeof globalThis.demo11ExitToDemo1 === "function") globalThis.demo11ExitToDemo1();
    }
}

// ============================================================
// SettingsScene
// ============================================================
//
// Master / BGM / SE の 3 軸それぞれ独立に音量調整。
//   Master → Assets.audioContext.masterVolume (AudioEngine master)
//   BGM    → Assets.bgmGroup.volume           (ma_sound_group)
//   SE     → Assets.seGroup.volume            (ma_sound_group)
// localStorage の "demo11_volumes" に { master, bgm, se } を保存。
// フォーカス: 上下で 5 行 (master/bgm/se スライダー + Test SE + Back) を循環、
//             左右でスライダー値変更、confirm でボタンクリック、cancel で pop。
//             マウスはスライダーをドラッグ、ボタンをクリックでも操作可能。
var VOL_STORE_KEY = "demo11_volumes";

function getStoredVolumes() {
    try {
        var raw = localStorage.getItem(VOL_STORE_KEY);
        if (raw) {
            var o = JSON.parse(raw);
            return {
                master: clampVol(o.master, 1.0),
                bgm:    clampVol(o.bgm,    1.0),
                se:     clampVol(o.se,     1.0),
            };
        }
    } catch (_) {}
    // 旧キーから移行 (demo11_volume = master のみ)
    var legacy = parseFloat(localStorage.getItem("demo11_volume"));
    if (!isNaN(legacy) && legacy >= 0 && legacy <= 1) {
        return { master: legacy, bgm: 1.0, se: 1.0 };
    }
    return { master: 1.0, bgm: 1.0, se: 1.0 };
}
function clampVol(v, fallback) {
    if (typeof v !== "number" || isNaN(v) || v < 0 || v > 1) return fallback;
    return v;
}
function setStoredVolumes(v) {
    try { localStorage.setItem(VOL_STORE_KEY, JSON.stringify(v)); } catch (_) {}
}

// AudioGroup.volume と masterVolume はインスタント反映 (ramp 非対応)。
// ramp が欲しい用途 (BGM のクロスフェード) は SoundManager 側で localGain にかける。
function applyAllVolumes(v) {
    Assets.audioContext.masterVolume = v.master;
    if (Assets.bgmGroup) Assets.bgmGroup.volume = v.bgm;
    if (Assets.seGroup)  Assets.seGroup.volume  = v.se;
}

// --- pixi.ui Slider 行 ----------------------------------------
// 1 行 = ラベル + スライダー本体 + パーセント表示。focus 状態で枠が黄色に光る。
class SliderRow extends PIXI.Container {
    constructor(label, value, onChange) {
        super();
        this.w = 480;
        this.h = 56;
        this.label = label;
        this.value = value;  // 0..1
        this.onChange = onChange;

        // フォーカス枠
        this.focusFrame = new PIXI.Graphics();
        this.addChild(this.focusFrame);

        // ラベル
        this.labelText = new PIXI.Text(label, {
            fontFamily: "Arial", fontSize: 18, fill: 0xffffff,
        });
        this.labelText.x = 12; this.labelText.y = 8;
        this.addChild(this.labelText);

        // 値テキスト (右寄せ)
        this.valueText = new PIXI.Text(Math.round(value * 100) + "%", {
            fontFamily: "Arial", fontSize: 16, fill: 0xb0c0d0,
        });
        this.valueText.x = this.w - 60; this.valueText.y = 10;
        this.addChild(this.valueText);

        // PIXI.ui Slider
        var sliderW = this.w - 24;
        var sliderBg   = new PIXI.Graphics().beginFill(0x303848).drawRoundedRect(0, 0, sliderW, 12, 6).endFill();
        var sliderFill = new PIXI.Graphics().beginFill(0x60a0ff).drawRoundedRect(0, 0, sliderW, 12, 6).endFill();
        var sliderKnob = new PIXI.Graphics().beginFill(0xffffff).drawCircle(0, 0, 12).endFill();
        var slider = new PIXI.ui.Slider({
            bg: sliderBg, fill: sliderFill, slider: sliderKnob,
            min: 0, max: 100, value: Math.round(value * 100),
        });
        slider.x = 12; slider.y = 32;
        this.addChild(slider);
        this.slider = slider;
        var self = this;
        slider.onUpdate.connect(function(v) {
            self.value = v / 100;
            self.valueText.text = Math.round(v) + "%";
            if (self.onChange) self.onChange(self.value);
        });

        this._redrawFocus();
    }
    setFocused(b) {
        this._focused = !!b;
        this._redrawFocus();
    }
    handleLeft() {
        var newVal = Math.max(0, Math.round(this.value * 100) - 5);
        this.slider.value = newVal;  // pixi.ui Slider は value 代入で onUpdate を発火する
    }
    handleRight() {
        var newVal = Math.min(100, Math.round(this.value * 100) + 5);
        this.slider.value = newVal;
    }
    handleConfirm() { /* slider は confirm 何もしない */ }
    _redrawFocus() {
        var g = this.focusFrame;
        g.clear();
        if (this._focused) {
            g.lineStyle(2, 0xffcc66, 1).beginFill(0x182030, 0.6).drawRoundedRect(0, 0, this.w, this.h, 8).endFill();
        } else {
            g.lineStyle(1, 0x303848, 1).beginFill(0x101820, 0.3).drawRoundedRect(0, 0, this.w, this.h, 8).endFill();
        }
    }
}

// --- ボタン行 (SimpleButton のラッパ) -------------------------
class ButtonRow extends PIXI.Container {
    constructor(label, opts) {
        super();
        opts = opts || {};
        this.w = opts.width  || 280;
        this.h = opts.height || 48;
        this.btn = new SimpleButton(label, { width: this.w, height: this.h, fontSize: opts.fontSize || 18 });
        this.addChild(this.btn);
        this.onClick = null;
        var self = this;
        this.btn.onClick = function() { if (self.onClick) self.onClick(); };
    }
    setFocused(b) { this.btn.setFocused(b); }
    handleLeft()  {}
    handleRight() {}
    handleConfirm() { if (this.onClick) this.onClick(); }
}

class SettingsScene extends Scene {
    enter() {
        this.container = new PIXI.Container();
        sceneRoot.addChild(this.container);

        var hdr = new PIXI.Text("SETTINGS", {
            fontFamily: "Arial", fontSize: 36, fill: 0xffffff, fontWeight: "bold",
        });
        hdr.x = 80; hdr.y = 50;
        this.container.addChild(hdr);

        var hint = new PIXI.Text(
            "↑↓: row    ←→: slider value    Enter: button    Esc: back",
            { fontFamily: "Arial", fontSize: 14, fill: 0x80909a });
        hint.x = 80; hint.y = 100;
        this.container.addChild(hint);

        // 現在の音量を読み込み
        this.vols = getStoredVolumes();
        applyAllVolumes(this.vols);  // 起動時点との整合

        // 3 スライダー
        var self = this;
        var sliderRows = [
            new SliderRow("Master Volume", this.vols.master, function(v) {
                self.vols.master = v;
                Assets.audioContext.masterVolume = v;
                setStoredVolumes(self.vols);
            }),
            new SliderRow("BGM Volume",    this.vols.bgm,    function(v) {
                self.vols.bgm = v;
                if (Assets.bgmGroup) Assets.bgmGroup.volume = v;
                setStoredVolumes(self.vols);
            }),
            new SliderRow("SE Volume",     this.vols.se,     function(v) {
                self.vols.se = v;
                if (Assets.seGroup) Assets.seGroup.volume = v;
                setStoredVolumes(self.vols);
            }),
        ];
        for (var i = 0; i < sliderRows.length; i++) {
            sliderRows[i].x = 80;
            sliderRows[i].y = 140 + i * 66;
            this.container.addChild(sliderRows[i]);
        }

        // テスト SE ボタン
        var testBtn = new ButtonRow("Test SE (play confirm.wav)",
            { width: 280, height: 44, fontSize: 16 });
        testBtn.x = 80; testBtn.y = 350;
        testBtn.onClick = function() { SoundManager.playSe("se_confirm"); };
        this.container.addChild(testBtn);

        // Back ボタン
        var back = new ButtonRow("Back  (Esc)", { width: 200, height: 48, fontSize: 18 });
        back.x = 80; back.y = 410;
        back.onClick = function() {
            SoundManager.playSe("se_cancel");
            SceneManager.pop();
        };
        this.container.addChild(back);

        this.rows = sliderRows.concat([testBtn, back]);
        this.focusIndex = 0;
        this._refocus();
    }
    exit() {
        sceneRoot.removeChild(this.container);
        this.container.destroy({ children: true });
        this.container = null;
        setStoredVolumes(this.vols);
    }
    pause(topOpts) { applyPause(this.container, topOpts); }
    resume() { applyResume(this.container); }
    update(_dt) {
        if (Input.isJustPressed("up"))   this._moveFocus(-1);
        if (Input.isJustPressed("down")) this._moveFocus(+1);
        if (Input.isJustPressed("left")) {
            var rL = this.rows[this.focusIndex];
            if (rL && rL.handleLeft) rL.handleLeft();
            SoundManager.playSe("se_select", { volume: 0.5 });
        }
        if (Input.isJustPressed("right")) {
            var rR = this.rows[this.focusIndex];
            if (rR && rR.handleRight) rR.handleRight();
            SoundManager.playSe("se_select", { volume: 0.5 });
        }
        if (Input.isJustPressed("confirm")) {
            var rC = this.rows[this.focusIndex];
            if (rC && rC.handleConfirm) rC.handleConfirm();
        }
        if (Input.isJustPressed("cancel")) {
            SoundManager.playSe("se_cancel");
            SceneManager.pop();
        }
    }
    _moveFocus(d) {
        var n = this.rows.length;
        this.focusIndex = (this.focusIndex + d + n) % n;
        this._refocus();
        SoundManager.playSe("se_select", { volume: 0.7 });
    }
    _refocus() {
        for (var i = 0; i < this.rows.length; i++) {
            this.rows[i].setFocused(i === this.focusIndex);
        }
    }
}

// ============================================================
// GameScene (ダミーゲーム)
// ============================================================
class GameScene extends Scene {
    constructor(args) {
        super();
        this.initialScore = (args && typeof args.score === "number") ? args.score : 0;
    }
    enter() {
        this.container = new PIXI.Container();
        sceneRoot.addChild(this.container);

        // 流れる背景線
        this.bgLines = new PIXI.Graphics();
        this.container.addChild(this.bgLines);

        this.player = new PIXI.Graphics();
        this.player.beginFill(0xff6644).drawCircle(0, 0, 28).endFill();
        this.player.x = 200; this.player.y = APP_H / 2;
        this.container.addChild(this.player);

        this.score = this.initialScore;
        this.scoreText = new PIXI.Text("SCORE: " + this.score, {
            fontFamily: "Arial", fontSize: 28, fill: 0xffffff, fontWeight: "bold",
        });
        this.scoreText.x = 40; this.scoreText.y = 30;
        this.container.addChild(this.scoreText);

        var hint = new PIXI.Text(
            "WASD / Arrows / Stick で移動、X / Gamepad X でスコア +1、Esc で Pause",
            { fontFamily: "Arial", fontSize: 14, fill: 0xa0b0c0 });
        hint.x = 40; hint.y = APP_H - 30;
        this.container.addChild(hint);

        this.t = 0;

        // ゲーム BGM へクロスフェード
        SoundManager.playBgm("bgm_game", { fadeIn: 600, volume: 0.5 });
    }
    pause(topOpts) {
        // Pause シーンは hideBelow=false で push されるので Game の表示は残る
        // BGM はダッキング (音量を下げてくぐもらせる)
        applyPause(this.container, topOpts);
        SoundManager.pauseBgm(0.18, 200);
    }
    resume() {
        applyResume(this.container);
        SoundManager.resumeBgm(200);
    }
    exit() {
        sceneRoot.removeChild(this.container);
        this.container.destroy({ children: true });
        this.container = null;
    }
    update(dt) {
        this.t += dt;

        // 背景線アニメ
        var g = this.bgLines;
        g.clear();
        g.lineStyle(2, 0x223344, 1);
        var offset = (this.t * 0.06) % 40;
        for (var x = -offset; x < APP_W + 40; x += 40) {
            g.moveTo(x, 0); g.lineTo(x, APP_H);
        }

        // 移動 (Action 経由)
        var mx = Input.getValue("right") - Input.getValue("left");
        var my = Input.getValue("down")  - Input.getValue("up");
        var sp = 0.4;
        this.player.x += mx * sp * dt;
        this.player.y += my * sp * dt;
        if (this.player.x < 30)        this.player.x = 30;
        if (this.player.x > APP_W - 30) this.player.x = APP_W - 30;
        if (this.player.y < 30)        this.player.y = 30;
        if (this.player.y > APP_H - 30) this.player.y = APP_H - 30;

        // fire でスコア +1
        if (Input.isJustPressed("fire")) {
            this.score++;
            this.scoreText.text = "SCORE: " + this.score;
            SoundManager.playSe("se_fire");
        }

        // menu で Pause を被せる
        if (Input.isJustPressed("menu")) {
            SoundManager.playSe("se_pause");
            SceneManager.push(new PauseScene({ score: this.score }), null, { pauseBelow: true });
        }
    }
}

// ============================================================
// PauseScene (modal)
// ============================================================
class PauseScene extends Scene {
    constructor(args) {
        super();
        this.scoreSnapshot = (args && typeof args.score === "number") ? args.score : 0;
    }
    enter() {
        this.container = new PIXI.Container();
        sceneRoot.addChild(this.container);

        // 半透過オーバーレイ
        var dim = new PIXI.Graphics();
        dim.beginFill(0x000000, 0.55).drawRect(0, 0, APP_W, APP_H).endFill();
        this.container.addChild(dim);

        // モーダル枠
        var panel = new PIXI.Graphics();
        panel.beginFill(0x182030, 1).lineStyle(2, 0xffcc66, 1)
             .drawRoundedRect(APP_W / 2 - 180, APP_H / 2 - 160, 360, 320, 12).endFill();
        this.container.addChild(panel);

        var title = new PIXI.Text("PAUSED", {
            fontFamily: "Arial", fontSize: 32, fill: 0xffcc66, fontWeight: "bold",
        });
        title.anchor.set(0.5, 0.5);
        title.x = APP_W / 2; title.y = APP_H / 2 - 110;
        this.container.addChild(title);

        var resume = new SimpleButton("Resume", { width: 280, height: 50, fontSize: 20 });
        resume.x = APP_W / 2 - 140; resume.y = APP_H / 2 - 60;
        resume.onClick = function() {
            SoundManager.playSe("se_confirm");
            SceneManager.pop();
        };
        this.container.addChild(resume);

        var save = new SimpleButton("Save", { width: 280, height: 50, fontSize: 20 });
        save.x = APP_W / 2 - 140; save.y = APP_H / 2;
        var self = this;
        save.onClick = function() {
            writeSave({ score: self.scoreSnapshot, savedAt: Date.now() });
            console.log("Demo11 saved: score=" + self.scoreSnapshot);
            SoundManager.playSe("se_confirm");
        };
        this.container.addChild(save);

        var title2 = new SimpleButton("Title (discard)", { width: 280, height: 50, fontSize: 20 });
        title2.x = APP_W / 2 - 140; title2.y = APP_H / 2 + 60;
        title2.onClick = function() {
            SoundManager.playSe("se_cancel");
            // Title へ戻る: Game BGM → Title BGM へクロスフェード
            SceneManager.clear();
            SceneManager.push(new TitleScene());
        };
        this.container.addChild(title2);

        this.buttons = [resume, save, title2];
        this.focusIndex = 0;
        this._refocus();
    }
    exit() {
        sceneRoot.removeChild(this.container);
        this.container.destroy({ children: true });
        this.container = null;
    }
    update(_dt) {
        if (Input.isJustPressed("up")) {
            this.focusIndex = (this.focusIndex + this.buttons.length - 1) % this.buttons.length;
            this._refocus();
            SoundManager.playSe("se_select", { volume: 0.7 });
        }
        if (Input.isJustPressed("down")) {
            this.focusIndex = (this.focusIndex + 1) % this.buttons.length;
            this._refocus();
            SoundManager.playSe("se_select", { volume: 0.7 });
        }
        if (Input.isJustPressed("confirm")) {
            var b = this.buttons[this.focusIndex];
            if (b && b.onClick) b.onClick();
        }
        if (Input.isJustPressed("cancel") || Input.isJustPressed("menu")) {
            SoundManager.playSe("se_cancel");
            SceneManager.pop();
        }
    }
    _refocus() {
        for (var i = 0; i < this.buttons.length; i++) this.buttons[i].setFocused(i === this.focusIndex);
    }
}

// ============================================================
// BootScene — PIXI.Assets で音声をロードしてから Title へ
// ============================================================
//
// PIXI.Assets.load を Promise で呼んで完了したら replace(Title)。
// 完了率は表示しない (短くて表示する間もない) が、テキストだけ "Loading..."
// を出す。Assets ロードが二度呼ばれても PIXI.Assets 側でキャッシュされる。
class BootScene extends Scene {
    enter() {
        this.container = new PIXI.Container();
        sceneRoot.addChild(this.container);

        var msg = new PIXI.Text("Loading...", {
            fontFamily: "Arial", fontSize: 28, fill: 0xffffff,
        });
        msg.anchor.set(0.5, 0.5);
        msg.x = APP_W / 2; msg.y = APP_H / 2;
        this.container.addChild(msg);

        this.msg = msg;
        this.t = 0;
        this.done = false;
        this.failed = false;

        var self = this;
        var bundle = {
            "bgm_title":  "bgm/title.wav",
            "bgm_game":   "bgm/game.wav",
            "se_select":  "se/select.wav",
            "se_confirm": "se/confirm.wav",
            "se_cancel":  "se/cancel.wav",
            "se_fire":    "se/fire.wav",
            "se_pause":   "se/pause.wav",
        };
        Assets.preloadAudio(bundle)
            .then(function(aliases) {
                self.done = true;
                if (aliases.length > 0) {
                    console.log("Demo 11: assets loaded (" + aliases.length + " items)");
                }
            })
            .catch(function(e) {
                self.failed = true;
                console.error("Demo 11: asset load failed:", e, e && e.stack);
                if (self.msg) self.msg.text = "Asset load failed (see console)";
            });

        // 起動時に保存済み音量を反映
        applyAllVolumes(getStoredVolumes());
    }
    exit() {
        sceneRoot.removeChild(this.container);
        this.container.destroy({ children: true });
        this.container = null;
    }
    update(dt) {
        this.t += dt;
        // ドットアニメ
        var n = (Math.floor(this.t / 250) % 4);
        if (this.msg && !this.failed) {
            this.msg.text = "Loading" + ".".repeat(n);
        }
        if (this.done) {
            SceneManager.replace(new TitleScene());
        }
    }
}

// ============================================================
// 公開エントリポイント (main.js から呼ばれる)
// ============================================================
// 名前空間 globalThis.demo11 にまとめて main.js の関数名と衝突しないようにする

globalThis.demo11 = {
    init: function() {
        if (typeof PIXI === "undefined" || !globalThis.SceneManager || !globalThis.Input
            || !globalThis.SoundManager || !globalThis.Assets) {
            console.error("Demo 11: framework が未ロード (scene_manager / input_action / sound_manager / assets_ext / pixi)");
            return;
        }
        if (!ensurePixi()) return;
        setupInput();
        if (!SceneManager.top()) {
            SceneManager.push(new BootScene());
        }
    },
    update: function(dt) {
        if (!pixiApp) return;
        Input.update();
        SoundManager.tick();
        SceneManager.update(dt);
    },
    render: function() {
        if (!pixiApp) return;
        pixiApp.renderer.reset();
        pixiApp.renderer.render(pixiApp.stage);
    },
    handleEvent: function(e) {
        if (!pixiApp) return;
        SceneManager.handleEvent(e);
    },
    // main.js が他デモへ切り替える直前に呼ぶ。BGM を止めるだけで pixiApp 自体は残置
    // (Demo 11 に戻ったとき同じ pixiApp を再利用するため)
    onLeave: function() {
        if (globalThis.SoundManager) SoundManager.stopBgm(150);
    },
    // main.js 側で「Demo メニューに戻る」処理を関数として注入する
    // (loadScript される demo 側からは main.js のスコープが見えないため)
    exitToDemo1Hook: null,
};

// Menu の "Back to Demo Menu" から呼ばれる橋
globalThis.demo11ExitToDemo1 = function() {
    if (typeof globalThis.demo11 !== "undefined" && globalThis.demo11.exitToDemo1Hook) {
        globalThis.demo11.exitToDemo1Hook();
    }
};

console.log("demos/demo11_scene_showcase.js loaded");

})();
