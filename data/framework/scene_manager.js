// ============================================================
// SceneManager — Cocos2d Director 風 シーン管理
// ============================================================
//
// 提供する globalThis:
//   class Scene { enter / exit / pause / resume / update / render / handleEvent }
//   SceneManager.push(scene, args, opts)
//   SceneManager.pop()
//   SceneManager.replace(scene, args, opts)
//   SceneManager.clear()        — 全シーンを exit させて空にする (Demo 切替時に推奨)
//   SceneManager.top()
//   SceneManager.count()        — スタック深さ (leak 監視用)
//   SceneManager.stack          (読み取り推奨、書き換えないこと)
//   SceneManager.update(dt)     — 毎フレーム main.js から呼ぶ
//   SceneManager.render()       — 毎フレーム main.js から呼ぶ
//   SceneManager.handleEvent(e) — addEventListener から渡す
//
//   トランジション:
//     SceneManager.transitionTarget   — fade 対象 (PIXI.Container 想定)。
//                                        Demo 側で sceneRoot 等をセットしておく
//     SceneManager.isTransitioning()  — fade 中なら true (入力ロックに使う)
//     SceneManager.replaceWithFade(scene, { duration, args, sceneOpts })
//                                     — fade out → replace → fade in を Promise で完了通知
//     SceneManager.pushWithFade(scene, { ... })   — push 版 (現シーンは pause される)
//
// シーンライフサイクル:
//   enter(args) : push / replace で stack に積まれた直後 (1 回)
//   exit()      : pop / replace で stack から外れる直前 (1 回)
//   pause()     : 自分の上に別シーンが push された時
//   resume()    : 自分の上のシーンが pop された時
//   update(dt)  : 毎フレーム (pauseBelow フラグの影響を受ける)
//   render()    : 毎フレーム (全シーン分が下から順に呼ばれる)
//   handleEvent : 最上位シーンのみ
//
// push の opts:
//   { pauseBelow: true }  この上に乗っかってる間、下のシーンの update を呼ばない
//                         (描画は続行。一時停止メニューで使う)
//   { hideBelow:  true }  下のシーンの render も呼ばない (完全に被せる時)
//
// 使用例:
//   class TitleScene extends Scene {
//       enter() { this.bgm = playBgm("title.mp3"); }
//       exit()  { this.bgm.stop(); }
//       handleEvent(e) {
//           if (e.type === "keydown" && e.code === "Enter") {
//               SceneManager.replace(new MenuScene());
//           }
//       }
//   }
//   SceneManager.push(new TitleScene());

