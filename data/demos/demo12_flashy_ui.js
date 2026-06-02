// ============================================================
// Demo 12: 派手 UI ショーケース
// ============================================================
//
// 目的:
//   - 本物の tweedle.js (pixi-ui-deps-shim 内に置換済) で
//     pixi.ui FancyButton の hover / pressed アニメが動くことの実証
//   - UIEffects (flash / ripple / bounce / toast) のショーケース
//   - SceneManager.replaceWithFade のシーン間トランジション
//
// 1280×720。`=` キーで起動。`-` (Demo 11) と並列の派手版。
//
// 構成 (シーン 2 つ):
//   MainScene:
//     - アニメ背景線
//     - 自動アニメ ProgressBar (2 秒で 0→100 をループ)
//     - FancyButton 4 つ:
//         [Tap me!]     — ripple at touch + flash
//         [Show toast]  — UIEffects.toast 発火
//         [Bounce]      — UIEffects.bounce で scale ばね
//         [Go Settings] — replaceWithFade で SettingsScene へ
//     - [Back to Demo Menu] — replaceWithFade → Demo 1 へ
//   SettingsScene (中身は薄め、トランジションのデモが主):
//     - 説明テキスト
//     - [Back] — replaceWithFade → MainScene
//
// 依存:
//   PIXI v7, pixi.ui v1, tweedle_js (本物実装),
//   framework/scene_manager.js, framework/perf_hud.js (任意), framework/ui_effects.js

