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
        addEventListener("keydown", function(e) { self._keys[e.code] = true; });
        addEventListener("keyup",   function(e) { delete self._keys[e.code]; });
        addEventListener("mousedown", function(e) { self._mouseBtns[e.button] = true; });
        addEventListener("mouseup",   function(e) { delete self._mouseBtns[e.button]; });
        // ウィンドウフォーカス喪失等のリセット手段は今のところ無し
        // (jsengine は SDL の WINDOW_FOCUS_LOST を JS に発火していない)
    }
}

globalThis.Input = new InputImpl();

console.log("framework/input_action.js loaded");

})();