(function() {

class Scene {
    constructor() {
        // ライフサイクルメソッドは override 前提なので空実装
    }
    enter(_args)    {}
    exit()          {}
    pause(_topOpts) {}  // 引数: 自分の上に push されたシーンの opts ({ hideBelow, pauseBelow })
    resume()        {}
    update(_dt)     {}
    render()        {}
    handleEvent(_e) {}
}

// alpha 補間 Promise (tweedle が無ければ即時に到達)
function _tweenAlpha(target, from, to, duration) {
    if (!target) return Promise.resolve();
    target.alpha = from;
    if (typeof tweedle_js === "undefined" || !tweedle_js.Tween || duration <= 0) {
        target.alpha = to;
        return Promise.resolve();
    }
    return new Promise(function(resolve) {
        new tweedle_js.Tween(target)
            .to({ alpha: to }, duration)
            .onComplete(function() { resolve(); })
            .start();
    });
}

class SceneManagerImpl {
    constructor() {
        this.stack = [];
        this.transitionTarget = null;   // Demo 側で sceneRoot 等を入れる
        this._locked = false;
    }

    top() {
        return this.stack.length > 0 ? this.stack[this.stack.length - 1] : null;
    }

    // スタック深さ (PerfHud 等の計測用)
    count() { return this.stack.length; }

    push(scene, args, opts) {
        var newOpts = opts || {};
        // pause(topOpts) で「自分の上に乗ったシーンの opts」を渡すと、
        // 下のシーンが hideBelow=true を見て自分の表示を消せる
        var prev = this.top();
        if (prev) {
            try { prev.pause(newOpts); } catch (e) { console.error("Scene.pause() error:", e); }
        }
        scene.__opts = newOpts;
        this.stack.push(scene);
        scene.enter(args);
    }

    pop() {
        if (this.stack.length === 0) return;
        var top = this.stack.pop();
        try { top.exit(); } catch (e) { console.error("Scene.exit() error:", e); }
        var newTop = this.top();
        if (newTop) {
            try { newTop.resume(); } catch (e) { console.error("Scene.resume() error:", e); }
        }
    }

    replace(scene, args, opts) {
        if (this.stack.length > 0) {
            var old = this.stack.pop();
            try { old.exit(); } catch (e) { console.error("Scene.exit() error:", e); }
        }
        scene.__opts = opts || {};
        this.stack.push(scene);
        scene.enter(args);
    }

    // 全シーン破棄 (Demo 切替時など)
    clear() {
        while (this.stack.length > 0) {
            var s = this.stack.pop();
            try { s.exit(); } catch (e) { console.error("Scene.exit() error:", e); }
        }
    }

    isTransitioning() { return this._locked; }

    // fade out → replace → fade in を Promise で待てる形にした replace。
    // opts: { duration: ms (既定 300), args, sceneOpts, target (省略時 this.transitionTarget) }
    replaceWithFade(scene, opts) {
        return this._fadeSwap("replace", scene, opts);
    }
    pushWithFade(scene, opts) {
        return this._fadeSwap("push", scene, opts);
    }

    _fadeSwap(kind, scene, opts) {
        opts = opts || {};
        var self = this;
        var duration = (typeof opts.duration === "number") ? opts.duration : 300;
        var target = opts.target || this.transitionTarget;
        // 既にトランジション中なら拒否 (二重発火防止)
        if (this._locked) return Promise.resolve();
        this._locked = true;
        var half = duration / 2;
        return _tweenAlpha(target, target ? target.alpha : 1, 0, half).then(function() {
            if (kind === "replace") self.replace(scene, opts.args, opts.sceneOpts);
            else                    self.push(scene,   opts.args, opts.sceneOpts);
            return _tweenAlpha(target, 0, 1, half);
        }).then(function() {
            self._locked = false;
            if (target) target.alpha = 1;
        }, function(err) {
            self._locked = false;
            if (target) target.alpha = 1;
            throw err;
        });
    }

    // 内部用: index i のシーンが「上の pauseBelow / hideBelow に塞がれてる」か判定
    _isBlocked(i, flag) {
        for (var j = i + 1; j < this.stack.length; j++) {
            if (this.stack[j].__opts && this.stack[j].__opts[flag]) return true;
        }
        return false;
    }

    update(dt) {
        // スタック評価中に push/pop/replace が起きると stack が変わるので
        // スナップショットで反復する
        var snapshot = this.stack.slice();
        for (var i = 0; i < snapshot.length; i++) {
            // 上にいるシーンの pauseBelow フラグで止まる
            var paused = false;
            for (var j = i + 1; j < snapshot.length; j++) {
                if (snapshot[j].__opts && snapshot[j].__opts.pauseBelow) { paused = true; break; }
            }
            if (paused) continue;
            try { snapshot[i].update(dt); } catch (e) { console.error("Scene.update() error:", e); }
        }
    }

    render() {
        var snapshot = this.stack.slice();
        for (var i = 0; i < snapshot.length; i++) {
            var hidden = false;
            for (var j = i + 1; j < snapshot.length; j++) {
                if (snapshot[j].__opts && snapshot[j].__opts.hideBelow) { hidden = true; break; }
            }
            if (hidden) continue;
            try { snapshot[i].render(); } catch (e) { console.error("Scene.render() error:", e); }
        }
    }

    // 最上位のみに配信 (キーボード / マウス / ポインタ / ゲームパッド接続イベント等)
    handleEvent(e) {
        var top = this.top();
        if (top) {
            try { top.handleEvent(e); } catch (err) { console.error("Scene.handleEvent() error:", err); }
        }
    }
}

globalThis.Scene = Scene;
globalThis.SceneManager = new SceneManagerImpl();

console.log("framework/scene_manager.js loaded");

})();