(function() {

var APP_W = 1280, APP_H = 720;

var pixiApp = null;
var sceneRoot = null;
var bgAnim = null;        // 背景の流れる線
var bgT = 0;

function ensurePixi() {
    if (pixiApp) return pixiApp;
    if (typeof PIXI === "undefined") {
        console.error("Demo 12: PIXI が未ロード");
        return null;
    }
    pixiApp = new PIXI.Application({
        width: APP_W, height: APP_H,
        backgroundColor: 0x0a1828,
        antialias: true,
        autoStart: false,
        sharedTicker: false,
    });
    pixiApp.stage.interactive = true;
    pixiApp.stage.hitArea = pixiApp.screen;

    sceneRoot = new PIXI.Container();
    pixiApp.stage.addChild(sceneRoot);

    // ガス雲 (グラデ風) の背景
    bgAnim = new PIXI.Graphics();
    pixiApp.stage.addChildAt(bgAnim, 0);
    return pixiApp;
}

function drawBackground(t) {
    var g = bgAnim;
    g.clear();
    // 上から下へのグラデーション (帯) + 流れる線
    for (var i = 0; i < 20; i++) {
        var y = (i / 19) * APP_H;
        var alpha = 0.08 + 0.10 * Math.sin(t * 0.0008 + i * 0.4);
        g.beginFill(0x2050a0, alpha).drawRect(0, y, APP_W, APP_H / 19 + 2).endFill();
    }
    // 流れる斜線
    var offset = (t * 0.04) % 60;
    g.lineStyle(1, 0x4080ff, 0.18);
    for (var x = -200; x < APP_W + 200; x += 60) {
        g.moveTo(x + offset, 0);
        g.lineTo(x + offset + APP_H, APP_H);
    }
}

// ============================================================
// ボタン工房 — FancyButton と背景 Graphics を簡潔に生成
// ============================================================
function makeRoundedBg(w, h, color, opts) {
    opts = opts || {};
    var g = new PIXI.Graphics();
    g.beginFill(color, 1);
    if (opts.border) g.lineStyle(opts.border, opts.borderColor || 0xffffff, 1);
    g.drawRoundedRect(0, 0, w, h, opts.radius || 12);
    g.endFill();
    return g;
}
function fancy(label, w, h, theme) {
    theme = theme || {};
    var base   = theme.base   || 0x2d6cdf;
    var hover  = theme.hover  || 0x4d8cff;
    var press  = theme.press  || 0x1d4cbf;
    var border = theme.border || 0x70a0ff;

    // 注: animations.default を必ず指定すること。指定しないと FancyButton の
    //     playAnimations("default") が「フォールバック tween で innerView を
    //     originalInnerViewState (= setState 時点でまだ defaultView 未追加なので
    //     width:0/height:0) へ縮める」挙動になり、ボタンが一瞬表示後に潰れる。
    var btn = new PIXI.ui.FancyButton({
        defaultView: makeRoundedBg(w, h, base,  { radius: 14, border: 1, borderColor: border }),
        hoverView:   makeRoundedBg(w, h, hover, { radius: 14, border: 2, borderColor: 0xffffff }),
        pressedView: makeRoundedBg(w, h, press, { radius: 14, border: 1, borderColor: border }),
        text: new PIXI.Text(label, {
            fontFamily: "Arial", fontSize: 20, fill: 0xffffff, fontWeight: "bold",
        }),
        animations: {
            default: { props: { scale: { x: 1.00, y: 1.00 } }, duration: 100 },
            hover:   { props: { scale: { x: 1.05, y: 1.05 } }, duration: 120 },
            pressed: { props: { scale: { x: 0.93, y: 0.93 } }, duration: 90  },
        },
    });
    return btn;
}

// ============================================================
// MainScene
// ============================================================
class MainScene extends Scene {
    enter() {
        this.container = new PIXI.Container();
        sceneRoot.addChild(this.container);

        // タイトル
        var hdr = new PIXI.Text("FANCY UI SHOWCASE", {
            fontFamily: "Arial", fontSize: 44, fill: 0xffffff, fontWeight: "bold",
            dropShadow: true, dropShadowColor: 0x000000, dropShadowDistance: 3, dropShadowBlur: 2,
        });
        hdr.x = 80; hdr.y = 50;
        this.container.addChild(hdr);

        var sub = new PIXI.Text("FancyButton + bounce + ripple + toast + scene fade (powered by tweedle.js)", {
            fontFamily: "Arial", fontSize: 16, fill: 0x80c0ff,
        });
        sub.x = 80; sub.y = 108;
        this.container.addChild(sub);

        // 自動アニメ ProgressBar
        var pbBgX = 80, pbBgY = 160, pbW = 1120, pbH = 18;
        this.pbBg = new PIXI.Graphics();
        this.pbBg.beginFill(0x1a2840, 1).lineStyle(1, 0x3060a0, 1)
                 .drawRoundedRect(pbBgX, pbBgY, pbW, pbH, 9).endFill();
        this.container.addChild(this.pbBg);

        this.pbFill = new PIXI.Graphics();
        this.container.addChild(this.pbFill);
        this._pbX = pbBgX; this._pbY = pbBgY; this._pbW = pbW; this._pbH = pbH;
        this._pbValue = { v: 0 };   // tweedle target
        this._startPbLoop();

        // ボタン群
        var btnY = 240;
        var spacing = 16;
        var bw = 240, bh = 64;

        this.tapBtn = fancy("Tap me!", bw, bh, { base: 0x2da34a, hover: 0x3ec466, press: 0x1f7c38 });
        this.tapBtn.x = 80; this.tapBtn.y = btnY;
        this.container.addChild(this.tapBtn);
        var self = this;
        this.tapBtn.onPress.connect(function(_btn, ev) {
            // ev は pixi の FederatedPointerEvent (もしくは null)。global 座標が取れるなら ripple をその位置に
            var lx = bw / 2, ly = bh / 2;
            if (ev && ev.global && self.tapBtn.toLocal) {
                try { var p = self.tapBtn.toLocal(ev.global); lx = p.x; ly = p.y; } catch (_) {}
            }
            UIEffects.ripple(self.tapBtn, lx, ly, { duration: 500, startAlpha: 0.35 });
            UIEffects.flash(self.tapBtn, { alpha: 0.25, duration: 200 });
        });

        this.toastBtn = fancy("Show toast", bw, bh, { base: 0xd09030, hover: 0xe6a040, press: 0xa0701c });
        this.toastBtn.x = 80 + (bw + spacing); this.toastBtn.y = btnY;
        this.container.addChild(this.toastBtn);
        this._toastCount = 0;
        this.toastBtn.onPress.connect(function() {
            self._toastCount++;
            UIEffects.toast(self.container, "Toast #" + self._toastCount + " — auto fades out", {
                duration: 1500, bottom: 50,
            });
        });

        this.bounceBtn = fancy("Bounce", bw, bh, { base: 0x9040c0, hover: 0xb050e0, press: 0x6020a0 });
        this.bounceBtn.x = 80 + (bw + spacing) * 2; this.bounceBtn.y = btnY;
        this.container.addChild(this.bounceBtn);
        this.bounceBtn.onPress.connect(function() {
            // FancyButton 自身の pressed animation も走るが、追加 bounce で派手に
            UIEffects.bounce(self.bounceBtn, { downScale: 0.78, upScale: 1.0, downDur: 100, upDur: 280 });
        });

        this.gotoBtn = fancy("Go Settings →", bw, bh, { base: 0x405078, hover: 0x6070a0, press: 0x303860 });
        this.gotoBtn.x = 80 + (bw + spacing) * 3; this.gotoBtn.y = btnY;
        this.container.addChild(this.gotoBtn);
        this.gotoBtn.onPress.connect(function() {
            if (SceneManager.isTransitioning()) return;
            SceneManager.replaceWithFade(new SettingsScene(), { duration: 400 });
        });

        // Back to Demo Menu
        this.backBtn = fancy("◀ Back to Demo Menu", 280, 48, { base: 0x4a2030, hover: 0x6a3048, press: 0x351822 });
        this.backBtn.x = 80; this.backBtn.y = APP_H - 90;
        this.container.addChild(this.backBtn);
        this.backBtn.onPress.connect(function() {
            if (SceneManager.isTransitioning()) return;
            // フェードアウトで現シーンを消してから Demo 1 へ戻す
            SceneManager.replaceWithFade(new EmptyScene(), { duration: 300 }).then(function() {
                if (typeof globalThis.demo12ExitToDemo1 === "function") globalThis.demo12ExitToDemo1();
            });
        });

        var hint = new PIXI.Text(
            "Click any button to see hover/press animation. tweedle.js drives FancyButton + UIEffects.",
            { fontFamily: "Arial", fontSize: 14, fill: 0x80909a });
        hint.x = 80; hint.y = APP_H - 40;
        this.container.addChild(hint);
    }

    _startPbLoop() {
        var self = this;
        function loop() {
            if (!self.container) return;   // 既に exit 済みなら継続を打ち切る
            self._pbTween = new tweedle_js.Tween(self._pbValue)
                .to({ v: 100 }, 1600)
                .easing(tweedle_js.Easing.Quadratic.InOut)
                .onUpdate(function() { self._redrawPb(); })
                .onComplete(function() {
                    if (!self.container) return;
                    self._pbTween = new tweedle_js.Tween(self._pbValue)
                        .to({ v: 0 }, 1600)
                        .easing(tweedle_js.Easing.Quadratic.InOut)
                        .onUpdate(function() { self._redrawPb(); })
                        .onComplete(function() {
                            if (self.container) loop();
                        })
                        .start();
                })
                .start();
        }
        loop();
    }
    _redrawPb() {
        // destroyed 後に onUpdate が一度だけ来るケースを弾く
        var g = this.pbFill;
        if (!g || g._destroyed || g.destroyed) return;
        g.clear();
        var fillW = Math.max(0.1, (this._pbW - 2) * (this._pbValue.v / 100));
        g.beginFill(0x60a0ff, 1).drawRoundedRect(this._pbX + 1, this._pbY + 1, fillW, this._pbH - 2, 8).endFill();
    }

    exit() {
        // ProgressBar の自動アニメ tween を確実に止めてから container を destroy
        if (this._pbTween) { try { this._pbTween.stop(); } catch (_) {} this._pbTween = null; }
        sceneRoot.removeChild(this.container);
        this.container.destroy({ children: true });
        this.container = null;
        this.pbFill = null;
        this.pbBg = null;
    }
    update(_dt) {}
}

// ============================================================
// SettingsScene (フェードトランジション先のサンプル)
// ============================================================
class SettingsScene extends Scene {
    enter() {
        this.container = new PIXI.Container();
        sceneRoot.addChild(this.container);

        var hdr = new PIXI.Text("SETTINGS (placeholder)", {
            fontFamily: "Arial", fontSize: 40, fill: 0xffffff, fontWeight: "bold",
            dropShadow: true, dropShadowColor: 0x000000, dropShadowDistance: 3, dropShadowBlur: 2,
        });
        hdr.x = 80; hdr.y = 60;
        this.container.addChild(hdr);

        var body = new PIXI.Text(
            "このシーンは replaceWithFade のフェード遷移サンプル用です。\n" +
            "Back ボタンで前のシーンへフェード遷移して戻ります。",
            { fontFamily: "Arial", fontSize: 18, fill: 0xb0c0d0, lineHeight: 28 });
        body.x = 80; body.y = 140;
        this.container.addChild(body);

        this.backBtn = fancy("◀ Back", 220, 56, { base: 0x405078, hover: 0x6070a0, press: 0x303860 });
        this.backBtn.x = 80; this.backBtn.y = 260;
        this.container.addChild(this.backBtn);
        this.backBtn.onPress.connect(function() {
            if (SceneManager.isTransitioning()) return;
            SceneManager.replaceWithFade(new MainScene(), { duration: 400 });
        });
    }
    exit() {
        sceneRoot.removeChild(this.container);
        this.container.destroy({ children: true });
        this.container = null;
    }
    update(_dt) {}
}

// fade out 用に空のシーン
class EmptyScene extends Scene {
    enter() {
        this.container = new PIXI.Container();
        sceneRoot.addChild(this.container);
    }
    exit() {
        sceneRoot.removeChild(this.container);
        this.container.destroy({ children: true });
        this.container = null;
    }
    update(_dt) {}
}

// ============================================================
// 公開エントリ
// ============================================================

globalThis.demo12 = {
    init: function() {
        if (typeof PIXI === "undefined" || !PIXI.ui || !globalThis.SceneManager
            || !globalThis.tweedle_js || !globalThis.UIEffects) {
            console.error("Demo 12: framework が未ロード (pixi / pixi.ui / scene_manager / tweedle / ui_effects)");
            return;
        }
        if (!ensurePixi()) return;
        SceneManager.transitionTarget = sceneRoot;
        if (!SceneManager.top()) SceneManager.push(new MainScene());
        bgT = 0;
    },
    update: function(dt) {
        if (!pixiApp) return;
        bgT += dt;
        drawBackground(bgT);
        if (globalThis.PerfHud) PerfHud.update(dt);
        tweedle_js.Group.shared.update();
        SceneManager.update(dt);
        if (globalThis.PerfHud) {
            PerfHud.set("Scenes", SceneManager.count());
            PerfHud.set("Tweens", tweedle_js.Group.shared.getAll().length);
            PerfHud.refresh();
        }
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
    onLeave: function() {
        if (globalThis.SceneManager) SceneManager.clear();
        if (globalThis.PerfHud) {
            PerfHud.unset("Scenes");
            PerfHud.unset("Tweens");
        }
    },
    exitToDemo1Hook: null,
};

globalThis.demo12ExitToDemo1 = function() {
    if (typeof globalThis.demo12 !== "undefined" && globalThis.demo12.exitToDemo1Hook) {
        globalThis.demo12.exitToDemo1Hook();
    }
};

console.log("demos/demo12_flashy_ui.js loaded");

})();
