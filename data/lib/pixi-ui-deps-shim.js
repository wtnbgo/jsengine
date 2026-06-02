// ============================================================
// pixi.ui v1.x の依存ライブラリ最小実装
// - typed-signals: Signal クラス (シム)
// - tweedle.js:    Tween / Group / Easing (時間ベース本実装)
// ============================================================
// pixi-ui.js (IIFE) は末尾で `(..., typedSignals, ..., tweedle_js)` を
// 参照するため、これらをグローバルに用意しておく必要がある。
//
// tweedle 部は当初「アニメーション無しの即時遷移シム」だったが、FancyButton の
// scale bounce / シーン fade トランジションを動かすため、duration / easing /
// Group.shared.update(time) を持つ本物の最小実装に差し替えた。
// 利用側 (Demo 11/12) は毎フレーム `tweedle_js.Group.shared.update()` を呼ぶこと。
// 呼ばないと FancyButton.animations と framework.SceneTransition のフェードが進まない。

if (!globalThis.__pixi_ui_deps_shim_loaded) {
    globalThis.__pixi_ui_deps_shim_loaded = true;

    // ============================================================
    // typed-signals (最小実装)
    // ============================================================
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

    // ============================================================
    // tweedle.js (実装版 — duration / easing / chain / Group ticker)
    // ============================================================

    function _now() {
        return (typeof performance !== "undefined" && performance.now)
             ? performance.now() : Date.now();
    }

    // Easing 関数群 (Robert Penner)
    // 入力 k ∈ [0,1] → 出力 ∈ [0,1] (場合により外側)
    var Easing = {
        Linear:    { None:  function (k) { return k; } },
        Quadratic: {
            In:    function (k) { return k * k; },
            Out:   function (k) { return k * (2 - k); },
            InOut: function (k) {
                if ((k *= 2) < 1) return 0.5 * k * k;
                return -0.5 * (--k * (k - 2) - 1);
            },
        },
        Cubic: {
            In:    function (k) { return k * k * k; },
            Out:   function (k) { return --k * k * k + 1; },
            InOut: function (k) {
                if ((k *= 2) < 1) return 0.5 * k * k * k;
                return 0.5 * ((k -= 2) * k * k + 2);
            },
        },
        Quartic: {
            In:    function (k) { return k * k * k * k; },
            Out:   function (k) { return 1 - (--k * k * k * k); },
        },
        Sinusoidal: {
            In:    function (k) { return 1 - Math.cos((k * Math.PI) / 2); },
            Out:   function (k) { return Math.sin((k * Math.PI) / 2); },
            InOut: function (k) { return 0.5 * (1 - Math.cos(Math.PI * k)); },
        },
        Exponential: {
            Out:   function (k) { return k === 1 ? 1 : 1 - Math.pow(2, -10 * k); },
        },
        Back: {
            In:    function (k) { var s = 1.70158; return k * k * ((s + 1) * k - s); },
            Out:   function (k) { var s = 1.70158; return --k * k * ((s + 1) * k + s) + 1; },
            InOut: function (k) {
                var s = 1.70158 * 1.525;
                if ((k *= 2) < 1) return 0.5 * (k * k * ((s + 1) * k - s));
                return 0.5 * ((k -= 2) * k * ((s + 1) * k + s) + 2);
            },
        },
        Elastic: {
            Out: function (k) {
                if (k === 0 || k === 1) return k;
                var p = 0.3;
                return Math.pow(2, -10 * k) * Math.sin(((k - p / 4) * (2 * Math.PI)) / p) + 1;
            },
        },
    };

    // --- Group ---
    // 任意個の Tween を保持し、update(time) で一括前進する。
    // FancyButton の animations は Group.shared に自動 attach するので、
    // ホスト側は毎フレーム `Group.shared.update()` を呼ぶだけで OK。
    function Group() { this._tweens = []; }
    Group.prototype.add = function (t) {
        if (this._tweens.indexOf(t) < 0) this._tweens.push(t);
    };
    Group.prototype.remove = function (t) {
        var i = this._tweens.indexOf(t);
        if (i >= 0) this._tweens.splice(i, 1);
    };
    Group.prototype.update = function (time) {
        if (time === undefined) time = _now();
        // 反復中に splice が起きても安全なよう逆順走査
        for (var i = this._tweens.length - 1; i >= 0; i--) {
            var t = this._tweens[i];
            if (!t._update(time)) {
                this._tweens.splice(i, 1);
            }
        }
        return this._tweens.length > 0;
    };
    Group.prototype.getAll = function () { return this._tweens.slice(); };
    Group.prototype.removeAll = function () { this._tweens.length = 0; };
    Group.shared = new Group();

    // --- Tween ---
    function Tween(target, group) {
        this._target = target;
        this._toProps = null;
        this._fromProps = null;
        this._duration = 0;
        this._delay = 0;
        this._easing = Easing.Linear.None;
        this._onStart = null;
        this._onUpdate = null;
        this._onComplete = null;
        this._onStop = null;
        this._startTime = 0;
        this._isPlaying = false;
        this._hasStarted = false;
        this._repeat = 0;
        this._yoyo = false;
        this._reversed = false;
        this._chained = [];
        this._group = group || Group.shared;
    }
    Tween.prototype.to = function (props, duration) {
        this._toProps = props;
        this._duration = duration || 0;
        return this;
    };
    Tween.prototype.from = function (props) {
        this._fromProps = props;
        return this;
    };
    Tween.prototype.duration = function (d) { this._duration = d; return this; };
    Tween.prototype.delay = function (d)    { this._delay = d || 0; return this; };
    Tween.prototype.easing = function (fn)  { this._easing = fn || Easing.Linear.None; return this; };
    Tween.prototype.onStart   = function (fn) { this._onStart   = fn; return this; };
    Tween.prototype.onUpdate  = function (fn) { this._onUpdate  = fn; return this; };
    Tween.prototype.onComplete = function (fn) { this._onComplete = fn; return this; };
    Tween.prototype.onStop    = function (fn) { this._onStop    = fn; return this; };
    Tween.prototype.repeat    = function (n)  { this._repeat = n | 0; return this; };
    Tween.prototype.yoyo      = function (b)  { this._yoyo = !!b; return this; };
    Tween.prototype.chain     = function () {
        for (var i = 0; i < arguments.length; i++) this._chained.push(arguments[i]);
        return this;
    };
    Tween.prototype.group     = function (g)  { this._group = g; return this; };

    Tween.prototype.start = function (time) {
        if (time === undefined) time = _now();
        this._startTime  = time + this._delay;
        this._hasStarted = false;   // 実 update 時に from を捕る (delay 中の値変化に追随)
        this._isPlaying  = true;
        this._reversed   = false;
        this._group.add(this);
        return this;
    };

    Tween.prototype.stop = function () {
        if (!this._isPlaying) return this;
        this._isPlaying = false;
        this._group.remove(this);
        if (this._onStop) try { this._onStop(this._target); } catch (e) { console.error(e); }
        return this;
    };

    Tween.prototype.isPlaying = function () { return this._isPlaying; };

    // 数値プロパティを深さ優先で snapshot / interpolate するヘルパー。
    // toProps が {scale: {x: 1, y: 1}, alpha: 0} のようなネスト構造でも、
    // 数値リーフのみを処理して target を破壊しない (= scale を NaN にしない)。
    function _snapshotValues(target, toProps) {
        var snap = {};
        if (!target || !toProps) return snap;
        for (var k in toProps) {
            if (!toProps.hasOwnProperty(k)) continue;
            var v = toProps[k];
            if (typeof v === "number") {
                snap[k] = (k in target && typeof target[k] === "number") ? target[k] : 0;
            } else if (v !== null && typeof v === "object" && target[k] && typeof target[k] === "object") {
                snap[k] = _snapshotValues(target[k], v);
            }
        }
        return snap;
    }
    function _applyValues(target, startValues, toProps, alpha) {
        if (!target || !toProps) return;
        for (var k in toProps) {
            if (!toProps.hasOwnProperty(k)) continue;
            var to = toProps[k];
            var from = startValues ? startValues[k] : undefined;
            if (typeof to === "number") {
                var s = (typeof from === "number") ? from : 0;
                target[k] = s + (to - s) * alpha;
            } else if (to !== null && typeof to === "object" && target[k] && typeof target[k] === "object") {
                _applyValues(target[k], from, to, alpha);
            }
        }
    }

    // group から呼ばれる内部 update。false を返すとリストから除去される。
    Tween.prototype._update = function (time) {
        if (!this._isPlaying) return false;
        if (time < this._startTime) return true;   // delay 中

        // 初回 update で start 値を記憶 (ネストオブジェクトも辿る)
        if (!this._hasStarted) {
            this._hasStarted = true;
            if (this._fromProps) {
                _applyValues(this._target, _snapshotValues(this._target, this._fromProps), this._fromProps, 1);
            }
            this._startValues = _snapshotValues(this._target, this._toProps);
            if (this._onStart) try { this._onStart(this._target); } catch (e) { console.error(e); }
        }

        var elapsed = this._duration > 0 ? (time - this._startTime) / this._duration : 1;
        if (elapsed > 1) elapsed = 1;
        var alpha = this._easing(this._reversed ? (1 - elapsed) : elapsed);

        _applyValues(this._target, this._startValues, this._toProps, alpha);

        if (this._onUpdate) {
            try { this._onUpdate(this._target, elapsed); } catch (err) { console.error(err); }
        }

        if (elapsed >= 1) {
            if (this._repeat > 0) {
                this._repeat--;
                if (this._yoyo) this._reversed = !this._reversed;
                this._startTime = time;
                this._hasStarted = false;
                return true;
            }
            this._isPlaying = false;
            if (this._onComplete) {
                try { this._onComplete(this._target); } catch (err) { console.error(err); }
            }
            // chain
            for (var i = 0; i < this._chained.length; i++) {
                this._chained[i].start(time);
            }
            return false;
        }
        return true;
    };

    globalThis.tweedle_js = { Tween: Tween, Group: Group, Easing: Easing };

    console.log("pixi-ui deps loaded (typed-signals + tweedle.js real impl)");
}
