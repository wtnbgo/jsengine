// ============================================================
// VRM スターターキット (standalone サンプル)
// ============================================================
//
// jsengine 上で VRM を使うためのベースシステム (vrmkit/) と、
// それを使った 2 つのモードのひな型:
//
//   [3D 探索モード]   WASD 移動 + 三人称カメラ + VRMA モーション + NPC 会話
//   [ノベルモード]    望遠カメラの 2D 風構図 + メッセージウィンドウ + 選択肢
//
// 操作:
//   Tab            : モード切替 (探索 ⇄ ノベル)
//   F1             : ヘルプ表示切替
//   -- 探索モード --
//   WASD / 矢印 / 左スティック : 移動
//   マウス左ドラッグ / ホイール : カメラ回転 / ズーム
//   1〜7           : VRMA モーション再生
//   E              : (NPC の近くで) 会話
//   -- ノベルモード --
//   クリック / Enter / Space   : 読み進め
//   ↑↓ + Enter / マウス       : 選択肢
//
// 起動方法 (リポジトリルートから):
//   jsengine.exe -data samples/vrm_starter
//   (Makefile 経由なら: make run ARGS="-data samples/vrm_starter")

// ---- フレームワーク (classic script、 globalThis に登録される) ----
loadScript("framework/scene_manager.js");
loadScript("framework/input_action.js");

// ---- フォント (Canvas2D の UI テキスト用) ----
Canvas2D.loadFont("fonts/NotoSansJP-Regular.otf");
Canvas2D.loadFont("fonts/OpenSans-Regular.ttf");
Canvas2D.loadFont("fonts/OpenSans-Bold.ttf");

// ---- vrmkit (three.js / three-vrm / three-vrm-animation はこの中で解決) ----
const VK = loadModule("vrmkit/vrmkit.js");
const THREE = VK.THREE;
globalThis.THREE = THREE;  // コンソールデバッグ用
console.log("vrmkit loaded (three r" + THREE.REVISION + ")");

const { ExploreScene } = loadModule("scenes/explore_scene.js");
const { NovelScene } = loadModule("scenes/novel_scene.js");

// ---- 入力バインド ----
Input.bind("moveUp",    ["KeyW", "ArrowUp", "Gamepad:LeftStickUp"]);
Input.bind("moveDown",  ["KeyS", "ArrowDown", "Gamepad:LeftStickDown"]);
Input.bind("moveLeft",  ["KeyA", "ArrowLeft", "Gamepad:LeftStickLeft"]);
Input.bind("moveRight", ["KeyD", "ArrowRight", "Gamepad:LeftStickRight"]);
Input.bind("talk",      ["KeyE", "Gamepad:A"]);
Input.bind("switchMode", ["Tab", "Gamepad:Y"]);

// ---- 共有コンテキスト ----
const rc = VK.createRenderer({ width: 1280, height: 720 });
const app = {
    renderer: rc.renderer,
    camera: new THREE.PerspectiveCamera(40, rc.width / rc.height, 0.1, 100),
    actors: {},          // a: アオイ (AvatarSample_A), b: ミドリ (AvatarSample_B)
    novelUI: new VK.NovelUI(),
};

let ready = false;
let currentMode = "explore";   // "explore" | "novel"
let loadStatus = "loading...";

// ---- アセットロード (VRM 2 体 + VRMA 7 本) ----
const VRMA_CLIPS = [
    ["showFull", "vrma/VRMA_01.vrma"],  // 全身を見せる
    ["greeting", "vrma/VRMA_02.vrma"],  // 挨拶
    ["peace",    "vrma/VRMA_03.vrma"],  // V サイン
    ["shoot",    "vrma/VRMA_04.vrma"],  // 撃つ
    ["spin",     "vrma/VRMA_05.vrma"],  // 回る
    ["pose",     "vrma/VRMA_06.vrma"],  // モデルポーズ
    ["squat",    "vrma/VRMA_07.vrma"],  // 屈伸
];

async function boot() {
    loadStatus = "loading VRM 1/2: AvatarSample_A";
    const vrmA = await VK.loadVRM("models/AvatarSample_A.vrm");
    loadStatus = "loading VRM 2/2: AvatarSample_B";
    const vrmB = await VK.loadVRM("models/AvatarSample_B.vrm");
    app.actors.a = new VK.VRMActor(vrmA, { name: "アオイ" });
    app.actors.b = new VK.VRMActor(vrmB, { name: "ミドリ" });

    for (let i = 0; i < VRMA_CLIPS.length; i++) {
        const name = VRMA_CLIPS[i][0], path = VRMA_CLIPS[i][1];
        loadStatus = "loading VRMA " + (i + 1) + "/" + VRMA_CLIPS.length + ": " + path;
        const anim = await VK.loadVRMA(path);
        app.actors.a.addClip(name, anim);
        app.actors.b.addClip(name, anim);
    }

    // 歩行/アイドルの VRMA を置いてあれば procedural の代わりにそちらを使う
    // (vrma/walk.vrma + vrma/idle.vrma。 BOOTH 等で入手したモーションをリネームして
    //  置くだけでよい。 無ければ内蔵の procedural 歩行にフォールバック)
    let hasWalk = false, hasIdle = false;
    try {
        const walkAnim = await VK.loadVRMA("vrma/walk.vrma");
        app.actors.a.addClip("walk", walkAnim);
        app.actors.b.addClip("walk", walkAnim);
        hasWalk = true;
    } catch (e) { /* 無ければ procedural */ }
    try {
        const idleAnim = await VK.loadVRMA("vrma/idle.vrma");
        app.actors.a.addClip("idle", idleAnim);
        app.actors.b.addClip("idle", idleAnim);
        hasIdle = true;
    } catch (e) { /* 無ければ procedural */ }
    if (hasWalk && hasIdle) {
        app.actors.a.setLocomotionClips({ idle: "idle", walk: "walk" });
        app.actors.b.setLocomotionClips({ idle: "idle", walk: "walk" });
        console.log("locomotion: using VRMA clips (vrma/walk.vrma + vrma/idle.vrma)");
    } else {
        console.log("locomotion: using procedural gait (put vrma/walk.vrma + vrma/idle.vrma to override)");
    }

    ready = true;
    loadStatus = "ready";
    SceneManager.push(new ExploreScene(app));
    console.log("vrm_starter ready");
}
boot().catch(function(e) {
    loadStatus = "load error: " + e;
    console.error("boot failed: " + e);
    if (e.stack) console.error(e.stack);
});

