// ============================================================
// Input Action — Unity InputAction 風 入力抽象化
// ============================================================
//
// キーボード / マウス / ゲームパッドの生入力を「Action 名」にまとめて扱う。
// シーン側は Input.isPressed("jump") のように生入力を意識せずに済む。
//
// 提供する globalThis.Input:
//   Input.bind(action, sources)        — Action にバインドを設定 (上書き)
//   Input.addBinding(action, source)   — 既存バインドに 1 件追加
//   Input.unbind(action)               — バインド解除
//   Input.isPressed(action)            — boolean
//   Input.isJustPressed(action)        — このフレームで押下開始
//   Input.isJustReleased(action)       — このフレームで離されたか
//   Input.getValue(action)             — 0.0 ~ 1.0 (アナログ軸 / トリガー値)
//   Input.update()                     — 毎フレーム 1 回呼ぶ (justPressed 判定のため)
//   Input.bindings                     — 現在の設定 (Settings 画面で表示用、書き換え非推奨)
//
// キーバインド設定 UI 用:
//   Input.captureNext(opts)            — 次の入力 (key/mouse/gamepad) を 1 回キャプチャ
//                                        → Promise<sourceString | null>。null は cancel/timeout
//                                        opts: { cancelOnEsc?: boolean = true, timeoutMs?: number }
//   Input.captureCancel()              — 進行中の captureNext を null で解決
//   Input.snapshotBindings()           — 現在のバインドの deep copy
//   Input.restoreBindings(snap)        — snapshot から全 binding を一括復元
//   Input.serialize() / deserialize(o) — JSON-safe な保存/復元
//
// source 文字列フォーマット:
//   キーボード      : KeyboardEvent.code 文字列 ("Space", "ArrowLeft", "KeyZ", "Enter")
//   マウスボタン    : "Mouse:Left" / "Mouse:Right" / "Mouse:Middle"
//   ゲームパッド    : "Gamepad:A" / "Gamepad:B" / ... (W3C 標準 17 ボタン名)
//                     "Gamepad:LeftStickUp" / "Gamepad:LeftStickDown" / ...Left / ...Right
//                     "Gamepad:RightStickUp" 等 (軸を方向ボタンとして扱う)
//                     "Gamepad:LT" / "Gamepad:RT" はトリガー (アナログ値)
//
// 使用例:
//   Input.bind("jump",   ["Space", "Gamepad:A"]);
//   Input.bind("attack", ["KeyZ", "Mouse:Left", "Gamepad:X"]);
//   Input.bind("moveX",  ["ArrowLeft|ArrowRight", "Gamepad:LeftStickX"]);
//     // 補注: 軸系は単独の source 名で +/- 両方向を含む
//
//   function update(dt) {
//       Input.update();
//       if (Input.isJustPressed("jump")) player.jump();
//       player.x += Input.getValue("moveX") * dt * 0.4;
//   }

