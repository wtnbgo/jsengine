// ============================================================
// UI Effects — tweedle + PIXI を使った汎用 UI 演出ヘルパー
// ============================================================
//
// 前提:
//   - PIXI が globalThis にロード済
//   - tweedle_js が globalThis にロード済 (pixi-ui-deps-shim.js)
//   - ホスト側が毎フレーム tweedle_js.Group.shared.update() を呼んでいる
//     (Demo 11 の update ループ参照)
//
// 提供 (globalThis.UIEffects):
//   UIEffects.flash(container, opts)
//       container 全体を半透明色で覆い、フェードアウトで消す。タップフラッシュ等に。
//       opts: { color (0xffffff), alpha (0.6), duration (250), easing, bounds }
//
//   UIEffects.ripple(container, x, y, opts)
//       (x, y) を中心に円を広げつつフェードアウト。Material Design 風タップ感。
//       opts: { color (0xffffff), startAlpha (0.5), maxRadius (推定), duration (400), easing }
//
//   UIEffects.bounce(target, opts)
//       target.scale を 1 → smallScale → 1 にバウンドさせる。FancyButton 無しでも
//       SimpleButton 等に「触ったぞ」感を足せる。
//       opts: { downScale (0.92), upScale (1.0), downDur (80), upDur (180), easing }
//
//   UIEffects.toast(parent, message, opts)
//       parent 下に短時間表示される通知ラベル (下からスライドイン + 自動フェードアウト)。
//       opts: { duration (1800), bgColor (0x101820), textColor (0xffffff), fontSize (16),
//               width (auto), bottom (40), enterDur (200), exitDur (250) }
//       戻り値: 生成した Container (途中で remove したいとき用)
//
// 全ヘルパーは Container を引数として受け取り、エフェクトオブジェクトを引数 parent の上に
// 一時的に addChild する (完了時に自動 destroy)。

