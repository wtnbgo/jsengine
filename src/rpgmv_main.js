// ============================================================
// RPG Maker MV ブートストラップ (jsengine 用)
// ブラウザ基本 API は jsengine 内蔵 sysinit.js が提供する。
// このファイルでは RPG Maker MV / pixi v4 を動かすための固有シムと
// オーバーライドを当ててから RPG MV のメインスクリプトを起動する。
// ============================================================

console.log("=== RPG Maker MV bootstrap ===");

// ------------------------------------------------------------
// セーブデータ保存先を分離
// data/System.json の gameTitle を識別子に使って、
// %APPDATA%/jsengine_rpgmv/<gameTitle>/localStorage.json に保存する。
// (default は %APPDATA%/jsengine/jsengine/localStorage.json なので
//  Demo 等と混ざらないようにこのタイミングで切り替えておく。)
try {
    var sys = JSON.parse(fs.readText("data/System.json"));
    var title = (sys && sys.gameTitle) ? sys.gameTitle : "default";
    localStorage.setPath("jsengine_rpgmv", title);
} catch(e) {
    localStorage.setPath("jsengine_rpgmv", "default");
}

// ------------------------------------------------------------
// RPG Maker MV 固有のシム / ダミー (sysinit の汎用シムでは足りない部分)
// ------------------------------------------------------------

// document.body に "GameCanvas" を追跡できるよう appendChild をラップ
var _gameCanvas = null;
var _origBodyAppend = document.body.appendChild;
document.body.appendChild = function(child) {
    if (child && child.id === "GameCanvas") _gameCanvas = child;
    return _origBodyAppend.call(this, child);
};
document.getElementById = function(id) {
    if (id === "GameCanvas") return _gameCanvas;
    return null;
};

// iphone-inline-video の代替 (RPG MV のプラグインが参照する)
if (typeof makeVideoPlayableInline === "undefined") {
    var makeVideoPlayableInline = function() {};
    window.makeVideoPlayableInline = makeVideoPlayableInline;
}

// FPSMeter ダミー (RPG MV の Graphics._createFPSMeter が new FPSMeter() する)
if (typeof FPSMeter === "undefined") {
    var FPSMeter = function() {};
    FPSMeter.prototype.tickStart = function() {};
    FPSMeter.prototype.tick = function() {};
    FPSMeter.prototype.show = function() {};
    FPSMeter.prototype.hide = function() {};
    FPSMeter.prototype.destroy = function() {};
    window.FPSMeter = FPSMeter;
}

// GameFont を alias 付きでロード (RPG Maker MV は "GameFont" というフォント名を使う)
try {
    Canvas2D.loadFont("fonts/mplus-1m-regular.ttf", "GameFont");
} catch(e) {
    console.error("GameFont load failed: " + e);
}

// ------------------------------------------------------------
// pixi.js v4 + RPG Maker MV コアをロード
//
// index.html を読んで <script src="..."> の列挙をそのまま再生する。
// これによりプロジェクト毎に異なる構成 (pixi-tilemap / pixi-picture を含まない
// バージョン、 デプロイ前に minified ファイル名に変わっているケース等) でも
// 追加の特殊対応なしに動く。
//   - js/main.js は我々の bootstrap がその役割を担うのでスキップ
//   - iphone-inline-video.* は makeVideoPlayableInline ダミーで吸収済みなのでスキップ
//   - index.html を読めない場合は標準構成 (pixi + tilemap + picture + fpsmeter +
//     lz-string + rpg_*) にフォールバック
// ------------------------------------------------------------
(function loadProjectScripts() {
    var SKIP_PATTERNS = [
        /(^|\/)main\.js$/i,        // 我々の bootstrap が main.js の役割
        /iphone-inline-video/i,    // makeVideoPlayableInline ダミーで吸収
        /(^|\/)plugins\.js$/i,     // override 適用後に明示 loadScript する
    ];
    var scripts = null;
    try {
        var html = fs.readText("index.html");
        scripts = [];
        // <script ... src="..."> を順序を保ったまま抽出。 attribute 順は問わない。
        var re = /<script\b[^>]*\bsrc\s*=\s*("([^"]+)"|'([^']+)')[^>]*>/gi;
        var m;
        while ((m = re.exec(html)) !== null) {
            var src = m[2] || m[3];
            if (!src) continue;
            // 同階層相対パスの先頭 "./" を剥がす
            if (src.indexOf("./") === 0) src = src.substring(2);
            var skip = false;
            for (var i = 0; i < SKIP_PATTERNS.length; i++) {
                if (SKIP_PATTERNS[i].test(src)) { skip = true; break; }
            }
            if (!skip) scripts.push(src);
        }
        if (scripts.length === 0) scripts = null;  // <script src> が無ければフォールバック
    } catch(e) {
        console.log("index.html not found, using default script list");
        scripts = null;
    }
    if (!scripts) {
        scripts = [
            "js/libs/pixi.js",
            "js/libs/pixi-tilemap.js",
            "js/libs/pixi-picture.js",
            "js/libs/fpsmeter.js",
            "js/libs/lz-string.js",
            "js/rpg_core.js",
            "js/rpg_managers.js",
            "js/rpg_objects.js",
            "js/rpg_scenes.js",
            "js/rpg_sprites.js",
            "js/rpg_windows.js",
        ];
    }
    console.log("Loading " + scripts.length + " scripts from index.html");
    for (var j = 0; j < scripts.length; j++) {
        loadScript(scripts[j]);
    }
})();

