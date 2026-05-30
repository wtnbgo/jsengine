// ============================================================
// Demo 11: シーン管理ショーケース (RPG メニュー型)
// ============================================================
//
// SceneManager + Input + PIXI.Assets を組み合わせた最初のフレームワーク事例。
// 遷移パターン:
//   Title → (replace) → Menu → (push)    → Settings → (pop)  → Menu
//                              → (replace) → Game     → (push) → Pause
//                                                              → (pop)  → Game
//                                                              → (clear → push Title)
//   セーブデータは localStorage の "demo11_save" キーに JSON で保存。

(function() {

var APP_W = 1280, APP_H = 720;

// ---------- 共有 pixi Application ----------
var pixiApp = null;
var sceneRoot = null;  // 全シーンの Container がぶら下がる親

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

        var sub = new PIXI.Text("SceneManager + Input + Assets サンプル", {
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
    }
    exit() {
        sceneRoot.removeChild(this.container);
        this.container.destroy({ children: true });
        this.container = null;
    }
    update(dt) {
        this.t += dt;
        // プロンプトの脈動
        var a = 0.5 + 0.5 * Math.sin(this.t * 0.005);
        this.prompt.alpha = 0.6 + 0.4 * a;

        if (Input.isJustPressed("confirm")) {
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
    resume() {
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
            if (it && !it.disabled && it.action) it.action();
        }
    }
    _moveFocus(d) {
        var n = this.items.length;
        for (var k = 0; k < n; k++) {
            this.focusIndex = (this.focusIndex + d + n) % n;
            if (!this.items[this.focusIndex].disabled) break;
        }
        this._refocus();
    }
    _refocus() {
        for (var i = 0; i < this.buttons.length; i++) {
            this.buttons[i].setFocused(i === this.focusIndex);
        }
    }
    _onNewGame()  { clearSave(); SceneManager.replace(new GameScene({ score: 0 })); }
    _onContinue() { var s = loadSave(); if (s) SceneManager.replace(new GameScene({ score: s.score })); }
    _onSettings() { SceneManager.push(new SettingsScene()); }
    _onBack()     {
        // Demo Menu (= Demo 1) に戻す。main.js のグローバルを直接いじる
        SceneManager.clear();
        if (typeof globalThis.demo11ExitToDemo1 === "function") globalThis.demo11ExitToDemo1();
    }
}

// ============================================================
// SettingsScene
// ============================================================
class SettingsScene extends Scene {
    enter() {
        this.container = new PIXI.Container();
        sceneRoot.addChild(this.container);

        var hdr = new PIXI.Text("SETTINGS", {
            fontFamily: "Arial", fontSize: 36, fill: 0xffffff, fontWeight: "bold",
        });
        hdr.x = 80; hdr.y = 60;
        this.container.addChild(hdr);

        // ボリューム表示 (簡易、← → で増減)
        this.volume = Math.round((Assets.audioContext ? Assets.audioContext.destination ? 1.0 : 1.0 : 1.0) * 100);
        var saved = parseFloat(localStorage.getItem("demo11_volume"));
        if (!isNaN(saved)) this.volume = Math.round(saved * 100);
        this.volText = new PIXI.Text("Master Volume: " + this.volume + "%   (← →)", {
            fontFamily: "Arial", fontSize: 22, fill: 0xffffff,
        });
        this.volText.x = 80; this.volText.y = 150;
        this.container.addChild(this.volText);
        this._applyVolume();

        // キーバインド一覧 (固定表示、Settings のサンプルとして)
        var lines = [
            "Key bindings (固定表示):",
            "  confirm : Space / Enter / Gamepad A",
            "  cancel  : Esc / Backspace / Gamepad B",
            "  up/down/left/right : Arrows / WASD / DPad / Left Stick",
            "  fire    : X / Gamepad X (GameScene でスコア +1)",
            "  menu    : Esc / Gamepad Start (GameScene で Pause)",
        ];
        for (var i = 0; i < lines.length; i++) {
            var t = new PIXI.Text(lines[i], { fontFamily: "Arial", fontSize: 16, fill: 0xa0b0c0 });
            t.x = 80; t.y = 220 + i * 26;
            this.container.addChild(t);
        }

        var back = new SimpleButton("Back  (Esc)", { width: 180, height: 48, fontSize: 18 });
        back.x = 80; back.y = APP_H - 100;
        back.onClick = function() { SceneManager.pop(); };
        this.container.addChild(back);
    }
    exit() {
        sceneRoot.removeChild(this.container);
        this.container.destroy({ children: true });
        this.container = null;
        localStorage.setItem("demo11_volume", String(this.volume / 100));
    }
    update(_dt) {
        var changed = false;
        if (Input.isJustPressed("left"))  { this.volume = Math.max(0, this.volume - 5); changed = true; }
        if (Input.isJustPressed("right")) { this.volume = Math.min(100, this.volume + 5); changed = true; }
        if (changed) {
            this.volText.text = "Master Volume: " + this.volume + "%   (← →)";
            this._applyVolume();
        }
        if (Input.isJustPressed("cancel")) SceneManager.pop();
    }
    _applyVolume() {
        if (Assets.audioContext) {
            // gain ノードがあれば本来そちらで調整するが、ここでは masterVolume を簡略採用
            // (jsengine の AudioContext.masterVolume はエンジン全体の master)
            // ※ Web Audio 標準にはこのプロパティは無いので jsengine 拡張依存
            if (typeof Assets.audioContext.masterVolume !== "undefined") {
                Assets.audioContext.masterVolume = this.volume / 100;
            }
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
    }
    pause() {
        // Pause シーンが上に乗った時のフック (BGM フェード等を入れる場所)
        // ここではダミー
    }
    resume() {
        // Pause から戻った時
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
        }

        // menu で Pause を被せる
        if (Input.isJustPressed("menu")) {
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
        resume.onClick = function() { SceneManager.pop(); };
        this.container.addChild(resume);

        var save = new SimpleButton("Save", { width: 280, height: 50, fontSize: 20 });
        save.x = APP_W / 2 - 140; save.y = APP_H / 2;
        var self = this;
        save.onClick = function() {
            writeSave({ score: self.scoreSnapshot, savedAt: Date.now() });
            console.log("Demo11 saved: score=" + self.scoreSnapshot);
        };
        this.container.addChild(save);

        var title2 = new SimpleButton("Title (discard)", { width: 280, height: 50, fontSize: 20 });
        title2.x = APP_W / 2 - 140; title2.y = APP_H / 2 + 60;
        title2.onClick = function() {
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
        if (Input.isJustPressed("up"))   { this.focusIndex = (this.focusIndex + this.buttons.length - 1) % this.buttons.length; this._refocus(); }
        if (Input.isJustPressed("down")) { this.focusIndex = (this.focusIndex + 1) % this.buttons.length; this._refocus(); }
        if (Input.isJustPressed("confirm")) {
            var b = this.buttons[this.focusIndex];
            if (b && b.onClick) b.onClick();
        }
        if (Input.isJustPressed("cancel") || Input.isJustPressed("menu")) {
            SceneManager.pop();
        }
    }
    _refocus() {
        for (var i = 0; i < this.buttons.length; i++) this.buttons[i].setFocused(i === this.focusIndex);
    }
}

// ============================================================
// 公開エントリポイント (main.js から呼ばれる)
// ============================================================
// 名前空間 globalThis.demo11 にまとめて main.js の関数名と衝突しないようにする

globalThis.demo11 = {
    init: function() {
        if (typeof PIXI === "undefined" || !globalThis.SceneManager || !globalThis.Input) {
            console.error("Demo 11: framework が未ロード (scene_manager / input_action / pixi)");
            return;
        }
        if (!ensurePixi()) return;
        setupInput();
        if (!SceneManager.top()) {
            SceneManager.push(new TitleScene());
        }
    },
    update: function(dt) {
        if (!pixiApp) return;
        Input.update();
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