(function() {

if (typeof PIXI === "undefined") {
    console.warn("framework/ui_effects.js: PIXI が無い");
    return;
}
if (typeof tweedle_js === "undefined") {
    console.warn("framework/ui_effects.js: tweedle_js が無い (実装版 pixi-ui-deps-shim.js が必要)");
    return;
}

var Easing = tweedle_js.Easing;

// container の getBounds が壊れている (空 container 等) ケースをガード
function localBounds(container) {
    try {
        var b = container.getLocalBounds();
        if (b && isFinite(b.width) && isFinite(b.height) && b.width > 0 && b.height > 0) return b;
    } catch (_) {}
    return null;
}

function destroyLater(obj, delayMs) {
    setTimeout(function() {
        try { obj.parent && obj.parent.removeChild(obj); obj.destroy(); } catch (_) {}
    }, Math.max(0, delayMs | 0));
}

var UIEffects = {

    flash: function(container, opts) {
        opts = opts || {};
        var color    = (typeof opts.color    === "number") ? opts.color    : 0xffffff;
        var alpha    = (typeof opts.alpha    === "number") ? opts.alpha    : 0.6;
        var duration = (typeof opts.duration === "number") ? opts.duration : 250;
        var easing   = opts.easing || Easing.Quadratic.Out;
        var b = opts.bounds || localBounds(container) || { x: 0, y: 0, width: 100, height: 100 };

        var g = new PIXI.Graphics();
        g.beginFill(color, 1).drawRect(b.x, b.y, b.width, b.height).endFill();
        g.alpha = alpha;
        container.addChild(g);

        new tweedle_js.Tween(g)
            .to({ alpha: 0 }, duration)
            .easing(easing)
            .onComplete(function() {
                if (g.parent) g.parent.removeChild(g);
                g.destroy();
            })
            .start();
        return g;
    },

    ripple: function(container, x, y, opts) {
        opts = opts || {};
        var color      = (typeof opts.color      === "number") ? opts.color      : 0xffffff;
        var startAlpha = (typeof opts.startAlpha === "number") ? opts.startAlpha : 0.5;
        var duration   = (typeof opts.duration   === "number") ? opts.duration   : 400;
        var easing     = opts.easing || Easing.Quadratic.Out;
        var b = localBounds(container);
        var maxR = opts.maxRadius;
        if (typeof maxR !== "number") {
            if (b) {
                // 中心から最も遠い角までの距離
                var dx1 = x - b.x, dy1 = y - b.y;
                var dx2 = (b.x + b.width)  - x, dy2 = (b.y + b.height) - y;
                var ax = Math.max(Math.abs(dx1), Math.abs(dx2));
                var ay = Math.max(Math.abs(dy1), Math.abs(dy2));
                maxR = Math.sqrt(ax * ax + ay * ay);
            } else {
                maxR = 80;
            }
        }

        var g = new PIXI.Graphics();
        g.x = x; g.y = y;
        g.scale.set(0.001);   // 0 だと描画されないので極小から
        g.alpha = startAlpha;
        g.beginFill(color, 1).drawCircle(0, 0, maxR).endFill();
        container.addChild(g);

        new tweedle_js.Tween(g.scale)
            .to({ x: 1, y: 1 }, duration)
            .easing(easing)
            .start();
        new tweedle_js.Tween(g)
            .to({ alpha: 0 }, duration)
            .easing(easing)
            .onComplete(function() {
                if (g.parent) g.parent.removeChild(g);
                g.destroy();
            })
            .start();
        return g;
    },

    bounce: function(target, opts) {
        opts = opts || {};
        var downScale = (typeof opts.downScale === "number") ? opts.downScale : 0.92;
        var upScale   = (typeof opts.upScale   === "number") ? opts.upScale   : 1.0;
        var downDur   = (typeof opts.downDur   === "number") ? opts.downDur   : 80;
        var upDur     = (typeof opts.upDur     === "number") ? opts.upDur     : 180;
        var easing    = opts.easing || Easing.Back.Out;
        if (!target || !target.scale) return;

        new tweedle_js.Tween(target.scale)
            .to({ x: downScale, y: downScale }, downDur)
            .easing(Easing.Quadratic.Out)
            .chain(
                new tweedle_js.Tween(target.scale)
                    .to({ x: upScale, y: upScale }, upDur)
                    .easing(easing)
            )
            .start();
    },

    toast: function(parent, message, opts) {
        opts = opts || {};
        var duration  = (typeof opts.duration  === "number") ? opts.duration  : 1800;
        var bgColor   = (typeof opts.bgColor   === "number") ? opts.bgColor   : 0x101820;
        var textColor = (typeof opts.textColor === "number") ? opts.textColor : 0xffffff;
        var fontSize  = (typeof opts.fontSize  === "number") ? opts.fontSize  : 16;
        var bottom    = (typeof opts.bottom    === "number") ? opts.bottom    : 40;
        var enterDur  = (typeof opts.enterDur  === "number") ? opts.enterDur  : 200;
        var exitDur   = (typeof opts.exitDur   === "number") ? opts.exitDur   : 250;

        var container = new PIXI.Container();
        var label = new PIXI.Text(message, {
            fontFamily: "Arial", fontSize: fontSize, fill: textColor,
        });
        var pad = 12;
        var w = (typeof opts.width === "number") ? opts.width : (label.width + pad * 2);
        var h = label.height + pad;
        var bg = new PIXI.Graphics();
        bg.beginFill(bgColor, 0.92).lineStyle(1, 0x303848, 1).drawRoundedRect(0, 0, w, h, 8).endFill();
        container.addChild(bg);
        label.anchor.set(0.5, 0.5);
        label.x = w / 2; label.y = h / 2;
        container.addChild(label);

        // 親の幅から中央寄せ
        var parentW = (parent.width && parent.width > 0) ? parent.width
                    : (parent.screen ? parent.screen.width : (localBounds(parent) || { width: 0 }).width);
        var parentH = (parent.height && parent.height > 0) ? parent.height
                    : (parent.screen ? parent.screen.height : (localBounds(parent) || { height: 0 }).height);
        container.x = (parentW > 0) ? Math.round((parentW - w) / 2) : 0;
        var targetY = (parentH > 0) ? (parentH - h - bottom) : (- h - bottom);
        container.y = targetY + 30;  // 30px 下からスライドイン
        container.alpha = 0;
        parent.addChild(container);

        // enter
        new tweedle_js.Tween(container)
            .to({ alpha: 1, y: targetY }, enterDur)
            .easing(Easing.Quadratic.Out)
            .start();
        // exit (enterDur + duration 後に開始)
        setTimeout(function() {
            new tweedle_js.Tween(container)
                .to({ alpha: 0, y: targetY - 20 }, exitDur)
                .easing(Easing.Quadratic.In)
                .onComplete(function() {
                    if (container.parent) container.parent.removeChild(container);
                    container.destroy({ children: true });
                })
                .start();
        }, enterDur + duration);

        return container;
    },
};

globalThis.UIEffects = UIEffects;

console.log("framework/ui_effects.js loaded");

})();