(function() {

// W3C 標準 Gamepad レイアウト ボタンインデックス
var BTN_INDEX = {
    A: 0, B: 1, X: 2, Y: 3,
    LB: 4, RB: 5, LT: 6, RT: 7,
    Back: 8, Start: 9, L3: 10, R3: 11,
    DpadUp: 12, DpadDown: 13, DpadLeft: 14, DpadRight: 15,
    Guide: 16
};

// 軸インデックスとそれの方向別マッピング
// "LeftStickX" 単独 → 軸値そのまま (-1..1)
// "LeftStickUp/Down/Left/Right" → 半軸 (0..1) として button っぽく扱う
var AXIS_INDEX = {
    LeftStickX: { axis: 0, sign:  0 },
    LeftStickY: { axis: 1, sign:  0 },
    RightStickX: { axis: 2, sign:  0 },
    RightStickY: { axis: 3, sign:  0 },
    LeftStickRight: { axis: 0, sign: +1 },
    LeftStickLeft:  { axis: 0, sign: -1 },
    LeftStickDown:  { axis: 1, sign: +1 },
    LeftStickUp:    { axis: 1, sign: -1 },
    RightStickRight: { axis: 2, sign: +1 },
    RightStickLeft:  { axis: 2, sign: -1 },
    RightStickDown:  { axis: 3, sign: +1 },
    RightStickUp:    { axis: 3, sign: -1 },
};

var MOUSE_INDEX = { Left: 0, Middle: 1, Right: 2 };

class InputImpl {
    constructor() {
        this.bindings = {};   // action -> [source, ...]
        this.state = {};       // action -> { pressed, justPressed, justReleased, value, prev }
        this.deadzone = 0.15;
        this._keys = {};       // KeyboardEvent.code -> true
        this._mouseBtns = {};  // button index -> true
        this._capture = null;  // captureNext 中の状態: { resolve, cancelOnEsc, blockedKeys, blockedMouse, initialPadState, deadlineMs? }
        this._wire();
    }

    bind(action, sources) {
        if (!Array.isArray(sources)) sources = [sources];
        this.bindings[action] = sources.slice();
        if (!this.state[action]) {
            this.state[action] = { pressed: false, justPressed: false, justReleased: false, value: 0, prev: false };
        }
    }

    addBinding(action, source) {
        if (!this.bindings[action]) this.bindings[action] = [];
        this.bindings[action].push(source);
        if (!this.state[action]) {
            this.state[action] = { pressed: false, justPressed: false, justReleased: false, value: 0, prev: false };
        }
    }

    unbind(action) {
        delete this.bindings[action];
        delete this.state[action];
    }

    isPressed(action)       { var s = this.state[action]; return s ? s.pressed       : false; }
    isJustPressed(action)   { var s = this.state[action]; return s ? s.justPressed   : false; }
    isJustReleased(action)  { var s = this.state[action]; return s ? s.justReleased  : false; }
    getValue(action)        { var s = this.state[action]; return s ? s.value         : 0; }

    // --- バインド snapshot / 永続化 ---
    snapshotBindings() {
        var snap = {};
        for (var action in this.bindings) {
            snap[action] = this.bindings[action].slice();
        }
        return snap;
    }
    restoreBindings(snap) {
        this.bindings = {};
        this.state = {};
        for (var action in snap) {
            this.bind(action, snap[action]);
        }
    }
    serialize()    { return this.snapshotBindings(); }
    deserialize(o) { if (o && typeof o === "object") this.restoreBindings(o); }

    // --- 次の入力をキャプチャ (キーバインド UI 用) ---
    captureNext(opts) {
        opts = opts || {};
        var self = this;
        var cancelOnEsc = (opts.cancelOnEsc !== false);
        var timeoutMs   = (typeof opts.timeoutMs === "number") ? opts.timeoutMs : 0;

        // 進行中の capture があれば null で解決して上書き
        if (self._capture) {
            var prev = self._capture; self._capture = null;
            if (prev.resolve) prev.resolve(null);
        }

        return new Promise(function(resolve) {
            // 既に押されているキー/ボタン/パッド状態を「初期状態」として記録。
            // ここに含まれているものは、リリース→再プレスで初めてキャプチャされる
            // (rebind 入口の Enter 等が即座に再キャプチャされるのを防ぐため)
            self._capture = {
                resolve:         resolve,
                cancelOnEsc:     cancelOnEsc,
                blockedKeys:     Object.assign({}, self._keys),
                blockedMouse:    Object.assign({}, self._mouseBtns),
                initialPadState: self._padSnapshot(),
                deadlineMs:      (timeoutMs > 0) ? (Date.now() + timeoutMs) : 0,
            };
        });
    }

    captureCancel() {
        var cap = this._capture;
        if (cap) {
            this._capture = null;
            if (cap.resolve) cap.resolve(null);
        }
    }

    _captureFinish(src) {
        var cap = this._capture;
        if (!cap) return;
        this._capture = null;
        if (cap.resolve) cap.resolve(src);
    }

    _padSnapshot() {
        var pads = (typeof navigator !== "undefined" && navigator.getGamepads)
                   ? navigator.getGamepads() : [];
        var snap = [];
        for (var i = 0; i < pads.length; i++) {
            var pad = pads[i];
            if (!pad) { snap.push(null); continue; }
            var btns = [];
            for (var j = 0; j < pad.buttons.length; j++) {
                btns.push(pad.buttons[j] ? !!pad.buttons[j].pressed : false);
            }
            snap.push({ buttons: btns, axes: pad.axes.slice() });
        }
        return snap;
    }

    _detectGamepadCapture(pads) {
        var cap = this._capture;
        if (!cap) return null;
        var initial = cap.initialPadState;
        var T = 0.6;  // 軸キャプチャ閾値 (デッドゾーンより大きく)
        for (var i = 0; i < pads.length; i++) {
            var pad = pads[i];
            if (!pad) continue;
            var init = initial[i];
            // ボタン (rising edge)
            for (var btnName in BTN_INDEX) {
                var idx = BTN_INDEX[btnName];
                var b = pad.buttons[idx];
                if (!b) continue;
                var prevPressed = init ? !!init.buttons[idx] : false;
                if (b.pressed && !prevPressed) {
                    return "Gamepad:" + btnName;
                }
            }
            // 軸 (半軸が閾値を越えた)
            for (var axisName in AXIS_INDEX) {
                var def = AXIS_INDEX[axisName];
                if (def.sign === 0) continue;  // 軸そのまま (full-axis) は capture 対象外
                var av = pad.axes[def.axis];
                if (typeof av !== "number") continue;
                var v = av * def.sign;
                var pv = (init && typeof init.axes[def.axis] === "number") ? init.axes[def.axis] * def.sign : 0;
                if (v > T && pv <= T) {
                    return "Gamepad:" + axisName;
                }
            }
        }
        return null;
    }

    update() {
        var pads = (typeof navigator !== "undefined" && navigator.getGamepads)
                   ? navigator.getGamepads() : [];

        for (var action in this.bindings) {
            var st = this.state[action];
            st.prev = st.pressed;
            var pressed = false;
            var value = 0;

            var srcs = this.bindings[action];
            for (var i = 0; i < srcs.length; i++) {
                var src = srcs[i];
                var v = this._readSource(src, pads);
                if (v.pressed) pressed = true;
                if (Math.abs(v.value) > Math.abs(value)) value = v.value;
            }

            st.pressed = pressed;
            st.value = value;
            st.justPressed  = pressed && !st.prev;
            st.justReleased = !pressed && st.prev;
        }

        // 入力キャプチャ中なら、ゲームパッドの新規押下/軸超過を検出
        if (this._capture) {
            var src = this._detectGamepadCapture(pads);
            if (src) {
                this._captureFinish(src);
            } else if (this._capture && this._capture.deadlineMs > 0 && Date.now() > this._capture.deadlineMs) {
                this._captureFinish(null);  // timeout
            }
        }
    }

    // ソース 1 件の読み出し
    _readSource(src, pads) {
        // Gamepad:...
        if (src.indexOf("Gamepad:") === 0) {
            var name = src.slice(8);
            return this._readGamepad(name, pads);
        }
        // Mouse:Left / Right / Middle
        if (src.indexOf("Mouse:") === 0) {
            var btn = MOUSE_INDEX[src.slice(6)];
            if (btn === undefined) return { pressed: false, value: 0 };
            var p = !!this._mouseBtns[btn];
            return { pressed: p, value: p ? 1 : 0 };
        }
        // キーボード
        var p = !!this._keys[src];
        return { pressed: p, value: p ? 1 : 0 };
    }

    _readGamepad(name, pads) {
        // 全パッドの OR を取る (どのパッドで押しても OK)
        var pressed = false;
        var value = 0;
        for (var i = 0; i < pads.length; i++) {
            var pad = pads[i];
            if (!pad) continue;

            if (name in BTN_INDEX) {
                var b = pad.buttons[BTN_INDEX[name]];
                if (b) {
                    if (b.pressed) pressed = true;
                    if (b.value > Math.abs(value)) value = b.value;
                }
                continue;
            }
            if (name in AXIS_INDEX) {
                var def = AXIS_INDEX[name];
                var axisVal = pad.axes[def.axis];
                if (typeof axisVal !== "number") continue;
                if (def.sign === 0) {
                    // 軸そのまま (-1..1)
                    if (Math.abs(axisVal) > Math.abs(value)) value = axisVal;
                    if (Math.abs(axisVal) > this.deadzone) pressed = true;
                } else {
                    // 半軸 (0..1)
                    var v = axisVal * def.sign;
                    if (v < 0) v = 0;
                    if (v > value) value = v;
                    if (v > this.deadzone) pressed = true;
                }
            }
        }
        return { pressed: pressed, value: value };
    }

    _wire() {
        var self = this;
        addEventListener("keydown", function(e) {
            self._keys[e.code] = true;
            // キャプチャ中なら、ブロック対象でないキーを 1 件だけ確定して終了
            if (self._capture) {
                if (self._capture.cancelOnEsc && e.code === "Escape") {
                    self._captureFinish(null);
                    return;
                }
                if (!self._capture.blockedKeys[e.code]) {
                    self._captureFinish(e.code);
                }
            }
        });
        addEventListener("keyup", function(e) {
            delete self._keys[e.code];
            // キャプチャ開始時に押されていたキーが離されたらブロック解除
            if (self._capture && self._capture.blockedKeys[e.code]) {
                delete self._capture.blockedKeys[e.code];
            }
        });
        addEventListener("mousedown", function(e) {
            self._mouseBtns[e.button] = true;
            if (self._capture && !self._capture.blockedMouse[e.button]) {
                var name = (e.button === 0) ? "Left"
                         : (e.button === 1) ? "Middle"
                         : (e.button === 2) ? "Right" : null;
                if (name) self._captureFinish("Mouse:" + name);
            }
        });
        addEventListener("mouseup", function(e) {
            delete self._mouseBtns[e.button];
            if (self._capture && self._capture.blockedMouse[e.button]) {
                delete self._capture.blockedMouse[e.button];
            }
        });
        // ウィンドウフォーカス喪失等のリセット手段は今のところ無し
        // (jsengine は SDL の WINDOW_FOCUS_LOST を JS に発火していない)
    }
}

globalThis.Input = new InputImpl();

console.log("framework/input_action.js loaded");

})();