// ------------------------------------------------------------
// RPG Maker MV クラスへのオーバーライド (rpg_core.js 読み込み後)
// ------------------------------------------------------------

// Utils.canReadGameFiles: file:// 等で常に true 扱い
if (typeof Utils !== "undefined") {
    Utils.canReadGameFiles = function() { return true; };
}
// XMLHttpRequest.overrideMimeType の no-op 追加 (RPG MV が呼ぶ)
if (typeof XMLHttpRequest !== "undefined" && !XMLHttpRequest.prototype.overrideMimeType) {
    XMLHttpRequest.prototype.overrideMimeType = function() {};
}
// Bitmap の画像ロードエラーをログ
if (typeof Bitmap !== "undefined") {
    var _origBitmapErr = Bitmap.prototype._onError;
    Bitmap.prototype._onError = function() {
        console.error("Image load failed: " + (this._image ? this._image._src : "unknown"));
        if (_origBitmapErr) _origBitmapErr.call(this);
    };
}
// Graphics: CSS フォントロードは即完了扱い。 _createRenderer のエラーをログに残す。
// + ゲーム解像度 (816x624) を SDL ウィンドウに反映する。
if (typeof Graphics !== "undefined") {
    Graphics._cssFontLoading = false;
    Graphics.isFontLoaded = function() { return true; };

    Graphics._createRenderer = function() {
        try {
            PIXI.dontSayHello = true;
            var options = { view: this._canvas };
            this._renderer = PIXI.autoDetectRenderer(this._width, this._height, options);
            if (this._renderer && this._renderer.textureGC) {
                this._renderer.textureGC.maxIdle = 1;
            }
            // ネイティブ window をゲーム解像度に追従させる
            if (typeof window.resizeTo === "function") {
                window.resizeTo(this._width, this._height);
            }
        } catch(e) {
            console.error("_createRenderer error: " + e);
            if (e.stack) console.error(e.stack);
            this._renderer = null;
        }
    };
}

// WindowLayer のフィルタ描画を修正
// RPG Maker MV の元実装は VoidFilter + scissor mask で「window が前面 sprite を覆う領域」を
// 一旦クリアしてから window を描く前提だが、 空の WindowLayer (どの window も openness=0) でも
// pushFilter/popFilter を走らせると、 背景が空 FBO の合成で上書きされて消える。
// → 開いている window がなければ FBO 経路をスキップして直接描画する。
if (typeof WindowLayer !== "undefined") {
    WindowLayer.prototype.renderWebGL = function(renderer) {
        if (!this.visible || !this.renderable || this.children.length === 0) return;

        var hasOpenWindow = false;
        for (var i = 0; i < this.children.length; i++) {
            var child = this.children[i];
            if (child._isWindow && child.visible && child.openness > 0) {
                hasOpenWindow = true;
                break;
            }
        }

        if (!hasOpenWindow) {
            for (var j = 0; j < this.children.length; j++) {
                if (!this.children[j]._isWindow) {
                    this.children[j].renderWebGL(renderer);
                }
            }
            return;
        }

        // 元の FBO 経路 (rpg_core.js WindowLayer.renderWebGL と同じ流れ)
        renderer.flush();
        this.filterArea.copy(this);
        renderer.filterManager.pushFilter(this, this.filters);
        renderer.currentRenderer.start();

        var shift = new PIXI.Point();
        var rt = renderer._activeRenderTarget;
        var projectionMatrix = rt.projectionMatrix;
        shift.x = Math.round((projectionMatrix.tx + 1) / 2 * rt.sourceFrame.width);
        shift.y = Math.round((projectionMatrix.ty + 1) / 2 * rt.sourceFrame.height);

        for (var k = 0; k < this.children.length; k++) {
            var child2 = this.children[k];
            if (child2._isWindow && child2.visible && child2.openness > 0) {
                this._maskWindow(child2, shift);
                renderer.maskManager.pushScissorMask(this, this._windowMask);
                renderer.clear();
                renderer.maskManager.popScissorMask();
                renderer.currentRenderer.start();
                child2.renderWebGL(renderer);
                renderer.currentRenderer.flush();
            }
        }

        renderer.flush();
        renderer.filterManager.popFilter();
        renderer.maskManager.popScissorMask();

        for (var l = 0; l < this.children.length; l++) {
            if (!this.children[l]._isWindow) {
                this.children[l].renderWebGL(renderer);
            }
        }
    };
}

// ------------------------------------------------------------
// プラグインとメイン処理を起動
// ------------------------------------------------------------
loadScript("js/plugins.js");
PluginManager.setup($plugins);

SceneManager.initialize();
SceneManager.goto(Scene_Boot);
SceneManager.requestUpdate();

// ------------------------------------------------------------
// jsengine ライフサイクル
// RPG Maker MV は requestAnimationFrame で自身のループを回すので update/render は空。
// ES Module 評価のため globalThis に明示露出。
// ------------------------------------------------------------
function update(dt) {}
function render() {}
function done() {}
globalThis.update = update;
globalThis.render = render;
globalThis.done   = done;