// ---- モード切替 ----
function switchMode() {
    if (!ready || SceneManager.count() !== 1) return;  // 会話中 (push 中) は切替不可
    if (currentMode === "explore") {
        currentMode = "novel";
        SceneManager.replace(new NovelScene(app));
    } else {
        currentMode = "explore";
        SceneManager.replace(new ExploreScene(app));
    }
    helpDirty = true;
}

// ---- ヘルプ HUD (F1) ----
let helpVisible = true;
let helpDirty = true;
let helpOverlay = null;

function drawHelp() {
    if (!helpOverlay) helpOverlay = new VK.CanvasOverlay(360, 250, 1280, 720);
    if (helpDirty) {
        const c = helpOverlay.canvas;
        c.clearRect(0, 0, 360, 250);
        c.fillStyle = "rgba(0,0,0,0.6)";
        c.fillRect(0, 0, 360, 250);
        c.font = "17px OpenSans-Bold";
        c.textAlign = "left";
        c.textBaseline = "top";
        c.fillStyle = "#ffcc00";
        c.fillText("VRM Starter Kit — " + (currentMode === "explore" ? "3D Explore" : "Novel"), 14, 10);
        c.font = "14px NotoSansJP-Regular";
        const lines = (currentMode === "explore") ? [
            "WASD / 矢印 / スティック : 移動",
            "マウスドラッグ / ホイール : カメラ",
            "1〜7 : VRMA モーション再生",
            "E : NPC の近くで会話",
            "",
            "Tab : ノベルモードへ",
            "F1 : このヘルプを消す",
        ] : [
            "クリック / Enter : 読み進め",
            "↑↓ + Enter / マウス : 選択肢",
            "",
            "Tab : 3D 探索モードへ",
            "F1 : このヘルプを消す",
        ];
        let y = 44;
        c.fillStyle = "#e8e8f8";
        for (let i = 0; i < lines.length; i++) { c.fillText(lines[i], 14, y); y += 24; }
        c.flush();
        helpDirty = false;
    }
    helpOverlay.draw(12, 12);
}

// ---- ローディング画面 ----
let loadingOverlay = null;
let lastLoadStatus = "";

function drawLoading() {
    gl.clearColor(0.12, 0.12, 0.2, 1.0);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    if (!loadingOverlay) loadingOverlay = new VK.CanvasOverlay(800, 90, 1280, 720);
    if (lastLoadStatus !== loadStatus) {
        const c = loadingOverlay.canvas;
        c.clearRect(0, 0, 800, 90);
        c.font = "28px OpenSans-Bold";
        c.textAlign = "center";
        c.textBaseline = "top";
        c.fillStyle = "#ffffff";
        c.fillText("VRM Starter Kit", 400, 6);
        c.font = "18px OpenSans-Regular";
        c.fillStyle = "#a0b8e0";
        c.fillText(loadStatus, 400, 52);
        c.flush();
        lastLoadStatus = loadStatus;
    }
    loadingOverlay.draw(240, 320);
}

// ---- ライフサイクル ----
function update(dt) {
    Input.update();
    if (ready && Input.isJustPressed("switchMode")) switchMode();
    SceneManager.update(dt);
}

function render() {
    if (!ready) {
        drawLoading();
        return;
    }
    SceneManager.render();
    if (helpVisible) drawHelp();
}

function done() {
    SceneManager.clear();
}

globalThis.update = update;
globalThis.render = render;
globalThis.done = done;

// ---- イベント転送 ----
const EVENT_TYPES = ["keydown", "keyup", "mousedown", "mouseup", "mousemove", "wheel"];
for (let i = 0; i < EVENT_TYPES.length; i++) {
    (function(type) {
        addEventListener(type, function(e) {
            if (type === "keydown" && e.code === "F1") {
                helpVisible = !helpVisible;
                helpDirty = true;
                return;
            }
            SceneManager.handleEvent(e);
        });
    })(EVENT_TYPES[i]);
}

// REPL / 外部エージェントからの操作用フック (-replfile デバッグ)
globalThis.__vrmstarter = {
    app: app,
    switchMode: switchMode,
    isReady: function() { return ready; },
    status: function() { return loadStatus; },
};

console.log("--- VRM Starter Kit ---");
console.log("Tab: switch mode, F1: help");
