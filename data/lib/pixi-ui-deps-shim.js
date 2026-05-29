// ============================================================
// pixi.ui v1.x の依存ライブラリ最小シム
// - typed-signals: Signal クラス
// - tweedle.js: Tween, Group（アニメーション無しの即時遷移）
// ============================================================
// pixi-ui.js (IIFE) は末尾で `(..., typedSignals, ..., tweedle_js)` を
// 参照するため、これらをグローバルに用意しておく必要がある。

if (!globalThis.__pixi_ui_deps_shim_loaded) {
    globalThis.__pixi_ui_deps_shim_loaded = true;

    // --- typed-signals (最小実装) ---
    function Signal() {
        this._handlers = [];
    }
    Signal.prototype.connect = function (cb) {
        this._handlers.push(cb);
        return cb;
    };
    Signal.prototype.disconnect = function (cb) {
        var i = this._handlers.indexOf(cb);
        if (i >= 0) this._handlers.splice(i, 1);
    };
    Signal.prototype.disconnectAll = function () {
        this._handlers = [];
    };
    Signal.prototype.emit = function () {
        var args = arguments;
        var hs = this._handlers.slice();
        for (var i = 0; i < hs.length; i++) {
            try { hs[i].apply(null, args); }
            catch (e) { console.error("Signal handler error: " + e); }
        }
    };

    globalThis.typedSignals = { Signal: Signal };

    // --- tweedle.js (最小実装: アニメーション無しで即時に目標値へ) ---
    function Tween(target) {
        this._target = target;
        this._toProps = null;
        this._duration = 0;
        this._easing = null;
        this._onUpdate = null;
        this._onComplete = null;
    }
    Tween.prototype.to = function (props, duration) {
        this._toProps = props;
        this._duration = duration || 0;
        return this;
    };
    Tween.prototype.easing = function (fn) {
        this._easing = fn;
        return this;
    };
    Tween.prototype.onUpdate = function (fn) {
        this._onUpdate = fn;
        return this;
    };
    Tween.prototype.onComplete = function (fn) {
        this._onComplete = fn;
        return this;
    };
    Tween.prototype.start = function () {
        if (this._toProps && this._target) {
            for (var k in this._toProps) {
                if (Object.prototype.hasOwnProperty.call(this._toProps, k)) {
                    this._target[k] = this._toProps[k];
                }
            }
        }
        if (this._onUpdate) this._onUpdate(this._target);
        if (this._onComplete) this._onComplete(this._target);
        return this;
    };
    Tween.prototype.stop = function () { return this; };
    Tween.prototype.update = function () { return false; };
    Tween.prototype.chain = function () { return this; };

    function Group() { this._tweens = []; }
    Group.prototype.add = function (t) { this._tweens.push(t); };
    Group.prototype.remove = function () {};
    Group.prototype.update = function () { return false; };

    globalThis.tweedle_js = { Tween: Tween, Group: Group };

    console.log("pixi-ui deps shim loaded (typed-signals, tweedle.js minimal)");
}
